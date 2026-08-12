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

    if (!request.controls_simple.isEmpty())
        details.append(QStringLiteral("Simple controls: %1").arg(request.controls_simple.size()));
    if (!request.controls_rules.isEmpty())
        details.append(QStringLiteral("Rule-based controls: %1").arg(request.controls_rules.size()));

    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        if (pump.control_type != HydraulicLinkPumpControlType::None)
            details.append(QStringLiteral("Pump control requires the unsupported control builder: %1").arg(pump.id));
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


HydraulicSimulationDiagnostic diagnosticFromBuildStatus(const HydraulicSimulationStatus &status)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Error;
    diagnostic.stage = status.stage;
    diagnostic.operation = status.operation;
    diagnostic.property = status.property;
    diagnostic.entity = status.entity;
    diagnostic.message = status.message;
    diagnostic.details = status.details;
    diagnostic.backend_name = status.backend_name;
    diagnostic.backend_error_code = status.backend_error_code;
    diagnostic.backend_operation = status.backend_operation;
    diagnostic.message_backend = status.message_backend;
    return diagnostic;
}

void collectBuildFailure(EpanetProject &project, const HydraulicSimulationStatus &status, HydraulicSimulationStatus &first_failure)
{
    if (status.success)
        return;

    project.appendDiagnostic(diagnosticFromBuildStatus(status));
    if (first_failure.success)
        first_failure = status;
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

bool resolveValveType(HydraulicLinkValveType type, int &backend_type)
{
    switch (type)
    {
    case HydraulicLinkValveType::PRV:
        backend_type = EN_PRV;
        return true;
    case HydraulicLinkValveType::PSV:
        backend_type = EN_PSV;
        return true;
    case HydraulicLinkValveType::FCV:
        backend_type = EN_FCV;
        return true;
    case HydraulicLinkValveType::PBV:
        backend_type = EN_PBV;
        return true;
    case HydraulicLinkValveType::TCV:
        backend_type = EN_TCV;
        return true;
    case HydraulicLinkValveType::GPV:
        backend_type = EN_GPV;
        return true;
    case HydraulicLinkValveType::PCV:
        backend_type = EN_PCV;
        return true;
    }

    return false;
}

HydraulicSimulationStatus setLinkVertices(EpanetProject &project, int link_index, const QList<HydraulicLinkVertex> &vertices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &entity_name)
{
    if (vertices.isEmpty())
        return makeEpanetSuccess();

    QList<double> x_coordinates;
    QList<double> y_coordinates;
    x_coordinates.reserve(vertices.size());
    y_coordinates.reserve(vertices.size());

    for (const HydraulicLinkVertex &vertex : vertices)
    {
        x_coordinates.append(vertex.coordinate_wgs84.longitude_deg);
        y_coordinates.append(vertex.coordinate_wgs84.latitude_deg);
    }

    const int error = EN_setvertices(project.handle(), link_index, x_coordinates.data(), y_coordinates.data(), static_cast<int>(vertices.size()));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setvertices"), entity_type, entity_id, entity_uuid, QStringLiteral("Failed to set %1 vertices").arg(entity_name));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus addCurveData(EpanetProject &project, const QString &curve_id_string, const QUuid &curve_uuid, const QList<double> &x_values, const QList<double> &y_values, int backend_curve_type, QHash<QUuid, int> &indices, const QString &curve_name)
{
    const QByteArray curve_id = curve_id_string.toUtf8();
    int error = EN_addcurve(project.handle(), curve_id.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_addcurve"), HydraulicSimulationStatusEntityType::Curve, curve_id_string, curve_uuid, QStringLiteral("Failed to add %1").arg(curve_name));
        if (!epanet_status.success)
            return epanet_status;
    }

    int curve_index = 0;
    error = EN_getcurveindex(project.handle(), curve_id.constData(), &curve_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcurveindex"), HydraulicSimulationStatusEntityType::Curve, curve_id_string, curve_uuid, QStringLiteral("Failed to get %1 index").arg(curve_name));
        if (!epanet_status.success)
            return epanet_status;
    }

    QList<double> writable_x_values = x_values;
    QList<double> writable_y_values = y_values;
    error = EN_setcurve(project.handle(), curve_index, writable_x_values.data(), writable_y_values.data(), static_cast<int>(writable_x_values.size()));
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_setcurve"), HydraulicSimulationStatusEntityType::Curve, curve_id_string, curve_uuid, QStringLiteral("Failed to set %1 data").arg(curve_name));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    error = EN_setcurvetype(project.handle(), curve_index, backend_curve_type);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcurvetype"), HydraulicSimulationStatusEntityType::Curve, curve_id_string, curve_uuid, QStringLiteral("Failed to set %1 type").arg(curve_name));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    indices.insert(curve_uuid, curve_index);
    return makeEpanetSuccess();
}

