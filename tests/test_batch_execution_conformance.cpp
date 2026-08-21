#include <aowis/epanet/epanet_batch_request.h>
#include <aowis/epanet/epanet_result_batch.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/batch_execution_scenarios.h"
#include "conformance/conformance_test_framework.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::HydraulicQuantity;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr NumericTolerance kQualityTolerance{1.0e-8, 1.0e-7};

ComparisonContext comparison(
    std::string field,
    std::int64_t time_s = -1,
    std::string entity_type = {},
    std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

NetworkHydraulic batchFixtureNetwork()
{
    NetworkHydraulic network;
    network.id = QStringLiteral("batch-reuse-conformance");
    network.uuid = QUuid::createUuid();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;
    network.timestep_report_s = 1800;

    network.options_reaction.global_pipe_bulk_reaction.coefficient = -0.20;
    network.options_reaction.global_pipe_bulk_reaction.order = 1.0;
    network.options_reaction.global_pipe_wall_reaction.coefficient = -0.10;
    network.options_reaction.global_pipe_wall_reaction.order = 1.0;
    network.options_reaction.roughness_reaction_factor = -2.0;

    HydraulicNodeReservoir source;
    source.id = QStringLiteral("R_SOURCE");
    source.uuid = QUuid::createUuid();
    source.hydraulic_head_m = 100.0;
    source.initial_chemical_concentration_mg_per_l = 1.0;
    source.quality_source.type = HydraulicNodeQualitySourceType::Concentration;
    source.quality_source.chemical_concentration_mg_per_l = 1.0;

    HydraulicNodeJunction middle;
    middle.id = QStringLiteral("J_MIDDLE");
    middle.uuid = QUuid::createUuid();
    middle.elevation_m = 10.0;

    HydraulicNodeJunction sink;
    sink.id = QStringLiteral("J_SINK");
    sink.uuid = QUuid::createUuid();
    sink.elevation_m = 5.0;
    HydraulicNodeJunctionDemand demand;
    demand.base_demand_m3_per_h = 12.0;
    sink.demands.append(demand);

    HydraulicLinkPipe first_pipe;
    first_pipe.id = QStringLiteral("P_FIRST");
    first_pipe.uuid = QUuid::createUuid();
    first_pipe.node_uuid_from = source.uuid;
    first_pipe.node_uuid_to = middle.uuid;
    first_pipe.length_calculated_m = 700.0;
    first_pipe.diameter_mm = 200.0;
    first_pipe.roughness_hazen_williams = 120.0;
    first_pipe.roughness_darcy_weisbach_mm = 0.30;
    first_pipe.roughness_chezy_manning = 0.013;

    HydraulicLinkPipe second_pipe;
    second_pipe.id = QStringLiteral("P_SECOND");
    second_pipe.uuid = QUuid::createUuid();
    second_pipe.node_uuid_from = middle.uuid;
    second_pipe.node_uuid_to = sink.uuid;
    second_pipe.length_calculated_m = 900.0;
    second_pipe.diameter_mm = 150.0;
    second_pipe.roughness_hazen_williams = 105.0;
    second_pipe.roughness_darcy_weisbach_mm = 0.45;
    second_pipe.roughness_chezy_manning = 0.016;

    network.nodes_reservoirs.append(source);
    network.nodes_junctions.append(middle);
    network.nodes_junctions.append(sink);
    network.links_pipes.append(first_pipe);
    network.links_pipes.append(second_pipe);
    return network;
}

WaterQualitySolverOptions chemicalOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::Chemical;
    options.chemical_name = QStringLiteral("Chlorine");
    return options;
}

WaterQualitySolverOptions waterAgeOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::WaterAge;
    return options;
}

WaterQualitySolverOptions traceOptions(const QUuid &trace_node_uuid)
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::SourceTrace;
    options.trace_node_uuid = trace_node_uuid;
    return options;
}

EpanetBatchRequest batchRequest(
    const NetworkHydraulic &network,
    const QList<HydraulicHeadlossFormula> &formulas,
    const QList<WaterQualitySolverOptions> &quality_runs)
{
    EpanetBatchRequest request;
    request.network = network;
    request.plan.headloss_formulas = formulas;
    request.plan.quality_runs = quality_runs;
    return request;
}

EpanetResultRun isolatedRun(
    NetworkHydraulic network,
    HydraulicHeadlossFormula formula,
    const WaterQualitySolverOptions &quality_options)
{
    network.options_hydraulic.headloss_formula = formula;
    network.options_quality = quality_options;
    return EpanetRunner().run(network);
}

