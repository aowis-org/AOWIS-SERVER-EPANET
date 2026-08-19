#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_hydraulic_solver.h"
#include "internal/epanet_index_registry.h"
#include "internal/epanet_network_builder.h"
#include "internal/epanet_network_preparer.h"
#include "internal/epanet_project.h"
#include "internal/epanet_quality_result_reader.h"
#include "internal/epanet_quality_solver.h"
#include "internal/epanet_report_collector.h"
#include "internal/epanet_result_reader.h"
#include "internal/epanet_status_helpers.h"

#include <utility>

#include <QDateTime>
#include <QList>

namespace
{
HydraulicSimulationDiagnostic diagnosticFromStatus(const HydraulicSimulationStatus &status)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = status.backend_error_code > 0 && status.backend_error_code < 100
        ? HydraulicSimulationDiagnosticSeverity::Warning
        : HydraulicSimulationDiagnosticSeverity::Error;
    diagnostic.stage = status.stage;
    diagnostic.operation = status.operation;
    diagnostic.property = status.property;
    diagnostic.entity = status.entity;
    diagnostic.message = status.message;
    diagnostic.details = status.details;
    diagnostic.backend_name = status.backend_name;
    diagnostic.backend_error_code = status.backend_error_code;
    diagnostic.backend_operation = status.backend_operation;
    diagnostic.message_backend = status.message_backend;
    return diagnostic;
}

bool diagnosticsEquivalent(const HydraulicSimulationDiagnostic &left, const HydraulicSimulationDiagnostic &right)
{
    return left.stage == right.stage
        && left.operation == right.operation
        && left.entity.uuid == right.entity.uuid
        && left.entity.type == right.entity.type
        && left.backend_error_code == right.backend_error_code
        && left.backend_operation == right.backend_operation
        && left.message == right.message
        && left.details == right.details
        && left.message_backend == right.message_backend;
}

void appendDiagnosticIfUnique(QList<HydraulicSimulationDiagnostic> &diagnostics, const HydraulicSimulationDiagnostic &diagnostic)
{
    for (const HydraulicSimulationDiagnostic &existing : diagnostics)
    {
        if (diagnosticsEquivalent(existing, diagnostic))
            return;
    }

    diagnostics.append(diagnostic);
}

bool diagnosticInvalidatesResults(const HydraulicSimulationDiagnostic &diagnostic)
{
    if (diagnostic.severity != HydraulicSimulationDiagnosticSeverity::Error
        && diagnostic.severity != HydraulicSimulationDiagnosticSeverity::Fatal)
    {
        return false;
    }

    switch (diagnostic.stage)
    {
    case HydraulicSimulationStatusStage::CloseHydraulics:
    case HydraulicSimulationStatusStage::CloseQuality:
    case HydraulicSimulationStatusStage::SaveHydraulics:
    case HydraulicSimulationStatusStage::GenerateReport:
    case HydraulicSimulationStatusStage::Cleanup:
        return false;
    default:
        return true;
    }
}

void finalizeResultValidity(HydraulicSimulationResultTimeline &timeline)
{
    bool has_invalidating_diagnostic = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : timeline.diagnostics)
    {
        if (diagnosticInvalidatesResults(diagnostic))
        {
            has_invalidating_diagnostic = true;
            break;
        }
    }

    if (has_invalidating_diagnostic)
    {
        timeline.validity = timeline.results.isEmpty()
            ? HydraulicSimulationResultValidity::Invalid
            : HydraulicSimulationResultValidity::Partial;
        return;
    }

    timeline.validity = timeline.results.isEmpty()
        ? HydraulicSimulationResultValidity::Invalid
        : HydraulicSimulationResultValidity::Valid;
}

void finalizeQualityResultValidity(WaterQualitySimulationResultTimeline &timeline)
{
    if (timeline.analysis == WaterQualityAnalysisType::None)
    {
        timeline.validity = WaterQualitySimulationResultValidity::NotRun;
        return;
    }

    bool has_invalidating_diagnostic = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : timeline.diagnostics)
    {
        if (diagnosticInvalidatesResults(diagnostic))
        {
            has_invalidating_diagnostic = true;
            break;
        }
    }

    if (has_invalidating_diagnostic)
    {
        timeline.validity = timeline.results.isEmpty()
            ? WaterQualitySimulationResultValidity::Invalid
            : WaterQualitySimulationResultValidity::Partial;
        return;
    }

    timeline.validity = timeline.results.isEmpty()
        ? WaterQualitySimulationResultValidity::Invalid
        : WaterQualitySimulationResultValidity::Valid;
}

