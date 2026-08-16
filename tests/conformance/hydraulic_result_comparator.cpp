#include "hydraulic_result_comparator.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace AowisEpanetTests
{
namespace
{
constexpr NumericTolerance kSolverRelativeErrorTolerance{2.0e-8, 1.0e-7};

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

const HydraulicSimulationResultNodeJunction *findJunction(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const HydraulicSimulationResultNodeReservoir *findReservoir(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

const HydraulicSimulationResultNodeTank *findTank(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPipe *findPipe(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPump *findPump(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
    {
        if (pump.id == id)
            return &pump;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkValve *findValve(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
    {
        if (valve.id == id)
            return &valve;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPumpEnergyUsage *findPumpEnergy(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkPumpEnergyUsage &usage : result.links_pump_energy_usage)
    {
        if (usage.pump_id == id)
            return &usage;
    }
    return nullptr;
}

const HydraulicNodeJunction *findModelJunction(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const HydraulicNodeReservoir *findModelReservoir(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

const HydraulicNodeTank *findModelTank(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const HydraulicLinkPipe *findModelPipe(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const HydraulicLinkPump *findModelPump(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.id == id)
            return &pump;
    }
    return nullptr;
}

const HydraulicLinkValve *findModelValve(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (valve.id == id)
            return &valve;
    }
    return nullptr;
}

const HydraulicControlSimple *findModelControl(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        if (control.id == id)
            return &control;
    }
    return nullptr;
}

void expectUuid(TestContext &context, const QUuid &actual, const QUuid &expected, const ComparisonContext &where)
{
    const std::string actual_text = actual.toString().toStdString();
    const std::string expected_text = expected.toString().toStdString();
    context.expectEqual(actual_text, expected_text, where, "wrapper result UUID must identify the corresponding AOWIS fixture entity");
}

HydraulicSimulationPumpState modelPumpState(NativePumpState state)
{
    switch (state)
    {
    case NativePumpState::CannotSupplyHead:
        return HydraulicSimulationPumpState::CannotSupplyHead;
    case NativePumpState::Closed:
        return HydraulicSimulationPumpState::Closed;
    case NativePumpState::Open:
        return HydraulicSimulationPumpState::Open;
    case NativePumpState::CannotSupplyFlow:
        return HydraulicSimulationPumpState::CannotSupplyFlow;
    }
    return HydraulicSimulationPumpState::Closed;
}

HydraulicSimulationTimestepEventType modelEventType(NativeTimestepEventType type)
{
    switch (type)
    {
    case NativeTimestepEventType::ReportStep:
        return HydraulicSimulationTimestepEventType::ReportStep;
    case NativeTimestepEventType::HydraulicStep:
        return HydraulicSimulationTimestepEventType::HydraulicStep;
    case NativeTimestepEventType::QualityStep:
        return HydraulicSimulationTimestepEventType::QualityStep;
    case NativeTimestepEventType::TankEvent:
        return HydraulicSimulationTimestepEventType::TankEvent;
    case NativeTimestepEventType::ControlEvent:
        return HydraulicSimulationTimestepEventType::ControlEvent;
    }
    return HydraulicSimulationTimestepEventType::HydraulicStep;
}

void compareJunctions(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.nodes_junctions.size()),
        static_cast<std::int64_t>(expected.nodes_junctions.size()),
        comparison("nodes_junctions.size", expected.time_elapsed_s));

    for (const NativeJunctionResult &expected_junction : expected.nodes_junctions)
    {
        const std::string id = expected_junction.id.toStdString();
        const HydraulicSimulationResultNodeJunction *actual_junction = findJunction(actual, expected_junction.id);
        context.expect(actual_junction != nullptr, "wrapper is missing native junction result " + id);
        if (actual_junction == nullptr)
            continue;

        const HydraulicNodeJunction *model_junction = findModelJunction(network, expected_junction.id);
        context.expect(model_junction != nullptr, "AOWIS fixture is missing junction " + id);
        if (model_junction != nullptr)
            expectUuid(context, actual_junction->uuid, model_junction->uuid,
                comparison("uuid", expected.time_elapsed_s, "Junction", id));

        context.expectEqual(actual_junction->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->demand_requested_m3_per_h, expected_junction.demand_requested_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("demand_requested_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->demand_delivered_m3_per_h, expected_junction.demand_delivered_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("demand_delivered_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->demand_deficit_m3_per_h, expected_junction.demand_deficit_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("demand_deficit_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->total_demand_m3_per_h, expected_junction.total_demand_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("total_demand_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->emitter_flow_m3_per_h, expected_junction.emitter_flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("emitter_flow_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->leakage_flow_m3_per_h, expected_junction.leakage_flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("leakage_flow_m3_per_h", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->head_m, expected_junction.head_m,
            HydraulicQuantity::HeadMetres, comparison("head_m", expected.time_elapsed_s, "Junction", id));
        context.expectNear(actual_junction->pressure_head_m, expected_junction.pressure_head_m,
            HydraulicQuantity::PressureHeadMetres, comparison("pressure_head_m", expected.time_elapsed_s, "Junction", id));
        context.expectEqual(actual_junction->appears_in_control, expected_junction.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Junction", id));
    }
}

void compareReservoirs(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.nodes_reservoirs.size()),
        static_cast<std::int64_t>(expected.nodes_reservoirs.size()),
        comparison("nodes_reservoirs.size", expected.time_elapsed_s));

    for (const NativeReservoirResult &expected_reservoir : expected.nodes_reservoirs)
    {
        const std::string id = expected_reservoir.id.toStdString();
        const HydraulicSimulationResultNodeReservoir *actual_reservoir = findReservoir(actual, expected_reservoir.id);
        context.expect(actual_reservoir != nullptr, "wrapper is missing native reservoir result " + id);
        if (actual_reservoir == nullptr)
            continue;

        const HydraulicNodeReservoir *model_reservoir = findModelReservoir(network, expected_reservoir.id);
        context.expect(model_reservoir != nullptr, "AOWIS fixture is missing reservoir " + id);
        if (model_reservoir != nullptr)
            expectUuid(context, actual_reservoir->uuid, model_reservoir->uuid,
                comparison("uuid", expected.time_elapsed_s, "Reservoir", id));

        context.expectEqual(actual_reservoir->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Reservoir", id));
        context.expectNear(actual_reservoir->net_demand_m3_per_h, expected_reservoir.net_demand_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("net_demand_m3_per_h", expected.time_elapsed_s, "Reservoir", id));
        context.expectNear(actual_reservoir->head_m, expected_reservoir.head_m,
            HydraulicQuantity::HeadMetres, comparison("head_m", expected.time_elapsed_s, "Reservoir", id));
        context.expectNear(actual_reservoir->pressure_head_m, expected_reservoir.pressure_head_m,
            HydraulicQuantity::PressureHeadMetres, comparison("pressure_head_m", expected.time_elapsed_s, "Reservoir", id));
        context.expectEqual(actual_reservoir->appears_in_control, expected_reservoir.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Reservoir", id));
    }
}

void compareTanks(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.nodes_tanks.size()),
        static_cast<std::int64_t>(expected.nodes_tanks.size()),
        comparison("nodes_tanks.size", expected.time_elapsed_s));

    for (const NativeTankResult &expected_tank : expected.nodes_tanks)
    {
        const std::string id = expected_tank.id.toStdString();
        const HydraulicSimulationResultNodeTank *actual_tank = findTank(actual, expected_tank.id);
        context.expect(actual_tank != nullptr, "wrapper is missing native tank result " + id);
        if (actual_tank == nullptr)
            continue;

        const HydraulicNodeTank *model_tank = findModelTank(network, expected_tank.id);
        context.expect(model_tank != nullptr, "AOWIS fixture is missing tank " + id);
        if (model_tank != nullptr)
            expectUuid(context, actual_tank->uuid, model_tank->uuid,
                comparison("uuid", expected.time_elapsed_s, "Tank", id));

        context.expectEqual(actual_tank->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->net_demand_m3_per_h, expected_tank.net_demand_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("net_demand_m3_per_h", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->head_m, expected_tank.head_m,
            HydraulicQuantity::HeadMetres, comparison("head_m", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->pressure_head_m, expected_tank.pressure_head_m,
            HydraulicQuantity::PressureHeadMetres, comparison("pressure_head_m", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->water_level_m, expected_tank.water_level_m,
            HydraulicQuantity::LengthMetres, comparison("water_level_m", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->volume_m3, expected_tank.volume_m3,
            HydraulicQuantity::VolumeM3, comparison("volume_m3", expected.time_elapsed_s, "Tank", id));
        context.expectNear(actual_tank->mixing_zone_volume_m3, expected_tank.mixing_zone_volume_m3,
            HydraulicQuantity::VolumeM3, comparison("mixing_zone_volume_m3", expected.time_elapsed_s, "Tank", id));
        context.expectEqual(actual_tank->appears_in_control, expected_tank.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Tank", id));
    }
}

void comparePipeRoughness(const NativePipeResult &expected, const HydraulicSimulationResultLinkPipe &actual, HydraulicHeadlossFormula formula, std::int64_t time_s, TestContext &context)
{
    const std::string id = expected.id.toStdString();
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        context.expect(actual.roughness_hw.has_value(), "Hazen-Williams pipe result is missing roughness");
        context.expect(!actual.roughness_dw_mm.has_value() && !actual.roughness_cm.has_value(), "Hazen-Williams pipe result populated the wrong roughness fields");
        if (actual.roughness_hw.has_value())
            context.expectNear(actual.roughness_hw.value(), expected.roughness, HydraulicQuantity::Setting,
                comparison("roughness_hw", time_s, "Pipe", id));
        break;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        context.expect(actual.roughness_dw_mm.has_value(), "Darcy-Weisbach pipe result is missing roughness");
        context.expect(!actual.roughness_hw.has_value() && !actual.roughness_cm.has_value(), "Darcy-Weisbach pipe result populated the wrong roughness fields");
        if (actual.roughness_dw_mm.has_value())
            context.expectNear(actual.roughness_dw_mm.value(), expected.roughness, HydraulicQuantity::Setting,
                comparison("roughness_dw_mm", time_s, "Pipe", id));
        break;
    case HydraulicHeadlossFormula::ChezyManning:
        context.expect(actual.roughness_cm.has_value(), "Chezy-Manning pipe result is missing roughness");
        context.expect(!actual.roughness_hw.has_value() && !actual.roughness_dw_mm.has_value(), "Chezy-Manning pipe result populated the wrong roughness fields");
        if (actual.roughness_cm.has_value())
            context.expectNear(actual.roughness_cm.value(), expected.roughness, HydraulicQuantity::Setting,
                comparison("roughness_cm", time_s, "Pipe", id));
        break;
    }
}

void comparePipes(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.links_pipes.size()),
        static_cast<std::int64_t>(expected.links_pipes.size()),
        comparison("links_pipes.size", expected.time_elapsed_s));

    for (const NativePipeResult &expected_pipe : expected.links_pipes)
    {
        const std::string id = expected_pipe.id.toStdString();
        const HydraulicSimulationResultLinkPipe *actual_pipe = findPipe(actual, expected_pipe.id);
        context.expect(actual_pipe != nullptr, "wrapper is missing native pipe result " + id);
        if (actual_pipe == nullptr)
            continue;

        const HydraulicLinkPipe *model_pipe = findModelPipe(network, expected_pipe.id);
        context.expect(model_pipe != nullptr, "AOWIS fixture is missing pipe " + id);
        if (model_pipe != nullptr)
            expectUuid(context, actual_pipe->uuid, model_pipe->uuid,
                comparison("uuid", expected.time_elapsed_s, "Pipe", id));

        context.expectEqual(actual_pipe->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Pipe", id));
        context.expectNear(actual_pipe->flow_m3_per_h, expected_pipe.flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("flow_m3_per_h", expected.time_elapsed_s, "Pipe", id));
        context.expectNear(actual_pipe->leakage_flow_m3_per_h, expected_pipe.leakage_flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("leakage_flow_m3_per_h", expected.time_elapsed_s, "Pipe", id));
        context.expectNear(actual_pipe->velocity_m_per_s, expected_pipe.velocity_m_per_s,
            HydraulicQuantity::VelocityMetresPerSecond, comparison("velocity_m_per_s", expected.time_elapsed_s, "Pipe", id));
        context.expectNear(actual_pipe->head_loss_m, expected_pipe.head_loss_m,
            HydraulicQuantity::HeadMetres, comparison("head_loss_m", expected.time_elapsed_s, "Pipe", id));
        context.expectNear(actual_pipe->unit_head_loss_m_per_km, expected_pipe.unit_head_loss_m_per_km,
            HydraulicQuantity::HeadMetres, comparison("unit_head_loss_m_per_km", expected.time_elapsed_s, "Pipe", id));

        // EPANET reports an equivalent friction factor reconstructed from head loss / flow^2.
        // Near a stagnant state both quantities are solver residuals, so changing only the
        // external EPANET unit system (for example GPM versus CMH) can amplify harmless
        // round-off into a visibly different friction factor even when the hydraulic state
        // itself agrees. Only compare the derived value when head loss is resolvable at the
        // same absolute scale used by the primary head-loss conformance check.
        const NumericTolerance head_loss_tolerance = toleranceFor(HydraulicQuantity::HeadMetres);
        const bool friction_factor_resolvable =
            std::max(std::abs(actual_pipe->head_loss_m), std::abs(expected_pipe.head_loss_m)) > head_loss_tolerance.absolute;
        if (friction_factor_resolvable)
        {
            context.expectNear(actual_pipe->friction_factor, expected_pipe.friction_factor,
                HydraulicQuantity::FrictionFactor, comparison("friction_factor", expected.time_elapsed_s, "Pipe", id));
        }

        context.expectEqual(actual_pipe->open, expected_pipe.open,
            comparison("open", expected.time_elapsed_s, "Pipe", id));
        context.expectEqual(actual_pipe->appears_in_control, expected_pipe.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Pipe", id));
        comparePipeRoughness(expected_pipe, *actual_pipe, network.options_hydraulic.headloss_formula,
            expected.time_elapsed_s, context);
    }
}

void comparePumps(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.links_pumps.size()),
        static_cast<std::int64_t>(expected.links_pumps.size()),
        comparison("links_pumps.size", expected.time_elapsed_s));

    for (const NativePumpResult &expected_pump : expected.links_pumps)
    {
        const std::string id = expected_pump.id.toStdString();
        const HydraulicSimulationResultLinkPump *actual_pump = findPump(actual, expected_pump.id);
        context.expect(actual_pump != nullptr, "wrapper is missing native pump result " + id);
        if (actual_pump == nullptr)
            continue;

        const HydraulicLinkPump *model_pump = findModelPump(network, expected_pump.id);
        context.expect(model_pump != nullptr, "AOWIS fixture is missing pump " + id);
        if (model_pump != nullptr)
            expectUuid(context, actual_pump->uuid, model_pump->uuid,
                comparison("uuid", expected.time_elapsed_s, "Pump", id));

        context.expectEqual(actual_pump->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->flow_m3_per_h, expected_pump.flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("flow_m3_per_h", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->velocity_m_per_s, expected_pump.velocity_m_per_s,
            HydraulicQuantity::VelocityMetresPerSecond, comparison("velocity_m_per_s", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->head_gain_m, expected_pump.head_gain_m,
            HydraulicQuantity::HeadMetres, comparison("head_gain_m", expected.time_elapsed_s, "Pump", id));
        context.expectEqual(actual_pump->open, expected_pump.open,
            comparison("open", expected.time_elapsed_s, "Pump", id));
        context.expectEqual(static_cast<std::int64_t>(actual_pump->state),
            static_cast<std::int64_t>(modelPumpState(expected_pump.state)),
            comparison("state", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->speed, expected_pump.speed,
            HydraulicQuantity::Setting, comparison("speed", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->efficiency_percent, expected_pump.efficiency_percent,
            HydraulicQuantity::Percent, comparison("efficiency_percent", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_pump->power_kw, expected_pump.power_kw,
            HydraulicQuantity::PowerKw, comparison("power_kw", expected.time_elapsed_s, "Pump", id));
        context.expectEqual(actual_pump->appears_in_control, expected_pump.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Pump", id));
    }
}

void compareValves(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.links_valves.size()),
        static_cast<std::int64_t>(expected.links_valves.size()),
        comparison("links_valves.size", expected.time_elapsed_s));

    for (const NativeValveResult &expected_valve : expected.links_valves)
    {
        const std::string id = expected_valve.id.toStdString();
        const HydraulicSimulationResultLinkValve *actual_valve = findValve(actual, expected_valve.id);
        context.expect(actual_valve != nullptr, "wrapper is missing native valve result " + id);
        if (actual_valve == nullptr)
            continue;

        const HydraulicLinkValve *model_valve = findModelValve(network, expected_valve.id);
        context.expect(model_valve != nullptr, "fixture is missing valve " + id);
        if (model_valve != nullptr)
            expectUuid(context, actual_valve->uuid, model_valve->uuid,
                comparison("uuid", expected.time_elapsed_s, "Valve", id));

        context.expectEqual(actual_valve->id.toStdString(), id,
            comparison("id", expected.time_elapsed_s, "Valve", id));
        context.expectNear(actual_valve->flow_m3_per_h, expected_valve.flow_m3_per_h,
            HydraulicQuantity::FlowM3PerHour, comparison("flow_m3_per_h", expected.time_elapsed_s, "Valve", id));
        context.expectNear(actual_valve->velocity_m_per_s, expected_valve.velocity_m_per_s,
            HydraulicQuantity::VelocityMetresPerSecond, comparison("velocity_m_per_s", expected.time_elapsed_s, "Valve", id));
        context.expectNear(actual_valve->head_loss_m, expected_valve.head_loss_m,
            HydraulicQuantity::HeadMetres, comparison("head_loss_m", expected.time_elapsed_s, "Valve", id));
        context.expectEqual(actual_valve->open, expected_valve.open,
            comparison("open", expected.time_elapsed_s, "Valve", id));
        context.expectEqual(actual_valve->active, expected_valve.active,
            comparison("active", expected.time_elapsed_s, "Valve", id));
        context.expectNear(actual_valve->setting, expected_valve.setting,
            HydraulicQuantity::Setting, comparison("setting", expected.time_elapsed_s, "Valve", id));
        context.expectEqual(actual_valve->appears_in_control, expected_valve.appears_in_control,
            comparison("appears_in_control", expected.time_elapsed_s, "Valve", id));
    }
}

void compareStatistics(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, TestContext &context)
{
    context.expectEqual(actual.statistics.hydraulic_iterations, expected.statistics.hydraulic_iterations,
        comparison("statistics.hydraulic_iterations", expected.time_elapsed_s));
    // EN_RELATIVEERROR is a solver convergence diagnostic rather than a physical output.
    // Equivalent networks expressed in different EPANET unit systems can follow slightly
    // different floating-point convergence paths while producing the same hydraulic state.
    context.expectNear(actual.statistics.relative_error, expected.statistics.relative_error,
        kSolverRelativeErrorTolerance, comparison("statistics.relative_error", expected.time_elapsed_s));
    context.expectNear(actual.statistics.maximum_head_error_m, expected.statistics.maximum_head_error_m,
        HydraulicQuantity::HeadMetres, comparison("statistics.maximum_head_error_m", expected.time_elapsed_s));
    context.expectNear(actual.statistics.maximum_flow_change_m3_per_h, expected.statistics.maximum_flow_change_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("statistics.maximum_flow_change_m3_per_h", expected.time_elapsed_s));
    context.expectEqual(actual.statistics.deficient_nodes, expected.statistics.deficient_nodes,
        comparison("statistics.deficient_nodes", expected.time_elapsed_s));
    context.expectNear(actual.statistics.demand_reduction_percent, expected.statistics.demand_reduction_percent,
        HydraulicQuantity::Percent, comparison("statistics.demand_reduction_percent", expected.time_elapsed_s));
    context.expectNear(actual.statistics.leakage_loss_percent, expected.statistics.leakage_loss_percent,
        HydraulicQuantity::Percent, comparison("statistics.leakage_loss_percent", expected.time_elapsed_s));
}

void compareEvent(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.event_next.type),
        static_cast<std::int64_t>(modelEventType(expected.event_next.type)),
        comparison("event_next.type", expected.time_elapsed_s));
    context.expectEqual(static_cast<std::int64_t>(actual.event_next.time_until_event_s),
        expected.event_next.time_until_event_s,
        comparison("event_next.time_until_event_s", expected.time_elapsed_s));
    context.expectEqual(actual.event_next.tank_id.toStdString(), expected.event_next.tank_id.toStdString(),
        comparison("event_next.tank_id", expected.time_elapsed_s));
    context.expectEqual(actual.event_next.control_id.toStdString(), expected.event_next.control_id.toStdString(),
        comparison("event_next.control_id", expected.time_elapsed_s));

    if (expected.event_next.tank_id.isEmpty())
        context.expect(actual.event_next.tank_uuid.isNull(), "event without a tank ID must not return a tank UUID");
    else
    {
        const HydraulicNodeTank *tank = findModelTank(network, expected.event_next.tank_id);
        context.expect(tank != nullptr, "native tank event references a tank missing from the fixture");
        if (tank != nullptr)
            expectUuid(context, actual.event_next.tank_uuid, tank->uuid,
                comparison("event_next.tank_uuid", expected.time_elapsed_s, "Tank", expected.event_next.tank_id.toStdString()));
    }

    if (expected.event_next.control_id.isEmpty())
        context.expect(actual.event_next.control_uuid.isNull(), "event without a control ID must not return a control UUID");
    else
    {
        const HydraulicControlSimple *control = findModelControl(network, expected.event_next.control_id);
        context.expect(control != nullptr, "native control event references a control missing from the fixture");
        if (control != nullptr)
            expectUuid(context, actual.event_next.control_uuid, control->uuid,
                comparison("event_next.control_uuid", expected.time_elapsed_s, "Control", expected.event_next.control_id.toStdString()));
    }
}

void comparePumpEnergy(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, const NetworkHydraulic &network, TestContext &context)
{
    context.expectEqual(static_cast<std::int64_t>(actual.links_pump_energy_usage.size()),
        static_cast<std::int64_t>(expected.links_pump_energy_usage.size()),
        comparison("links_pump_energy_usage.size", expected.time_elapsed_s));
    for (const NativePumpEnergyUsage &expected_usage : expected.links_pump_energy_usage)
    {
        const std::string id = expected_usage.pump_id.toStdString();
        const HydraulicSimulationResultLinkPumpEnergyUsage *actual_usage = findPumpEnergy(actual, expected_usage.pump_id);
        context.expect(actual_usage != nullptr, "wrapper is missing native pump energy summary " + id);
        if (actual_usage == nullptr)
            continue;

        const HydraulicLinkPump *model_pump = findModelPump(network, expected_usage.pump_id);
        context.expect(model_pump != nullptr, "fixture is missing pump energy entity " + id);
        if (model_pump != nullptr)
            expectUuid(context, actual_usage->pump_uuid, model_pump->uuid,
                comparison("pump_uuid", expected.time_elapsed_s, "Pump", id));
        context.expectEqual(actual_usage->pump_id.toStdString(), id,
            comparison("pump_id", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->time_online_percent, expected_usage.time_online_percent,
            HydraulicQuantity::Percent, comparison("time_online_percent", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->average_efficiency_percent, expected_usage.average_efficiency_percent,
            HydraulicQuantity::Percent, comparison("average_efficiency_percent", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->average_kw_per_flow_unit, expected_usage.average_kw_per_flow_unit,
            NumericTolerance{1.0e-9, 1.0e-6}, comparison("average_kw_per_flow_unit", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->average_power_kw, expected_usage.average_power_kw,
            HydraulicQuantity::PowerKw, comparison("average_power_kw", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->peak_power_kw, expected_usage.peak_power_kw,
            HydraulicQuantity::PowerKw, comparison("peak_power_kw", expected.time_elapsed_s, "Pump", id));
        context.expectNear(actual_usage->average_cost_per_day, expected_usage.average_cost_per_day,
            HydraulicQuantity::Cost, comparison("average_cost_per_day", expected.time_elapsed_s, "Pump", id));
    }
}

void compareSummaries(const NativeHydraulicResult &expected, const HydraulicSimulationResult &actual, TestContext &context)
{
    context.expectNear(actual.flow_balance.total_inflow_m3_per_h, expected.flow_balance.total_inflow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.total_inflow_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.total_outflow_m3_per_h, expected.flow_balance.total_outflow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.total_outflow_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.consumer_demand_m3_per_h, expected.flow_balance.consumer_demand_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.consumer_demand_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.demand_deficit_m3_per_h, expected.flow_balance.demand_deficit_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.demand_deficit_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.emitter_flow_m3_per_h, expected.flow_balance.emitter_flow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.emitter_flow_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.leakage_flow_m3_per_h, expected.flow_balance.leakage_flow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.leakage_flow_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.storage_flow_m3_per_h, expected.flow_balance.storage_flow_m3_per_h,
        HydraulicQuantity::FlowM3PerHour, comparison("flow_balance.storage_flow_m3_per_h", expected.time_elapsed_s));
    context.expectNear(actual.flow_balance.flow_balance_ratio, expected.flow_balance.flow_balance_ratio,
        HydraulicQuantity::Dimensionless, comparison("flow_balance.flow_balance_ratio", expected.time_elapsed_s));

    context.expectNear(actual.energy_usage.peak_power_kw, expected.energy_usage.peak_power_kw,
        HydraulicQuantity::PowerKw, comparison("energy_usage.peak_power_kw", expected.time_elapsed_s));
    context.expectNear(actual.energy_usage.energy_cost_per_day, expected.energy_usage.energy_cost_per_day,
        HydraulicQuantity::Cost, comparison("energy_usage.energy_cost_per_day", expected.time_elapsed_s));
    context.expectNear(actual.energy_usage.demand_charge_per_day, expected.energy_usage.demand_charge_per_day,
        HydraulicQuantity::Cost, comparison("energy_usage.demand_charge_per_day", expected.time_elapsed_s));
    context.expectNear(actual.energy_usage.total_cost_per_day, expected.energy_usage.total_cost_per_day,
        HydraulicQuantity::Cost, comparison("energy_usage.total_cost_per_day", expected.time_elapsed_s));
}
}

void compareHydraulicTimelines(const NativeHydraulicTimeline &expected,
    const EpanetResultRun &actual,
    const NetworkHydraulic &network,
    TestContext &context)
{
    context.expect(expected.success, expected.error.toStdString());
    context.expect(!actual.cancelled, "wrapper run must not be cancelled");
    context.expect(actual.result_timeline.status.success, "wrapper run must return a successful status");
    context.expect(actual.result_timeline.validity == HydraulicSimulationResultValidity::Valid,
        "wrapper run must return valid numerical results");
    context.expectEqual(static_cast<std::int64_t>(actual.result_timeline.results.size()),
        static_cast<std::int64_t>(expected.results.size()), comparison("results.size"),
        "native and wrapper timelines must contain identical hydraulic event sequences");

    const int comparable_results = std::min(static_cast<int>(actual.result_timeline.results.size()),
        static_cast<int>(expected.results.size()));
    for (int index = 0; index < comparable_results; index++)
    {
        const NativeHydraulicResult &expected_result = expected.results.at(index);
        const HydraulicSimulationResult &actual_result = actual.result_timeline.results.at(index);
        context.expectEqual(static_cast<std::int64_t>(actual_result.time_elapsed_s), expected_result.time_elapsed_s,
            comparison("time_elapsed_s", expected_result.time_elapsed_s));
        context.expect(actual_result.status.success,
            "wrapper timestep status failed at " + std::to_string(expected_result.time_elapsed_s) + " seconds");

        compareJunctions(expected_result, actual_result, network, context);
        compareReservoirs(expected_result, actual_result, network, context);
        compareTanks(expected_result, actual_result, network, context);
        comparePipes(expected_result, actual_result, network, context);
        comparePumps(expected_result, actual_result, network, context);
        compareValves(expected_result, actual_result, network, context);
        compareStatistics(expected_result, actual_result, context);
        compareEvent(expected_result, actual_result, network, context);
        comparePumpEnergy(expected_result, actual_result, network, context);
        compareSummaries(expected_result, actual_result, context);
    }
}
}