const EpanetBatchHydraulicResult *findHydraulicResult(
    const EpanetResultBatch &result,
    HydraulicHeadlossFormula formula)
{
    for (const EpanetBatchHydraulicResult &hydraulic_result : result.hydraulic_runs)
    {
        if (hydraulic_result.headloss_formula == formula)
            return &hydraulic_result;
    }
    return nullptr;
}

const EpanetBatchQualityResult *findQualityResult(
    const EpanetBatchHydraulicResult &hydraulic_result,
    const WaterQualitySolverOptions &options)
{
    for (const EpanetBatchQualityResult &quality_result : hydraulic_result.quality_results)
    {
        if (quality_result.options.analysis != options.analysis)
            continue;
        if (quality_result.options.analysis == WaterQualityAnalysisType::SourceTrace
            && quality_result.options.trace_node_uuid != options.trace_node_uuid)
        {
            continue;
        }
        return &quality_result;
    }
    return nullptr;
}

void compareOptionalRoughness(
    TestContext &context,
    const std::optional<double> &actual,
    const std::optional<double> &expected,
    const ComparisonContext &comparison_value)
{
    context.expectEqual(actual.has_value(), expected.has_value(), comparison_value, "formula-specific roughness presence must match the isolated run");
    if (actual.has_value() && expected.has_value())
        context.expectNear(*actual, *expected, NumericTolerance{1.0e-12, 1.0e-9}, comparison_value, "formula-specific roughness must match the isolated run");
}

