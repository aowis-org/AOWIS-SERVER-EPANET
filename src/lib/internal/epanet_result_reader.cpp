#include "epanet_result_reader.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

EpanetResultReader::EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices)
    : project(project), network(network), indices(indices)
{
}

EpanetStatus EpanetResultReader::read(EpanetResult &result) const
{
    EpanetStatus status = readNodesJunctions(result);
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
    result.status = status;
    return status;
}

EpanetStatus EpanetResultReader::readNodesJunctions(EpanetResult &result) const
{
    for (const EpanetNodeJunction &junction : this->network.nodes_junctions)
    {
        const int junction_index = this->indices.nodes_junctions.value(junction.id, 0);
        if (junction_index == 0)
            return makeEpanetStatus(EpanetStatusStage::ReadJunctionResults, EpanetStatusOperation::None, EpanetStatusEntityType::Junction, junction.id, "Junction index is missing from the registry");

        double head_m = 0.0;
        double pressure_head_m = 0.0;
        int error = EN_getnodevalue(this->project.handle(), junction_index, EN_HEAD, &head_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadJunctionResults, EpanetStatusOperation::EN_getnodevalue, EpanetStatusEntityType::Junction, junction.id, "Failed to get junction head");
            status.property = EpanetStatusProperty::Head;
            status.entity.index = junction_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), junction_index, EN_PRESSURE, &pressure_head_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadJunctionResults, EpanetStatusOperation::EN_getnodevalue, EpanetStatusEntityType::Junction, junction.id, "Failed to get junction pressure");
            status.property = EpanetStatusProperty::Pressure;
            status.entity.index = junction_index;
            return status;
        }

        EpanetResultNodeJunction junction_result;
        junction_result.id = junction.id;
        junction_result.head_m = head_m;
        junction_result.pressure_head_m = pressure_head_m;
        result.nodes_junctions.append(junction_result);
    }

    return makeEpanetSuccess();
}

EpanetStatus EpanetResultReader::readNodesTanks(EpanetResult &result) const
{
    for (const EpanetNodeTank &tank : this->network.nodes_tanks)
    {
        const int tank_index = this->indices.nodes_tanks.value(tank.id, 0);
        if (tank_index == 0)
            return makeEpanetStatus(EpanetStatusStage::ReadTankResults, EpanetStatusOperation::None, EpanetStatusEntityType::Tank, tank.id, "Tank index is missing from the registry");

        double head_m = 0.0;
        double water_level_m = 0.0;
        double volume_m3 = 0.0;
        int error = EN_getnodevalue(this->project.handle(), tank_index, EN_HEAD, &head_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadTankResults, EpanetStatusOperation::EN_getnodevalue, EpanetStatusEntityType::Tank, tank.id, "Failed to get tank head");
            status.property = EpanetStatusProperty::Head;
            status.entity.index = tank_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), tank_index, EN_TANKLEVEL, &water_level_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadTankResults, EpanetStatusOperation::EN_getnodevalue, EpanetStatusEntityType::Tank, tank.id, "Failed to get tank level");
            status.property = EpanetStatusProperty::Level;
            status.entity.index = tank_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), tank_index, EN_TANKVOLUME, &volume_m3);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadTankResults, EpanetStatusOperation::EN_getnodevalue, EpanetStatusEntityType::Tank, tank.id, "Failed to get tank volume");
            status.property = EpanetStatusProperty::Volume;
            status.entity.index = tank_index;
            return status;
        }

        EpanetResultNodeTank tank_result;
        tank_result.id = tank.id;
        tank_result.head_m = head_m;
        tank_result.water_level_m = water_level_m;
        tank_result.volume_m3 = volume_m3;
        result.nodes_tanks.append(tank_result);
    }

    return makeEpanetSuccess();
}

EpanetStatus EpanetResultReader::readLinksPipes(EpanetResult &result) const
{
    for (const EpanetLinkPipe &pipe : this->network.links_pipes)
    {
        const int pipe_index = this->indices.links_pipes.value(pipe.id, 0);
        if (pipe_index == 0)
            return makeEpanetStatus(EpanetStatusStage::ReadPipeResults, EpanetStatusOperation::None, EpanetStatusEntityType::Pipe, pipe.id, "Pipe index is missing from the registry");

        double flow_epanet_lps = 0.0;
        double velocity_m_per_s = 0.0;
        double headloss = 0.0;
        int error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_FLOW, &flow_epanet_lps);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadPipeResults, EpanetStatusOperation::EN_getlinkvalue, EpanetStatusEntityType::Pipe, pipe.id, "Failed to get pipe flow");
            status.property = EpanetStatusProperty::Flow;
            status.entity.index = pipe_index;
            return status;
        }

        error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_VELOCITY, &velocity_m_per_s);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadPipeResults, EpanetStatusOperation::EN_getlinkvalue, EpanetStatusEntityType::Pipe, pipe.id, "Failed to get pipe velocity");
            status.property = EpanetStatusProperty::Velocity;
            status.entity.index = pipe_index;
            return status;
        }

        error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_HEADLOSS, &headloss);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::ReadPipeResults, EpanetStatusOperation::EN_getlinkvalue, EpanetStatusEntityType::Pipe, pipe.id, "Failed to get pipe headloss");
            status.property = EpanetStatusProperty::Headloss;
            status.entity.index = pipe_index;
            return status;
        }

        EpanetResultLinkPipe pipe_result;
        pipe_result.id = pipe.id;
        pipe_result.flow_m3_per_h = flow_epanet_lps * 3.6;
        pipe_result.velocity_m_per_s = velocity_m_per_s;
        pipe_result.headloss = headloss;
        result.links_pipes.append(pipe_result);
    }

    return makeEpanetSuccess();
}
