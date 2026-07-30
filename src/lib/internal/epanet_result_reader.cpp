#include "epanet_result_reader.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

namespace
{
HydraulicSimulationStatus readNodeValue(const EpanetProject &project, int node_index, int backend_property, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, HydraulicSimulationStatusProperty property, const QString &message, double &value)
{
    const int error = EN_getnodevalue(project.handle(), node_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = makeEpanetError(project, error, stage, HydraulicSimulationStatusOperation::ReadNodeResult, QStringLiteral("EN_getnodevalue"), entity_type, entity_id, entity_uuid, message);
    status.property = property;
    status.entity.index = node_index;
    return status;
}

HydraulicSimulationStatus readLinkValue(const EpanetProject &project, int link_index, int backend_property, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, HydraulicSimulationStatusProperty property, const QString &message, double &value)
{
    const int error = EN_getlinkvalue(project.handle(), link_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = makeEpanetError(project, error, stage, HydraulicSimulationStatusOperation::ReadLinkResult, QStringLiteral("EN_getlinkvalue"), entity_type, entity_id, entity_uuid, message);
    status.property = property;
    status.entity.index = link_index;
    return status;
}
}

EpanetResultReader::EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices)
    : project(project), network(network), indices(indices)
{
}

HydraulicSimulationStatus EpanetResultReader::read(HydraulicSimulationResult &result) const
{
    HydraulicSimulationStatus status = this->readNodesJunctions(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = this->readNodesReservoirs(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = this->readNodesTanks(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = this->readLinksPipes(result);
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
        junction_result.demand_requested_m3_per_h *= 3.6;

        status = readNodeValue(this->project, junction_index, EN_DEMANDFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Demand, QStringLiteral("Failed to get delivered junction demand"), junction_result.demand_delivered_m3_per_h);
        if (!status.success)
            return status;
        junction_result.demand_delivered_m3_per_h *= 3.6;

        status = readNodeValue(this->project, junction_index, EN_DEMANDDEFICIT, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::DemandDeficit, QStringLiteral("Failed to get junction demand deficit"), junction_result.demand_deficit_m3_per_h);
        if (!status.success)
            return status;
        junction_result.demand_deficit_m3_per_h *= 3.6;

        status = readNodeValue(this->project, junction_index, EN_EMITTERFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Emitter, QStringLiteral("Failed to get junction emitter flow"), junction_result.emitter_flow_m3_per_h);
        if (!status.success)
            return status;
        junction_result.emitter_flow_m3_per_h *= 3.6;

        status = readNodeValue(this->project, junction_index, EN_LEAKAGEFLOW, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Leakage, QStringLiteral("Failed to get junction leakage flow"), junction_result.leakage_flow_m3_per_h);
        if (!status.success)
            return status;
        junction_result.leakage_flow_m3_per_h *= 3.6;

        status = readNodeValue(this->project, junction_index, EN_HEAD, HydraulicSimulationStatusStage::ReadJunctionResults, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get junction head"), junction_result.head_m);
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
        reservoir_result.net_demand_m3_per_h *= 3.6;

        status = readNodeValue(this->project, reservoir_index, EN_HEAD, HydraulicSimulationStatusStage::ReadReservoirResults, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get reservoir head"), reservoir_result.head_m);
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
        tank_result.net_demand_m3_per_h *= 3.6;

        status = readNodeValue(this->project, tank_index, EN_HEAD, HydraulicSimulationStatusStage::ReadTankResults, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Head, QStringLiteral("Failed to get tank head"), tank_result.head_m);
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
        pipe_result.flow_m3_per_h *= 3.6;

        status = readLinkValue(this->project, pipe_index, EN_LINK_LEAKAGE, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Leakage, QStringLiteral("Failed to get pipe leakage flow"), pipe_result.leakage_flow_m3_per_h);
        if (!status.success)
            return status;
        pipe_result.leakage_flow_m3_per_h *= 3.6;

        status = readLinkValue(this->project, pipe_index, EN_VELOCITY, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Velocity, QStringLiteral("Failed to get pipe velocity"), pipe_result.velocity_m_per_s);
        if (!status.success)
            return status;

        status = readLinkValue(this->project, pipe_index, EN_HEADLOSS, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Headloss, QStringLiteral("Failed to get pipe head loss"), pipe_result.head_loss_m);
        if (!status.success)
            return status;

        double link_status = 0.0;
        status = readLinkValue(this->project, pipe_index, EN_STATUS, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Status, QStringLiteral("Failed to get pipe status"), link_status);
        if (!status.success)
            return status;
        pipe_result.open = static_cast<int>(link_status) != EN_CLOSED;

        status = readLinkValue(this->project, pipe_index, EN_SETTING, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::Setting, QStringLiteral("Failed to get pipe setting"), pipe_result.setting);
        if (!status.success)
            return status;

        double appears_in_control = 0.0;
        status = readLinkValue(this->project, pipe_index, EN_LINK_INCONTROL, HydraulicSimulationStatusStage::ReadPipeResults, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, HydraulicSimulationStatusProperty::None, QStringLiteral("Failed to read pipe control membership"), appears_in_control);
        if (!status.success)
            return status;
        pipe_result.appears_in_control = appears_in_control != 0.0;

        result.links_pipes.append(pipe_result);
    }

    return makeEpanetSuccess();
}