void appendProjectDiagnostics(
    QList<HydraulicSimulationDiagnostic> &target,
    const QList<HydraulicSimulationDiagnostic> &source,
    qsizetype begin_index,
    qsizetype end_index)
{
    const qsizetype bounded_begin = qMax<qsizetype>(0, begin_index);
    const qsizetype bounded_end = qMin<qsizetype>(source.size(), end_index);
    for (qsizetype index = bounded_begin; index < bounded_end; index++)
        appendDiagnosticIfUnique(target, source.at(index));
}

void appendReportDiagnostics(QList<HydraulicSimulationDiagnostic> &diagnostics, const QStringList &report_lines)
{
    for (const QString &line : report_lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        HydraulicSimulationDiagnostic diagnostic;
        if (trimmed.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive))
            diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Error;
        else if (trimmed.contains(QStringLiteral("WARNING"), Qt::CaseInsensitive))
            diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Warning;
        else
            continue;

        diagnostic.stage = HydraulicSimulationStatusStage::GenerateReport;
        diagnostic.operation = HydraulicSimulationStatusOperation::GenerateReport;
        diagnostic.entity.type = HydraulicSimulationStatusEntityType::Report;
        diagnostic.message = trimmed;
        diagnostic.backend_name = QStringLiteral("EPANET");
        diagnostic.backend_operation = QStringLiteral("report callback");
        appendDiagnosticIfUnique(diagnostics, diagnostic);
    }
}

bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

EpanetResultRun finishCancelledRun(
    EpanetResultRun result,
    const EpanetReportCollector &report_collector,
    bool hydraulics_complete = false,
    bool quality_attempted = false,
    bool quality_complete = false)
{
    result.cancelled = true;
    result.report_lines = report_collector.lines();

    if (!hydraulics_complete)
    {
        result.result_timeline.validity = result.result_timeline.results.isEmpty()
            ? HydraulicSimulationResultValidity::Invalid
            : HydraulicSimulationResultValidity::Partial;
    }

    if (quality_attempted && !quality_complete && result.quality_result_timeline.analysis != WaterQualityAnalysisType::None)
    {
        result.quality_result_timeline.validity = result.quality_result_timeline.results.isEmpty()
            ? WaterQualitySimulationResultValidity::Invalid
            : WaterQualitySimulationResultValidity::Partial;
    }

    return result;
}

EpanetResultInp finishInp(EpanetResultInp result, const HydraulicSimulationStatus &status, const EpanetReportCollector &report_collector)
{
    result.status = status;
    result.report_lines = report_collector.lines();
    return result;
}

EpanetResultRun finishRun(EpanetResultRun result, const HydraulicSimulationStatus &status, const EpanetProject &project, const EpanetReportCollector &report_collector)
{
    result.result_timeline.status = status;
    result.report_lines = report_collector.lines();

    for (const HydraulicSimulationDiagnostic &diagnostic : project.diagnostics())
        appendDiagnosticIfUnique(result.result_timeline.diagnostics, diagnostic);

    if (!status.success)
        appendDiagnosticIfUnique(result.result_timeline.diagnostics, diagnosticFromStatus(status));

    appendReportDiagnostics(result.result_timeline.diagnostics, result.report_lines);
    finalizeResultValidity(result.result_timeline);
    return result;
}

HydraulicSimulationStatus prepareProject(const NetworkHydraulic &request, NetworkHydraulic &prepared_request, EpanetProject &project, EpanetReportCollector &report_collector, EpanetIndexRegistry &indices)
{
    QList<HydraulicSimulationStatus> validation_failures;
    HydraulicSimulationStatus status = prepareEpanetNetwork(request, prepared_request, &validation_failures);
    for (const HydraulicSimulationStatus &validation_failure : validation_failures)
        project.appendDiagnostic(diagnosticFromStatus(validation_failure));

    if (!status.success)
        return status;

    status = project.create();
    if (!status.success)
        return status;

    status = project.initialize(prepared_request, report_collector);
    if (!status.success)
        return status;

    EpanetNetworkBuilder network_builder(project, indices);
    return network_builder.build(prepared_request);
}
}

EpanetResultInp EpanetRunner::retrieveInp(const NetworkHydraulic &request) const
{
    EpanetResultInp result;
    EpanetReportCollector report_collector;
    EpanetProject project;
    EpanetIndexRegistry indices;
    NetworkHydraulic prepared_request;

    HydraulicSimulationStatus status = prepareProject(request, prepared_request, project, report_collector, indices);
    if (!status.success)
        return finishInp(std::move(result), status, report_collector);

    status = project.configureReport(prepared_request);
    if (!status.success)
        return finishInp(std::move(result), status, report_collector);

    status = project.retrieveInpText(prepared_request, result.inp_text);
    return finishInp(std::move(result), status, report_collector);
}

EpanetResultRun EpanetRunner::run(const NetworkHydraulic &request) const
{
    return run(request, std::function<bool()>());
}

