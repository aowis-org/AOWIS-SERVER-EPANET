#include <aowis/epanet/epanet_runner.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
int failure_count = 0;

void expect(bool condition, const char *message)
{
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    failure_count++;
}

bool approximatelyEqual(double left, double right, double relative_tolerance = 1.0e-6)
{
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= relative_tolerance * scale;
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
    reservoir.head_m = 40.0;

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
    pipe.roughness_hw = 130.0;
    pipe.leak_area_mm2_per_100m = 5.0;
    pipe.leak_expansion_mm2_per_m_head = 0.05;

    HydraulicControlSimple level_control;
    level_control.id = QStringLiteral("LEVEL_CONTROL");
    level_control.uuid = QUuid::createUuid();
    level_control.type = HydraulicControlSimpleType::LowLevel;
    level_control.link_uuid = pipe.uuid;
    level_control.action = HydraulicControlActionType::Open;
    level_control.trigger_node_uuid = junction.uuid;
    level_control.trigger_level_or_pressure_head_m = 1.0;
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
    source.head_m = 50.0;

    HydraulicNodeReservoir sink;
    sink.id = QStringLiteral("R_SINK");
    sink.uuid = QUuid::createUuid();
    sink.head_m = 20.0;

    HydraulicLinkPipe pipe;
    pipe.id = QStringLiteral("P_RULE");
    pipe.uuid = QUuid::createUuid();
    pipe.node_uuid_from = source.uuid;
    pipe.node_uuid_to = sink.uuid;
    pipe.length_calculated_m = 500.0;
    pipe.diameter_mm = 200.0;
    pipe.roughness_hw = 130.0;

    HydraulicControlRule rule;
    rule.id = QStringLiteral("RULE_CLOSE");
    rule.uuid = QUuid::createUuid();

    HydraulicControlRulePremise premise;
    premise.object = HydraulicControlRuleObject::System;
    premise.variable = HydraulicControlRuleVariable::Time;
    premise.comparison = HydraulicControlRuleOperator::GreaterOrEqual;
    premise.value = 1800.0;
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
    network.options_energy.global_pump_efficiency_percent = 80.0;
    network.options_energy.global_energy_price_per_kw_h = 0.25;
    network.options_energy.demand_charge_per_kw = 2.0;

    HydraulicNodeReservoir source;
    source.id = QStringLiteral("R_LOW");
    source.uuid = QUuid::createUuid();
    source.head_m = 0.0;

    HydraulicNodeReservoir sink;
    sink.id = QStringLiteral("R_HIGH");
    sink.uuid = QUuid::createUuid();
    sink.head_m = 10.0;

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
    pump.initial_speed = 1.0;
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
        control.trigger_time_s = 1800;
        network.controls_simple.append(control);
    }

    return network;
}

void testPhysicalResultContractAndLeakage()
{
    const NetworkHydraulic network = makeJunctionNetwork();
    const EpanetResultRun run = EpanetRunner().run(network);
    expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "leakage network should produce valid results");
    expect(!run.result_timeline.results.isEmpty(), "leakage network should return timesteps");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    expect(first.nodes_junctions.size() == 1, "junction result should be returned");
    expect(first.links_pipes.size() == 1, "pipe result should be returned");
    if (first.nodes_junctions.size() != 1 || first.links_pipes.size() != 1)
        return;

    const HydraulicSimulationResultNodeJunction &junction = first.nodes_junctions.first();
    const HydraulicSimulationResultLinkPipe &pipe = first.links_pipes.first();
    expect(approximatelyEqual(junction.total_demand_m3_per_h,
        junction.demand_delivered_m3_per_h + junction.emitter_flow_m3_per_h + junction.leakage_flow_m3_per_h),
        "junction total demand should equal all physical outflow components");
    expect(pipe.leakage_flow_m3_per_h > 0.0, "native EPANET pipe leakage should be positive");
    expect(junction.leakage_flow_m3_per_h > 0.0, "pipe leakage should be assigned to the junction");
    expect(junction.appears_in_control && pipe.appears_in_control, "disabled level control should still be represented in control membership");
    expect(approximatelyEqual(pipe.unit_head_loss_m_per_km, pipe.head_loss_m / 250.0 * 1000.0),
        "pipe unit head loss should match total head loss and length");
    expect(pipe.friction_factor > 0.0, "flowing pipe should return a friction factor");

    const HydraulicSimulationResult &final_result = run.result_timeline.results.last();
    expect(final_result.flow_balance.leakage_flow_m3_per_h > 0.0, "run flow balance should include leakage");
    expect(approximatelyEqual(final_result.flow_balance.flow_balance_ratio, 1.0, 1.0e-4),
        "run flow balance should close");
}

