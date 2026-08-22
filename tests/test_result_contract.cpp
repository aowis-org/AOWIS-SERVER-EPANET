#include <aowis/epanet/epanet_result_run.h>
#include <aowis/epanet/epanet_run_request.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/result_contract_scenarios.h"

#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::HydraulicQuantity;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext context;
    context.time_s = time_s;
    context.entity_type = std::move(entity_type);
    context.entity_id = std::move(entity_id);
    context.field = std::move(field);
    return context;
}

NetworkHydraulic makeJunctionNetwork()
{
    NetworkHydraulic network;
    network.id = QStringLiteral("junction-contract");
    network.uuid = QUuid::createUuid();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 3600;
    network.timestep_report_s = 3600;

    HydraulicNodeReservoir reservoir;
    reservoir.id = QStringLiteral("R1");
    reservoir.uuid = QUuid::createUuid();
    reservoir.hydraulic_head_m = 40.0;

    HydraulicNodeJunction junction;
    junction.id = QStringLiteral("J1");
    junction.uuid = QUuid::createUuid();
    junction.elevation_m = 5.0;
    HydraulicNodeJunctionDemand demand;
    demand.base_demand_m3_per_h = 3.6;
    junction.demands.append(demand);

    HydraulicLinkPipe pipe;
    pipe.id = QStringLiteral("P1");
    pipe.uuid = QUuid::createUuid();
    pipe.node_uuid_from = reservoir.uuid;
    pipe.node_uuid_to = junction.uuid;
    pipe.length_calculated_m = 250.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hazen_williams = 130.0;
    pipe.leak_area_mm2_per_100m = 5.0;
    pipe.leak_area_expansion_per_pressure_head_mm2_per_m = 0.05;

    HydraulicControlSimple level_control;
    level_control.id = QStringLiteral("LEVEL_CONTROL");
    level_control.uuid = QUuid::createUuid();
    level_control.type = HydraulicControlSimpleType::LowLevel;
    level_control.link_uuid = pipe.uuid;
    level_control.action = HydraulicControlActionType::Open;
    level_control.trigger_node_uuid = junction.uuid;
    level_control.trigger_pressure_head_m = 1.0;
    level_control.enabled = false;

    network.nodes_reservoirs.append(reservoir);
    network.nodes_junctions.append(junction);
    network.links_pipes.append(pipe);
    network.controls_simple.append(level_control);
    return network;
}

NetworkHydraulic makeReservoirPipeNetwork()
{
    NetworkHydraulic network;
    network.id = QStringLiteral("rule-control");
    network.uuid = QUuid::createUuid();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 3600;
    network.timestep_report_s = 3600;
    network.timestep_rule_s = 60;

    HydraulicNodeReservoir source;
    source.id = QStringLiteral("R_SOURCE");
    source.uuid = QUuid::createUuid();
    source.hydraulic_head_m = 50.0;

    HydraulicNodeReservoir sink;
    sink.id = QStringLiteral("R_SINK");
    sink.uuid = QUuid::createUuid();
    sink.hydraulic_head_m = 20.0;

    HydraulicLinkPipe pipe;
    pipe.id = QStringLiteral("P_RULE");
    pipe.uuid = QUuid::createUuid();
    pipe.node_uuid_from = source.uuid;
    pipe.node_uuid_to = sink.uuid;
    pipe.length_calculated_m = 500.0;
    pipe.diameter_mm = 200.0;
    pipe.roughness_hazen_williams = 130.0;

    HydraulicControlRule rule;
    rule.id = QStringLiteral("RULE_CLOSE");
    rule.uuid = QUuid::createUuid();

    HydraulicControlRulePremise premise;
    premise.object = HydraulicControlRuleObject::System;
    premise.variable = HydraulicControlRuleVariable::Time;
    premise.comparison = HydraulicControlRuleOperator::GreaterOrEqual;
    premise.elapsed_time_s = 1800;
    rule.premises.append(premise);

    HydraulicControlRuleAction action;
    action.link_uuid = pipe.uuid;
    action.status = HydraulicControlRuleStatus::Closed;
    rule.actions_then.append(action);

    network.nodes_reservoirs.append(source);
    network.nodes_reservoirs.append(sink);
    network.links_pipes.append(pipe);
    network.controls_rules.append(rule);
    return network;
}

