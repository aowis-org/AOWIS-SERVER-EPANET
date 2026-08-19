#include "epanet_result_reader.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <cmath>

namespace
{
HydraulicSimulationStatus readNodeValue(const EpanetProject &project, int node_index, int backend_property, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, HydraulicSimulationStatusProperty property, const QString &message, double &value)
{
    const int error = EN_getnodevalue(project.handle(), node_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ReadNodeResult, QStringLiteral("EN_getnodevalue"), entity_type, entity_id, entity_uuid, message);
    if (status.success)
        return status;

    status.property = property;
    status.entity.index = node_index;
    return status;
}

HydraulicSimulationStatus readLinkValue(const EpanetProject &project, int link_index, int backend_property, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, HydraulicSimulationStatusProperty property, const QString &message, double &value)
{
    const int error = EN_getlinkvalue(project.handle(), link_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ReadLinkResult, QStringLiteral("EN_getlinkvalue"), entity_type, entity_id, entity_uuid, message);
    if (status.success)
        return status;

    status.property = property;
    status.entity.index = link_index;
    return status;
}

HydraulicSimulationStatus readStatistic(const EpanetProject &project, int backend_statistic, const QString &message, double &value)
{
    const int error = EN_getstatistic(project.handle(), backend_statistic, &value);
    if (error == 0)
        return makeEpanetSuccess();

    return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ReadStatistics, HydraulicSimulationStatusOperation::ReadStatistic, QStringLiteral("EN_getstatistic"), HydraulicSimulationStatusEntityType::Result, QString(), message);
}

bool assignPipeRoughness(HydraulicSimulationResultLinkPipe &result, HydraulicHeadlossFormula headloss_formula, double roughness_backend_value)
{
    switch (headloss_formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        result.roughness_hazen_williams = roughness_backend_value;
        return true;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        result.roughness_darcy_weisbach_mm = roughness_backend_value;
        return true;
    case HydraulicHeadlossFormula::ChezyManning:
        result.roughness_chezy_manning = roughness_backend_value;
        return true;
    }

    return false;
}

bool resolvePumpState(double backend_state, HydraulicSimulationPumpState &state)
{
    switch (static_cast<int>(backend_state))
    {
    case EN_PUMP_XHEAD:
        state = HydraulicSimulationPumpState::CannotSupplyHead;
        return true;
    case EN_PUMP_CLOSED:
        state = HydraulicSimulationPumpState::Closed;
        return true;
    case EN_PUMP_OPEN:
        state = HydraulicSimulationPumpState::Open;
        return true;
    case EN_PUMP_XFLOW:
        state = HydraulicSimulationPumpState::CannotSupplyFlow;
        return true;
    }

    return false;
}
}

EpanetResultReader::EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices)
    : project(project), network(network), indices(indices)
{
}

