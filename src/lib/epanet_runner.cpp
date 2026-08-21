#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_batch_executor.h"
#include "internal/epanet_diagnostic_helpers.h"
#include "internal/epanet_hydraulic_run_configurator.h"
#include "internal/epanet_inp_exporter.h"
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


EpanetResultBatch initializeBatchResult(const EpanetBatchPlan &plan, const QDateTime &simulation_start_utc)
{
    EpanetResultBatch result;
    result.status = makeEpanetSuccess();

    for (const HydraulicHeadlossFormula formula : plan.headloss_formulas)
    {
        EpanetBatchHydraulicResult hydraulic_result;
        hydraulic_result.headloss_formula = formula;
        hydraulic_result.result_timeline.simulation_start_utc = simulation_start_utc;

        for (const WaterQualitySolverOptions &quality_options : plan.quality_runs)
        {
            EpanetBatchQualityResult quality_result;
            quality_result.options = quality_options;
            quality_result.result_timeline.analysis = quality_options.analysis;
            quality_result.result_timeline.simulation_start_utc = simulation_start_utc;
            hydraulic_result.quality_results.append(quality_result);
        }

        result.hydraulic_runs.append(hydraulic_result);
    }

    return result;
}

void markBatchPendingState(EpanetResultBatch &result, EpanetBatchRunState state)
{
    for (EpanetBatchHydraulicResult &hydraulic_result : result.hydraulic_runs)
    {
        if (hydraulic_result.state == EpanetBatchRunState::Pending)
            hydraulic_result.state = state;

        for (EpanetBatchQualityResult &quality_result : hydraulic_result.quality_results)
        {
            if (quality_result.state == EpanetBatchRunState::Pending)
                quality_result.state = state;
        }
    }
}

EpanetResultBatch cancelledBatch(EpanetResultBatch result, const EpanetPreparedProject &prepared_project)
{
    result.cancelled = true;
    result.state = EpanetBatchRunState::Cancelled;
    markBatchPendingState(result, EpanetBatchRunState::Cancelled);
    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    return result;
}

EpanetResultBatch failedBatch(
    EpanetResultBatch result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project)
{
    result.status = status;
    result.state = EpanetBatchRunState::Error;
    markBatchPendingState(result, EpanetBatchRunState::Skipped);
    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    if (!status.success)
        appendEpanetDiagnosticIfUnique(result.diagnostics, epanetDiagnosticFromStatus(status));
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

EpanetResultBatch EpanetRunner::runBatch(const EpanetBatchRequest &request) const
{
    return runBatch(request, std::function<bool()>());
}

EpanetResultBatch EpanetRunner::runBatch(
    const EpanetBatchRequest &request,
    const std::function<bool()> &cancellation_requested) const
{
    const QDateTime simulation_start_utc = QDateTime::currentDateTimeUtc();
    EpanetResultBatch result = initializeBatchResult(request.plan, simulation_start_utc);
    EpanetPreparedProject prepared_project;

    if (request.plan.headloss_formulas.isEmpty())
    {
        const HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureHydraulics,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("EPANET batch execution requires at least one headloss formula"));
        return failedBatch(std::move(result), status, prepared_project);
    }

    if (cancellationRequested(cancellation_requested))
        return cancelledBatch(std::move(result), prepared_project);

    NetworkHydraulic preparation_network = request.network;
    preparation_network.options_hydraulic.headloss_formula = request.plan.headloss_formulas.first();
    preparation_network.options_quality.analysis = WaterQualityAnalysisType::None;
    preparation_network.options_quality.trace_node_uuid = QUuid();

    const HydraulicSimulationStatus status = prepared_project.prepare(preparation_network);
    if (cancellationRequested(cancellation_requested))
        return cancelledBatch(std::move(result), prepared_project);

    if (!status.success)
        return failedBatch(std::move(result), status, prepared_project);

    appendEpanetDiagnostics(result.diagnostics, prepared_project.project().diagnostics());
    EpanetBatchExecutor executor(prepared_project);
    return executor.run(std::move(result), cancellation_requested);
}