NetworkHydraulic makePumpNetwork(bool with_timer_control, quint64 duration_s)
{
    NetworkHydraulic network;
    network.id = QStringLiteral("pump-control");
    network.uuid = QUuid::createUuid();
    network.duration_s = duration_s;
    network.timestep_hydraulic_s = 3600;
    network.timestep_report_s = 3600;
    network.options_energy.currency_iso4217 = QStringLiteral("EUR");
    network.options_energy.global_pump_efficiency_percent = 80.0;
    network.options_energy.global_energy_price_per_kw_h = 0.25;
    network.options_energy.demand_charge_per_kw = 2.0;

    HydraulicNodeReservoir source;
    source.id = QStringLiteral("R_LOW");
    source.uuid = QUuid::createUuid();
    source.hydraulic_head_m = 0.0;

    HydraulicNodeReservoir sink;
    sink.id = QStringLiteral("R_HIGH");
    sink.uuid = QUuid::createUuid();
    sink.hydraulic_head_m = 10.0;

    HydraulicCurvePumpHead curve;
    curve.id = QStringLiteral("C_PUMP");
    curve.uuid = QUuid::createUuid();
    HydraulicCurvePumpHeadPoint point_1;
    point_1.flow_m3_per_h = 0.0;
    point_1.head_gain_m = 30.0;
    HydraulicCurvePumpHeadPoint point_2;
    point_2.flow_m3_per_h = 20.0;
    point_2.head_gain_m = 20.0;
    HydraulicCurvePumpHeadPoint point_3;
    point_3.flow_m3_per_h = 40.0;
    point_3.head_gain_m = 5.0;
    curve.points.append(point_1);
    curve.points.append(point_2);
    curve.points.append(point_3);

    HydraulicLinkPump pump;
    pump.id = QStringLiteral("PU1");
    pump.uuid = QUuid::createUuid();
    pump.node_uuid_from = source.uuid;
    pump.node_uuid_to = sink.uuid;
    pump.definition_type = HydraulicLinkPumpDefinitionType::ThreePointCurve;
    pump.head_curve_uuid = curve.uuid;
    pump.initial_status = HydraulicLinkPumpInitialStatus::On;
    pump.initial_speed_ratio = 1.0;
    pump.control_type = with_timer_control
        ? HydraulicLinkPumpControlType::TimeBased
        : HydraulicLinkPumpControlType::None;

    network.nodes_reservoirs.append(source);
    network.nodes_reservoirs.append(sink);
    network.curves_pump_head.append(curve);
    network.links_pumps.append(pump);

    if (with_timer_control)
    {
        HydraulicControlSimple control;
        control.id = QStringLiteral("CLOSE_PUMP");
        control.uuid = QUuid::createUuid();
        control.type = HydraulicControlSimpleType::Timer;
        control.link_uuid = pump.uuid;
        control.action = HydraulicControlActionType::Close;
        control.trigger_elapsed_time_s = 1800;
        network.controls_simple.append(control);
    }

    return network;
}