HydraulicSimulationStatus EpanetResultReader::read(HydraulicSimulationResult &result) const
{
    HydraulicSimulationStatus status = readNodesJunctions(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readNodesReservoirs(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readNodesTanks(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksPipes(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksPumps(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksValves(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readStatistics(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readNextEvent(result);
    result.status = status;
    return status;
}

HydraulicSimulationStatus EpanetResultReader::readNodesJunctions(HydraulicSimulationResult &result) const
{
    for (const HydraulicNodeJunction &junction : this->network.nodes_junctions)
    {
        const int junction_index = this->indices.nodes_junctions.value(junction.uuid, 0);
        if (junction_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Junction index is missing from the EPANET registry"));

        HydraulicSimulationResultNodeJunction junction_result;
        junction_result.id = junction.id;
        junction_result.uuid = junction.uuid;

        HydraulicSimulationStatus status = readNodeValue(this->project, junction_index, EN_FULLDEMAND, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get requested junction demand"), junction_result.demand_requested_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_DEMANDFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get delivered junction demand"), junction_result.demand_delivered_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_DEMANDDEFICIT, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::DemandDeficit, QStringLiteral("Failed to get junction demand deficit"), junction_result.demand_deficit_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_DEMAND, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get total junction demand"), junction_result.total_demand_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_EMITTERFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Emitter, QStringLiteral("Failed to get junction emitter flow"), junction_result.emitter_flow_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_LEAKAGEFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Leakage, QStringLiteral("Failed to get junction leakage flow"), junction_result.leakage_flow_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_HEAD, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get junction head"), junction_result.hydraulic_head_m);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, junction_index, EN_PRESSURE, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Pressure, QStringLiteral("Failed to get junction pressure"), junction_result.pressure_head_m);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readNodeValue(this->project, junction_index, EN_NODE_INCONTROL, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read junction control membership"), appears_in_control);
        if (!status.success)
            return status;
        junction_result.appears_in_control = appears_in_control != 0.0;

        result.nodes_junctions.append(junction_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readNodesReservoirs(HydraulicSimulationResult &result) const
{
    for (const HydraulicNodeReservoir &reservoir : this->network.nodes_reservoirs)
    {
        const int reservoir_index = this->indices.nodes_reservoirs.value(reservoir.uuid, 0);
        if (reservoir_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Reservoir index is missing from the EPANET registry"));

        HydraulicSimulationResultNodeReservoir reservoir_result;
        reservoir_result.id = reservoir.id;
        reservoir_result.uuid = reservoir.uuid;

        HydraulicSimulationStatus status = readNodeValue(this->project, reservoir_index, EN_DEMAND, HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get reservoir net demand"), reservoir_result.net_demand_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, reservoir_index, EN_HEAD, HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get reservoir head"), reservoir_result.hydraulic_head_m);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, reservoir_index, EN_PRESSURE, HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::Pressure, QStringLiteral("Failed to get reservoir pressure"), reservoir_result.pressure_head_m);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readNodeValue(this->project, reservoir_index, EN_NODE_INCONTROL, HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read reservoir control membership"), appears_in_control);
        if (!status.success)
            return status;
        reservoir_result.appears_in_control = appears_in_control != 0.0;

        result.nodes_reservoirs.append(reservoir_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readNodesTanks(HydraulicSimulationResult &result) const
{
    for (const HydraulicNodeTank &tank : this->network.nodes_tanks)
    {
        const int tank_index = this->indices.nodes_tanks.value(tank.uuid, 0);
        if (tank_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Tank index is missing from the EPANET registry"));

        HydraulicSimulationResultNodeTank tank_result;
        tank_result.id = tank.id;
        tank_result.uuid = tank.uuid;

        HydraulicSimulationStatus status = readNodeValue(this->project, tank_index, EN_DEMAND, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get tank net demand"), tank_result.net_demand_m3_per_h);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, tank_index, EN_HEAD, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get tank head"), tank_result.hydraulic_head_m);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, tank_index, EN_PRESSURE, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Pressure, QStringLiteral("Failed to get tank pressure"), tank_result.pressure_head_m);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, tank_index, EN_TANKLEVEL, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Level, QStringLiteral("Failed to get tank level"), tank_result.water_level_m);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, tank_index, EN_TANKVOLUME, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Volume, QStringLiteral("Failed to get tank volume"), tank_result.volume_m3);
        if (!status.success)
            return status;

        status = readNodeValue(this->project, tank_index, EN_MIXZONEVOL, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Volume, QStringLiteral("Failed to get tank mixing-zone volume"), tank_result.mixing_zone_volume_m3);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readNodeValue(this->project, tank_index, EN_NODE_INCONTROL, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read tank control membership"), appears_in_control);
        if (!status.success)
            return status;
        tank_result.appears_in_control = appears_in_control != 0.0;

        result.nodes_tanks.append(tank_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readLinksPipes(HydraulicSimulationResult &result) const
{
    for (const HydraulicLinkPipe &pipe : this->network.links_pipes)
    {
        const int pipe_index = this->indices.links_pipes.value(pipe.uuid, 0);
        if (pipe_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe index is missing from the EPANET registry"));

        HydraulicSimulationResultLinkPipe pipe_result;
        pipe_result.id = pipe.id;
        pipe_result.uuid = pipe.uuid;

        HydraulicSimulationStatus status = readLinkValue(this->project, pipe_index, EN_FLOW, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Flow, QStringLiteral("Failed to get pipe flow"), pipe_result.flow_m3_per_h);
        if (!status.success)
            return status;

        status = readLinkValue(this->project, pipe_index, EN_LINK_LEAKAGE, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Leakage, QStringLiteral("Failed to get pipe leakage flow"), pipe_result.leakage_flow_m3_per_h);
        if (!status.success)
            return status;

        status = readLinkValue(this->project, pipe_index, EN_VELOCITY, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Velocity, QStringLiteral("Failed to get pipe velocity"), pipe_result.velocity_m_per_s);
        if (!status.success)
            return status;

        status = readLinkValue(this->project, pipe_index, EN_HEADLOSS, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Headloss, QStringLiteral("Failed to get pipe head loss"), pipe_result.head_loss_m);
        if (!status.success)
            return status;

        const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
        if (length_m > 0.0)
            pipe_result.head_loss_gradient_m_per_km = pipe_result.head_loss_m / length_m * 1000.0;

        constexpr double meters_per_foot = 0.3048;
        constexpr double cubic_meters_per_hour_per_cubic_foot_per_second = 101.94;
        constexpr double tiny_flow_cubic_feet_per_second = 1.0e-6;
        const double flow_cubic_feet_per_second = std::abs(pipe_result.flow_m3_per_h) / cubic_meters_per_hour_per_cubic_foot_per_second;
        if (length_m > 0.0 && pipe.diameter_mm > 0.0 && flow_cubic_feet_per_second > tiny_flow_cubic_feet_per_second)
        {
            const double head_loss_ft = pipe_result.head_loss_m / meters_per_foot;
            const double diameter_ft = pipe.diameter_mm / 1000.0 / meters_per_foot;
            const double length_ft = length_m / meters_per_foot;
            pipe_result.friction_factor = 39.725 * head_loss_ft * std::pow(diameter_ft, 5.0)
                / length_ft / std::pow(flow_cubic_feet_per_second, 2.0);
        }

        double link_status = 0.0;
        status = readLinkValue(this->project, pipe_index, EN_STATUS, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Status, QStringLiteral("Failed to get pipe status"), link_status);
        if (!status.success)
            return status;
        pipe_result.open = static_cast<int>(link_status) != EN_CLOSED;

        double roughness_backend_value = 0.0;
        status = readLinkValue(this->project, pipe_index, EN_SETTING, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Setting, QStringLiteral("Failed to get pipe roughness"), roughness_backend_value);
        if (!status.success)
            return status;
        if (!assignPipeRoughness(pipe_result, this->network.options_hydraulic.headloss_formula, roughness_backend_value))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusOperation::ReadLinkResult, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Unsupported hydraulic headloss formula while reading pipe roughness"));

        double appears_in_control = 0.0;
        status = readLinkValue(this->project, pipe_index, EN_LINK_INCONTROL, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read pipe control membership"), appears_in_control);
        if (!status.success)
            return status;
        pipe_result.appears_in_control = appears_in_control != 0.0;

        result.links_pipes.append(pipe_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readLinksPumps(HydraulicSimulationResult &result) const
{
    for (const HydraulicLinkPump &pump : this->network.links_pumps)
    {
        const int pump_index = this->indices.links_pumps.value(pump.uuid, 0);
        if (pump_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump index is missing from the EPANET registry"));

        HydraulicSimulationResultLinkPump pump_result;
        pump_result.id = pump.id;
        pump_result.uuid = pump.uuid;

        HydraulicSimulationStatus status = readLinkValue(this->project, pump_index, EN_FLOW, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Flow, QStringLiteral("Failed to get pump flow"), pump_result.flow_m3_per_h);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, pump_index, EN_VELOCITY, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Velocity, QStringLiteral("Failed to get pump velocity"), pump_result.velocity_m_per_s);
        if (!status.success)
            return status;

        double signed_head_loss_m = 0.0;
        status = readLinkValue(this->project, pump_index, EN_HEADLOSS, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Headloss, QStringLiteral("Failed to get pump head gain"), signed_head_loss_m);
        if (!status.success)
            return status;
        pump_result.head_gain_m = -signed_head_loss_m;

        double backend_status = 0.0;
        status = readLinkValue(this->project, pump_index, EN_STATUS, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Status, QStringLiteral("Failed to get pump status"), backend_status);
        if (!status.success)
            return status;
        pump_result.open = static_cast<int>(backend_status) != EN_CLOSED;

        double backend_state = 0.0;
        status = readLinkValue(this->project, pump_index, EN_PUMP_STATE, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Status, QStringLiteral("Failed to get pump operating state"), backend_state);
        if (!status.success)
            return status;
        if (!resolvePumpState(backend_state, pump_result.state))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusOperation::ReadLinkResult, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("EPANET returned an unknown pump operating state"));

        status = readLinkValue(this->project, pump_index, EN_SETTING, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Setting, QStringLiteral("Failed to get pump speed"), pump_result.speed_ratio);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, pump_index, EN_PUMP_EFFIC, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Efficiency, QStringLiteral("Failed to get pump efficiency"), pump_result.efficiency_percent);
        if (!status.success)
            return status;
        pump_result.efficiency_percent *= 100.0;
        status = readLinkValue(this->project, pump_index, EN_ENERGY, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Energy, QStringLiteral("Failed to get pump power"), pump_result.power_kw);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, pump_index, EN_LINKQUAL, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::Quality, QStringLiteral("Failed to get pump quality"), pump_result.quality);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readLinkValue(this->project, pump_index, EN_LINK_INCONTROL, HydraulicSimulationStatusStage::ReadPumpResults, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read pump control membership"), appears_in_control);
        if (!status.success)
            return status;
        pump_result.appears_in_control = appears_in_control != 0.0;

        result.links_pumps.append(pump_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readLinksValves(HydraulicSimulationResult &result) const
{
    for (const HydraulicLinkValve &valve : this->network.links_valves)
    {
        const int valve_index = this->indices.links_valves.value(valve.uuid, 0);
        if (valve_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Valve index is missing from the EPANET registry"));

        HydraulicSimulationResultLinkValve valve_result;
        valve_result.id = valve.id;
        valve_result.uuid = valve.uuid;

        HydraulicSimulationStatus status = readLinkValue(this->project, valve_index, EN_FLOW, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Flow, QStringLiteral("Failed to get valve flow"), valve_result.flow_m3_per_h);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, valve_index, EN_VELOCITY, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Velocity, QStringLiteral("Failed to get valve velocity"), valve_result.velocity_m_per_s);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, valve_index, EN_HEADLOSS, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Headloss, QStringLiteral("Failed to get valve head loss"), valve_result.head_loss_m);
        if (!status.success)
            return status;

        double backend_status = 0.0;
        status = readLinkValue(this->project, valve_index, EN_STATUS, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Status, QStringLiteral("Failed to get valve status"), backend_status);
        if (!status.success)
            return status;
        valve_result.open = static_cast<int>(backend_status) != EN_CLOSED;
        valve_result.active = static_cast<int>(backend_status) > EN_OPEN;

        status = readLinkValue(this->project, valve_index, EN_SETTING, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Setting, QStringLiteral("Failed to get valve setting"), valve_result.setting);
        if (!status.success)
            return status;
        status = readLinkValue(this->project, valve_index, EN_LINKQUAL, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::Quality, QStringLiteral("Failed to get valve quality"), valve_result.quality);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readLinkValue(this->project, valve_index, EN_LINK_INCONTROL, HydraulicSimulationStatusStage::ReadValveResults, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read valve control membership"), appears_in_control);
        if (!status.success)
            return status;
        valve_result.appears_in_control = appears_in_control != 0.0;

        result.links_valves.append(valve_result);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readStatistics(HydraulicSimulationResult &result) const
{
    double hydraulic_iterations = 0.0;
    HydraulicSimulationStatus status = readStatistic(this->project, EN_ITERATIONS, QStringLiteral("Failed to get hydraulic iteration count"), hydraulic_iterations);
    if (!status.success)
        return status;
    result.statistics.hydraulic_iterations = static_cast<qint64>(hydraulic_iterations);

    status = readStatistic(this->project, EN_RELATIVEERROR, QStringLiteral("Failed to get hydraulic relative error"), result.statistics.relative_error);
    if (!status.success)
        return status;

    status = readStatistic(this->project, EN_MAXHEADERROR, QStringLiteral("Failed to get maximum hydraulic head error"), result.statistics.maximum_head_error_m);
    if (!status.success)
        return status;

    status = readStatistic(this->project, EN_MAXFLOWCHANGE, QStringLiteral("Failed to get maximum hydraulic flow change"), result.statistics.maximum_flow_change_m3_per_h);
    if (!status.success)
        return status;

    status = readStatistic(this->project, EN_MASSBALANCE, QStringLiteral("Failed to get water-quality mass balance ratio"), result.statistics.quality_mass_balance_ratio);
    if (!status.success)
        return status;

    double deficient_nodes = 0.0;
    status = readStatistic(this->project, EN_DEFICIENTNODES, QStringLiteral("Failed to get pressure-deficient node count"), deficient_nodes);
    if (!status.success)
        return status;
    result.statistics.deficient_nodes = static_cast<qint64>(deficient_nodes);

    status = readStatistic(this->project, EN_DEMANDREDUCTION, QStringLiteral("Failed to get demand reduction percentage"), result.statistics.demand_reduction_percent);
    if (!status.success)
        return status;

    status = readStatistic(this->project, EN_LEAKAGELOSS, QStringLiteral("Failed to get leakage loss percentage"), result.statistics.leakage_loss_percent);
    if (!status.success)
        return status;

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetResultReader::readNextEvent(HydraulicSimulationResult &result) const
{
    int backend_event_type = 0;
    long duration_s = 0;
    int element_index = 0;
    const int error = EN_timetonextevent(this->project.handle(), &backend_event_type, &duration_s, &element_index);
    if (error != 0)
    {
        return processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::ReadNextEvent, HydraulicSimulationStatusOperation::ReadNextEvent, QStringLiteral("EN_timetonextevent"), HydraulicSimulationStatusEntityType::Result, QString(), QStringLiteral("Failed to read the next hydraulic timestep event"));
    }
    if (duration_s < 0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::ReadNextEvent, HydraulicSimulationStatusOperation::ReadNextEvent, HydraulicSimulationStatusEntityType::Result, QString(), QStringLiteral("EPANET returned a negative next-event duration"));

    result.event_next.time_until_event_s = static_cast<quint64>(duration_s);
    switch (backend_event_type)
    {
    case EN_STEP_REPORT:
        result.event_next.type = HydraulicSimulationTimestepEventType::ReportStep;
        break;
    case EN_STEP_HYD:
        result.event_next.type = HydraulicSimulationTimestepEventType::HydraulicStep;
        break;
    case EN_STEP_WQ:
        result.event_next.type = HydraulicSimulationTimestepEventType::QualityStep;
        break;
    case EN_STEP_TANKEVENT:
    {
        result.event_next.type = HydraulicSimulationTimestepEventType::TankEvent;
        const QUuid tank_uuid = this->indices.nodes_tanks.key(element_index);
        if (tank_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadNextEvent, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Tank, QString(), QStringLiteral("Could not resolve the tank that causes the next hydraulic event"));
        for (const HydraulicNodeTank &tank : this->network.nodes_tanks)
        {
            if (tank.uuid != tank_uuid)
                continue;
            result.event_next.tank_id = tank.id;
            result.event_next.tank_uuid = tank.uuid;
            break;
        }
        break;
    }
    case EN_STEP_CONTROLEVENT:
    {
        result.event_next.type = HydraulicSimulationTimestepEventType::ControlEvent;
        const QUuid control_uuid = this->indices.controls_simple.key(element_index);
        if (control_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadNextEvent, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Control, QString(), QStringLiteral("Could not resolve the simple control that causes the next hydraulic event"));
        for (const HydraulicControlSimple &control : this->network.controls_simple)
        {
            if (control.uuid != control_uuid)
                continue;
            result.event_next.control_id = control.id;
            result.event_next.control_uuid = control.uuid;
            break;
        }
        break;
    }
    default:
        return makeEpanetStatus(HydraulicSimulationStatusStage::ReadNextEvent, HydraulicSimulationStatusOperation::ReadNextEvent, HydraulicSimulationStatusEntityType::Result, QString(), QStringLiteral("EPANET returned an unknown hydraulic timestep event type"));
    }

    return makeEpanetSuccess();
}