void testStructuredRuleControl()
{
    const NetworkHydraulic network = makeReservoirPipeNetwork();
    const EpanetResultRun run = EpanetRunner().run(network);
    expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "rule-controlled network should produce valid results");
    expect(!run.result_timeline.results.isEmpty(), "rule-controlled network should return timesteps");

    bool saw_closed_result = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s >= 1800 && !result.links_pipes.isEmpty() && !result.links_pipes.first().open)
            saw_closed_result = true;
    }
    expect(saw_closed_result, "structured time rule should close its pipe");
    if (!run.result_timeline.results.isEmpty() && !run.result_timeline.results.first().links_pipes.isEmpty())
        expect(run.result_timeline.results.first().links_pipes.first().appears_in_control, "rule-controlled pipe should report control membership");
}

void testTimerPumpControlAndEnergySummary()
{
    const NetworkHydraulic network = makePumpNetwork(true, 3600);
    const EpanetResultRun run = EpanetRunner().run(network);
    expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "timer-controlled pump network should produce valid results");
    expect(!run.result_timeline.results.isEmpty(), "timer-controlled pump network should return timesteps");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    expect(first.event_next.type == HydraulicSimulationTimestepEventType::ControlEvent, "timer should be the first next hydraulic event");
    expect(first.event_next.time_until_event_s == 1800, "timer event should occur after 1800 seconds");
    expect(first.event_next.control_id == QStringLiteral("CLOSE_PUMP"), "timer event should identify its simple control");
    expect(!first.links_pumps.isEmpty() && first.links_pumps.first().appears_in_control, "timer-controlled pump should report control membership");

    bool saw_closed_pump = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s >= 1800 && !result.links_pumps.isEmpty() && !result.links_pumps.first().open)
            saw_closed_pump = true;
    }
    expect(saw_closed_pump, "timer control should close the pump");

    const HydraulicSimulationResult &final_result = run.result_timeline.results.last();
    expect(final_result.links_pump_energy_usage.size() == 1, "pump energy usage should be returned");
    if (!final_result.links_pump_energy_usage.isEmpty())
        expect(approximatelyEqual(final_result.links_pump_energy_usage.first().time_online_percent, 50.0, 1.0e-4), "pump should be online for half of the run");
    expect(final_result.energy_usage.peak_power_kw > 0.0, "system peak pump power should be returned");
    expect(approximatelyEqual(final_result.energy_usage.demand_charge_per_day, final_result.energy_usage.peak_power_kw * 2.0), "demand charge should use simultaneous peak power");
    expect(approximatelyEqual(final_result.energy_usage.total_cost_per_day,
        final_result.energy_usage.energy_cost_per_day + final_result.energy_usage.demand_charge_per_day),
        "total energy cost should include energy and demand charges");
}

void testPumpPowerRule()
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
    premise.value = 0.1;
    rule.premises.append(premise);

    HydraulicControlRuleAction action;
    action.link_uuid = network.links_pumps.first().uuid;
    action.status = HydraulicControlRuleStatus::Closed;
    rule.actions_then.append(action);
    network.controls_rules.append(rule);

    const EpanetResultRun run = EpanetRunner().run(network);
    expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "pump POWER rule network should produce valid results");
    bool saw_closed_pump = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s >= 60 && !result.links_pumps.isEmpty() && !result.links_pumps.first().open)
            saw_closed_pump = true;
    }
    expect(saw_closed_pump, "POWER premise should evaluate pump power and close the pump");
}

void testSteadyStatePumpEnergyRegression()
{
    const NetworkHydraulic network = makePumpNetwork(false, 0);
    const EpanetResultRun run = EpanetRunner().run(network);
    expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "steady-state pump network should produce valid results");
    expect(run.result_timeline.results.size() == 1, "steady-state run should return one timestep");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &result = run.result_timeline.results.last();
    expect(result.links_pump_energy_usage.size() == 1, "steady-state pump energy usage should be returned");
    if (!result.links_pump_energy_usage.isEmpty())
        expect(approximatelyEqual(result.links_pump_energy_usage.first().time_online_percent, 100.0), "running steady-state pump should report 100 percent online");
    expect(result.energy_usage.peak_power_kw > 0.0, "steady-state system peak power should be returned");
    expect(approximatelyEqual(result.flow_balance.flow_balance_ratio, 1.0, 1.0e-4), "steady-state flow balance should close");
}
}

int main()
{
    testPhysicalResultContractAndLeakage();
    testStructuredRuleControl();
    testTimerPumpControlAndEnergySummary();
    testPumpPowerRule();
    testSteadyStatePumpEnergyRegression();

    if (failure_count == 0)
        std::cout << "All EPANET result-contract integration tests passed.\n";
    return failure_count == 0 ? 0 : 1;
}