void compareHydraulicTimelines(
    TestContext &context,
    const HydraulicSimulationResultTimeline &actual,
    const HydraulicSimulationResultTimeline &expected,
    const std::string &prefix)
{
    context.expect(actual.validity == expected.validity, prefix + ": hydraulic validity must match isolated execution");
    context.expectEqual(actual.status.success, expected.status.success, comparison(prefix + ".status.success"), "hydraulic status must match isolated execution");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        comparison(prefix + ".results.size"),
        "hydraulic timestep count must match isolated execution");

    const qsizetype count = qMin(actual.results.size(), expected.results.size());
    for (qsizetype step_index = 0; step_index < count; step_index++)
    {
        const HydraulicSimulationResult &actual_step = actual.results.at(step_index);
        const HydraulicSimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(expected_step.time_elapsed_s);

        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            time_s,
            comparison(prefix + ".time_elapsed_s", time_s),
            "hydraulic timestep identity must match isolated execution");
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.nodes_junctions.size()),
            static_cast<std::int64_t>(expected_step.nodes_junctions.size()),
            comparison(prefix + ".nodes_junctions.size", time_s),
            "junction result count must match isolated execution");
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.nodes_reservoirs.size()),
            static_cast<std::int64_t>(expected_step.nodes_reservoirs.size()),
            comparison(prefix + ".nodes_reservoirs.size", time_s),
            "reservoir result count must match isolated execution");
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.links_pipes.size()),
            static_cast<std::int64_t>(expected_step.links_pipes.size()),
            comparison(prefix + ".links_pipes.size", time_s),
            "pipe result count must match isolated execution");

        const qsizetype junction_count = qMin(actual_step.nodes_junctions.size(), expected_step.nodes_junctions.size());
        for (qsizetype index = 0; index < junction_count; index++)
        {
            const HydraulicSimulationResultNodeJunction &actual_node = actual_step.nodes_junctions.at(index);
            const HydraulicSimulationResultNodeJunction &expected_node = expected_step.nodes_junctions.at(index);
            const std::string entity_id = expected_node.id.toStdString();
            context.expectEqual(actual_node.id.toStdString(), expected_node.id.toStdString(), comparison(prefix + ".junction.id", time_s, "Junction", entity_id));
            context.expectNear(actual_node.demand_requested_m3_per_h, expected_node.demand_requested_m3_per_h, HydraulicQuantity::FlowM3PerHour, comparison(prefix + ".demand_requested", time_s, "Junction", entity_id));
            context.expectNear(actual_node.demand_delivered_m3_per_h, expected_node.demand_delivered_m3_per_h, HydraulicQuantity::FlowM3PerHour, comparison(prefix + ".demand_delivered", time_s, "Junction", entity_id));
            context.expectNear(actual_node.total_demand_m3_per_h, expected_node.total_demand_m3_per_h, HydraulicQuantity::FlowM3PerHour, comparison(prefix + ".total_demand", time_s, "Junction", entity_id));
            context.expectNear(actual_node.hydraulic_head_m, expected_node.hydraulic_head_m, HydraulicQuantity::HeadMetres, comparison(prefix + ".hydraulic_head", time_s, "Junction", entity_id));
            context.expectNear(actual_node.pressure_head_m, expected_node.pressure_head_m, HydraulicQuantity::PressureHeadMetres, comparison(prefix + ".pressure_head", time_s, "Junction", entity_id));
        }

        const qsizetype reservoir_count = qMin(actual_step.nodes_reservoirs.size(), expected_step.nodes_reservoirs.size());
        for (qsizetype index = 0; index < reservoir_count; index++)
        {
            const HydraulicSimulationResultNodeReservoir &actual_node = actual_step.nodes_reservoirs.at(index);
            const HydraulicSimulationResultNodeReservoir &expected_node = expected_step.nodes_reservoirs.at(index);
            const std::string entity_id = expected_node.id.toStdString();
            context.expectNear(actual_node.net_demand_m3_per_h, expected_node.net_demand_m3_per_h, HydraulicQuantity::FlowM3PerHour, comparison(prefix + ".net_demand", time_s, "Reservoir", entity_id));
            context.expectNear(actual_node.hydraulic_head_m, expected_node.hydraulic_head_m, HydraulicQuantity::HeadMetres, comparison(prefix + ".hydraulic_head", time_s, "Reservoir", entity_id));
        }

        const qsizetype pipe_count = qMin(actual_step.links_pipes.size(), expected_step.links_pipes.size());
        for (qsizetype index = 0; index < pipe_count; index++)
        {
            const HydraulicSimulationResultLinkPipe &actual_pipe = actual_step.links_pipes.at(index);
            const HydraulicSimulationResultLinkPipe &expected_pipe = expected_step.links_pipes.at(index);
            const std::string entity_id = expected_pipe.id.toStdString();
            context.expectNear(actual_pipe.flow_m3_per_h, expected_pipe.flow_m3_per_h, HydraulicQuantity::FlowM3PerHour, comparison(prefix + ".flow", time_s, "Pipe", entity_id));
            context.expectNear(actual_pipe.velocity_m_per_s, expected_pipe.velocity_m_per_s, HydraulicQuantity::VelocityMetresPerSecond, comparison(prefix + ".velocity", time_s, "Pipe", entity_id));
            context.expectNear(actual_pipe.head_loss_m, expected_pipe.head_loss_m, HydraulicQuantity::HeadMetres, comparison(prefix + ".head_loss", time_s, "Pipe", entity_id));
            context.expectNear(actual_pipe.head_loss_gradient_m_per_km, expected_pipe.head_loss_gradient_m_per_km, NumericTolerance{1.0e-7, 1.0e-6}, comparison(prefix + ".head_loss_gradient", time_s, "Pipe", entity_id));
            context.expectNear(actual_pipe.friction_factor, expected_pipe.friction_factor, HydraulicQuantity::FrictionFactor, comparison(prefix + ".friction_factor", time_s, "Pipe", entity_id));
            context.expectEqual(actual_pipe.open, expected_pipe.open, comparison(prefix + ".open", time_s, "Pipe", entity_id));
            compareOptionalRoughness(context, actual_pipe.roughness_hazen_williams, expected_pipe.roughness_hazen_williams, comparison(prefix + ".roughness_hazen_williams", time_s, "Pipe", entity_id));
            compareOptionalRoughness(context, actual_pipe.roughness_darcy_weisbach_mm, expected_pipe.roughness_darcy_weisbach_mm, comparison(prefix + ".roughness_darcy_weisbach_mm", time_s, "Pipe", entity_id));
            compareOptionalRoughness(context, actual_pipe.roughness_chezy_manning, expected_pipe.roughness_chezy_manning, comparison(prefix + ".roughness_chezy_manning", time_s, "Pipe", entity_id));
        }

        context.expectEqual(
            static_cast<std::int64_t>(actual_step.statistics.hydraulic_iterations),
            static_cast<std::int64_t>(expected_step.statistics.hydraulic_iterations),
            comparison(prefix + ".statistics.hydraulic_iterations", time_s),
            "solver iteration count must match isolated execution");
        context.expectNear(actual_step.statistics.relative_error, expected_step.statistics.relative_error, NumericTolerance{1.0e-12, 1.0e-8}, comparison(prefix + ".statistics.relative_error", time_s));
        context.expectNear(actual_step.flow_balance.flow_balance_ratio, expected_step.flow_balance.flow_balance_ratio, NumericTolerance{1.0e-9, 1.0e-7}, comparison(prefix + ".flow_balance_ratio", time_s));
    }
}

