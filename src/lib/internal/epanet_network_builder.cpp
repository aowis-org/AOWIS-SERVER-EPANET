#include "epanet_network_builder.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace
{
bool resolveBackendId(const QHash<QUuid, QString> &ids_by_uuid, const QUuid &uuid, QByteArray &backend_id)
{
    if (uuid.isNull())
    {
        backend_id.clear();
        return true;
    }

    const QString id = ids_by_uuid.value(uuid);
    if (id.isEmpty())
        return false;

    backend_id = id.toUtf8();
    return true;
}

bool resolveBackendIndex(const QHash<QUuid, int> &indices_by_uuid, const QUuid &uuid, int &backend_index)
{
    if (uuid.isNull())
    {
        backend_index = 0;
        return true;
    }

    backend_index = indices_by_uuid.value(uuid, 0);
    return backend_index > 0;
}

HydraulicSimulationStatus registerBackendId(QHash<QUuid, QString> &ids_by_uuid, const QUuid &uuid, const QString &id, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    if (uuid.isNull())
        return makeEpanetStatus(stage, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, id, uuid, QStringLiteral("%1 has no UUID").arg(entity_name));
    if (id.isEmpty())
        return makeEpanetStatus(stage, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, id, uuid, QStringLiteral("%1 has no ID").arg(entity_name));
    if (ids_by_uuid.contains(uuid))
        return makeEpanetStatus(stage, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, id, uuid, QStringLiteral("%1 UUID is duplicated").arg(entity_name));

    ids_by_uuid.insert(uuid, id);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateSupportedFeatures(const NetworkHydraulic &request)
{
    QStringList details;

    if (!request.curves_pump_head.isEmpty())
        details.append(QStringLiteral("Pump head curves: %1").arg(request.curves_pump_head.size()));
    if (!request.curves_pump_efficiency.isEmpty())
        details.append(QStringLiteral("Pump efficiency curves: %1").arg(request.curves_pump_efficiency.size()));
    if (!request.curves_valve_headloss.isEmpty())
        details.append(QStringLiteral("Valve headloss curves: %1").arg(request.curves_valve_headloss.size()));
    if (!request.curves_valve_characteristic.isEmpty())
        details.append(QStringLiteral("Valve characteristic curves: %1").arg(request.curves_valve_characteristic.size()));
    if (!request.curves_generic.isEmpty())
        details.append(QStringLiteral("Generic curves: %1").arg(request.curves_generic.size()));
    if (!request.links_pumps.isEmpty())
        details.append(QStringLiteral("Pumps: %1").arg(request.links_pumps.size()));
    if (!request.links_valves.isEmpty())
        details.append(QStringLiteral("Valves: %1").arg(request.links_valves.size()));
    if (!request.controls_simple.isEmpty())
        details.append(QStringLiteral("Simple controls: %1").arg(request.controls_simple.size()));
    if (!request.controls_rules.isEmpty())
        details.append(QStringLiteral("Rule-based controls: %1").arg(request.controls_rules.size()));

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        if (junction.emitter_coefficient_m3_per_h_per_m_exponent != 0.0)
            details.append(QStringLiteral("Junction emitter coefficient is unsupported: %1").arg(junction.id));
    }

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        if (pipe.leak_area_mm2_per_100m != 0.0 || pipe.leak_expansion_mm2_per_m_head != 0.0)
            details.append(QStringLiteral("Pipe leakage parameters are unsupported: %1").arg(pipe.id));
    }

    if (details.isEmpty())
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = makeEpanetStatus(HydraulicSimulationStatusStage::BuildNetwork, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Network contains physical features that are not implemented by the EPANET adapter"));
    status.details = details;
    return status;
}

bool resolvePipeRoughness(const HydraulicLinkPipe &pipe, HydraulicHeadlossFormula headloss_formula, double &roughness)
{
    switch (headloss_formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        roughness = pipe.roughness_hw;
        return true;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        roughness = pipe.roughness_dw_mm;
        return true;
    case HydraulicHeadlossFormula::ChezyManning:
        roughness = pipe.roughness_cm;
        return true;
    }

    return false;
}

template<typename Entity>
HydraulicSimulationStatus rebuildNodeIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getnodeindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
            return makeEpanetError(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getnodeindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET node reindexing").arg(entity_name));

        indices.insert(entity.uuid, entity_index);
    }

    return makeEpanetSuccess();
}