void testPhysicalResultContractAndLeakage(TestContext &context)
{
    const NetworkHydraulic network = makeJunctionNetwork();
    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "leakage network should produce valid results");
    context.expect(!run.result_timeline.results.isEmpty(), "leakage network should return timesteps");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    context.expectEqual(static_cast<std::int64_t>(first.nodes_junctions.size()), std::int64_t{1}, comparison("nodes_junctions.size", first.time_elapsed_s), "junction result should be returned");
    context.expectEqual(static_cast<std::int64_t>(first.links_pipes.size()), std::int64_t{1}, comparison("links_pipes.size", first.time_elapsed_s), "pipe result should be returned");
    if (first.nodes_junctions.size() != 1 || first.links_pipes.size() != 1)
        return;

    const HydraulicSimulationResultNodeJunction &junction = first.nodes_junctions.first();
    const HydraulicSimulationResultLinkPipe &pipe = first.links_pipes.first();
    context.expectNear(junction.total_demand_m3_per_h,
        junction.demand_delivered_m3_per_h + junction.emitter_flow_m3_per_h + junction.leakage_flow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour,
        comparison("total_demand_m3_per_h", first.time_elapsed_s, "Junction", junction.id.toStdString()),
        "junction total demand should equal all physical outflow components");
    context.expect(pipe.leakage_flow_m3_per_h > 0.0, "native EPANET pipe leakage should be positive");
    context.expect(junction.leakage_flow_m3_per_h > 0.0, "pipe leakage should be assigned to the junction");
    context.expect(junction.appears_in_control && pipe.appears_in_control, "disabled level control should still be represented in control membership");
    context.expectNear(pipe.head_loss_gradient_m_per_km, pipe.head_loss_m / 250.0 * 1000.0,
        HydraulicQuantity::HeadMetres,
        comparison("head_loss_gradient_m_per_km", first.time_elapsed_s, "Pipe", pipe.id.toStdString()),
        "pipe unit head loss should match total head loss and length");
    context.expect(pipe.friction_factor > 0.0, "flowing pipe should return a friction factor");

    const HydraulicSimulationResult &final_result = run.result_timeline.results.last();
    context.expect(final_result.flow_balance.leakage_flow_m3_per_h > 0.0, "run flow balance should include leakage");
    context.expectNear(final_result.flow_balance.flow_balance_ratio, 1.0, NumericTolerance{0.0, 1.0e-4},
        comparison("flow_balance.flow_balance_ratio", final_result.time_elapsed_s),
        "run flow balance should close");
}

void testStructuredRuleControl(TestContext &context)
{
    const NetworkHydraulic network = makeReservoirPipeNetwork();
    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "rule-controlled network should produce valid results");
    context.expect(!run.result_timeline.results.isEmpty(), "rule-controlled network should return timesteps");

    bool saw_closed_result = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s >= 1800 && !result.links_pipes.isEmpty() && !result.links_pipes.first().open)
            saw_closed_result = true;
    }
    context.expect(saw_closed_result, "structured time rule should close its pipe");
    if (!run.result_timeline.results.isEmpty() && !run.result_timeline.results.first().links_pipes.isEmpty())
        context.expect(run.result_timeline.results.first().links_pipes.first().appears_in_control, "rule-controlled pipe should report control membership");
}

void testTimerPumpControlAndEnergySummary(TestContext &context)
{
    const NetworkHydraulic network = makePumpNetwork(true, 3600);
    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "timer-controlled pump network should produce valid results");
    context.expect(!run.result_timeline.results.isEmpty(), "timer-controlled pump network should return timesteps");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    context.expect(first.event_next.type == HydraulicSimulationTimestepEventType::ControlEvent, "timer should be the first next hydraulic event");
    context.expectEqual(static_cast<std::int64_t>(first.event_next.time_until_event_s), std::int64_t{1800}, comparison("event_next.time_until_event_s", first.time_elapsed_s), "timer event should occur after 1800 seconds");
    context.expectEqual(first.event_next.control_id.toStdString(), "CLOSE_PUMP", comparison("event_next.control_id", first.time_elapsed_s), "timer event should identify its simple control");
    context.expect(!first.links_pumps.isEmpty() && first.links_pumps.first().appears_in_control, "timer-controlled pump should report control membership");

    bool saw_closed_pump = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s >= 1800 && !result.links_pumps.isEmpty() && !result.links_pumps.first().open)
            saw_closed_pump = true;
    }
    context.expect(saw_closed_pump, "timer control should close the pump");

    const HydraulicSimulationResult &final_result = run.result_timeline.results.last();
    context.expectEqual(static_cast<std::int64_t>(final_result.links_pump_energy_usage.size()), std::int64_t{1}, comparison("links_pump_energy_usage.size", final_result.time_elapsed_s), "pump energy usage should be returned");
    if (!final_result.links_pump_energy_usage.isEmpty())
        context.expectNear(final_result.links_pump_energy_usage.first().time_online_percent, 50.0,
            NumericTolerance{0.0, 1.0e-4}, comparison("time_online_percent", final_result.time_elapsed_s, "Pump", final_result.links_pump_energy_usage.first().pump_id.toStdString()),
            "pump should be online for half of the run");
    context.expectEqual(final_result.energy_usage.currency_iso4217.toStdString(), std::string("EUR"),
        comparison("energy_usage.currency_iso4217", final_result.time_elapsed_s),
        "system energy summary should preserve the configured ISO 4217 currency");
    if (!final_result.links_pump_energy_usage.isEmpty())
        context.expectEqual(final_result.links_pump_energy_usage.first().currency_iso4217.toStdString(), std::string("EUR"),
            comparison("pump_energy.currency_iso4217", final_result.time_elapsed_s),
            "pump energy summary should preserve the configured ISO 4217 currency");
    context.expect(final_result.energy_usage.peak_power_kw > 0.0, "system peak pump power should be returned");
    context.expectNear(final_result.energy_usage.demand_charge_per_day, final_result.energy_usage.peak_power_kw * 2.0,
        HydraulicQuantity::Cost, comparison("energy_usage.demand_charge_per_day", final_result.time_elapsed_s),
        "demand charge should use simultaneous peak power");
    context.expectNear(final_result.energy_usage.total_cost_per_day,
        final_result.energy_usage.energy_cost_per_day + final_result.energy_usage.demand_charge_per_day,
        HydraulicQuantity::Cost, comparison("energy_usage.total_cost_per_day", final_result.time_elapsed_s),
        "total energy cost should include energy and demand charges");
}