template<typename ResultType>
void compareQualityNodes(
    TestContext &context,
    const QList<ResultType> &actual,
    const QList<ResultType> &expected,
    std::int64_t time_s,
    const std::string &prefix,
    const char *entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        comparison(prefix + ".size", time_s, entity_type),
        "quality node result count must match isolated execution");
    const qsizetype count = qMin(actual.size(), expected.size());
    for (qsizetype index = 0; index < count; index++)
    {
        const ResultType &actual_node = actual.at(index);
        const ResultType &expected_node = expected.at(index);
        const std::string entity_id = expected_node.id.toStdString();
        context.expectEqual(actual_node.id.toStdString(), expected_node.id.toStdString(), comparison(prefix + ".id", time_s, entity_type, entity_id));
        context.expectNear(actual_node.chemical_concentration_mg_per_l, expected_node.chemical_concentration_mg_per_l, kQualityTolerance, comparison(prefix + ".chemical_concentration", time_s, entity_type, entity_id));
        context.expectNear(actual_node.water_age_h, expected_node.water_age_h, kQualityTolerance, comparison(prefix + ".water_age", time_s, entity_type, entity_id));
        context.expectNear(actual_node.source_trace_percent, expected_node.source_trace_percent, kQualityTolerance, comparison(prefix + ".source_trace", time_s, entity_type, entity_id));
        context.expectNear(actual_node.source_mass_flow_mg_per_min, expected_node.source_mass_flow_mg_per_min, kQualityTolerance, comparison(prefix + ".source_mass_flow", time_s, entity_type, entity_id));
    }
}

template<typename ResultType>
void compareQualityLinks(
    TestContext &context,
    const QList<ResultType> &actual,
    const QList<ResultType> &expected,
    std::int64_t time_s,
    const std::string &prefix,
    const char *entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        comparison(prefix + ".size", time_s, entity_type),
        "quality link result count must match isolated execution");
    const qsizetype count = qMin(actual.size(), expected.size());
    for (qsizetype index = 0; index < count; index++)
    {
        const ResultType &actual_link = actual.at(index);
        const ResultType &expected_link = expected.at(index);
        const std::string entity_id = expected_link.id.toStdString();
        context.expectEqual(actual_link.id.toStdString(), expected_link.id.toStdString(), comparison(prefix + ".id", time_s, entity_type, entity_id));
        context.expectNear(actual_link.chemical_concentration_mg_per_l, expected_link.chemical_concentration_mg_per_l, kQualityTolerance, comparison(prefix + ".chemical_concentration", time_s, entity_type, entity_id));
        context.expectNear(actual_link.water_age_h, expected_link.water_age_h, kQualityTolerance, comparison(prefix + ".water_age", time_s, entity_type, entity_id));
        context.expectNear(actual_link.source_trace_percent, expected_link.source_trace_percent, kQualityTolerance, comparison(prefix + ".source_trace", time_s, entity_type, entity_id));
    }
}

void compareQualityTimelines(
    TestContext &context,
    const WaterQualitySimulationResultTimeline &actual,
    const WaterQualitySimulationResultTimeline &expected,
    const std::string &prefix)
{
    context.expect(actual.analysis == expected.analysis, prefix + ": quality analysis identity must match isolated execution");
    context.expect(actual.validity == expected.validity, prefix + ": quality validity must match isolated execution");
    context.expectEqual(actual.status.success, expected.status.success, comparison(prefix + ".status.success"), "quality status must match isolated execution");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        comparison(prefix + ".results.size"),
        "quality timestep count must match isolated execution");

    const qsizetype count = qMin(actual.results.size(), expected.results.size());
    for (qsizetype step_index = 0; step_index < count; step_index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(step_index);
        const WaterQualitySimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(expected_step.time_elapsed_s);
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            time_s,
            comparison(prefix + ".time_elapsed_s", time_s),
            "quality timestep identity must match isolated execution");
        compareQualityNodes(context, actual_step.nodes_junctions, expected_step.nodes_junctions, time_s, prefix + ".junctions", "Junction");
        compareQualityNodes(context, actual_step.nodes_reservoirs, expected_step.nodes_reservoirs, time_s, prefix + ".reservoirs", "Reservoir");
        compareQualityNodes(context, actual_step.nodes_tanks, expected_step.nodes_tanks, time_s, prefix + ".tanks", "Tank");
        compareQualityLinks(context, actual_step.links_pipes, expected_step.links_pipes, time_s, prefix + ".pipes", "Pipe");
        compareQualityLinks(context, actual_step.links_pumps, expected_step.links_pumps, time_s, prefix + ".pumps", "Pump");
        compareQualityLinks(context, actual_step.links_valves, expected_step.links_valves, time_s, prefix + ".valves", "Valve");
        context.expectNear(actual_step.statistics.mass_balance_ratio, expected_step.statistics.mass_balance_ratio, NumericTolerance{1.0e-8, 1.0e-7}, comparison(prefix + ".mass_balance_ratio", time_s));
    }
}

