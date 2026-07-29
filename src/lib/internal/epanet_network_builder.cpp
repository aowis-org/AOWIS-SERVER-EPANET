#include "epanet_network_builder.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>
#include <QByteArray>
#include <QList>

EpanetNetworkBuilder::EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices)
    : project(project), indices(indices)
{
}

EpanetStatus EpanetNetworkBuilder::build(const NetworkHydraulic &request)
{
    for (const EpanetCurveTankVolume &curve : request.curves_tank_volume)
    {
        const EpanetStatus status = addCurveTankVolume(curve);
        if (!status.success)
            return status;
    }

    for (const EpanetNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        const EpanetStatus status = addNodeReservoir(reservoir);
        if (!status.success)
            return status;
    }

    for (const EpanetNodeJunction &junction : request.nodes_junctions)
    {
        const EpanetStatus status = addNodeJunction(junction);
        if (!status.success)
            return status;
    }

    for (const EpanetNodeTank &tank : request.nodes_tanks)
    {
        const EpanetStatus status = addNodeTank(tank);
        if (!status.success)
            return status;
    }

    for (const EpanetLinkPipe &pipe : request.links_pipes)
    {
        const EpanetStatus status = addLinkPipe(pipe);
        if (!status.success)
            return status;
    }

    return makeEpanetSuccess();
}

EpanetStatus EpanetNetworkBuilder::addCurveTankVolume(const EpanetCurveTankVolume &curve)
{
    if (curve.id.isEmpty())
        return makeEpanetStatus(EpanetStatusStage::AddCurve, EpanetStatusOperation::None, EpanetStatusEntityType::Curve, QString(), "Tank volume curve has no ID");

    if (curve.points.length() < 2)
        return makeEpanetStatus(EpanetStatusStage::AddCurve, EpanetStatusOperation::None, EpanetStatusEntityType::Curve, curve.id, "Tank volume curve requires at least two points");

    const QByteArray curve_id = curve.id.toUtf8();

    int error = EN_addcurve(this->project.handle(), curve_id.constData());
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddCurve, EpanetStatusOperation::EN_addcurve, EpanetStatusEntityType::Curve, curve.id, "Failed to add tank volume curve");

    int curve_index = 0;
    error = EN_getcurveindex(this->project.handle(), curve_id.constData(), &curve_index);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddCurve, EpanetStatusOperation::EN_getcurveindex, EpanetStatusEntityType::Curve, curve.id, "Failed to get tank volume curve index");

    QList<double> levels_m;
    QList<double> volumes_m3;

    levels_m.reserve(curve.points.length());
    volumes_m3.reserve(curve.points.length());

    for (int index = 0; index < curve.points.length(); index++)
    {
        const EpanetCurveTankVolumePoint &point = curve.points.at(index);

        if (index > 0)
        {
            const EpanetCurveTankVolumePoint &previous_point = curve.points.at(index - 1);

            if (point.water_level_m <= previous_point.water_level_m)
                return makeEpanetStatus(EpanetStatusStage::AddCurve, EpanetStatusOperation::None, EpanetStatusEntityType::Curve, curve.id, "Tank volume curve levels must increase");

            if (point.volume_m3 <= previous_point.volume_m3)
                return makeEpanetStatus(EpanetStatusStage::AddCurve, EpanetStatusOperation::None, EpanetStatusEntityType::Curve, curve.id, "Tank volume curve volumes must increase");
        }

        levels_m.append(point.water_level_m);
        volumes_m3.append(point.volume_m3);
    }

    error = EN_setcurve(this->project.handle(), curve_index, levels_m.data(), volumes_m3.data(), levels_m.length());
    if (error != 0)
    {
        EpanetStatus status = makeEpanetError(this->project, error, EpanetStatusStage::AddCurve, EpanetStatusOperation::EN_setcurve, EpanetStatusEntityType::Curve, curve.id, "Failed to set tank volume curve data");
        status.entity.index = curve_index;
        return status;
    }

    this->indices.curves_tank_volume.insert(curve.id, curve_index);

    return makeEpanetSuccess();
}

EpanetStatus EpanetNetworkBuilder::addNodeReservoir(const EpanetNodeReservoir &reservoir)
{
    const QByteArray reservoir_id = reservoir.id.toUtf8();
    int reservoir_index = 0;
    int error = EN_addnode(this->project.handle(), reservoir_id.constData(), EN_RESERVOIR, &reservoir_index);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddReservoir, EpanetStatusOperation::EN_addnode, EpanetStatusEntityType::Reservoir, reservoir.id, "Failed to add reservoir");

    error = EN_setnodevalue(this->project.handle(), reservoir_index, EN_ELEVATION, reservoir.head_m);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddReservoir, EpanetStatusOperation::EN_setnodevalue, EpanetStatusEntityType::Reservoir, reservoir.id, "Failed to set reservoir head");

    this->indices.nodes_reservoirs.insert(reservoir.id, reservoir_index);
    return makeEpanetSuccess();
}