EpanetResultRun EpanetRunner::run(
    const NetworkHydraulic &request,
    const std::function<bool()> &cancellation_requested) const
{
    EpanetResultRun result;
    const QDateTime simulation_start_utc = QDateTime::currentDateTimeUtc();
    result.result_timeline.simulation_start_utc = simulation_start_utc;
    result.quality_result_timeline.analysis = request.options_quality.analysis;
    result.quality_result_timeline.simulation_start_utc = simulation_start_utc;

    EpanetReportCollector report_collector;
    EpanetProject project;
    EpanetIndexRegistry indices;
    NetworkHydraulic prepared_request;

    if (cancellationRequested(cancellation_requested))
        return finishCancelledRun(std::move(result), report_collector);

    HydraulicSimulationStatus status = prepareProject(request, prepared_request, project, report_collector, indices);
    if (cancellationRequested(cancellation_requested))
        return finishCancelledRun(std::move(result), report_collector);

    if (!status.success)
        return finishRun(std::move(result), status, project, report_collector);

    EpanetResultReader result_reader(project, prepared_request, indices);
    EpanetHydraulicSolver hydraulic_solver(project, prepared_request, result_reader);
    bool cancelled = false;
    status = hydraulic_solver.run(result.result_timeline, cancellation_requested, cancelled);
    if (cancelled || cancellationRequested(cancellation_requested))
        return finishCancelledRun(std::move(result), report_collector);

    if (!status.success)
        return finishRun(std::move(result), status, project, report_collector);

    int error = EN_saveH(project.handle());
    if (cancellationRequested(cancellation_requested))
        return finishCancelledRun(std::move(result), report_collector);

    if (error != 0)
    {
        status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::SaveHydraulics, HydraulicSimulationStatusOperation::SaveHydraulics, QStringLiteral("EN_saveH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to save EPANET hydraulic results"));
        if (!status.success)
            return finishRun(std::move(result), status, project, report_collector);
    }

    // Hydraulics are complete before quality starts. Freeze their diagnostics and
    // validity here so later quality diagnostics cannot invalidate a valid hydraulic
    // timeline.
    const qsizetype quality_diagnostic_begin = project.diagnostics().size();
    result.result_timeline.status = makeEpanetSuccess();
    appendProjectDiagnostics(result.result_timeline.diagnostics, project.diagnostics(), 0, quality_diagnostic_begin);
    finalizeResultValidity(result.result_timeline);

    bool quality_attempted = false;
    if (prepared_request.options_quality.analysis != WaterQualityAnalysisType::None)
    {
        quality_attempted = true;
        EpanetQualityResultReader quality_result_reader(project, prepared_request, indices);
        EpanetQualitySolver quality_solver(project, prepared_request, quality_result_reader);
        status = quality_solver.run(result.quality_result_timeline, cancellation_requested, cancelled);

        const qsizetype quality_diagnostic_end = project.diagnostics().size();
        appendProjectDiagnostics(result.quality_result_timeline.diagnostics, project.diagnostics(), quality_diagnostic_begin, quality_diagnostic_end);
        if (!status.success)
            appendDiagnosticIfUnique(result.quality_result_timeline.diagnostics, diagnosticFromStatus(status));
        result.quality_result_timeline.status = status;

        if (cancelled)
        {
            result.quality_result_timeline.validity = result.quality_result_timeline.results.isEmpty()
                ? WaterQualitySimulationResultValidity::Invalid
                : WaterQualitySimulationResultValidity::Partial;
            return finishCancelledRun(std::move(result), report_collector, true, true, false);
        }

        finalizeQualityResultValidity(result.quality_result_timeline);
        if (cancellationRequested(cancellation_requested))
            return finishCancelledRun(std::move(result), report_collector, true, true, true);
    }
    else
    {
        result.quality_result_timeline.status = makeEpanetSuccess();
        result.quality_result_timeline.validity = WaterQualitySimulationResultValidity::NotRun;
    }

    const qsizetype report_diagnostic_begin = project.diagnostics().size();
    error = EN_report(project.handle());
    if (cancellationRequested(cancellation_requested))
        return finishCancelledRun(std::move(result), report_collector, true, quality_attempted, true);

    if (error != 0)
    {
        const HydraulicSimulationStatus report_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_report"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to generate EPANET report"));
        if (!report_status.success)
        {
            result.result_timeline.status = report_status;
            appendDiagnosticIfUnique(result.result_timeline.diagnostics, diagnosticFromStatus(report_status));
        }
    }

    appendProjectDiagnostics(result.result_timeline.diagnostics, project.diagnostics(), report_diagnostic_begin, project.diagnostics().size());
    result.report_lines = report_collector.lines();
    appendReportDiagnostics(result.result_timeline.diagnostics, result.report_lines);
    finalizeResultValidity(result.result_timeline);
    if (result.result_timeline.status.backend_name.isEmpty())
        result.result_timeline.status = makeEpanetSuccess();

    return result;
}
