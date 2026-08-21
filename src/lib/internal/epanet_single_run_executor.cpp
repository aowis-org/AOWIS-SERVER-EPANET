#include "epanet_single_run_executor.h"

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

#include <utility>

namespace
{
bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

HydraulicSimulationStatus configureProjectRun(EpanetPreparedProject &prepared_project)
{
    HydraulicSimulationStatus status = configureEpanetHydraulicRun(
        prepared_project.project(),
        prepared_project.network(),
        prepared_project.indices());
    if (!status.success)
        return status;

    return configureEpanetQualityRun(
        prepared_project.project(),
        prepared_project.network(),
        prepared_project.indices());
}
}

EpanetSingleRunExecutor::EpanetSingleRunExecutor(EpanetPreparedProject &prepared_project)
    : prepared_project_(prepared_project)
{
}

EpanetResultRun EpanetSingleRunExecutor::run(
    EpanetResultRun result,
    const std::function<bool()> &cancellation_requested)
{
    HydraulicSimulationStatus status = configureProjectRun(this->prepared_project_);
    if (!status.success)
        return finalizeEpanetFailedRun(std::move(result), status, this->prepared_project_);

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
    if (cancelled || cancellationRequested(cancellation_requested))
        return finalizeEpanetCancelledRun(std::move(result), this->prepared_project_);

    if (!status.success)
        return finalizeEpanetFailedRun(std::move(result), status, this->prepared_project_);

    int error = EN_saveH(this->prepared_project_.project().handle());
    if (cancellationRequested(cancellation_requested))
        return finalizeEpanetCancelledRun(std::move(result), this->prepared_project_);

    if (error != 0)
    {
        status = processEpanetReturnCode(
            this->prepared_project_.project(),
            error,
            HydraulicSimulationStatusStage::SaveHydraulics,
            HydraulicSimulationStatusOperation::SaveHydraulics,
            QStringLiteral("EN_saveH"),
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("Failed to save EPANET hydraulic results"));
        if (!status.success)
            return finalizeEpanetFailedRun(std::move(result), status, this->prepared_project_);
    }

    // Hydraulics are complete before quality starts. Freeze their diagnostics and
    // validity here so later quality diagnostics cannot invalidate a valid hydraulic
    // timeline.
    result.result_timeline.status = makeEpanetSuccess();
    appendEpanetDiagnostics(result.result_timeline.diagnostics, this->prepared_project_.project().diagnostics());
    finalizeEpanetHydraulicResultValidity(result.result_timeline);
    const EpanetDiagnosticCheckpoint quality_diagnostics(this->prepared_project_.project().diagnostics());

    bool quality_attempted = false;
    if (this->prepared_project_.network().options_quality.analysis != WaterQualityAnalysisType::None)
    {
        quality_attempted = true;
        EpanetQualityResultReader quality_result_reader(
            this->prepared_project_.project(),
            this->prepared_project_.network(),
            this->prepared_project_.indices());
        EpanetQualitySolver quality_solver(
            this->prepared_project_.project(),
            this->prepared_project_.network(),
            quality_result_reader);
        status = quality_solver.run(result.quality_result_timeline, cancellation_requested, cancelled);

        quality_diagnostics.appendSince(result.quality_result_timeline.diagnostics, this->prepared_project_.project().diagnostics());
        if (!status.success)
            appendEpanetDiagnosticIfUnique(result.quality_result_timeline.diagnostics, epanetDiagnosticFromStatus(status));
        result.quality_result_timeline.status = status;

        if (cancelled)
        {
            markEpanetQualityResultCancelled(result.quality_result_timeline);
            return finalizeEpanetCancelledRun(std::move(result), this->prepared_project_, true, true, false);
        }

        finalizeEpanetQualityResultValidity(result.quality_result_timeline);
        if (cancellationRequested(cancellation_requested))
            return finalizeEpanetCancelledRun(std::move(result), this->prepared_project_, true, true, true);
    }
    else
    {
        result.quality_result_timeline.status = makeEpanetSuccess();
        result.quality_result_timeline.validity = WaterQualitySimulationResultValidity::NotRun;
    }

    const EpanetDiagnosticCheckpoint report_diagnostics(this->prepared_project_.project().diagnostics());
    error = EN_report(this->prepared_project_.project().handle());
    if (cancellationRequested(cancellation_requested))
        return finalizeEpanetCancelledRun(std::move(result), this->prepared_project_, true, quality_attempted, true);

    if (error != 0)
    {
        const HydraulicSimulationStatus report_status = processEpanetReturnCode(
            this->prepared_project_.project(),
            error,
            HydraulicSimulationStatusStage::GenerateReport,
            HydraulicSimulationStatusOperation::GenerateReport,
            QStringLiteral("EN_report"),
            HydraulicSimulationStatusEntityType::Report,
            QString(),
            QStringLiteral("Failed to generate EPANET report"));
        if (!report_status.success)
        {
            result.result_timeline.status = report_status;
            appendEpanetDiagnosticIfUnique(result.result_timeline.diagnostics, epanetDiagnosticFromStatus(report_status));
        }
    }

    report_diagnostics.appendSince(result.result_timeline.diagnostics, this->prepared_project_.project().diagnostics());
    result.report_lines = this->prepared_project_.reportCollector().lines();
    appendEpanetReportDiagnostics(result.result_timeline.diagnostics, result.report_lines);
    finalizeEpanetHydraulicResultValidity(result.result_timeline);
    if (result.result_timeline.status.backend_name.isEmpty())
        result.result_timeline.status = makeEpanetSuccess();

    return result;
}
