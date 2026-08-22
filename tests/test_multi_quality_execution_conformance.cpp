#include <aowis/epanet/epanet_runner.h>
#include <aowis/epanet/epanet_run_request.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/multi_quality_execution_scenarios.h"

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

NetworkHydraulic multiQualityFixtureNetwork()
{
    NetworkHydraulic network;
    network.id = QStringLiteral("multi-quality-reuse-conformance");
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

EpanetRunRequest runRequest(
    NetworkHydraulic network,
    HydraulicHeadlossFormula formula,
    const QList<WaterQualitySolverOptions> &quality_runs)
{
    network.options_hydraulic.headloss_formula = formula;

    EpanetRunRequest request;
    request.network = network;
    request.quality_runs = quality_runs;
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

const EpanetQualityResult *findQualityResult(
    const EpanetResultRun &result,
    const WaterQualitySolverOptions &options)
{
    for (const EpanetQualityResult &quality_result : result.quality_results)
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

void compareMultiQualityWithIsolated(
    TestContext &context,
    const NetworkHydraulic &network,
    HydraulicHeadlossFormula formula,
    const EpanetResultRun &multi_quality_result,
    const WaterQualitySolverOptions &quality_options,
    const std::string &prefix)
{
    const EpanetResultRun isolated = isolatedRun(network, formula, quality_options);
    compareHydraulicTimelines(context, multi_quality_result.result_timeline, isolated.result_timeline, prefix + ".hydraulic");

    const EpanetQualityResult *quality_result = findQualityResult(multi_quality_result, quality_options);
    context.expect(quality_result != nullptr, prefix + ": multi-quality result must contain the requested quality child");
    if (quality_result != nullptr)
        compareQualityTimelines(context, quality_result->result_timeline, isolated.quality_result_timeline, prefix + ".quality");
}

void testMultiQualityIsolatedEquivalence(TestContext &context)
{
    const NetworkHydraulic network = multiQualityFixtureNetwork();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions trace = traceOptions(network.nodes_reservoirs.first().uuid);
    const QList<WaterQualitySolverOptions> quality_runs = {chemical, water_age, trace};
    const QList<HydraulicHeadlossFormula> formulas = {
        HydraulicHeadlossFormula::HazenWilliams,
        HydraulicHeadlossFormula::DarcyWeisbach,
        HydraulicHeadlossFormula::ChezyManning
    };

    for (const HydraulicHeadlossFormula formula : formulas)
    {
        const EpanetResultRun result = EpanetRunner().run(runRequest(network, formula, quality_runs));
        context.expect(result.state == EpanetRunState::Success, "multi-quality run must succeed for each supported headloss formula");
        context.expect(result.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "multi-quality run must retain one valid hydraulic timeline");
        context.expectEqual(
            static_cast<std::int64_t>(result.quality_results.size()),
            static_cast<std::int64_t>(quality_runs.size()),
            comparison("isolated-equivalence.quality_results.size"),
            "multi-quality run must return every requested quality result");

        for (const WaterQualitySolverOptions &quality_options : quality_runs)
        {
            compareMultiQualityWithIsolated(
                context,
                network,
                formula,
                result,
                quality_options,
                std::string("formula-")
                    + std::to_string(static_cast<int>(formula))
                    + "-quality-"
                    + std::to_string(static_cast<int>(quality_options.analysis)));
        }
    }
}

void testMultiQualityOrderIndependence(TestContext &context)
{
    const NetworkHydraulic network = multiQualityFixtureNetwork();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions trace = traceOptions(network.nodes_reservoirs.first().uuid);
    const QList<WaterQualitySolverOptions> forward_quality = {chemical, water_age, trace};
    const QList<WaterQualitySolverOptions> reverse_quality = {trace, water_age, chemical};
    const HydraulicHeadlossFormula formula = HydraulicHeadlossFormula::DarcyWeisbach;

    const EpanetResultRun forward = EpanetRunner().run(runRequest(network, formula, forward_quality));
    const EpanetResultRun reverse = EpanetRunner().run(runRequest(network, formula, reverse_quality));
    context.expect(forward.state == EpanetRunState::Success, "forward quality order must succeed");
    context.expect(reverse.state == EpanetRunState::Success, "reverse quality order must succeed");
    compareHydraulicTimelines(context, reverse.result_timeline, forward.result_timeline, "quality-order.hydraulic");

    for (const WaterQualitySolverOptions &quality_options : forward_quality)
    {
        const EpanetQualityResult *forward_child = findQualityResult(forward, quality_options);
        const EpanetQualityResult *reverse_child = findQualityResult(reverse, quality_options);
        context.expect(forward_child != nullptr && reverse_child != nullptr, "both quality orders must contain every requested quality result");
        if (forward_child != nullptr && reverse_child != nullptr)
        {
            compareQualityTimelines(
                context,
                reverse_child->result_timeline,
                forward_child->result_timeline,
                std::string("quality-order-") + std::to_string(static_cast<int>(quality_options.analysis)));
        }
    }
}

void testMultiQualityRepeatedSourceTrace(TestContext &context)
{
    const NetworkHydraulic network = multiQualityFixtureNetwork();
    const WaterQualitySolverOptions trace_source = traceOptions(network.nodes_reservoirs.first().uuid);
    const WaterQualitySolverOptions trace_middle = traceOptions(network.nodes_junctions.first().uuid);
    const HydraulicHeadlossFormula formula = HydraulicHeadlossFormula::DarcyWeisbach;
    const EpanetResultRun result = EpanetRunner().run(runRequest(network, formula, {trace_source, trace_middle}));

    context.expect(result.state == EpanetRunState::Success, "repeated source-trace run must succeed");
    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{2},
        comparison("trace.quality_results.size"),
        "both source-trace runs must be retained");

    const EpanetQualityResult *source_child = findQualityResult(result, trace_source);
    const EpanetQualityResult *middle_child = findQualityResult(result, trace_middle);
    context.expect(source_child != nullptr, "first source-trace result must remain identifiable by trace node");
    context.expect(middle_child != nullptr, "second source-trace result must remain identifiable by trace node");

    const EpanetResultRun isolated_source = isolatedRun(network, formula, trace_source);
    const EpanetResultRun isolated_middle = isolatedRun(network, formula, trace_middle);
    if (source_child != nullptr)
        compareQualityTimelines(context, source_child->result_timeline, isolated_source.quality_result_timeline, "trace-source");
    if (middle_child != nullptr)
        compareQualityTimelines(context, middle_child->result_timeline, isolated_middle.quality_result_timeline, "trace-middle");
}

void testMultiQualityFailureIsolation(TestContext &context)
{
    const NetworkHydraulic network = multiQualityFixtureNetwork();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions invalid_trace = traceOptions(QUuid::createUuid());
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const HydraulicHeadlossFormula formula = HydraulicHeadlossFormula::HazenWilliams;

    const EpanetResultRun result = EpanetRunner().run(runRequest(
        network,
        formula,
        {water_age, invalid_trace, chemical}));

    context.expect(result.state == EpanetRunState::Error, "one failed quality run must make the aggregate run erroneous");
    context.expect(!result.cancelled, "quality configuration failure must not cancel the run");
    context.expect(result.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "quality failure must not downgrade completed hydraulics");

    const EpanetQualityResult *age_child = findQualityResult(result, water_age);
    const EpanetQualityResult *invalid_child = findQualityResult(result, invalid_trace);
    const EpanetQualityResult *chemical_child = findQualityResult(result, chemical);
    context.expect(age_child != nullptr && age_child->state == EpanetRunState::Success, "quality run before the failure must remain successful");
    context.expect(invalid_child != nullptr && invalid_child->state == EpanetRunState::Error, "invalid source trace must fail only its own quality run");
    context.expect(chemical_child != nullptr && chemical_child->state == EpanetRunState::Success, "quality run after the failure must still execute successfully");

    if (invalid_child != nullptr)
        context.expect(!invalid_child->result_timeline.diagnostics.isEmpty(), "failed quality run must retain its diagnostics");
    if (chemical_child != nullptr)
    {
        context.expect(chemical_child->result_timeline.diagnostics.isEmpty(), "later successful quality run must not inherit diagnostics from the failed run");
        const EpanetResultRun isolated = isolatedRun(network, formula, chemical);
        compareQualityTimelines(context, chemical_child->result_timeline, isolated.quality_result_timeline, "quality-failure-later-run");
    }
}

int countCancellationPolls(const EpanetRunRequest &request)
{
    int poll_count = 0;
    const std::function<bool()> counter = [&poll_count]()
    {
        poll_count++;
        return false;
    };
    EpanetRunner().run(request, counter);
    return poll_count;
}

void testMultiQualityCancellationBetweenRuns(TestContext &context)
{
    const NetworkHydraulic network = multiQualityFixtureNetwork();
    const WaterQualitySolverOptions water_age = waterAgeOptions();
    const WaterQualitySolverOptions chemical = chemicalOptions();
    const HydraulicHeadlossFormula formula = HydraulicHeadlossFormula::HazenWilliams;
    const EpanetRunRequest one_quality = runRequest(network, formula, {water_age});
    const int completed_first_quality_poll_count = countCancellationPolls(one_quality);
    context.expect(completed_first_quality_poll_count > 0, "quality poll-count fixture must observe cancellation checks");

    const EpanetRunRequest two_quality = runRequest(network, formula, {water_age, chemical});
    int poll_count = 0;
    const std::function<bool()> cancel_after_first_quality = [&poll_count, completed_first_quality_poll_count]()
    {
        poll_count++;
        return poll_count > completed_first_quality_poll_count;
    };
    const EpanetResultRun result = EpanetRunner().run(two_quality, cancel_after_first_quality);

    context.expect(result.cancelled, "run must report cancellation between quality runs");
    context.expect(result.state == EpanetRunState::Cancelled, "cancelled run must expose Cancelled aggregate state");
    context.expect(result.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "completed hydraulics must remain valid after quality-run cancellation");
    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{2},
        comparison("cancel-quality.quality_results.size"),
        "cancelled run must preserve both requested quality result slots");
    if (result.quality_results.size() != 2)
        return;

    context.expect(result.quality_results.at(0).state == EpanetRunState::Success, "completed quality run must remain successful after later cancellation");
    context.expect(result.quality_results.at(0).result_timeline.validity == WaterQualitySimulationResultValidity::Valid, "completed quality results must remain valid after later cancellation");
    context.expect(result.quality_results.at(1).state == EpanetRunState::Cancelled, "not-yet-started quality run must be marked cancelled");
}
}

namespace AowisEpanetTests
{
void registerMultiQualityExecutionScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-isolated-equivalence",
        "Compares each quality result from one hydraulic run with independently executed single-quality references across all supported headloss formulas.",
        {"conformance", "hydraulic", "quality", "proof"},
        &testMultiQualityIsolatedEquivalence});
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-order-independence",
        "Runs the same quality analyses in forward and reverse order and verifies that reusable quality state does not leak between runs.",
        {"conformance", "hydraulic", "quality", "proof"},
        &testMultiQualityOrderIndependence});
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-repeated-source-trace",
        "Checks multiple source-trace runs with different trace nodes against isolated references.",
        {"conformance", "quality"},
        &testMultiQualityRepeatedSourceTrace});
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-failure-isolation",
        "Checks that a failed quality run retains diagnostics without poisoning later quality execution on the same hydraulic solution.",
        {"conformance", "quality", "negative"},
        &testMultiQualityFailureIsolation});
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-cancellation-between-runs",
        "Checks cancellation between quality runs while preserving completed hydraulics and already completed quality results.",
        {"conformance", "hydraulic", "quality"},
        &testMultiQualityCancellationBetweenRuns});
}
}
