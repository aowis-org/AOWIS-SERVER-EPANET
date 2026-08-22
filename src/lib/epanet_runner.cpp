#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_diagnostic_helpers.h"
#include "internal/epanet_hydraulic_run_configurator.h"
#include "internal/epanet_inp_exporter.h"
#include "internal/epanet_inp_importer.h"
#include "internal/epanet_multi_quality_run_executor.h"
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
    const QList<WaterQualitySolverOptions> &quality_runs,
    const QDateTime &simulation_start_utc)
{
    EpanetResultRun result;
    result.status = makeEpanetSuccess();
    result.result_timeline.simulation_start_utc = simulation_start_utc;

    for (const WaterQualitySolverOptions &quality_options : quality_runs)
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

EpanetResultRun cancelledRun(EpanetResultRun result, const EpanetPreparedProject &prepared_project)
{
    result.cancelled = true;
    result.state = EpanetRunState::Cancelled;
    markPendingQualityRuns(result, EpanetRunState::Cancelled);
    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    return result;
}

EpanetResultRun failedRun(
    EpanetResultRun result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project)
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
    EpanetResultRun result = initializeRunResult(request.quality_runs, simulation_start_utc);
    EpanetPreparedProject prepared_project;

    if (cancellationRequested(cancellation_requested))
        return cancelledRun(std::move(result), prepared_project);

    const HydraulicSimulationStatus status = prepared_project.prepare(request.network);
    if (cancellationRequested(cancellation_requested))
        return cancelledRun(std::move(result), prepared_project);

    if (!status.success)
        return failedRun(std::move(result), status, prepared_project);

    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    EpanetMultiQualityRunExecutor executor(prepared_project);
    return executor.run(std::move(result), cancellation_requested);
}