template<typename Entity>
HydraulicSimulationStatus rebuildLinkIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getlinkindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
            return makeEpanetError(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getlinkindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET link reindexing").arg(entity_name));

        indices.insert(entity.uuid, entity_index);
    }

    return makeEpanetSuccess();
}
}

EpanetNetworkBuilder::EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices)
    : project(project), indices(indices)
{
}

HydraulicSimulationStatus EpanetNetworkBuilder::build(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus status = validateSupportedFeatures(request);
    if (!status.success)
        return status;

    this->node_ids_by_uuid.clear();
    this->pattern_ids_by_uuid.clear();
    this->tank_volume_curve_ids_by_uuid.clear();
    this->pipe_ids_by_uuid.clear();

    this->indices.patterns_time.clear();
    this->indices.curves_tank_volume.clear();
    this->indices.nodes_reservoirs.clear();
    this->indices.nodes_junctions.clear();
    this->indices.nodes_tanks.clear();
    this->indices.links_pipes.clear();

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = registerBackendId(this->node_ids_by_uuid, junction.uuid, junction.id, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"));
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = registerBackendId(this->node_ids_by_uuid, reservoir.uuid, reservoir.id, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"));
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = registerBackendId(this->node_ids_by_uuid, tank.uuid, tank.id, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"));
        if (!status.success)
            return status;
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = registerBackendId(this->pattern_ids_by_uuid, pattern.uuid, pattern.id, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"));
        if (!status.success)
            return status;
    }

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = registerBackendId(this->tank_volume_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"));
        if (!status.success)
            return status;
    }

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = registerBackendId(this->pipe_ids_by_uuid, pipe.uuid, pipe.id, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"));
        if (!status.success)
            return status;
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = addPatternTime(pattern);
        if (!status.success)
            return status;
    }

    status = configureDefaultDemandPattern(request);
    if (!status.success)
        return status;

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = addCurveTankVolume(curve);
        if (!status.success)
            return status;
    }

    // EPANET keeps all junctions before tanks and reservoirs. Adding a junction after
    // a reservoir or tank can therefore change previously returned node indices.
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = addNodeJunction(junction);
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = addNodeReservoir(reservoir);
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = addNodeTank(tank);
        if (!status.success)
            return status;
    }

    status = refreshNodeIndices(request);
    if (!status.success)
        return status;

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = addLinkPipe(pipe, request.options_hydraulic.headloss_formula);
        if (!status.success)
            return status;
    }

    status = refreshLinkIndices(request);
    if (!status.success)
        return status;

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addPatternTime(const HydraulicPatternTime &pattern)
{
    if (pattern.id.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Pattern, QString(), pattern.uuid, QStringLiteral("Time pattern has no ID"));

    if (pattern.factors.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Time pattern requires at least one factor"));

    const QByteArray pattern_id = pattern.id.toUtf8();
    int error = EN_addpattern(this->project.handle(), pattern_id.constData());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_addpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to add time pattern"));

    int pattern_index = 0;
    error = EN_getpatternindex(this->project.handle(), pattern_id.constData(), &pattern_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to get time pattern index"));

    QList<double> factors = pattern.factors;
    error = EN_setpattern(this->project.handle(), pattern_index, factors.data(), factors.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to set time pattern factors"));
        status.entity.index = pattern_index;
        return status;
    }

    this->indices.patterns_time.insert(pattern.uuid, pattern_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureDefaultDemandPattern(const NetworkHydraulic &request)
{
    const QUuid pattern_uuid = request.options_hydraulic.default_demand_pattern_uuid;
    if (pattern_uuid.isNull())
        return makeEpanetSuccess();

    int pattern_index = 0;
    if (!resolveBackendIndex(this->indices.patterns_time, pattern_uuid, pattern_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Could not resolve default demand pattern UUID"));

    const int error = EN_setoption(this->project.handle(), EN_DEMANDPATTERN, static_cast<double>(pattern_index));
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_DEMANDPATTERN)"), HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Failed to configure the default demand pattern"));

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveTankVolume(const HydraulicCurveTankVolume &curve)
{
    if (curve.id.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, QString(), curve.uuid, QStringLiteral("Tank volume curve has no ID"));

    if (curve.points.length() < 2)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve requires at least two points"));

    const QByteArray curve_id = curve.id.toUtf8();
    int error = EN_addcurve(this->project.handle(), curve_id.constData());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_addcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to add tank volume curve"));

    int curve_index = 0;
    error = EN_getcurveindex(this->project.handle(), curve_id.constData(), &curve_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcurveindex"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to get tank volume curve index"));

    QList<double> levels_m;
    QList<double> volumes_m3;
    levels_m.reserve(curve.points.length());
    volumes_m3.reserve(curve.points.length());

    for (int index = 0; index < curve.points.length(); index++)
    {
        const HydraulicCurveTankVolumePoint &point = curve.points.at(index);
        if (index > 0)
        {
            const HydraulicCurveTankVolumePoint &previous_point = curve.points.at(index - 1);
            if (point.water_level_m <= previous_point.water_level_m)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve levels must increase"));
            if (point.volume_m3 <= previous_point.volume_m3)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve volumes must increase"));
        }

        levels_m.append(point.water_level_m);
        volumes_m3.append(point.volume_m3);
    }

    error = EN_setcurve(this->project.handle(), curve_index, levels_m.data(), volumes_m3.data(), levels_m.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_setcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to set tank volume curve data"));
        status.entity.index = curve_index;
        return status;
    }

    this->indices.curves_tank_volume.insert(curve.uuid, curve_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeReservoir(const HydraulicNodeReservoir &reservoir)
{
    const QByteArray reservoir_id = reservoir.id.toUtf8();
    int reservoir_index = 0;
    int error = EN_addnode(this->project.handle(), reservoir_id.constData(), EN_RESERVOIR, &reservoir_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to add reservoir"));

    double reservoir_head_m = reservoir.head_m;
    if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        reservoir_head_m = reservoir.terrain_elevation_m + reservoir.head_offset_m;

    error = EN_setnodevalue(this->project.handle(), reservoir_index, EN_ELEVATION, reservoir_head_m);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setnodevalue(EN_ELEVATION)"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir head"));

    if (!reservoir.head_pattern_uuid.isNull())
    {
        int pattern_index = 0;
        if (!resolveBackendIndex(this->indices.patterns_time, reservoir.head_pattern_uuid, pattern_index))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Could not resolve reservoir head pattern UUID"));

        error = EN_setnodevalue(this->project.handle(), reservoir_index, EN_PATTERN, static_cast<double>(pattern_index));
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setnodevalue(EN_PATTERN)"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir head pattern"));
    }

    error = EN_setcoord(this->project.handle(), reservoir_index, reservoir.coordinate_wgs84.longitude_deg, reservoir.coordinate_wgs84.latitude_deg);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Failed to set reservoir coordinates"));

    this->indices.nodes_reservoirs.insert(reservoir.uuid, reservoir_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeJunction(const HydraulicNodeJunction &junction)
{
    const QByteArray junction_id = junction.id.toUtf8();
    int junction_index = 0;
    int error = EN_addnode(this->project.handle(), junction_id.constData(), EN_JUNCTION, &junction_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to add junction"));

    double elevation_m = junction.elevation_m;
    if (junction.elevation_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        elevation_m = junction.terrain_elevation_m + junction.elevation_offset_m;

    if (junction.demands.isEmpty())
    {
        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, 0.0, "");
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_setjuncdata"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set junction data"));
    }
    else
    {
        const HydraulicNodeJunctionDemand &first_demand = junction.demands.first();
        QByteArray first_pattern_id;
        if (!resolveBackendId(this->pattern_ids_by_uuid, first_demand.pattern_uuid, first_pattern_id))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve primary demand pattern UUID"));

        error = EN_setjuncdata(this->project.handle(), junction_index, elevation_m, first_demand.base_demand_m3_per_h, first_pattern_id.constData());
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_setjuncdata"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set primary junction demand"));

        const QByteArray first_demand_name = first_demand.category_name.isEmpty() ? QByteArrayLiteral("Demand 1") : first_demand.category_name.toUtf8();
        error = EN_setdemandname(this->project.handle(), junction_index, 1, first_demand_name.constData());
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddDemand, QStringLiteral("EN_setdemandname"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to name primary junction demand"));

        for (int index = 1; index < junction.demands.length(); index++)
        {
            const HydraulicNodeJunctionDemand &demand = junction.demands.at(index);
            QByteArray pattern_id;
            if (!resolveBackendId(this->pattern_ids_by_uuid, demand.pattern_uuid, pattern_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve demand pattern UUID"));

            const QByteArray demand_name = demand.category_name.isEmpty() ? QStringLiteral("Demand %1").arg(index + 1).toUtf8() : demand.category_name.toUtf8();
            error = EN_adddemand(this->project.handle(), junction_index, demand.base_demand_m3_per_h, pattern_id.constData(), demand_name.constData());
            if (error != 0)
                return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::AddDemand, QStringLiteral("EN_adddemand"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to add junction demand category"));
        }
    }

    error = EN_setcoord(this->project.handle(), junction_index, junction.coordinate_wgs84.longitude_deg, junction.coordinate_wgs84.latitude_deg);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Failed to set junction coordinates"));

    this->indices.nodes_junctions.insert(junction.uuid, junction_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addNodeTank(const HydraulicNodeTank &tank)
{
    const QByteArray tank_id = tank.id.toUtf8();
    int tank_index = 0;
    int error = EN_addnode(this->project.handle(), tank_id.constData(), EN_TANK, &tank_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_addnode"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to add tank"));

    const double bottom_elevation_m = EpanetResolvers::resolveNodeTankBottomElevation(tank);
    const double diameter_m = EpanetResolvers::resolveNodeTankDiameter(tank);
    QByteArray volume_curve_id;

    if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeCurve)
    {
        if (tank.volume_curve_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Volume-curve tank has no volume curve UUID"));
        if (!resolveBackendId(this->tank_volume_curve_ids_by_uuid, tank.volume_curve_uuid, volume_curve_id))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Could not resolve tank volume curve UUID"));
    }

    error = EN_settankdata(this->project.handle(), tank_index, bottom_elevation_m, tank.water_level_initial_m, tank.water_level_minimum_m, tank.water_level_maximum_m, diameter_m, tank.minimum_volume_m3, volume_curve_id.constData());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::AddNode, QStringLiteral("EN_settankdata"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank data"));

    error = EN_setnodevalue(this->project.handle(), tank_index, EN_CANOVERFLOW, tank.can_overflow ? 1.0 : 0.0);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setnodevalue"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank overflow option"));

    error = EN_setcoord(this->project.handle(), tank_index, tank.coordinate_wgs84.longitude_deg, tank.coordinate_wgs84.latitude_deg);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setcoord"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to set tank coordinates"));

    this->indices.nodes_tanks.insert(tank.uuid, tank_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkPipe(const HydraulicLinkPipe &pipe, HydraulicHeadlossFormula headloss_formula)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(pipe.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Could not resolve pipe start-node UUID"));

    const QString node_id_to_string = this->node_ids_by_uuid.value(pipe.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Could not resolve pipe end-node UUID"));

    const QByteArray pipe_id = pipe.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    const int pipe_type = pipe.initial_status == HydraulicLinkPipeInitialStatus::CheckValve ? EN_CVPIPE : EN_PIPE;
    int pipe_index = 0;

    int error = EN_addlink(this->project.handle(), pipe_id.constData(), pipe_type, node_id_from.constData(), node_id_to.constData(), &pipe_index);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to add pipe"));

    const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
    double roughness_for_selected_formula = 0.0;
    if (!resolvePipeRoughness(pipe, headloss_formula, roughness_for_selected_formula))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Unsupported hydraulic headloss formula"));

    error = EN_setpipedata(this->project.handle(), pipe_index, length_m, pipe.diameter_mm, roughness_for_selected_formula, pipe.minor_loss);
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_setpipedata"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe data"));

    if (pipe.initial_status != HydraulicLinkPipeInitialStatus::CheckValve)
    {
        const double initial_status = pipe.initial_status == HydraulicLinkPipeInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_INITSTATUS, initial_status);
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe status"));
    }

    this->indices.links_pipes.insert(pipe.uuid, pipe_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshNodeIndices(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus status = rebuildNodeIndices(this->project, request.nodes_junctions, this->indices.nodes_junctions, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("junction"));
    if (!status.success)
        return status;

    status = rebuildNodeIndices(this->project, request.nodes_reservoirs, this->indices.nodes_reservoirs, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("reservoir"));
    if (!status.success)
        return status;

    return rebuildNodeIndices(this->project, request.nodes_tanks, this->indices.nodes_tanks, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("tank"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshLinkIndices(const NetworkHydraulic &request)
{
    return rebuildLinkIndices(this->project, request.links_pipes, this->indices.links_pipes, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("pipe"));
}
