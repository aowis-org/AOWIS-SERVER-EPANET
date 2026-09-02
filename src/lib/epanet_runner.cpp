#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_diagnostic_helpers.h"
#include "internal/epanet_hydraulic_run_configurator.h"
#include "internal/epanet_inp_exporter.h"
#include "internal/epanet_inp_importer.h"
#include "internal/epanet_multi_quality_run_executor.h"
#include "internal/epanet_msx_project.h"
#include "internal/epanet_network_validator.h"
#include "internal/epanet_prepared_project.h"
#include "internal/epanet_quality_run_configurator.h"
#include "internal/epanet_report_configurator.h"
#include "internal/epanet_result_finalizer.h"
#include "internal/epanet_status_helpers.h"

#include <utility>

#include <QDateTime>

namespace
{
bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

qsizetype sharedReportPrefixSize(const QStringList &first, const QStringList &second)
{
    const qsizetype maximum_prefix_size = qMin(first.size(), second.size());
    qsizetype prefix_size = 0;
    while (prefix_size < maximum_prefix_size)
    {
        if (first.at(prefix_size) != second.at(prefix_size))
            break;
        prefix_size++;
    }

    return prefix_size;
}

template<typename Node>
QString nodeIdForUuid(const QList<Node> &nodes, const QUuid &uuid)
{
    for (const Node &node : nodes)
    {
        if (node.uuid == uuid)
            return node.id;
    }

    return QString();
}

QString traceNodeLabel(const NetworkHydraulic &network, const QUuid &uuid)
{
    if (uuid.isNull())
        return QStringLiteral("not set");

    QString node_id = nodeIdForUuid(network.nodes_junctions, uuid);
    if (node_id.isEmpty())
        node_id = nodeIdForUuid(network.nodes_reservoirs, uuid);
    if (node_id.isEmpty())
        node_id = nodeIdForUuid(network.nodes_tanks, uuid);
    if (!node_id.isEmpty())
        return node_id;

    return QStringLiteral("unresolved UUID %1").arg(uuid.toString(QUuid::WithoutBraces));
}

QString qualitySectionHeading(
    const WaterQualitySolverOptions &options,
    const NetworkHydraulic &network)
{
    switch (options.analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        if (!options.chemical_name.isEmpty())
        {
            return QStringLiteral("=== Water quality: Chemical (%1) ===")
                .arg(options.chemical_name);
        }
        return QStringLiteral("=== Water quality: Chemical ===");
    case WaterQualityAnalysisType::WaterAge:
        return QStringLiteral("=== Water quality: Water age ===");
    case WaterQualityAnalysisType::SourceTrace:
        return QStringLiteral("=== Water quality: Source trace (origin: %1) ===")
            .arg(traceNodeLabel(network, options.trace_node_uuid));
    case WaterQualityAnalysisType::None:
        return QStringLiteral("=== Water quality: None ===");
    }

    return QStringLiteral("=== Water quality: Unknown ===");
}

QString qualityRunStatusText(const EpanetQualityResult &quality_result)
{
    const HydraulicSimulationStatus &status = quality_result.result_timeline.status;
    if (!status.success)
    {
        QStringList lines;
        lines.append(QStringLiteral("ERROR: %1").arg(
            status.message.isEmpty()
                ? QStringLiteral("The water-quality run failed.")
                : status.message));
        if (!status.message_backend.isEmpty())
            lines.append(QStringLiteral("EPANET error: %1").arg(status.message_backend));
        for (const QString &detail : status.details)
            lines.append(QStringLiteral("  - %1").arg(detail));
        return lines.join(QLatin1Char('\n'));
    }

    if (quality_result.state == EpanetRunState::Cancelled)
        return QStringLiteral("CANCELLED: The water-quality run was cancelled.");
    if (quality_result.state == EpanetRunState::Skipped)
        return QStringLiteral("SKIPPED: The water-quality run was not executed.");

    return QString();
}

void finalizeReportText(EpanetResultRun &result, const NetworkHydraulic &network)
{
    QStringList report_sections;
    if (!result.report_lines.isEmpty())
    {
        report_sections.append(
            QStringLiteral("=== Hydraulics ===\n\n%1")
                .arg(result.report_lines.join(QLatin1Char('\n'))));
    }

    for (EpanetQualityResult &quality_result : result.quality_results)
    {
        quality_result.report_text = quality_result.report_lines.join(QLatin1Char('\n'));

        const qsizetype repeated_header_size = sharedReportPrefixSize(
            result.report_lines,
            quality_result.report_lines);
        QStringList quality_section_body = quality_result.report_lines.mid(repeated_header_size);
        const QString status_text = qualityRunStatusText(quality_result);
        if (!status_text.isEmpty())
            quality_section_body.append(status_text);

        if (quality_section_body.isEmpty())
            continue;

        report_sections.append(
            QStringLiteral("%1\n\n%2")
                .arg(
                    qualitySectionHeading(quality_result.options, network),
                    quality_section_body.join(QLatin1Char('\n'))));
    }

    result.report_text = report_sections.join(QStringLiteral("\n\n"));
}

EpanetResultInp finishInp(
    EpanetResultInp result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project)
{
    result.status = status;
    result.report_lines = prepared_project.reportCollector().lines();
    return result;
}

EpanetResultRun initializeRunResult(
    const EpanetRunRequest &request,
    const QDateTime &simulation_start_utc)
{
    EpanetResultRun result;
    result.status = makeEpanetSuccess();
    result.result_timeline.simulation_start_utc = simulation_start_utc;

    for (const WaterQualitySolverOptions &quality_options : request.quality_runs)
    {
        EpanetQualityResult quality_result;
        quality_result.options = quality_options;
        quality_result.result_timeline.analysis = quality_options.analysis;
        quality_result.result_timeline.simulation_start_utc = simulation_start_utc;
        result.quality_results.append(quality_result);
    }

    return result;
}

void markPendingQualityRuns(EpanetResultRun &result, EpanetRunState state)
{
    for (EpanetQualityResult &quality_result : result.quality_results)
    {
        if (quality_result.state == EpanetRunState::Pending)
            quality_result.state = state;
    }
}

void markPendingMultiSpeciesRun(EpanetResultRun &result, EpanetRunState state)
{
    if (result.multi_species_result.has_value()
        && result.multi_species_result->state == EpanetRunState::Pending)
    {
        result.multi_species_result->state = state;
    }
}

EpanetResultRun cancelledRun(
    EpanetResultRun result,
    const EpanetPreparedProject &prepared_project,
    const NetworkHydraulic &network)
{
    result.cancelled = true;
    result.state = EpanetRunState::Cancelled;
    markPendingQualityRuns(result, EpanetRunState::Cancelled);
    markPendingMultiSpeciesRun(result, EpanetRunState::Cancelled);
    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    result.report_lines = prepared_project.reportCollector().lines();
    finalizeReportText(result, network);
    return result;
}

EpanetResultRun failedRun(
    EpanetResultRun result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project,
    const NetworkHydraulic &network)
{
    result.status = status;
    result.state = EpanetRunState::Error;
    result.result_timeline.status = status;
    result.report_lines = prepared_project.reportCollector().lines();

    appendEpanetDiagnostics(
        result.result_timeline.diagnostics,
        prepared_project.project().diagnostics());
    if (!status.success)
    {
        appendEpanetDiagnosticIfUnique(
            result.result_timeline.diagnostics,
            epanetDiagnosticFromStatus(status));
    }
    appendEpanetReportDiagnostics(result.result_timeline.diagnostics, result.report_lines);
    finalizeEpanetHydraulicResultValidity(result.result_timeline);

    appendEpanetDiagnostics(result.diagnostics, result.result_timeline.diagnostics);
    markPendingQualityRuns(result, EpanetRunState::Skipped);
    markPendingMultiSpeciesRun(result, EpanetRunState::Skipped);
    finalizeReportText(result, network);
    return result;
}

HydraulicSimulationStatus validateAndConfigureQualityForInp(
    EpanetPreparedProject &prepared_project,
    const WaterQualitySolverOptions &options)
{
    QList<HydraulicSimulationStatus> validation_failures;
    HydraulicSimulationStatus status = validateEpanetQualityRun(
        prepared_project.network(),
        options,
        &validation_failures);
    for (const HydraulicSimulationStatus &validation_failure : validation_failures)
    {
        prepared_project.project().appendDiagnostic(
            epanetDiagnosticFromStatus(validation_failure, HydraulicSimulationDiagnosticSeverity::Error));
    }
    if (!status.success)
        return status;

    return configureEpanetQualityRun(
        prepared_project.project(),
        prepared_project.network(),
        prepared_project.indices(),
        options);
}

}