void testUnsupportedPumpPowerRule(TestContext &context)
{
    NetworkHydraulic network = makePumpNetwork(false, 3600);
    network.timestep_rule_s = 60;

    HydraulicControlRule rule;
    rule.id = QStringLiteral("RULE_POWER");
    rule.uuid = QUuid::createUuid();

    HydraulicControlRulePremise premise;
    premise.object = HydraulicControlRuleObject::Link;
    premise.object_uuid = network.links_pumps.first().uuid;
    premise.variable = HydraulicControlRuleVariable::Power;
    premise.comparison = HydraulicControlRuleOperator::Greater;
    premise.power_kw = 0.1;
    rule.premises.append(premise);

    HydraulicControlRuleAction action;
    action.link_uuid = network.links_pumps.first().uuid;
    action.status = HydraulicControlRuleStatus::Closed;
    rule.actions_then.append(action);
    network.controls_rules.append(rule);

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid, "unsupported pump POWER rule should invalidate the run before simulation");
    context.expect(!run.result_timeline.status.success, "unsupported pump POWER rule should return an error status");
    context.expect(run.result_timeline.status.message.contains(QStringLiteral("POWER")), "unsupported pump POWER rule should return an explicit diagnostic");
    context.expect(run.result_timeline.results.isEmpty(), "unsupported pump POWER rule should not return hydraulic results");
}