void compareBatchBranchWithIsolated(
    TestContext &context,
    const NetworkHydraulic &network,
    const EpanetBatchHydraulicResult &batch_hydraulic,
    const WaterQualitySolverOptions &quality_options,
    const std::string &prefix)
{
    const EpanetResultRun isolated = isolatedRun(network, batch_hydraulic.headloss_formula, quality_options);
    compareHydraulicTimelines(context, batch_hydraulic.result_timeline, isolated.result_timeline, prefix + ".hydraulic");

    const EpanetBatchQualityResult *batch_quality = findQualityResult(batch_hydraulic, quality_options);
    context.expect(batch_quality != nullptr, prefix + ": batch branch must contain the requested quality child");
    if (batch_quality != nullptr)
        compareQualityTimelines(context, batch_quality->result_timeline, isolated.quality_result_timeline, prefix + ".quality");
}

void testBatchIsolatedEquivalence(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions trace = traceOptions(network.nodes_reservoirs.first().uuid);
    const QList<HydraulicHeadlossFormula> formulas = {
        HydraulicHeadlossFormula::HazenWilliams,
        HydraulicHeadlossFormula::DarcyWeisbach,
        HydraulicHeadlossFormula::ChezyManning
    };
    const QList<WaterQualitySolverOptions> quality_runs = {chemical, water_age, trace};

    const EpanetResultBatch batch = EpanetRunner().runBatch(batchRequest(network, formulas, quality_runs));
    context.expect(batch.state == EpanetBatchRunState::Success, "isolated-equivalence batch must succeed");

    for (const HydraulicHeadlossFormula formula : formulas)
    {
        const EpanetBatchHydraulicResult *batch_hydraulic = findHydraulicResult(batch, formula);
        context.expect(batch_hydraulic != nullptr, "batch must contain every requested hydraulic formula");
        if (batch_hydraulic == nullptr)
            continue;

        context.expect(batch_hydraulic->state == EpanetBatchRunState::Success, "batch hydraulic branch must succeed");
        for (const WaterQualitySolverOptions &quality_options : quality_runs)
        {
            compareBatchBranchWithIsolated(
                context,
                network,
                *batch_hydraulic,
                quality_options,
                std::string("formula-") + std::to_string(static_cast<int>(formula)) + "-quality-" + std::to_string(static_cast<int>(quality_options.analysis)));
        }
    }
}

void testBatchOrderIndependence(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions trace = traceOptions(network.nodes_reservoirs.first().uuid);

    const QList<HydraulicHeadlossFormula> forward_formulas = {
        HydraulicHeadlossFormula::HazenWilliams,
        HydraulicHeadlossFormula::DarcyWeisbach,
        HydraulicHeadlossFormula::ChezyManning
    };
    const QList<HydraulicHeadlossFormula> reverse_formulas = {
        HydraulicHeadlossFormula::ChezyManning,
        HydraulicHeadlossFormula::DarcyWeisbach,
        HydraulicHeadlossFormula::HazenWilliams
    };
    const QList<WaterQualitySolverOptions> forward_quality = {chemical, water_age, trace};
    const QList<WaterQualitySolverOptions> reverse_quality = {trace, water_age, chemical};

    const EpanetResultBatch forward = EpanetRunner().runBatch(batchRequest(network, forward_formulas, forward_quality));
    const EpanetResultBatch reverse = EpanetRunner().runBatch(batchRequest(network, reverse_formulas, reverse_quality));
    context.expect(forward.state == EpanetBatchRunState::Success, "forward-order batch must succeed");
    context.expect(reverse.state == EpanetBatchRunState::Success, "reverse-order batch must succeed");

    for (const HydraulicHeadlossFormula formula : forward_formulas)
    {
        const EpanetBatchHydraulicResult *forward_hydraulic = findHydraulicResult(forward, formula);
        const EpanetBatchHydraulicResult *reverse_hydraulic = findHydraulicResult(reverse, formula);
        context.expect(forward_hydraulic != nullptr && reverse_hydraulic != nullptr, "both execution orders must contain every hydraulic branch");
        if (forward_hydraulic == nullptr || reverse_hydraulic == nullptr)
            continue;

        const std::string prefix = std::string("order-formula-") + std::to_string(static_cast<int>(formula));
        compareHydraulicTimelines(context, reverse_hydraulic->result_timeline, forward_hydraulic->result_timeline, prefix + ".hydraulic");
        for (const WaterQualitySolverOptions &quality_options : forward_quality)
        {
            const EpanetBatchQualityResult *forward_child = findQualityResult(*forward_hydraulic, quality_options);
            const EpanetBatchQualityResult *reverse_child = findQualityResult(*reverse_hydraulic, quality_options);
            context.expect(forward_child != nullptr && reverse_child != nullptr, "both execution orders must contain every quality child");
            if (forward_child != nullptr && reverse_child != nullptr)
                compareQualityTimelines(context, reverse_child->result_timeline, forward_child->result_timeline, prefix + ".quality-" + std::to_string(static_cast<int>(quality_options.analysis)));
        }
    }
}