EpanetResultImport EpanetRunner::importInp(const QString &input_file_path) const
{
    return importEpanetInp(input_file_path);
}

EpanetResultInp EpanetRunner::retrieveInp(const EpanetRunRequest &request) const
{
    EpanetResultInp result;
    EpanetPreparedProject prepared_project;

    if (request.multi_species_run.has_value())
    {
        const HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            request.network.id,
            request.network.uuid,
            QStringLiteral("A multi-species (MSX) run cannot be represented in a single EPANET INP file; "
                           "retrieveInp() does not support multi_species_run"));
        return finishInp(std::move(result), status, prepared_project);
    }

    if (request.quality_runs.size() > 1)
    {
        const HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureQuality,
            HydraulicSimulationStatusEntityType::QualitySolver,
            request.network.id,
            request.network.uuid,
            QStringLiteral("An EPANET INP file can contain only one water-quality analysis configuration"));
        return finishInp(std::move(result), status, prepared_project);
    }

    HydraulicSimulationStatus status = prepared_project.prepare(request.network);
    if (!status.success)
        return finishInp(std::move(result), status, prepared_project);

    status = configureEpanetHydraulicRun(
        prepared_project.project(),
        prepared_project.network(),
        prepared_project.indices());
    if (!status.success)
        return finishInp(std::move(result), status, prepared_project);

    WaterQualitySolverOptions quality_options;
    if (!request.quality_runs.isEmpty())
        quality_options = request.quality_runs.constFirst();

    status = validateAndConfigureQualityForInp(prepared_project, quality_options);
    if (!status.success)
        return finishInp(std::move(result), status, prepared_project);

    status = configureEpanetReport(prepared_project.project(), prepared_project.network());
    if (!status.success)
        return finishInp(std::move(result), status, prepared_project);

    status = retrieveEpanetInpText(prepared_project.project(), prepared_project.network(), result.inp_text);
    return finishInp(std::move(result), status, prepared_project);
}

