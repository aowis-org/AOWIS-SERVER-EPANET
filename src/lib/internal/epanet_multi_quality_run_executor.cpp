#include "epanet_multi_quality_run_executor.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_hydraulic_run_configurator.h"
#include "epanet_hydraulic_solver.h"
#include "epanet_prepared_project.h"
#include "epanet_quality_result_reader.h"
#include "epanet_quality_run_configurator.h"
#include "epanet_quality_solver.h"
#include "epanet_result_finalizer.h"
#include "epanet_result_reader.h"
#include "epanet_status_helpers.h"

namespace
{
bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

bool hasDiagnosticSeverity(
    const QList<HydraulicSimulationDiagnostic> &diagnostics,
    HydraulicSimulationDiagnosticSeverity severity)
{
    for (const HydraulicSimulationDiagnostic &diagnostic : diagnostics)
    {
        if (diagnostic.severity == severity)
            return true;
    }

    return false;
}

bool hasErrorDiagnostic(const QList<HydraulicSimulationDiagnostic> &diagnostics)
{
    return hasDiagnosticSeverity(diagnostics, HydraulicSimulationDiagnosticSeverity::Error)
        || hasDiagnosticSeverity(diagnostics, HydraulicSimulationDiagnosticSeverity::Fatal);
}

bool hasWarningDiagnostic(const QList<HydraulicSimulationDiagnostic> &diagnostics)
{
    return hasDiagnosticSeverity(diagnostics, HydraulicSimulationDiagnosticSeverity::Warning);
}

EpanetRunState completedHydraulicState(const HydraulicSimulationResultTimeline &timeline)
{
    if (!timeline.status.success || hasErrorDiagnostic(timeline.diagnostics))
        return EpanetRunState::Error;
    if (hasWarningDiagnostic(timeline.diagnostics))
        return EpanetRunState::Warning;
    return EpanetRunState::Success;
}

EpanetRunState completedQualityState(const WaterQualitySimulationResultTimeline &timeline)
{
    if (timeline.analysis == WaterQualityAnalysisType::None)
        return EpanetRunState::Skipped;
    if (!timeline.status.success || hasErrorDiagnostic(timeline.diagnostics))
        return EpanetRunState::Error;
    if (hasWarningDiagnostic(timeline.diagnostics))
        return EpanetRunState::Warning;
    return EpanetRunState::Success;
}

void recordFailureStatus(EpanetResultRun &result, const HydraulicSimulationStatus &status)
{
    if (status.success)
        return;

    if (result.status.success)
        result.status = status;

    appendEpanetDiagnosticIfUnique(result.diagnostics, epanetDiagnosticFromStatus(status));
}

void markPendingQualityRuns(EpanetResultRun &result, EpanetRunState state)
{
    for (EpanetQualityResult &quality_result : result.quality_results)
    {
        if (quality_result.state == EpanetRunState::Pending)
            quality_result.state = state;
    }
}

void updateCompatibilityQualityTimeline(EpanetResultRun &result)
{
    if (!result.quality_results.isEmpty())
    {
        result.quality_result_timeline = result.quality_results.constFirst().result_timeline;
        return;
    }

    result.quality_result_timeline.analysis = WaterQualityAnalysisType::None;
    result.quality_result_timeline.status = makeEpanetSuccess();
    result.quality_result_timeline.validity = WaterQualitySimulationResultValidity::NotRun;
}

void finalizeRunState(EpanetResultRun &result)
{
    updateCompatibilityQualityTimeline(result);

    if (result.cancelled)
    {
        result.state = EpanetRunState::Cancelled;
        markPendingQualityRuns(result, EpanetRunState::Cancelled);
        return;
    }

    bool has_warning = hasWarningDiagnostic(result.diagnostics);
    bool has_error = hasErrorDiagnostic(result.diagnostics)
        || completedHydraulicState(result.result_timeline) == EpanetRunState::Error;

    if (completedHydraulicState(result.result_timeline) == EpanetRunState::Warning)
        has_warning = true;

    for (const EpanetQualityResult &quality_result : result.quality_results)
    {
        if (quality_result.state == EpanetRunState::Error)
            has_error = true;
        else if (quality_result.state == EpanetRunState::Warning)
            has_warning = true;
    }

    if (has_error)
        result.state = EpanetRunState::Error;
    else if (has_warning)
        result.state = EpanetRunState::Warning;
    else
        result.state = EpanetRunState::Success;

    if (result.status.backend_name.isEmpty())
        result.status = makeEpanetSuccess();
}

HydraulicSimulationStatus saveHydraulics(EpanetPreparedProject &prepared_project)
{
    const int error = EN_saveH(prepared_project.project().handle());
    if (error == 0)
        return makeEpanetSuccess();

    return processEpanetReturnCode(
        prepared_project.project(),
        error,
        HydraulicSimulationStatusStage::SaveHydraulics,
        HydraulicSimulationStatusOperation::SaveHydraulics,
        QStringLiteral("EN_saveH"),
        HydraulicSimulationStatusEntityType::HydraulicSolver,
        QString(),
        QStringLiteral("Failed to save EPANET hydraulic results"));
}

HydraulicSimulationStatus generateReport(
    EpanetPreparedProject &prepared_project,
    QStringList &report_lines,
    QList<HydraulicSimulationDiagnostic> &diagnostics)
{
    prepared_project.reportCollector().clear();
    const EpanetDiagnosticCheckpoint report_diagnostics(prepared_project.project().diagnostics());
    const int error = EN_report(prepared_project.project().handle());
    report_lines = prepared_project.reportCollector().lines();

    HydraulicSimulationStatus status = makeEpanetSuccess();
    if (error != 0)
    {
        status = processEpanetReturnCode(
            prepared_project.project(),
            error,
            HydraulicSimulationStatusStage::GenerateReport,
            HydraulicSimulationStatusOperation::GenerateReport,
            QStringLiteral("EN_report"),
            HydraulicSimulationStatusEntityType::Report,
            QString(),
            QStringLiteral("Failed to generate EPANET report"));
        if (!status.success)
            appendEpanetDiagnosticIfUnique(diagnostics, epanetDiagnosticFromStatus(status));
    }

    report_diagnostics.appendSince(diagnostics, prepared_project.project().diagnostics());
    appendEpanetReportDiagnostics(diagnostics, report_lines);
    return status;
}
}