void testBatchRepeatedSourceTrace(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const WaterQualitySolverOptions trace_source = traceOptions(network.nodes_reservoirs.first().uuid);
    const WaterQualitySolverOptions trace_middle = traceOptions(network.nodes_junctions.first().uuid);
    const QList<HydraulicHeadlossFormula> formulas = {HydraulicHeadlossFormula::DarcyWeisbach};
    const QList<WaterQualitySolverOptions> quality_runs = {trace_source, trace_middle};

    const EpanetResultBatch batch = EpanetRunner().runBatch(batchRequest(network, formulas, quality_runs));
    context.expect(batch.state == EpanetBatchRunState::Success, "repeated source-trace batch must succeed");
    context.expectEqual(static_cast<std::int64_t>(batch.hydraulic_runs.size()), std::int64_t{1}, comparison("trace.hydraulic_runs.size"));
    if (batch.hydraulic_runs.isEmpty())
        return;

    const EpanetBatchHydraulicResult &hydraulic = batch.hydraulic_runs.first();
    context.expectEqual(static_cast<std::int64_t>(hydraulic.quality_results.size()), std::int64_t{2}, comparison("trace.quality_results.size"));
    const EpanetBatchQualityResult *source_child = findQualityResult(hydraulic, trace_source);
    const EpanetBatchQualityResult *middle_child = findQualityResult(hydraulic, trace_middle);
    context.expect(source_child != nullptr, "first source-trace child must remain identifiable by trace node");
    context.expect(middle_child != nullptr, "second source-trace child must remain identifiable by trace node");

    const EpanetResultRun isolated_source = isolatedRun(network, HydraulicHeadlossFormula::DarcyWeisbach, trace_source);
    const EpanetResultRun isolated_middle = isolatedRun(network, HydraulicHeadlossFormula::DarcyWeisbach, trace_middle);
    if (source_child != nullptr)
        compareQualityTimelines(context, source_child->result_timeline, isolated_source.quality_result_timeline, "trace-source");
    if (middle_child != nullptr)
        compareQualityTimelines(context, middle_child->result_timeline, isolated_middle.quality_result_timeline, "trace-middle");
}

void testBatchQualityFailureIsolation(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions invalid_trace = traceOptions(QUuid::createUuid());
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const QList<HydraulicHeadlossFormula> formulas = {HydraulicHeadlossFormula::HazenWilliams};
    const QList<WaterQualitySolverOptions> quality_runs = {water_age, invalid_trace, chemical};

    const EpanetResultBatch batch = EpanetRunner().runBatch(batchRequest(network, formulas, quality_runs));
    context.expect(batch.state == EpanetBatchRunState::Error, "one failed quality child must make the aggregate batch erroneous");
    context.expect(!batch.cancelled, "quality configuration failure must not cancel the batch");
    context.expectEqual(static_cast<std::int64_t>(batch.hydraulic_runs.size()), std::int64_t{1}, comparison("quality-failure.hydraulic_runs.size"));
    if (batch.hydraulic_runs.isEmpty())
        return;

    const EpanetBatchHydraulicResult &hydraulic = batch.hydraulic_runs.first();
    context.expect(hydraulic.state == EpanetBatchRunState::Success, "quality-child failure must not downgrade completed hydraulics");
    const EpanetBatchQualityResult *age_child = findQualityResult(hydraulic, water_age);
    const EpanetBatchQualityResult *invalid_child = findQualityResult(hydraulic, invalid_trace);
    const EpanetBatchQualityResult *chemical_child = findQualityResult(hydraulic, chemical);
    context.expect(age_child != nullptr && age_child->state == EpanetBatchRunState::Success, "quality child before the failure must remain successful");
    context.expect(invalid_child != nullptr && invalid_child->state == EpanetBatchRunState::Error, "invalid source trace must fail only its own child");
    context.expect(chemical_child != nullptr && chemical_child->state == EpanetBatchRunState::Success, "quality child after the failure must still execute successfully");

    if (invalid_child != nullptr)
        context.expect(!invalid_child->result_timeline.diagnostics.isEmpty(), "failed quality child must retain its diagnostics");
    if (chemical_child != nullptr)
    {
        context.expect(chemical_child->result_timeline.diagnostics.isEmpty(), "later successful quality child must not inherit diagnostics from the failed child");
        const EpanetResultRun isolated = isolatedRun(network, HydraulicHeadlossFormula::HazenWilliams, chemical);
        compareQualityTimelines(context, chemical_child->result_timeline, isolated.quality_result_timeline, "quality-failure-later-child");
    }
}