void testWaterQualityModelBoundary(TestContext &context)
{
    HydraulicNodeJunction junction;
    junction.initial_chemical_concentration_mg_per_l = 1.25;
    junction.initial_water_age_h = 3.5;
    junction.quality_source.type = HydraulicNodeQualitySourceType::MassBooster;
    junction.quality_source.chemical_mass_flow_mg_per_min = 12.5;

    context.expectNear(junction.initial_chemical_concentration_mg_per_l, 1.25, NumericTolerance{0.0, 0.0}, comparison("initial_chemical_concentration_mg_per_l"), "chemical initial quality should be expressed explicitly in mg/L");
    context.expectNear(junction.initial_water_age_h, 3.5, NumericTolerance{0.0, 0.0}, comparison("initial_water_age_h"), "water-age initial quality should be expressed explicitly in hours");
    context.expectNear(junction.quality_source.chemical_mass_flow_mg_per_min, 12.5, NumericTolerance{0.0, 0.0}, comparison("chemical_mass_flow_mg_per_min"), "mass-booster source strength should be expressed explicitly in mg/min");

    HydraulicNodeQualitySource concentration_source;
    concentration_source.type = HydraulicNodeQualitySourceType::Concentration;
    concentration_source.chemical_concentration_mg_per_l = 0.8;
    context.expectNear(concentration_source.chemical_concentration_mg_per_l, 0.8, NumericTolerance{0.0, 0.0}, comparison("chemical_concentration_mg_per_l"), "concentration-source strength should be expressed explicitly in mg/L");

    WaterQualityBulkReaction bulk_reaction;
    bulk_reaction.coefficient = -0.15;
    bulk_reaction.order = 1.4;
    WaterQualityWallReaction wall_reaction;
    wall_reaction.coefficient = -0.05;
    wall_reaction.order = 0.0;
    context.expectNear(bulk_reaction.coefficient, -0.15, NumericTolerance{0.0, 0.0}, comparison("bulk_reaction.coefficient"), "bulk reaction coefficient should be stored with its order");
    context.expectNear(bulk_reaction.order, 1.4, NumericTolerance{0.0, 0.0}, comparison("bulk_reaction.order"), "bulk reaction order should be explicit");
    context.expectNear(wall_reaction.order, 0.0, NumericTolerance{0.0, 0.0}, comparison("wall_reaction.order"), "wall reaction order should be explicit");

    WaterQualitySolverOptions quality_options;
    quality_options.analysis = WaterQualityAnalysisType::Chemical;
    quality_options.chemical_tolerance_mg_per_l = 0.005;
    quality_options.water_age_tolerance_h = 0.02;
    quality_options.source_trace_tolerance_percent = 0.1;
    context.expectNear(quality_options.chemical_tolerance_mg_per_l, 0.005, NumericTolerance{0.0, 0.0}, comparison("chemical_tolerance_mg_per_l"), "chemical tolerance should be quantity-specific");
    context.expectNear(quality_options.water_age_tolerance_h, 0.02, NumericTolerance{0.0, 0.0}, comparison("water_age_tolerance_h"), "water-age tolerance should be quantity-specific");
    context.expectNear(quality_options.source_trace_tolerance_percent, 0.1, NumericTolerance{0.0, 0.0}, comparison("source_trace_tolerance_percent"), "trace tolerance should be quantity-specific");

    EpanetResultRun run;
    context.expect(run.quality_result_timeline.validity == WaterQualitySimulationResultValidity::NotRun, "quality timeline should explicitly distinguish not-run from invalid");
    context.expect(run.quality_result_timeline.results.isEmpty(), "a newly constructed quality timeline should contain no results");

    WaterQualitySimulationResult result;
    WaterQualitySimulationResultNodeJunction node_result;
    node_result.chemical_concentration_mg_per_l = 0.75;
    node_result.water_age_h = 4.0;
    node_result.source_trace_percent = 65.0;
    node_result.source_mass_flow_mg_per_min = 1.2;
    result.nodes_junctions.append(node_result);
    result.statistics.mass_balance_ratio = 0.9999;

    context.expectNear(result.nodes_junctions.first().chemical_concentration_mg_per_l, 0.75, NumericTolerance{0.0, 0.0}, comparison("quality_result.chemical_concentration_mg_per_l"), "quality results should expose typed chemical concentration");
    context.expectNear(result.nodes_junctions.first().water_age_h, 4.0, NumericTolerance{0.0, 0.0}, comparison("quality_result.water_age_h"), "quality results should expose typed water age");
    context.expectNear(result.nodes_junctions.first().source_trace_percent, 65.0, NumericTolerance{0.0, 0.0}, comparison("quality_result.source_trace_percent"), "quality results should expose typed source trace");
    context.expectNear(result.nodes_junctions.first().source_mass_flow_mg_per_min, 1.2, NumericTolerance{0.0, 0.0}, comparison("quality_result.source_mass_flow_mg_per_min"), "quality results should expose typed source mass flow");
    context.expectNear(result.statistics.mass_balance_ratio, 0.9999, NumericTolerance{0.0, 0.0}, comparison("quality_result.mass_balance_ratio"), "quality mass balance should live in the quality result timeline");
}