EpanetMultiQualityRunExecutor::EpanetMultiQualityRunExecutor(EpanetPreparedProject &prepared_project)
    : prepared_project_(prepared_project)
{
}

EpanetResultRun EpanetMultiQualityRunExecutor::run(
    EpanetResultRun result,
    const std::function<bool()> &cancellation_requested)
{
    result.state = EpanetRunState::Running;

    if (cancellationRequested(cancellation_requested))
    {
        result.cancelled = true;
        finalizeRunState(result);
        return result;
    }

    const EpanetDiagnosticCheckpoint hydraulic_diagnostics(this->prepared_project_.project().diagnostics());
    HydraulicSimulationStatus status = configureEpanetHydraulicRun(
        this->prepared_project_.project(),
        this->prepared_project_.network(),
        this->prepared_project_.indices());
    hydraulic_diagnostics.appendSince(
        result.result_timeline.diagnostics,
        this->prepared_project_.project().diagnostics());

    if (!status.success)
    {
        result.result_timeline.status = status;
        appendEpanetDiagnosticIfUnique(result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
        finalizeEpanetHydraulicResultValidity(result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
        recordFailureStatus(result, status);
        markPendingQualityRuns(result, EpanetRunState::Skipped);
        finalizeRunState(result);
        return result;
    }

    EpanetResultReader result_reader(
        this->prepared_project_.project(),
        this->prepared_project_.network(),
        this->prepared_project_.indices());
    EpanetHydraulicSolver hydraulic_solver(
        this->prepared_project_.project(),
        this->prepared_project_.network(),
        result_reader);
    bool cancelled = false;
    status = hydraulic_solver.run(result.result_timeline, cancellation_requested, cancelled);
    hydraulic_diagnostics.appendSince(
        result.result_timeline.diagnostics,
        this->prepared_project_.project().diagnostics());

    if (cancelled || cancellationRequested(cancellation_requested))
    {
        result.result_timeline.status = status;
        markEpanetHydraulicResultCancelled(result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
        result.cancelled = true;
        finalizeRunState(result);
        return result;
    }

    if (!status.success)
    {
        result.result_timeline.status = status;
        appendEpanetDiagnosticIfUnique(result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
        finalizeEpanetHydraulicResultValidity(result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
        recordFailureStatus(result, status);
        markPendingQualityRuns(result, EpanetRunState::Skipped);
        finalizeRunState(result);
        return result;
    }

    status = saveHydraulics(this->prepared_project_);
    hydraulic_diagnostics.appendSince(
        result.result_timeline.diagnostics,
        this->prepared_project_.project().diagnostics());

    if (cancellationRequested(cancellation_requested))
    {
        result.result_timeline.status = status;
        markEpanetHydraulicResultCancelled(result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
        result.cancelled = true;
        finalizeRunState(result);
        return result;
    }

    if (!status.success)
    {
        result.result_timeline.status = status;
        appendEpanetDiagnosticIfUnique(result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
        finalizeEpanetHydraulicResultValidity(result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
        recordFailureStatus(result, status);
        markPendingQualityRuns(result, EpanetRunState::Skipped);
        finalizeRunState(result);
        return result;
    }

    result.result_timeline.status = makeEpanetSuccess();
    finalizeEpanetHydraulicResultValidity(result.result_timeline);

    const HydraulicSimulationStatus hydraulic_report_status = generateReport(
        this->prepared_project_,
        result.report_lines,
        result.result_timeline.diagnostics);
    if (!hydraulic_report_status.success)
    {
        result.result_timeline.status = hydraulic_report_status;
        recordFailureStatus(result, hydraulic_report_status);
    }
    finalizeEpanetHydraulicResultValidity(result.result_timeline);
    appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);

    for (qsizetype quality_index = 0; quality_index < result.quality_results.size(); quality_index++)
    {
        EpanetQualityResult &quality_result = result.quality_results[quality_index];
        if (quality_result.options.analysis == WaterQualityAnalysisType::None)
        {
            quality_result.result_timeline.status = makeEpanetSuccess();
            quality_result.result_timeline.validity = WaterQualitySimulationResultValidity::NotRun;
            quality_result.state = EpanetRunState::Skipped;
            continue;
        }

        if (cancellationRequested(cancellation_requested))
        {
            result.cancelled = true;
            finalizeRunState(result);
            return result;
        }

        quality_result.state = EpanetRunState::Running;
        this->prepared_project_.network().options_quality = quality_result.options;
        const EpanetDiagnosticCheckpoint quality_diagnostics(this->prepared_project_.project().diagnostics());

        status = configureEpanetQualityRun(
            this->prepared_project_.project(),
            this->prepared_project_.network(),
            this->prepared_project_.indices());
        quality_diagnostics.appendSince(
            quality_result.result_timeline.diagnostics,
            this->prepared_project_.project().diagnostics());

        if (!status.success)
        {
            quality_result.result_timeline.status = status;
            appendEpanetDiagnosticIfUnique(quality_result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
            finalizeEpanetQualityResultValidity(quality_result.result_timeline);
            quality_result.state = EpanetRunState::Error;
            appendEpanetDiagnostics(result.diagnostics, quality_result.result_timeline.diagnostics);
            recordFailureStatus(result, status);
            continue;
        }

        EpanetQualityResultReader quality_result_reader(
            this->prepared_project_.project(),
            this->prepared_project_.network(),
            this->prepared_project_.indices());
        EpanetQualitySolver quality_solver(
            this->prepared_project_.project(),
            this->prepared_project_.network(),
            quality_result_reader);
        cancelled = false;
        status = quality_solver.run(quality_result.result_timeline, cancellation_requested, cancelled);
        quality_diagnostics.appendSince(
            quality_result.result_timeline.diagnostics,
            this->prepared_project_.project().diagnostics());

        if (!status.success)
            appendEpanetDiagnosticIfUnique(quality_result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
        quality_result.result_timeline.status = status;

        if (cancelled || cancellationRequested(cancellation_requested))
        {
            markEpanetQualityResultCancelled(quality_result.result_timeline);
            quality_result.state = EpanetRunState::Cancelled;
            appendEpanetDiagnostics(result.diagnostics, quality_result.result_timeline.diagnostics);
            result.cancelled = true;
            finalizeRunState(result);
            return result;
        }

        finalizeEpanetQualityResultValidity(quality_result.result_timeline);

        const HydraulicSimulationStatus quality_report_status = generateReport(
            this->prepared_project_,
            quality_result.report_lines,
            quality_result.result_timeline.diagnostics);
        if (!quality_report_status.success)
        {
            quality_result.result_timeline.status = quality_report_status;
            recordFailureStatus(result, quality_report_status);
        }
        finalizeEpanetQualityResultValidity(quality_result.result_timeline);
        quality_result.state = completedQualityState(quality_result.result_timeline);
        appendEpanetDiagnostics(result.diagnostics, quality_result.result_timeline.diagnostics);

        if (!status.success)
            recordFailureStatus(result, status);
    }

    appendEpanetDiagnostics(result.diagnostics, this->prepared_project_.project().diagnostics());
    finalizeRunState(result);
    return result;
}