void testBatchHydraulicFailureIsolation(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const HydraulicHeadlossFormula invalid_formula = static_cast<HydraulicHeadlossFormula>(999);
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const QList<HydraulicHeadlossFormula> formulas = {
        HydraulicHeadlossFormula::HazenWilliams,
        invalid_formula,
        HydraulicHeadlossFormula::DarcyWeisbach
    };
    const QList<WaterQualitySolverOptions> quality_runs = {water_age};

    const EpanetResultBatch batch = EpanetRunner().runBatch(batchRequest(network, formulas, quality_runs));
    context.expect(batch.state == EpanetBatchRunState::Error, "one failed hydraulic branch must make the aggregate batch erroneous");
    context.expect(!batch.cancelled, "hydraulic configuration failure must not cancel the batch");
    context.expectEqual(static_cast<std::int64_t>(batch.hydraulic_runs.size()), std::int64_t{3}, comparison("hydraulic-failure.hydraulic_runs.size"));
    if (batch.hydraulic_runs.size() != 3)
        return;

    const EpanetBatchHydraulicResult &first = batch.hydraulic_runs.at(0);
    const EpanetBatchHydraulicResult &failed = batch.hydraulic_runs.at(1);
    const EpanetBatchHydraulicResult &later = batch.hydraulic_runs.at(2);
    context.expect(first.state == EpanetBatchRunState::Success, "hydraulic branch before the failure must remain successful");
    context.expect(failed.state == EpanetBatchRunState::Error, "unsupported headloss formula must fail only its own branch");
    context.expect(!failed.result_timeline.diagnostics.isEmpty(), "failed hydraulic branch must retain its diagnostics");
    context.expectEqual(static_cast<std::int64_t>(failed.quality_results.size()), std::int64_t{1}, comparison("hydraulic-failure.failed.quality_results.size"));
    if (!failed.quality_results.isEmpty())
        context.expect(failed.quality_results.first().state == EpanetBatchRunState::Skipped, "quality children of a failed hydraulic branch must be skipped");
    context.expect(later.state == EpanetBatchRunState::Success, "hydraulic branch after the failure must still execute successfully");
    context.expect(later.result_timeline.diagnostics.isEmpty(), "later successful hydraulic branch must not inherit diagnostics from the failed branch");

    const EpanetResultRun isolated = isolatedRun(network, HydraulicHeadlossFormula::DarcyWeisbach, water_age);
    compareHydraulicTimelines(context, later.result_timeline, isolated.result_timeline, "hydraulic-failure-later-branch");
    if (!later.quality_results.isEmpty())
        compareQualityTimelines(context, later.quality_results.first().result_timeline, isolated.quality_result_timeline, "hydraulic-failure-later-quality");
}

int countCancellationPolls(const EpanetBatchRequest &request)
{
    int poll_count = 0;
    const std::function<bool()> counter = [&poll_count]()
    {
        poll_count++;
        return false;
    };
    EpanetRunner().runBatch(request, counter);
    return poll_count;
}

void testBatchCancellationBetweenHydraulicBranches(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const EpanetBatchRequest one_branch = batchRequest(
        network,
        {HydraulicHeadlossFormula::HazenWilliams},
        {});
    const int completed_first_branch_poll_count = countCancellationPolls(one_branch);
    context.expect(completed_first_branch_poll_count > 0, "poll-count fixture must observe cancellation checks");

    const EpanetBatchRequest two_branches = batchRequest(
        network,
        {HydraulicHeadlossFormula::HazenWilliams, HydraulicHeadlossFormula::DarcyWeisbach},
        {});
    int poll_count = 0;
    const std::function<bool()> cancel_after_first_branch = [&poll_count, completed_first_branch_poll_count]()
    {
        poll_count++;
        return poll_count > completed_first_branch_poll_count;
    };
    const EpanetResultBatch batch = EpanetRunner().runBatch(two_branches, cancel_after_first_branch);

    context.expect(batch.cancelled, "batch must report cancellation between hydraulic branches");
    context.expect(batch.state == EpanetBatchRunState::Cancelled, "cancelled batch must expose Cancelled aggregate state");
    context.expectEqual(static_cast<std::int64_t>(batch.hydraulic_runs.size()), std::int64_t{2}, comparison("cancel-hydraulic.hydraulic_runs.size"));
    if (batch.hydraulic_runs.size() != 2)
        return;

    context.expect(batch.hydraulic_runs.at(0).state == EpanetBatchRunState::Success, "completed hydraulic branch must remain successful after later cancellation");
    context.expect(batch.hydraulic_runs.at(0).result_timeline.validity == HydraulicSimulationResultValidity::Valid, "completed hydraulic results must remain valid after later cancellation");
    context.expect(batch.hydraulic_runs.at(1).state == EpanetBatchRunState::Cancelled, "not-yet-started hydraulic branch must be marked cancelled");
}

