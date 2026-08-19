#include "net1_fixture.h"

#include <QUuid>

namespace AowisEpanetTests
{
namespace
{
constexpr double kMetresPerFoot = 0.3048;
constexpr double kCubicMetresPerHourPerCubicFootPerSecond = 101.94;
constexpr double kGallonsPerMinutePerCubicFootPerSecond = 448.831;

double feetToMetres(double feet)
{
    return feet * kMetresPerFoot;
}

double gallonsPerMinuteToCubicMetresPerHour(double gallons_per_minute)
{
    return gallons_per_minute / kGallonsPerMinutePerCubicFootPerSecond
        * kCubicMetresPerHourPerCubicFootPerSecond;
}

QUuid appendJunction(NetworkHydraulic &network, const QString &id, double elevation_ft, double demand_gpm)
{
    HydraulicNodeJunction junction;
    junction.id = id;
    junction.uuid = QUuid::createUuid();
    junction.elevation_m = feetToMetres(elevation_ft);

    HydraulicNodeJunctionDemand demand;
    demand.category_name = QStringLiteral("Demand 1");
    demand.base_demand_m3_per_h = gallonsPerMinuteToCubicMetresPerHour(demand_gpm);
    demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
    junction.demands.append(demand);

    network.nodes_junctions.append(junction);
    return junction.uuid;
}

QUuid appendPipe(NetworkHydraulic &network, const QString &id, const QUuid &node_from, const QUuid &node_to, double length_ft, double diameter_in)
{
    HydraulicLinkPipe pipe;
    pipe.id = id;
    pipe.uuid = QUuid::createUuid();
    pipe.node_uuid_from = node_from;
    pipe.node_uuid_to = node_to;
    pipe.length_calculated_m = feetToMetres(length_ft);
    pipe.diameter_mm = diameter_in * 25.4;
    pipe.roughness_hazen_williams = 100.0;
    pipe.minor_loss_coefficient = 0.0;
    pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
    network.links_pipes.append(pipe);
    return pipe.uuid;
}
}

Net1Fixture makeNet1Fixture()
{
    Net1Fixture fixture;
    NetworkHydraulic &network = fixture.network;
    network.id = QStringLiteral("upstream-net1");
    network.uuid = QUuid::createUuid();
    network.title_line_1 = QStringLiteral("EPANET Example Network 1");
    network.title_line_2 = QStringLiteral("A simple example of modeling chlorine decay. Both bulk and");
    network.title_line_3 = QStringLiteral("wall reactions are included.");

    network.duration_s = 86400;
    network.timestep_hydraulic_s = 3600;
    network.timestep_quality_s = 300;
    network.timestep_pattern_s = 7200;
    network.start_pattern_s = 0;
    network.timestep_report_s = 3600;
    network.start_report_s = 0;
    network.timestep_rule_s = 360;
    network.start_time_of_day_s = 0;
    network.report_statistic = HydraulicSimulationReportStatistic::Series;

    network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    network.options_hydraulic.demand_model = HydraulicDemandModel::DemandDriven;
    network.options_hydraulic.maximum_trials = 40;
    network.options_hydraulic.accuracy = 0.001;
    network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
    network.options_hydraulic.unbalanced_extra_trials = 10;
    network.options_hydraulic.check_frequency = 2;
    network.options_hydraulic.maximum_check = 10;
    network.options_hydraulic.damping_limit = 0.0;
    network.options_hydraulic.maximum_head_error_m = 0.0;
    network.options_hydraulic.maximum_flow_change_m3_per_h = 0.0;
    network.options_hydraulic.demand_multiplier = 1.0;
    network.options_hydraulic.specific_gravity = 1.0;
    network.options_hydraulic.relative_viscosity = 1.0;

    network.options_energy.global_pump_efficiency_percent = 75.0;
    network.options_energy.global_energy_price_per_kw_h = 0.0;
    network.options_energy.demand_charge_per_kw = 0.0;

    HydraulicPatternTime demand_pattern;
    demand_pattern.id = QStringLiteral("1");
    demand_pattern.uuid = QUuid::createUuid();
    demand_pattern.multipliers = {1.0, 1.2, 1.4, 1.6, 1.4, 1.2, 1.0, 0.8, 0.6, 0.4, 0.6, 0.8};
    network.patterns_time.append(demand_pattern);
    network.options_hydraulic.default_demand_pattern_uuid = demand_pattern.uuid;

    const QUuid node_10 = appendJunction(network, QStringLiteral("10"), 710.0, 0.0);
    const QUuid node_11 = appendJunction(network, QStringLiteral("11"), 710.0, 150.0);
    const QUuid node_12 = appendJunction(network, QStringLiteral("12"), 700.0, 150.0);
    const QUuid node_13 = appendJunction(network, QStringLiteral("13"), 695.0, 100.0);
    const QUuid node_21 = appendJunction(network, QStringLiteral("21"), 700.0, 150.0);
    const QUuid node_22 = appendJunction(network, QStringLiteral("22"), 695.0, 200.0);
    const QUuid node_23 = appendJunction(network, QStringLiteral("23"), 690.0, 150.0);
    const QUuid node_31 = appendJunction(network, QStringLiteral("31"), 700.0, 100.0);
    const QUuid node_32 = appendJunction(network, QStringLiteral("32"), 710.0, 100.0);

    HydraulicNodeReservoir reservoir;
    reservoir.id = QStringLiteral("9");
    reservoir.uuid = QUuid::createUuid();
    reservoir.hydraulic_head_m = feetToMetres(800.0);
    network.nodes_reservoirs.append(reservoir);

    HydraulicNodeTank tank;
    tank.id = QStringLiteral("2");
    tank.uuid = QUuid::createUuid();
    tank.bottom_elevation_m = feetToMetres(850.0);
    tank.water_level_initial_m = feetToMetres(120.0);
    tank.water_level_minimum_m = feetToMetres(100.0);
    tank.water_level_maximum_m = feetToMetres(150.0);
    tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;
    tank.diameter_m = feetToMetres(50.5);
    tank.minimum_volume_m3 = 0.0;
    tank.can_overflow = false;
    network.nodes_tanks.append(tank);

    appendPipe(network, QStringLiteral("10"), node_10, node_11, 10530.0, 18.0);
    appendPipe(network, QStringLiteral("11"), node_11, node_12, 5280.0, 14.0);
    appendPipe(network, QStringLiteral("12"), node_12, node_13, 5280.0, 10.0);
    appendPipe(network, QStringLiteral("21"), node_21, node_22, 5280.0, 10.0);
    appendPipe(network, QStringLiteral("22"), node_22, node_23, 5280.0, 12.0);
    appendPipe(network, QStringLiteral("31"), node_31, node_32, 5280.0, 6.0);
    appendPipe(network, QStringLiteral("110"), tank.uuid, node_12, 200.0, 18.0);
    appendPipe(network, QStringLiteral("111"), node_11, node_21, 5280.0, 10.0);
    appendPipe(network, QStringLiteral("112"), node_12, node_22, 5280.0, 12.0);
    appendPipe(network, QStringLiteral("113"), node_13, node_23, 5280.0, 8.0);
    appendPipe(network, QStringLiteral("121"), node_21, node_31, 5280.0, 8.0);
    appendPipe(network, QStringLiteral("122"), node_22, node_32, 5280.0, 6.0);

    HydraulicCurvePumpHead pump_curve;
    pump_curve.id = QStringLiteral("1");
    pump_curve.uuid = QUuid::createUuid();
    HydraulicCurvePumpHeadPoint pump_point;
    pump_point.flow_m3_per_h = gallonsPerMinuteToCubicMetresPerHour(1500.0);
    pump_point.head_gain_m = feetToMetres(250.0);
    pump_curve.points.append(pump_point);
    network.curves_pump_head.append(pump_curve);

    HydraulicLinkPump pump;
    pump.id = QStringLiteral("9");
    pump.uuid = QUuid::createUuid();
    pump.node_uuid_from = reservoir.uuid;
    pump.node_uuid_to = node_10;
    pump.definition_type = HydraulicLinkPumpDefinitionType::OnePointCurve;
    pump.head_curve_uuid = pump_curve.uuid;
    pump.initial_speed_ratio = 1.0;
    pump.initial_status = HydraulicLinkPumpInitialStatus::On;
    pump.efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Global;
    pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Global;
    network.links_pumps.append(pump);

    HydraulicControlSimple open_pump;
    open_pump.id = QStringLiteral("NET1_OPEN_PUMP_BELOW_110_FT");
    open_pump.uuid = QUuid::createUuid();
    open_pump.type = HydraulicControlSimpleType::LowLevel;
    open_pump.link_uuid = pump.uuid;
    open_pump.action = HydraulicControlActionType::Open;
    open_pump.trigger_node_uuid = tank.uuid;
    open_pump.trigger_water_level_m = feetToMetres(110.0);
    network.controls_simple.append(open_pump);
    fixture.native_control_ids_by_index.insert(1, open_pump.id);

    HydraulicControlSimple close_pump;
    close_pump.id = QStringLiteral("NET1_CLOSE_PUMP_ABOVE_140_FT");
    close_pump.uuid = QUuid::createUuid();
    close_pump.type = HydraulicControlSimpleType::HighLevel;
    close_pump.link_uuid = pump.uuid;
    close_pump.action = HydraulicControlActionType::Close;
    close_pump.trigger_node_uuid = tank.uuid;
    close_pump.trigger_water_level_m = feetToMetres(140.0);
    network.controls_simple.append(close_pump);
    fixture.native_control_ids_by_index.insert(2, close_pump.id);

    return fixture;
}
}
