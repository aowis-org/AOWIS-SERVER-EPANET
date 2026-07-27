#include "epanet_result_reader.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

EpanetResultReader::EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices)
    : project(project), network(network), indices(indices)
{
}

EpanetStatus EpanetResultReader::read(SimulationResult &result) const
{
    EpanetStatus status = readJunctions(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readTanks(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readPipes(result);
    result.status = status;
    return status;
}

EpanetStatus EpanetResultReader::readJunctions(SimulationResult &result) const
{
    for (const Junction &junction : this->network.junctions)
    {
        const int junction_index = this->indices.junctions.value(junction.id, 0);
        if (junction_index == 0)
            return makeEpanetStatus(EpanetStage::ReadJunctionResults, EpanetOperation::None, EpanetEntityType::Junction, junction.id, "Junction index is missing from the registry");

        double head_m = 0.0;
        double pressure_m = 0.0;
        int error = EN_getnodevalue(this->project.handle(), junction_index, EN_HEAD, &head_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadJunctionResults, EpanetOperation::EN_getnodevalue, EpanetEntityType::Junction, junction.id, "Failed to get junction head");
            status.property = EpanetProperty::Head;
            status.entity.index = junction_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), junction_index, EN_PRESSURE, &pressure_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadJunctionResults, EpanetOperation::EN_getnodevalue, EpanetEntityType::Junction, junction.id, "Failed to get junction pressure");
            status.property = EpanetProperty::Pressure;
            status.entity.index = junction_index;
            return status;
        }

        JunctionResult junction_result;
        junction_result.id = junction.id;
        junction_result.head_m = head_m;
        junction_result.pressure_m = pressure_m;
        result.junctions.append(junction_result);
    }

    return makeEpanetSuccess();
}

EpanetStatus EpanetResultReader::readTanks(SimulationResult &result) const
{
    for (const Tank &tank : this->network.tanks)
    {
        const int tank_index = this->indices.tanks.value(tank.id, 0);
        if (tank_index == 0)
            return makeEpanetStatus(EpanetStage::ReadTankResults, EpanetOperation::None, EpanetEntityType::Tank, tank.id, "Tank index is missing from the registry");

        double head_m = 0.0;
        double level_m = 0.0;
        double volume_m3 = 0.0;
        int error = EN_getnodevalue(this->project.handle(), tank_index, EN_HEAD, &head_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadTankResults, EpanetOperation::EN_getnodevalue, EpanetEntityType::Tank, tank.id, "Failed to get tank head");
            status.property = EpanetProperty::Head;
            status.entity.index = tank_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), tank_index, EN_TANKLEVEL, &level_m);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadTankResults, EpanetOperation::EN_getnodevalue, EpanetEntityType::Tank, tank.id, "Failed to get tank level");
            status.property = EpanetProperty::Level;
            status.entity.index = tank_index;
            return status;
        }

        error = EN_getnodevalue(this->project.handle(), tank_index, EN_TANKVOLUME, &volume_m3);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadTankResults, EpanetOperation::EN_getnodevalue, EpanetEntityType::Tank, tank.id, "Failed to get tank volume");
            status.property = EpanetProperty::Volume;
            status.entity.index = tank_index;
            return status;
        }

        TankResult tank_result;
        tank_result.id = tank.id;
        tank_result.head_m = head_m;
        tank_result.level_m = level_m;
        tank_result.volume_m3 = volume_m3;
        result.tanks.append(tank_result);
    }

    return makeEpanetSuccess();
}

EpanetStatus EpanetResultReader::readPipes(SimulationResult &result) const
{
    for (const Pipe &pipe : this->network.pipes)
    {
        const int pipe_index = this->indices.pipes.value(pipe.id, 0);
        if (pipe_index == 0)
            return makeEpanetStatus(EpanetStage::ReadPipeResults, EpanetOperation::None, EpanetEntityType::Pipe, pipe.id, "Pipe index is missing from the registry");

        double flow_lps = 0.0;
        double velocity_mps = 0.0;
        double headloss = 0.0;
        int error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_FLOW, &flow_lps);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadPipeResults, EpanetOperation::EN_getlinkvalue, EpanetEntityType::Pipe, pipe.id, "Failed to get pipe flow");
            status.property = EpanetProperty::Flow;
            status.entity.index = pipe_index;
            return status;
        }

        error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_VELOCITY, &velocity_mps);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadPipeResults, EpanetOperation::EN_getlinkvalue, EpanetEntityType::Pipe, pipe.id, "Failed to get pipe velocity");
            status.property = EpanetProperty::Velocity;
            status.entity.index = pipe_index;
            return status;
        }

        error = EN_getlinkvalue(this->project.handle(), pipe_index, EN_HEADLOSS, &headloss);
        if (error != 0)
        {
            EpanetStatus status = makeEpanetError(this->project, error, EpanetStage::ReadPipeResults, EpanetOperation::EN_getlinkvalue, EpanetEntityType::Pipe, pipe.id, "Failed to get pipe headloss");
            status.property = EpanetProperty::Headloss;
            status.entity.index = pipe_index;
            return status;
        }

        PipeResult pipe_result;
        pipe_result.id = pipe.id;
        pipe_result.flow_lps = flow_lps;
        pipe_result.velocity_mps = velocity_mps;
        pipe_result.headloss = headloss;
        result.pipes.append(pipe_result);
    }

    return makeEpanetSuccess();
}