void testBatchCancellationBetweenQualityRuns(TestContext &context)
{
    const NetworkHydraulic network = batchFixtureNetwork();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const EpanetBatchRequest one_quality = batchRequest(
        network,
        {HydraulicHeadlossFormula::HazenWilliams},
        {water_age});
    const int completed_first_quality_poll_count = countCancellationPolls(one_quality);
    context.expect(completed_first_quality_poll_count > 0, "quality poll-count fixture must observe cancellation checks");

    const EpanetBatchRequest two_quality = batchRequest(
        network,
        {HydraulicHeadlossFormula::HazenWilliams},
        {water_age, chemical});
    int poll_count = 0;
    const std::function<bool()> cancel_after_first_quality = [&poll_count, completed_first_quality_poll_count]()
    {
        poll_count++;
        return poll_count > completed_first_quality_poll_count;
    };
    const EpanetResultBatch batch = EpanetRunner().runBatch(two_quality, cancel_after_first_quality);

    context.expect(batch.cancelled, "batch must report cancellation between quality children");
    context.expect(batch.state == EpanetBatchRunState::Cancelled, "quality-child cancellation must expose Cancelled aggregate state");
    context.expectEqual(static_cast<std::int64_t>(batch.hydraulic_runs.size()), std::int64_t{1}, comparison("cancel-quality.hydraulic_runs.size"));
    if (batch.hydraulic_runs.isEmpty())
        return;

    const EpanetBatchHydraulicResult &hydraulic = batch.hydraulic_runs.first();
    context.expect(hydraulic.state == EpanetBatchRunState::Success, "completed hydraulics must remain successful after quality-child cancellation");
    context.expectEqual(static_cast<std::int64_t>(hydraulic.quality_results.size()), std::int64_t{2}, comparison("cancel-quality.quality_results.size"));
    if (hydraulic.quality_results.size() != 2)
        return;

    context.expect(hydraulic.quality_results.at(0).state == EpanetBatchRunState::Success, "completed quality child must remain successful after later cancellation");
    context.expect(hydraulic.quality_results.at(0).result_timeline.validity == WaterQualitySimulationResultValidity::Valid, "completed quality results must remain valid after later cancellation");
    context.expect(hydraulic.quality_results.at(1).state == EpanetBatchRunState::Cancelled, "not-yet-started quality child must be marked cancelled");
}
}

namespace AowisEpanetTests
{
void registerBatchExecutionScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-batch-isolated-equivalence",
        "Compares every reused-project hydraulic and quality batch branch with an independently constructed single-run reference.",
        {"conformance", "hydraulic", "quality", "batch", "proof"},
        &testBatchIsolatedEquivalence});
    registry.add(ScenarioDefinition{
        "conformance-batch-order-independence",
        "Runs hydraulic formulas and quality analyses in forward and reverse order and verifies that reusable EPANET project state does not leak between runs.",
        {"conformance", "hydraulic", "quality", "batch", "proof"},
        &testBatchOrderIndependence});
    registry.add(ScenarioDefinition{
        "conformance-batch-repeated-source-trace",
        "Checks multiple source-trace children with different trace nodes against isolated references.",
        {"conformance", "quality", "batch"},
        &testBatchRepeatedSourceTrace});
    registry.add(ScenarioDefinition{
        "conformance-batch-quality-failure-isolation",
        "Checks that a failed quality child retains diagnostics without poisoning later quality execution on the same hydraulic solution.",
        {"conformance", "quality", "batch", "negative"},
        &testBatchQualityFailureIsolation});
    registry.add(ScenarioDefinition{
        "conformance-batch-hydraulic-failure-isolation",
        "Checks that one unsupported hydraulic branch is isolated and later valid branches still execute on the reusable project.",
        {"conformance", "hydraulic", "quality", "batch", "negative"},
        &testBatchHydraulicFailureIsolation});
    registry.add(ScenarioDefinition{
        "conformance-batch-cancellation-between-hydraulics",
        "Checks cancellation between hydraulic branches while preserving already completed hydraulic results.",
        {"conformance", "hydraulic", "batch"},
        &testBatchCancellationBetweenHydraulicBranches});
    registry.add(ScenarioDefinition{
        "conformance-batch-cancellation-between-quality",
        "Checks cancellation between quality children while preserving completed hydraulics and quality results.",
        {"conformance", "hydraulic", "quality", "batch"},
        &testBatchCancellationBetweenQualityRuns});
}
}
