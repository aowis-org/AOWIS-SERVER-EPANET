#include "epanet_network_builder.h"

#include "epanet_index_registry.h"
#include "epanet_network_builder_support.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeReservoir(const HydraulicNodeReservoir &reservoir)
{
    const QByteArray reservoir_id = reservoir.id.toUtf8();
    int reservoir_index = 0;
    int error = EN_addnode(this->project.handle(), reservoir_id.constData(), EN_RESERVOIR, &reservoir_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to add reservoir"));
        if (!epanet_status.success)
            return epanet_status;
    }

    double reservoir_head_m = reservoir.hydraulic_head_m;
    if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        reservoir_head_m = reservoir.terrain_elevation_m + reservoir.hydraulic_head_offset_m;

    error = EN_setnodevalue(this->project.handle(), reservoir_index, EN_ELEVATION, reservoir_head_m);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setnodevalue(EN_ELEVATION)"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir head"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (reservoir.head_pattern_mode == HydraulicTimePatternMode::TimePattern)
    {
        if (reservoir.head_pattern_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Reservoir head pattern mode is TimePattern, but no pattern UUID is set"));

        int pattern_index = 0;
        if (!EpanetNetworkBuilderSupport::resolveBackendIndex(this->indices.patterns_time, reservoir.head_pattern_uuid, pattern_index))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Could not resolve reservoir head pattern UUID"));

        error = EN_setnodevalue(this->project.handle(), reservoir_index, EN_PATTERN, static_cast<double>(pattern_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setnodevalue(EN_PATTERN)"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir head pattern"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    error = EN_setcoord(this->project.handle(), reservoir_index, reservoir.coordinate_wgs84.longitude_deg, reservoir.coordinate_wgs84.latitude_deg);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir coordinates"));
        if (!epanet_status.success)
            return epanet_status;
    }

    this->indices.nodes_reservoirs.insert(reservoir.uuid, reservoir_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeJunction(const HydraulicNodeJunction &junction)
{
    const QByteArray junction_id = junction.id.toUtf8();
    int junction_index = 0;
    int error = EN_addnode(this->project.handle(), junction_id.constData(), EN_JUNCTION, &junction_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to add junction"));
        if (!epanet_status.success)
            return epanet_status;
    }

    double elevation_m = junction.elevation_m;
    if (junction.elevation_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        elevation_m = junction.terrain_elevation_m + junction.elevation_offset_m;

    if (junction.demands.isEmpty())
    {
        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, 0.0, "");
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_setjuncdata"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set junction data"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else
    {
        const HydraulicNodeJunctionDemand &first_demand = junction.demands.first();
        QByteArray first_pattern_id;
        if (first_demand.pattern_mode == HydraulicTimePatternMode::Constant)
        {
            if (this->constant_demand_pattern_id.isEmpty())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Internal constant-demand pattern is unavailable"));
            first_pattern_id = this->constant_demand_pattern_id.toUtf8();
        }
        else if (!EpanetNetworkBuilderSupport::resolveBackendId(this->pattern_ids_by_uuid, first_demand.pattern_uuid, first_pattern_id))
        {
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve primary demand pattern UUID"));
        }

        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, first_demand.base_demand_m3_per_h, first_pattern_id.constData());
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_setjuncdata"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set primary junction demand"));
            if (!epanet_status.success)
                return epanet_status;
        }

        const QByteArray first_demand_name = first_demand.category_name.isEmpty() ? QByteArrayLiteral("Demand 1") : first_demand.category_name.toUtf8();
        error = EN_setdemandname(this->project.handle(), junction_index, 1, first_demand_name.constData());
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddDemand, QStringLiteral("EN_setdemandname"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to name primary junction demand"));
            if (!epanet_status.success)
                return epanet_status;
        }

        for (int index = 1; index < junction.demands.length(); index++)
        {
            const HydraulicNodeJunctionDemand &demand = junction.demands.at(index);
            QByteArray pattern_id;
            if (demand.pattern_mode == HydraulicTimePatternMode::Constant)
            {
                if (this->constant_demand_pattern_id.isEmpty())
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Internal constant-demand pattern is unavailable"));
                pattern_id = this->constant_demand_pattern_id.toUtf8();
            }
            else if (!EpanetNetworkBuilderSupport::resolveBackendId(this->pattern_ids_by_uuid, demand.pattern_uuid, pattern_id))
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve demand pattern UUID"));
            }

            const QByteArray demand_name = demand.category_name.isEmpty() ? QStringLiteral("Demand %1").arg(index + 1).toUtf8() : demand.category_name.toUtf8();
            error = EN_adddemand(this->project.handle(), junction_index, demand.base_demand_m3_per_h, pattern_id.constData(), demand_name.constData());
            if (error != 0)
            {
                const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddDemand, QStringLiteral("EN_adddemand"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to add junction demand category"));
                if (!epanet_status.success)
                    return epanet_status;
            }
        }
    }

    error = EN_setnodevalue(
        this->project.handle(),
        junction_index,
        EN_EMITTER,
        junction.emitter.coefficient);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setnodevalue(EN_EMITTER)"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set junction emitter coefficient"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setcoord(this->project.handle(), junction_index, junction.coordinate_wgs84.longitude_deg, junction.coordinate_wgs84.latitude_deg);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set junction coordinates"));
        if (!epanet_status.success)
            return epanet_status;
    }

    this->indices.nodes_junctions.insert(junction.uuid, junction_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeTank(const HydraulicNodeTank &tank)
{
    const QByteArray tank_id = tank.id.toUtf8();
    int tank_index = 0;
    int error = EN_addnode(this->project.handle(), tank_id.constData(), EN_TANK, &tank_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to add tank"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const double bottom_elevation_m = EpanetResolvers::resolveNodeTankBottomElevation(tank);
    const double diameter_m = EpanetResolvers::resolveNodeTankDiameter(tank);
    QByteArray volume_curve_id;

    if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeCurve)
    {
        if (tank.volume_curve_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Volume-curve tank has no volume curve UUID"));
        if (!EpanetNetworkBuilderSupport::resolveBackendId(this->tank_volume_curve_ids_by_uuid, tank.volume_curve_uuid, volume_curve_id))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Could not resolve tank volume curve UUID"));
    }

    error = EN_settankdata(this->project.handle(), tank_index, bottom_elevation_m, tank.water_level_initial_m, tank.water_level_minimum_m, tank.water_level_maximum_m, diameter_m, tank.minimum_volume_m3, volume_curve_id.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_settankdata"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setnodevalue(this->project.handle(), tank_index, EN_CANOVERFLOW, tank.can_overflow ? 1.0 : 0.0);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setnodevalue"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank overflow option"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setcoord(this->project.handle(), tank_index, tank.coordinate_wgs84.longitude_deg, tank.coordinate_wgs84.latitude_deg);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank coordinates"));
        if (!epanet_status.success)
            return epanet_status;
    }

    this->indices.nodes_tanks.insert(tank.uuid, tank_index);
    return makeEpanetSuccess();
}