EpanetResultRun EpanetRunner::run(const EpanetRunRequest &request) const
{
    return run(request, std::function<bool()>());
}

EpanetResultRun EpanetRunner::run(
    const EpanetRunRequest &request,
    const std::function<bool()> &cancellation_requested) const
{
    const QDateTime simulation_start_utc = QDateTime::currentDateTimeUtc();
    EpanetResultRun result = initializeRunResult(request, simulation_start_utc);
    EpanetPreparedProject prepared_project;

    if (cancellationRequested(cancellation_requested))
        return cancelledRun(std::move(result), prepared_project, request.network);

    if (request.multi_species_run.has_value())
    {
        EpanetMultiSpeciesResult multi_species_result;
        multi_species_result.options = request.multi_species_run.value();
        multi_species_result.result_timeline.simulation_start_utc = simulation_start_utc;
        result.multi_species_result = multi_species_result;
    }

    const HydraulicSimulationStatus status = prepared_project.prepare(request.network);
    if (cancellationRequested(cancellation_requested))
        return cancelledRun(std::move(result), prepared_project, request.network);

    if (!status.success)
        return failedRun(std::move(result), status, prepared_project, request.network);

    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    const bool needs_hydraulic_file = request.multi_species_run.has_value();
    EpanetMultiQualityRunExecutor executor(prepared_project, needs_hydraulic_file);
    EpanetResultRun completed_result = executor.run(std::move(result), cancellation_requested);

    if (request.multi_species_run.has_value() && completed_result.multi_species_result.has_value())
    {
        EpanetMultiSpeciesResult &multi_species_result = completed_result.multi_species_result.value();

        if (completed_result.cancelled)
        {
            markPendingMultiSpeciesRun(completed_result, EpanetRunState::Cancelled);
        }
        else if (!completed_result.result_timeline.status.success || !executor.hasHydraulicFile())
        {
            markPendingMultiSpeciesRun(completed_result, EpanetRunState::Skipped);
        }
        else
        {
            multi_species_result.state = EpanetRunState::Running;
            EpanetMsxProject msx_project;
            bool multi_species_cancelled = false;
            const HydraulicSimulationStatus multi_species_status = msx_project.run(
                request.network,
                request.multi_species_run.value(),
                executor.hydraulicFilePath(),
                multi_species_result.result_timeline,
                cancellation_requested,
                multi_species_cancelled);
            multi_species_result.result_timeline.simulation_start_utc = simulation_start_utc;
            appendEpanetDiagnostics(
                completed_result.diagnostics,
                multi_species_result.result_timeline.diagnostics);

            if (multi_species_cancelled || cancellationRequested(cancellation_requested))
            {
                multi_species_result.state = EpanetRunState::Cancelled;
                completed_result.cancelled = true;
                completed_result.state = EpanetRunState::Cancelled;
            }
            else if (!multi_species_status.success)
            {
                multi_species_result.state = EpanetRunState::Error;
                if (completed_result.status.success)
                    completed_result.status = multi_species_status;
                appendEpanetDiagnosticIfUnique(
                    completed_result.diagnostics,
                    epanetDiagnosticFromStatus(multi_species_status));
                completed_result.state = EpanetRunState::Error;
            }
            else
            {
                multi_species_result.state = EpanetRunState::Success;
            }
        }
    }

    finalizeReportText(completed_result, request.network);
    return completed_result;
}