EpanetStatus EpanetNetworkBuilder::addNodeJunction(const EpanetNodeJunction &junction)
{
    const QByteArray junction_id = junction.id.toUtf8();
    int junction_index = 0;
    int error = EN_addnode(this->project.handle(), junction_id.constData(), EN_JUNCTION, &junction_index);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::EN_addnode, EpanetStatusEntityType::Junction, junction.id, "Failed to add junction");

    double elevation_m = junction.elevation_m;
    if (junction.elevation_input_type == EpanetNodeElevationInputType::TerrainElevationAndOffset)
        elevation_m = junction.terrain_elevation_m + junction.elevation_offset_m;

    if (junction.demands.isEmpty())
    {
        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, 0.0, "");
        if (error != 0)
            return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::EN_setjuncdata, EpanetStatusEntityType::Junction, junction.id, "Failed to set junction data");
    }
    else
    {
        const EpanetNodeJunctionDemand &first_demand = junction.demands.first();
        const QByteArray first_pattern_id = first_demand.pattern_id.toUtf8();
        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, first_demand.base_demand_m3_per_h / 3.6, first_pattern_id.constData());
        if (error != 0)
            return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::EN_setjuncdata, EpanetStatusEntityType::Junction, junction.id, "Failed to set primary junction demand");

        const QByteArray first_demand_name = QByteArrayLiteral("Demand 1");
        error = EN_setdemandname(this->project.handle(), junction_index, 1, first_demand_name.constData());
        if (error != 0)
            return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::None, EpanetStatusEntityType::Junction, junction.id, "Failed to name primary junction demand");

        for (int index = 1; index < junction.demands.length(); index++)
        {
            const EpanetNodeJunctionDemand &demand = junction.demands.at(index);
            const QByteArray pattern_id = demand.pattern_id.toUtf8();
            const QByteArray demand_name = QStringLiteral("Demand %1").arg(index + 1).toUtf8();
            error = EN_adddemand(this->project.handle(), junction_index, demand.base_demand_m3_per_h / 3.6, pattern_id.constData(), demand_name.constData());
            if (error != 0)
                return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::None, EpanetStatusEntityType::Junction, junction.id, "Failed to add junction demand category");
        }
    }

    error = EN_setcoord(this->project.handle(), junction_index, junction.longitude_deg, junction.latitude_deg);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddJunction, EpanetStatusOperation::None, EpanetStatusEntityType::Junction, junction.id, "Failed to set junction coordinates");

    this->indices.nodes_junctions.insert(junction.id, junction_index);
    return makeEpanetSuccess();
}

EpanetStatus EpanetNetworkBuilder::addNodeTank(const EpanetNodeTank &tank)
{
    const QByteArray tank_id = tank.id.toUtf8();
    int tank_index = 0;
    int error = EN_addnode(this->project.handle(), tank_id.constData(), EN_TANK, &tank_index);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddTank, EpanetStatusOperation::EN_addnode, EpanetStatusEntityType::Tank, tank.id, "Failed to add tank");

    const double bottom_elevation_m = EpanetResolvers::resolveNodeTankBottomElevation(tank);
    const double diameter_m = EpanetResolvers::resolveNodeTankDiameter(tank);
    QByteArray volume_curve_id;

    if (tank.geometry_input_type == EpanetNodeTankGeometryInputType::VolumeCurve)
    {
        if (tank.volume_curve_id.isEmpty())
            return makeEpanetStatus(EpanetStatusStage::AddTank, EpanetStatusOperation::None, EpanetStatusEntityType::Tank, tank.id, "Volume-curve tank has no volume curve ID");
        volume_curve_id = tank.volume_curve_id.toUtf8();
    }

    error = EN_settankdata(this->project.handle(), tank_index, bottom_elevation_m, tank.water_level_initial_m, tank.water_level_minimum_m, tank.water_level_maximum_m, diameter_m, tank.minimum_volume_m3, volume_curve_id.constData());
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddTank, EpanetStatusOperation::EN_settankdata, EpanetStatusEntityType::Tank, tank.id, "Failed to set tank data");

    error = EN_setnodevalue(this->project.handle(), tank_index, EN_CANOVERFLOW, tank.can_overflow ? 1.0 : 0.0);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddTank, EpanetStatusOperation::EN_setnodevalue, EpanetStatusEntityType::Tank, tank.id, "Failed to set tank overflow option");

    this->indices.nodes_tanks.insert(tank.id, tank_index);
    return makeEpanetSuccess();
}

EpanetStatus EpanetNetworkBuilder::addLinkPipe(const EpanetLinkPipe &pipe)
{
    const QByteArray pipe_id = pipe.id.toUtf8();
    const QByteArray node_id_from = pipe.node_id_from.toUtf8();
    const QByteArray node_id_to = pipe.node_id_to.toUtf8();
    const int pipe_type = pipe.initial_status == EpanetLinkPipeInitialStatus::CheckValve ? EN_CVPIPE : EN_PIPE;
    int pipe_index = 0;

    int error = EN_addlink(this->project.handle(), pipe_id.constData(), pipe_type, node_id_from.constData(), node_id_to.constData(), &pipe_index);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddPipe, EpanetStatusOperation::EN_addlink, EpanetStatusEntityType::Pipe, pipe.id, "Failed to add pipe");

    const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
    error = EN_setpipedata(this->project.handle(), pipe_index, length_m, pipe.diameter_mm, pipe.roughness_hw, pipe.minor_loss);
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::AddPipe, EpanetStatusOperation::EN_setpipedata, EpanetStatusEntityType::Pipe, pipe.id, "Failed to set pipe data");

    if (pipe.initial_status != EpanetLinkPipeInitialStatus::CheckValve)
    {
        const double initial_status = pipe.initial_status == EpanetLinkPipeInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_INITSTATUS, initial_status);
        if (error != 0)
            return makeEpanetError(this->project, error, EpanetStatusStage::AddPipe, EpanetStatusOperation::EN_setlinkvalue, EpanetStatusEntityType::Pipe, pipe.id, "Failed to set pipe status");
    }

    this->indices.links_pipes.insert(pipe.id, pipe_index);
    return makeEpanetSuccess();
}
