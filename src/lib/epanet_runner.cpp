#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_hydraulic_solver.h"
#include "internal/epanet_index_registry.h"
#include "internal/epanet_network_builder.h"
#include "internal/epanet_network_preparer.h"
#include "internal/epanet_project.h"
#include "internal/epanet_report_collector.h"
#include "internal/epanet_result_reader.h"
#include "internal/epanet_status_helpers.h"

#include <utility>

#include <QDateTime>

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
    HydraulicSimulationStatus status = prepareEpanetNetwork(request, prepared_request);
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

    status = project.retrieveInpText(result.inp_text);
    return finishInp(std::move(result), status, report_collector);
}

EpanetResultRun EpanetRunner::run(const NetworkHydraulic &request) const
{
    EpanetResultRun result;
    result.result_timeline.simulation_start_utc = QDateTime::currentDateTimeUtc();

    EpanetReportCollector report_collector;
    EpanetProject project;
    EpanetIndexRegistry indices;
    NetworkHydraulic prepared_request;

    HydraulicSimulationStatus status = prepareProject(request, prepared_request, project, report_collector, indices);
    if (!status.success)
        return finishRun(std::move(result), status, project, report_collector);

    EpanetResultReader result_reader(project, prepared_request, indices);
    EpanetHydraulicSolver hydraulic_solver(project, prepared_request, result_reader);
    status = hydraulic_solver.run(result.result_timeline);
    if (!status.success)
        return finishRun(std::move(result), status, project, report_collector);

    int error = EN_saveH(project.handle());
    if (error != 0)
    {
        status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::SaveHydraulics, HydraulicSimulationStatusOperation::SaveHydraulics, QStringLiteral("EN_saveH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to save EPANET hydraulic results"));
        if (!status.success)
            return finishRun(std::move(result), status, project, report_collector);
    }

    error = EN_report(project.handle());
    if (error != 0)
    {
        status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_report"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to generate EPANET report"));
        if (!status.success)
            return finishRun(std::move(result), status, project, report_collector);
    }

    return finishRun(std::move(result), makeEpanetSuccess(), project, report_collector);
}