void testMultiQualityRunEnvelope(TestContext &context)
{
    EpanetRunRequest request;
    request.network = makeJunctionNetwork();
    request.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;

    WaterQualitySolverOptions chemical;
    chemical.analysis = WaterQualityAnalysisType::Chemical;
    chemical.chemical_name = QStringLiteral("Chlorine");
    request.quality_runs.append(chemical);

    WaterQualitySolverOptions water_age;
    water_age.analysis = WaterQualityAnalysisType::WaterAge;
    request.quality_runs.append(water_age);

    WaterQualitySolverOptions trace_first;
    trace_first.analysis = WaterQualityAnalysisType::SourceTrace;
    trace_first.trace_node_uuid = QUuid::createUuid();
    request.quality_runs.append(trace_first);

    WaterQualitySolverOptions trace_second;
    trace_second.analysis = WaterQualityAnalysisType::SourceTrace;
    trace_second.trace_node_uuid = QUuid::createUuid();
    request.quality_runs.append(trace_second);

    context.expect(
        request.network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach,
        "run request should carry exactly one headloss formula in the hydraulic network options");
    context.expectEqual(
        static_cast<std::int64_t>(request.quality_runs.size()),
        std::int64_t{4},
        comparison("run.quality_runs.size"),
        "run request should preserve all requested quality analyses");
    context.expect(
        request.quality_runs.at(2).trace_node_uuid != request.quality_runs.at(3).trace_node_uuid,
        "run request should support multiple source-trace runs with different trace nodes");

    EpanetResultRun result;
    result.state = EpanetRunState::Running;

    for (const WaterQualitySolverOptions &quality_options : request.quality_runs)
    {
        EpanetQualityResult quality_result;
        quality_result.options = quality_options;
        quality_result.result_timeline.analysis = quality_options.analysis;
        quality_result.state = EpanetRunState::Success;
        result.quality_results.append(quality_result);
    }

    result.state = EpanetRunState::Success;

    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{4},
        comparison("run.quality_results.size"),
        "run result should hold all quality analyses under one hydraulic result");
    context.expect(
        result.quality_results.at(0).options.analysis == WaterQualityAnalysisType::Chemical,
        "quality result should retain the options that identify its analysis");
    context.expect(
        result.quality_results.at(2).options.trace_node_uuid == trace_first.trace_node_uuid,
        "source-trace result should retain its trace-node identity");
    context.expect(
        result.quality_results.at(3).options.trace_node_uuid == trace_second.trace_node_uuid,
        "multiple source-trace results should remain independently identifiable");
}


void testMultiQualitySequentialExecution(TestContext &context)
{
    NetworkHydraulic network = makeJunctionNetwork();
    network.timestep_quality_s = 300;
    network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;
    network.links_pipes[0].roughness_darcy_weisbach_mm = 0.15;

    EpanetRunRequest request;
    request.network = network;

    WaterQualitySolverOptions chemical;
    chemical.analysis = WaterQualityAnalysisType::Chemical;
    chemical.chemical_name = QStringLiteral("Chlorine");
    request.quality_runs.append(chemical);

    WaterQualitySolverOptions water_age;
    water_age.analysis = WaterQualityAnalysisType::WaterAge;
    request.quality_runs.append(water_age);

    WaterQualitySolverOptions source_trace;
    source_trace.analysis = WaterQualityAnalysisType::SourceTrace;
    source_trace.trace_node_uuid = network.nodes_reservoirs.first().uuid;
    request.quality_runs.append(source_trace);

    const EpanetResultRun result = EpanetRunner().run(request);
    context.expect(!result.cancelled, "multi-quality execution should complete without cancellation");
    context.expect(result.state == EpanetRunState::Success, "multi-quality execution should complete successfully");
    context.expect(result.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "the single hydraulic run should return valid numerical results");
    context.expect(!result.result_timeline.results.isEmpty(), "the single hydraulic run should contain numerical results");
    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{3},
        comparison("run.quality_results.size"),
        "the hydraulic run should execute every requested quality analysis");

    for (const EpanetQualityResult &quality_result : result.quality_results)
    {
        context.expect(quality_result.state == EpanetRunState::Success, "each quality run should succeed");
        context.expect(quality_result.result_timeline.validity == WaterQualitySimulationResultValidity::Valid, "each quality run should return valid numerical results");
        context.expect(!quality_result.result_timeline.results.isEmpty(), "each quality run should contain numerical results");
        context.expect(quality_result.result_timeline.analysis == quality_result.options.analysis, "each quality run should retain its requested analysis identity");
    }
}