template<typename Entity>
HydraulicSimulationStatus rebuildNodeIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getnodeindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getnodeindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET node reindexing").arg(entity_name));
            if (!epanet_status.success)
            {
                collectBuildFailure(project, epanet_status, first_failure);
                continue;
            }
        }

        indices.insert(entity.uuid, entity_index);
    }

    return first_failure;
}

template<typename Entity>
HydraulicSimulationStatus rebuildLinkIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getlinkindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getlinkindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET link reindexing").arg(entity_name));
            if (!epanet_status.success)
            {
                collectBuildFailure(project, epanet_status, first_failure);
                continue;
            }
        }

        indices.insert(entity.uuid, entity_index);
    }

    return first_failure;
}
}

EpanetNetworkBuilder::EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices)
    : project(project), indices(indices)
{
}

HydraulicSimulationStatus EpanetNetworkBuilder::build(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = validateSupportedFeatures(request);
    collectBuildFailure(this->project, status, first_failure);

    this->node_ids_by_uuid.clear();
    this->pattern_ids_by_uuid.clear();
    this->tank_volume_curve_ids_by_uuid.clear();
    this->pump_head_curve_ids_by_uuid.clear();
    this->pump_head_curve_point_counts_by_uuid.clear();
    this->pump_efficiency_curve_ids_by_uuid.clear();
    this->valve_headloss_curve_ids_by_uuid.clear();
    this->valve_characteristic_curve_ids_by_uuid.clear();
    this->pipe_ids_by_uuid.clear();
    this->pump_ids_by_uuid.clear();
    this->valve_ids_by_uuid.clear();

    this->indices.patterns_time.clear();
    this->indices.curves_tank_volume.clear();
    this->indices.curves_pump_head.clear();
    this->indices.curves_pump_efficiency.clear();
    this->indices.curves_valve_headloss.clear();
    this->indices.curves_valve_characteristic.clear();
    this->indices.nodes_reservoirs.clear();
    this->indices.nodes_junctions.clear();
    this->indices.nodes_tanks.clear();
    this->indices.links_pipes.clear();
    this->indices.links_pumps.clear();
    this->indices.links_valves.clear();

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = registerBackendId(this->node_ids_by_uuid, junction.uuid, junction.id, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = registerBackendId(this->node_ids_by_uuid, reservoir.uuid, reservoir.id, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = registerBackendId(this->node_ids_by_uuid, tank.uuid, tank.id, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = registerBackendId(this->pattern_ids_by_uuid, pattern.uuid, pattern.id, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = registerBackendId(this->tank_volume_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpHead &curve : request.curves_pump_head)
    {
        status = registerBackendId(this->pump_head_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump head curve"));
        collectBuildFailure(this->project, status, first_failure);
        if (status.success)
            this->pump_head_curve_point_counts_by_uuid.insert(curve.uuid, static_cast<int>(curve.points.size()));
    }

    for (const HydraulicCurvePumpEfficiency &curve : request.curves_pump_efficiency)
    {
        status = registerBackendId(this->pump_efficiency_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump efficiency curve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveHeadloss &curve : request.curves_valve_headloss)
    {
        status = registerBackendId(this->valve_headloss_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve head-loss curve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveCharacteristic &curve : request.curves_valve_characteristic)
    {
        status = registerBackendId(this->valve_characteristic_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve characteristic curve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = registerBackendId(this->pipe_ids_by_uuid, pipe.uuid, pipe.id, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        status = registerBackendId(this->pump_ids_by_uuid, pump.uuid, pump.id, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, QStringLiteral("Pump"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        status = registerBackendId(this->valve_ids_by_uuid, valve.uuid, valve.id, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, QStringLiteral("Valve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = addPatternTime(pattern);
        collectBuildFailure(this->project, status, first_failure);
    }

    status = configureDefaultDemandPattern(request);
    collectBuildFailure(this->project, status, first_failure);

    status = configureGlobalEnergyPattern(request);
    collectBuildFailure(this->project, status, first_failure);

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = addCurveTankVolume(curve);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpHead &curve : request.curves_pump_head)
    {
        status = addCurvePumpHead(curve);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpEfficiency &curve : request.curves_pump_efficiency)
    {
        status = addCurvePumpEfficiency(curve);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveHeadloss &curve : request.curves_valve_headloss)
    {
        status = addCurveValveHeadloss(curve);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveCharacteristic &curve : request.curves_valve_characteristic)
    {
        status = addCurveValveCharacteristic(curve);
        collectBuildFailure(this->project, status, first_failure);
    }

    // EPANET keeps all junctions before tanks and reservoirs. Adding a junction after
    // a reservoir or tank can therefore change previously returned node indices.
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = addNodeJunction(junction);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = addNodeReservoir(reservoir);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = addNodeTank(tank);
        collectBuildFailure(this->project, status, first_failure);
    }

    status = refreshNodeIndices(request);
    if (!status.success && first_failure.success)
        first_failure = status;

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = addLinkPipe(pipe, request.options_hydraulic.headloss_formula);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        status = addLinkPump(pump);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        status = addLinkValve(valve);
        collectBuildFailure(this->project, status, first_failure);
    }

    status = refreshLinkIndices(request);
    if (!status.success && first_failure.success)
        first_failure = status;

    return first_failure;
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
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_addpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to add time pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int pattern_index = 0;
    error = EN_getpatternindex(this->project.handle(), pattern_id.constData(), &pattern_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to get time pattern index"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QList<double> factors = pattern.factors;
    error = EN_setpattern(this->project.handle(), pattern_index, factors.data(), factors.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to set time pattern factors"));
        if (!status.success)
        {
            status.entity.index = pattern_index;
            return status;
        }
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
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_DEMANDPATTERN)"), HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Failed to configure the default demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureGlobalEnergyPattern(const NetworkHydraulic &request)
{
    const QUuid pattern_uuid = request.options_energy.global_energy_price_pattern_uuid;
    if (pattern_uuid.isNull())
        return makeEpanetSuccess();

    int pattern_index = 0;
    if (!resolveBackendIndex(this->indices.patterns_time, pattern_uuid, pattern_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Could not resolve global energy-price pattern UUID"));

    const int error = EN_setoption(this->project.handle(), EN_GLOBALPATTERN, static_cast<double>(pattern_index));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_GLOBALPATTERN)"), HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Failed to configure the global energy-price pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

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
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_addcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to add tank volume curve"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int curve_index = 0;
    error = EN_getcurveindex(this->project.handle(), curve_id.constData(), &curve_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcurveindex"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to get tank volume curve index"));
        if (!epanet_status.success)
            return epanet_status;
    }

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
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_setcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to set tank volume curve data"));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    this->indices.curves_tank_volume.insert(curve.uuid, curve_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurvePumpHead(const HydraulicCurvePumpHead &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve requires at least one point"));

    QList<double> flows;
    QList<double> heads;
    flows.reserve(curve.points.size());
    heads.reserve(curve.points.size());
    if (curve.points.size() == 3 && curve.points.first().flow_m3_per_h != 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Three-point pump curve must start at zero flow"));
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurvePumpHeadPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.head_gain_m <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve requires non-negative flows and positive heads"));
        if (curve.points.size() == 1 && point.flow_m3_per_h <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("One-point pump curve requires positive design flow"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve flows must increase"));
        if (index > 0 && point.head_gain_m >= curve.points.at(index - 1).head_gain_m)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump curve heads must decrease"));
        flows.append(point.flow_m3_per_h);
        heads.append(point.head_gain_m);
    }

    return addCurveData(this->project, curve.id, curve.uuid, flows, heads, EN_PUMP_CURVE, this->indices.curves_pump_head, QStringLiteral("pump head curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurvePumpEfficiency(const HydraulicCurvePumpEfficiency &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve requires at least one point"));

    QList<double> flows;
    QList<double> efficiencies;
    flows.reserve(curve.points.size());
    efficiencies.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurvePumpEfficiencyPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.efficiency_percent <= 0.0 || point.efficiency_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve requires non-negative flows and efficiencies in (0, 100] percent"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve flows must increase"));
        flows.append(point.flow_m3_per_h);
        efficiencies.append(point.efficiency_percent);
    }

    return addCurveData(this->project, curve.id, curve.uuid, flows, efficiencies, EN_EFFIC_CURVE, this->indices.curves_pump_efficiency, QStringLiteral("pump efficiency curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveValveHeadloss(const HydraulicCurveValveHeadloss &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve requires at least one point"));

    QList<double> flows;
    QList<double> head_losses;
    flows.reserve(curve.points.size());
    head_losses.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveValveHeadlossPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.head_loss_m < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve values cannot be negative"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve flows must increase"));
        flows.append(point.flow_m3_per_h);
        head_losses.append(point.head_loss_m);
    }

    return addCurveData(this->project, curve.id, curve.uuid, flows, head_losses, EN_HLOSS_CURVE, this->indices.curves_valve_headloss, QStringLiteral("valve head-loss curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveValveCharacteristic(const HydraulicCurveValveCharacteristic &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve requires at least one point"));

    QList<double> positions;
    QList<double> relative_flows;
    positions.reserve(curve.points.size());
    relative_flows.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveValveCharacteristicPoint &point = curve.points.at(index);
        if (point.position_percent < 0.0 || point.position_percent > 100.0 || point.relative_flow_percent < 0.0 || point.relative_flow_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve values must be in [0, 100] percent"));
        if (index > 0 && point.position_percent <= curve.points.at(index - 1).position_percent)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve positions must increase"));
        positions.append(point.position_percent);
        relative_flows.append(point.relative_flow_percent);
    }

    return addCurveData(this->project, curve.id, curve.uuid, positions, relative_flows, EN_VALVE_CURVE, this->indices.curves_valve_characteristic, QStringLiteral("valve characteristic curve"));
}

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

    double reservoir_head_m = reservoir.head_m;
    if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        reservoir_head_m = reservoir.terrain_elevation_m + reservoir.head_offset_m;

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
        if (!resolveBackendIndex(this->indices.patterns_time, reservoir.head_pattern_uuid, pattern_index))
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
        if (!resolveBackendId(this->pattern_ids_by_uuid, first_demand.pattern_uuid, first_pattern_id))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve primary demand pattern UUID"));

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
            if (!resolveBackendId(this->pattern_ids_by_uuid, demand.pattern_uuid, pattern_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Could not resolve demand pattern UUID"));

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
        junction.emitter_coefficient_m3_per_h_per_m_exponent);
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
        if (!resolveBackendId(this->tank_volume_curve_ids_by_uuid, tank.volume_curve_uuid, volume_curve_id))
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
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to add pipe"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = setLinkVertices(this->project, pipe_index, pipe.vertices, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("pipe"));
    if (!status.success)
        return status;

    const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
    double roughness_for_selected_formula = 0.0;
    if (!resolvePipeRoughness(pipe, headloss_formula, roughness_for_selected_formula))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Unsupported hydraulic headloss formula"));

    error = EN_setpipedata(this->project.handle(), pipe_index, length_m, pipe.diameter_mm, roughness_for_selected_formula, pipe.minor_loss);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_setpipedata"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (pipe.initial_status != HydraulicLinkPipeInitialStatus::CheckValve)
    {
        const double initial_status = pipe.initial_status == HydraulicLinkPipeInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_INITSTATUS, initial_status);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe status"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_pipes.insert(pipe.uuid, pipe_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkPump(const HydraulicLinkPump &pump)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(pump.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump start-node UUID"));

    const QString node_id_to_string = this->node_ids_by_uuid.value(pump.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump end-node UUID"));

    const QByteArray pump_id = pump.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    int pump_index = 0;
    int error = EN_addlink(this->project.handle(), pump_id.constData(), EN_PUMP, node_id_from.constData(), node_id_to.constData(), &pump_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to add pump"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = setLinkVertices(this->project, pump_index, pump.vertices, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("pump"));
    if (!status.success)
        return status;

    if (pump.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower)
    {
        if (pump.constant_power_kw <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Constant-power pump requires positive power"));

        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_POWER, pump.constant_power_kw);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_POWER)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump constant power"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else
    {
        if (pump.head_curve_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Curve-based pump has no head curve UUID"));

        const int point_count = this->pump_head_curve_point_counts_by_uuid.value(pump.head_curve_uuid, 0);
        if (point_count == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump head curve UUID"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::OnePointCurve && point_count != 1)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("One-point pump definition requires exactly one head-curve point"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::ThreePointCurve && point_count != 3)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Three-point pump definition requires exactly three head-curve points"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::MultiPointCurve && (point_count == 1 || point_count == 3))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Multi-point pump definition requires two or at least four head-curve points"));

        const int head_curve_index = this->indices.curves_pump_head.value(pump.head_curve_uuid, 0);
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_HCURVE, static_cast<double>(head_curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_HCURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump head curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.initial_speed < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump initial speed cannot be negative"));
    error = EN_setlinkvalue(this->project.handle(), pump_index, EN_INITSETTING, pump.initial_speed);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSETTING)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump initial speed"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const double initial_status = pump.initial_status == HydraulicLinkPumpInitialStatus::On ? EN_OPEN : EN_CLOSED;
    error = EN_setlinkvalue(this->project.handle(), pump_index, EN_INITSTATUS, initial_status);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSTATUS)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump initial status"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!pump.speed_pattern_uuid.isNull())
    {
        const int speed_pattern_index = this->indices.patterns_time.value(pump.speed_pattern_uuid, 0);
        if (speed_pattern_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump speed pattern UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_LINKPATTERN, static_cast<double>(speed_pattern_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LINKPATTERN)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump speed pattern"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Constant)
    {
        if (pump.constant_efficiency_percent <= 0.0 || pump.constant_efficiency_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump constant efficiency must be in (0, 100] percent"));

        const QString curve_id = QStringLiteral("__aowis_eff_") + pump.uuid.toString(QUuid::Id128).left(19);
        QList<double> flows = {0.0};
        QList<double> efficiencies = {pump.constant_efficiency_percent};
        QHash<QUuid, int> synthetic_indices;
        status = addCurveData(this->project, curve_id, pump.uuid, flows, efficiencies, EN_EFFIC_CURVE, synthetic_indices, QStringLiteral("constant pump efficiency curve"));
        if (!status.success)
            return status;
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECURVE, static_cast<double>(synthetic_indices.value(pump.uuid)));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump constant efficiency"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve)
    {
        const int efficiency_curve_index = this->indices.curves_pump_efficiency.value(pump.efficiency_curve_uuid, 0);
        if (efficiency_curve_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump efficiency curve UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECURVE, static_cast<double>(efficiency_curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump efficiency curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
    {
        if (pump.energy_price_per_kw_h < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump energy price cannot be negative"));
        if (pump.energy_price_per_kw_h == 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("EPANET cannot represent a pump-specific zero energy price; zero selects the global price"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECOST, pump.energy_price_per_kw_h);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECOST)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump energy price"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
    {
        const int price_pattern_index = this->indices.patterns_time.value(pump.price_pattern_uuid, 0);
        if (price_pattern_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump energy-price pattern UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_EPAT, static_cast<double>(price_pattern_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_EPAT)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump energy-price pattern"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_pumps.insert(pump.uuid, pump_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkValve(const HydraulicLinkValve &valve)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(valve.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve valve start-node UUID"));
    const QString node_id_to_string = this->node_ids_by_uuid.value(valve.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve valve end-node UUID"));

    int backend_type = 0;
    if (!resolveValveType(valve.type, backend_type))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::AddLink, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Unsupported valve type"));

    const QByteArray valve_id = valve.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    int valve_index = 0;
    int error = EN_addlink(this->project.handle(), valve_id.constData(), backend_type, node_id_from.constData(), node_id_to.constData(), &valve_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to add valve"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = setLinkVertices(this->project, valve_index, valve.vertices, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("valve"));
    if (!status.success)
        return status;

    error = EN_setlinkvalue(this->project.handle(), valve_index, EN_DIAMETER, valve.diameter_mm);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setlinkvalue(EN_DIAMETER)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve diameter"));
        if (!epanet_status.success)
            return epanet_status;
    }
    error = EN_setlinkvalue(this->project.handle(), valve_index, EN_MINORLOSS, valve.minor_loss);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_MINORLOSS)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve minor-loss coefficient"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (valve.type == HydraulicLinkValveType::GPV)
    {
        const int curve_index = this->indices.curves_valve_headloss.value(valve.setting_curve_uuid, 0);
        if (curve_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve GPV head-loss curve UUID"));
        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_GPV_CURVE, static_cast<double>(curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_GPV_CURVE)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set GPV head-loss curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else
    {
        if (valve.type == HydraulicLinkValveType::PCV && (valve.setting < 0.0 || valve.setting > 100.0))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV position must be in [0, 100] percent"));
        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_INITSETTING, valve.setting);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSETTING)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve setting"));
            if (!epanet_status.success)
                return epanet_status;
        }

        if (valve.type == HydraulicLinkValveType::PCV && !valve.setting_curve_uuid.isNull())
        {
            const int curve_index = this->indices.curves_valve_characteristic.value(valve.setting_curve_uuid, 0);
            if (curve_index == 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve PCV characteristic curve UUID"));
            error = EN_setlinkvalue(this->project.handle(), valve_index, EN_PCV_CURVE, static_cast<double>(curve_index));
            if (error != 0)
            {
                const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PCV_CURVE)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set PCV characteristic curve"));
                if (!epanet_status.success)
                    return epanet_status;
            }
        }
    }

    if (valve.initial_status != HydraulicLinkValveInitialStatus::Active)
    {
        const double initial_status = valve.initial_status == HydraulicLinkValveInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_INITSTATUS, initial_status);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSTATUS)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve initial status"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_valves.insert(valve.uuid, valve_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshNodeIndices(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = rebuildNodeIndices(this->project, request.nodes_junctions, this->indices.nodes_junctions, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("junction"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildNodeIndices(this->project, request.nodes_reservoirs, this->indices.nodes_reservoirs, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("reservoir"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildNodeIndices(this->project, request.nodes_tanks, this->indices.nodes_tanks, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("tank"));
    if (!status.success && first_failure.success)
        first_failure = status;

    return first_failure;
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshLinkIndices(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = rebuildLinkIndices(this->project, request.links_pipes, this->indices.links_pipes, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("pipe"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildLinkIndices(this->project, request.links_pumps, this->indices.links_pumps, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, QStringLiteral("pump"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildLinkIndices(this->project, request.links_valves, this->indices.links_valves, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, QStringLiteral("valve"));
    if (!status.success && first_failure.success)
        first_failure = status;

    return first_failure;
}
