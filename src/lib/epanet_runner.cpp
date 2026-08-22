#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_diagnostic_helpers.h"
#include "internal/epanet_hydraulic_run_configurator.h"
#include "internal/epanet_inp_exporter.h"
#include "internal/epanet_multi_quality_run_executor.h"
#include "internal/epanet_prepared_project.h"
#include "internal/epanet_quality_run_configurator.h"
#include "internal/epanet_report_configurator.h"
#include "internal/epanet_result_finalizer.h"
#include "internal/epanet_single_run_executor.h"
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

    updateCompatibilityQualityTimeline(result);
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
    updateCompatibilityQualityTimeline(result);
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
    finalizeEpanetHydraulicResultValidity(result.result_timeline);
    markPendingQualityRuns(result, EpanetRunState::Skipped);
    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    if (!status.success)
        appendEpanetDiagnosticIfUnique(result.diagnostics, epanetDiagnosticFromStatus(status));
    updateCompatibilityQualityTimeline(result);
    return result;
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

EpanetResultInp EpanetRunner::retrieveInp(const NetworkHydraulic &request) const
{
    EpanetResultInp result;
    EpanetPreparedProject prepared_project;

    HydraulicSimulationStatus status = prepared_project.prepare(request);
    if (!status.success)
        return finishInp(std::move(result), status, prepared_project);

    status = configureProjectRun(prepared_project);
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

    NetworkHydraulic preparation_network = request.network;
    preparation_network.options_quality.analysis = WaterQualityAnalysisType::None;
    preparation_network.options_quality.trace_node_uuid = QUuid();

    const HydraulicSimulationStatus status = prepared_project.prepare(preparation_network);
    if (cancellationRequested(cancellation_requested))
        return cancelledRun(std::move(result), prepared_project);

    if (!status.success)
        return failedRun(std::move(result), status, prepared_project);

    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    EpanetMultiQualityRunExecutor executor(prepared_project);
    return executor.run(std::move(result), cancellation_requested);
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

    EpanetPreparedProject prepared_project;

    if (cancellationRequested(cancellation_requested))
        return finalizeEpanetCancelledRun(std::move(result), prepared_project);

    HydraulicSimulationStatus status = prepared_project.prepare(request);
    if (cancellationRequested(cancellation_requested))
        return finalizeEpanetCancelledRun(std::move(result), prepared_project);

    if (!status.success)
        return finalizeEpanetFailedRun(std::move(result), status, prepared_project);

    EpanetSingleRunExecutor executor(prepared_project);
    return executor.run(std::move(result), cancellation_requested);
}