void testSteadyStatePumpEnergyRegression(TestContext &context)
{
    const NetworkHydraulic network = makePumpNetwork(false, 0);
    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "steady-state pump network should produce valid results");
    context.expectEqual(static_cast<std::int64_t>(run.result_timeline.results.size()), std::int64_t{1}, comparison("results.size"), "steady-state run should return one timestep");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &result = run.result_timeline.results.last();
    context.expectEqual(static_cast<std::int64_t>(result.links_pump_energy_usage.size()), std::int64_t{1}, comparison("links_pump_energy_usage.size", result.time_elapsed_s), "steady-state pump energy usage should be returned");
    if (!result.links_pump_energy_usage.isEmpty())
        context.expectNear(result.links_pump_energy_usage.first().time_online_percent, 100.0,
            HydraulicQuantity::Percent,
            comparison("time_online_percent", result.time_elapsed_s, "Pump", result.links_pump_energy_usage.first().pump_id.toStdString()),
            "running steady-state pump should report 100 percent online");
    context.expect(result.energy_usage.peak_power_kw > 0.0, "steady-state system peak power should be returned");
    context.expectNear(result.flow_balance.flow_balance_ratio, 1.0, NumericTolerance{0.0, 1.0e-4},
        comparison("flow_balance.flow_balance_ratio", result.time_elapsed_s), "steady-state flow balance should close");
}
}

namespace AowisEpanetTests
{
void registerResultContractScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "contract-physical-results-and-leakage",
        "Checks physical junction and pipe result relationships, FAVAD leakage, control membership, and flow balance.",
        {"contract", "hydraulic", "leakage"},
        &testPhysicalResultContractAndLeakage});
    registry.add(ScenarioDefinition{
        "contract-structured-rule-control",
        "Checks a structured time rule, its action, and rule membership reporting.",
        {"contract", "hydraulic", "control"},
        &testStructuredRuleControl});
    registry.add(ScenarioDefinition{
        "contract-timer-pump-energy",
        "Checks a timer-controlled pump, next-event reporting, and pump and system energy summaries.",
        {"contract", "hydraulic", "pump", "energy"},
        &testTimerPumpControlAndEnergySummary});
    registry.add(ScenarioDefinition{
        "contract-reject-pump-power-rule",
        "Checks explicit rejection of pump POWER premises unsupported by the bundled EPANET rule engine.",
        {"contract", "negative", "control"},
        &testUnsupportedPumpPowerRule});
    registry.add(ScenarioDefinition{
        "contract-steady-state-pump-energy",
        "Checks steady-state pump energy accumulation and flow balance.",
        {"contract", "hydraulic", "pump", "energy"},
        &testSteadyStatePumpEnergyRegression});
    registry.add(ScenarioDefinition{
        "contract-quality-model-boundary",
        "Checks typed water-quality quantities, source strengths, reaction coefficient/order pairs, and the separate quality result timeline.",
        {"contract", "quality", "model"},
        &testWaterQualityModelBoundary});
    registry.add(ScenarioDefinition{
        "conformance-multi-quality-sequential-basic",
        "Checks one hydraulic solve followed by multiple sequential quality analyses on the same prepared EPANET project.",
        {"conformance", "hydraulic", "quality"},
        &testMultiQualitySequentialExecution});
    registry.add(ScenarioDefinition{
        "contract-multi-quality-run-envelope",
        "Checks the one-hydraulic-run request/result envelope with multiple quality analyses and repeated source-trace runs.",
        {"contract", "hydraulic", "quality"},
        &testMultiQualityRunEnvelope});
}
}
