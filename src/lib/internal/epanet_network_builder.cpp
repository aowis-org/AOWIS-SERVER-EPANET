#include "epanet_network_builder.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>
#include <QStringList>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

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
    if (ids_by_uuid.values().contains(id))
        return makeEpanetStatus(stage, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, id, uuid, QStringLiteral("%1 ID is duplicated").arg(entity_name));

    ids_by_uuid.insert(uuid, id);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateSupportedFeatures(const NetworkHydraulic &request)
{
    QStringList details;

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        if (!std::isfinite(pipe.leak_area_mm2_per_100m) || pipe.leak_area_mm2_per_100m < 0.0)
            details.append(QStringLiteral("Pipe %1 has an invalid fixed leakage area").arg(pipe.id));
        if (!std::isfinite(pipe.leak_area_expansion_per_pressure_head_mm2_per_m) || pipe.leak_area_expansion_per_pressure_head_mm2_per_m < 0.0)
            details.append(QStringLiteral("Pipe %1 has an invalid leakage expansion coefficient").arg(pipe.id));
    }

    if (details.isEmpty())
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = makeEpanetStatus(HydraulicSimulationStatusStage::BuildNetwork, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Network contains invalid hydraulic leakage configuration"));
    status.details = details;
    return status;
}

int controlLinkSettingValueCount(const HydraulicControlLinkSetting &setting)
{
    int count = 0;
    count += setting.pump_speed_ratio.has_value() ? 1 : 0;
    count += setting.valve_pressure_head_m.has_value() ? 1 : 0;
    count += setting.valve_flow_m3_per_h.has_value() ? 1 : 0;
    count += setting.valve_loss_coefficient.has_value() ? 1 : 0;
    count += setting.valve_position_percent.has_value() ? 1 : 0;
    return count;
}

int rulePremiseNumericValueCount(const HydraulicControlRulePremise &premise)
{
    int count = 0;
    count += premise.demand_m3_per_h.has_value() ? 1 : 0;
    count += premise.hydraulic_head_m.has_value() ? 1 : 0;
    count += premise.water_level_m.has_value() ? 1 : 0;
    count += premise.pressure_head_m.has_value() ? 1 : 0;
    count += premise.flow_m3_per_h.has_value() ? 1 : 0;
    count += controlLinkSettingValueCount(premise.link_setting);
    count += premise.power_kw.has_value() ? 1 : 0;
    count += premise.elapsed_time_s.has_value() ? 1 : 0;
    count += premise.time_of_day_s.has_value() ? 1 : 0;
    count += premise.fill_time_s.has_value() ? 1 : 0;
    count += premise.drain_time_s.has_value() ? 1 : 0;
    return count;
}

bool resolveSimpleControlType(HydraulicControlSimpleType type, int &backend_type)
{
    switch (type)
    {
    case HydraulicControlSimpleType::LowLevel:
        backend_type = EN_LOWLEVEL;
        return true;
    case HydraulicControlSimpleType::HighLevel:
        backend_type = EN_HILEVEL;
        return true;
    case HydraulicControlSimpleType::Timer:
        backend_type = EN_TIMER;
        return true;
    case HydraulicControlSimpleType::TimeOfDay:
        backend_type = EN_TIMEOFDAY;
        return true;
    }

    return false;
}

QString ruleLogicalOperatorText(HydraulicControlRuleLogicalOperator logical_operator)
{
    switch (logical_operator)
    {
    case HydraulicControlRuleLogicalOperator::If:
        return QStringLiteral("IF");
    case HydraulicControlRuleLogicalOperator::And:
        return QStringLiteral("AND");
    case HydraulicControlRuleLogicalOperator::Or:
        return QStringLiteral("OR");
    }

    return QString();
}

QString ruleVariableText(HydraulicControlRuleVariable variable)
{
    switch (variable)
    {
    case HydraulicControlRuleVariable::Demand:
        return QStringLiteral("DEMAND");
    case HydraulicControlRuleVariable::Head:
        return QStringLiteral("HEAD");
    case HydraulicControlRuleVariable::Grade:
        return QStringLiteral("GRADE");
    case HydraulicControlRuleVariable::Level:
        return QStringLiteral("LEVEL");
    case HydraulicControlRuleVariable::Pressure:
        return QStringLiteral("PRESSURE");
    case HydraulicControlRuleVariable::Flow:
        return QStringLiteral("FLOW");
    case HydraulicControlRuleVariable::Status:
        return QStringLiteral("STATUS");
    case HydraulicControlRuleVariable::Setting:
        return QStringLiteral("SETTING");
    case HydraulicControlRuleVariable::Power:
        return QStringLiteral("POWER");
    case HydraulicControlRuleVariable::Time:
        return QStringLiteral("TIME");
    case HydraulicControlRuleVariable::ClockTime:
        return QStringLiteral("CLOCKTIME");
    case HydraulicControlRuleVariable::FillTime:
        return QStringLiteral("FILLTIME");
    case HydraulicControlRuleVariable::DrainTime:
        return QStringLiteral("DRAINTIME");
    }

    return QString();
}

QString ruleComparisonText(HydraulicControlRuleOperator comparison)
{
    switch (comparison)
    {
    case HydraulicControlRuleOperator::Equal:
        return QStringLiteral("=");
    case HydraulicControlRuleOperator::NotEqual:
        return QStringLiteral("<>");
    case HydraulicControlRuleOperator::LessOrEqual:
        return QStringLiteral("<=");
    case HydraulicControlRuleOperator::GreaterOrEqual:
        return QStringLiteral(">=");
    case HydraulicControlRuleOperator::Less:
        return QStringLiteral("<");
    case HydraulicControlRuleOperator::Greater:
        return QStringLiteral(">");
    case HydraulicControlRuleOperator::Is:
        return QStringLiteral("IS");
    case HydraulicControlRuleOperator::IsNot:
        return QStringLiteral("NOT");
    case HydraulicControlRuleOperator::Below:
        return QStringLiteral("BELOW");
    case HydraulicControlRuleOperator::Above:
        return QStringLiteral("ABOVE");
    }

    return QString();
}

QString ruleStatusText(HydraulicControlRuleStatus status)
{
    switch (status)
    {
    case HydraulicControlRuleStatus::Open:
        return QStringLiteral("OPEN");
    case HydraulicControlRuleStatus::Closed:
        return QStringLiteral("CLOSED");
    case HydraulicControlRuleStatus::Active:
        return QStringLiteral("ACTIVE");
    }

    return QString();
}

bool isTimeRuleVariable(HydraulicControlRuleVariable variable)
{
    return variable == HydraulicControlRuleVariable::Time
        || variable == HydraulicControlRuleVariable::ClockTime
        || variable == HydraulicControlRuleVariable::FillTime
        || variable == HydraulicControlRuleVariable::DrainTime;
}


void collectBuildFailure(EpanetProject &project, const HydraulicSimulationStatus &status, HydraulicSimulationStatus &first_failure)
{
    if (status.success)
        return;

    project.appendDiagnostic(epanetDiagnosticFromStatus(status, HydraulicSimulationDiagnosticSeverity::Error));
    if (first_failure.success)
        first_failure = status;
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

HydraulicSimulationStatus setObjectComment(EpanetProject &project, int object_type, int object_index, const QString &comment, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &entity_name)
{
    if (comment.isEmpty())
        return makeEpanetSuccess();

    const QByteArray comment_utf8 = comment.toUtf8();
    const int error = EN_setcomment(project.handle(), object_type, object_index, comment_utf8.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcomment"), entity_type, entity_id, entity_uuid, QStringLiteral("Failed to set %1 comment").arg(entity_name));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus setNodeOrLinkMetadata(EpanetProject &project, int object_type, int object_index, const HydraulicEntityMetadata &metadata, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &entity_name)
{
    HydraulicSimulationStatus status = setObjectComment(project, object_type, object_index, metadata.comment, stage, entity_type, entity_id, entity_uuid, entity_name);
    if (!status.success)
        return status;

    if (metadata.tag.isEmpty())
        return makeEpanetSuccess();

    const QByteArray tag_utf8 = metadata.tag.toUtf8();
    const int error = EN_settag(project.handle(), object_type, object_index, tag_utf8.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_settag"), entity_type, entity_id, entity_uuid, QStringLiteral("Failed to set %1 tag").arg(entity_name));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus addCurveData(EpanetProject &project, const QString &curve_id_string, const QUuid &curve_uuid, const QString &comment, const QList<double> &x_values, const QList<double> &y_values, int backend_curve_type, QHash<QUuid, int> &indices, const QString &curve_name)
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

    HydraulicSimulationStatus status = setObjectComment(project, EN_CURVE, curve_index, comment, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, curve_id_string, curve_uuid, curve_name);
    if (!status.success)
        return status;

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
    this->generic_curve_ids_by_uuid.clear();
    this->pipe_ids_by_uuid.clear();
    this->pump_ids_by_uuid.clear();
    this->valve_ids_by_uuid.clear();
    this->valve_types_by_uuid.clear();
    this->control_simple_ids_by_uuid.clear();
    this->control_rule_ids_by_uuid.clear();
    this->constant_demand_pattern_id.clear();

    this->indices.patterns_time.clear();
    this->indices.curves_tank_volume.clear();
    this->indices.curves_pump_head.clear();
    this->indices.curves_pump_efficiency.clear();
    this->indices.curves_valve_headloss.clear();
    this->indices.curves_valve_characteristic.clear();
    this->indices.curves_generic.clear();
    this->indices.nodes_reservoirs.clear();
    this->indices.nodes_junctions.clear();
    this->indices.nodes_tanks.clear();
    this->indices.links_pipes.clear();
    this->indices.links_pumps.clear();
    this->indices.links_valves.clear();
    this->indices.controls_simple.clear();
    this->indices.controls_rules.clear();

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

    for (const HydraulicCurveGeneric &curve : request.curves_generic)
    {
        status = registerBackendId(this->generic_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Generic curve"));
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
        if (status.success)
            this->valve_types_by_uuid.insert(valve.uuid, valve.type);
    }

    for (const HydraulicControlSimple &control : request.controls_simple)
    {
        status = registerBackendId(this->control_simple_ids_by_uuid, control.uuid, control.id, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusEntityType::Control, QStringLiteral("Simple control"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlRule &rule : request.controls_rules)
    {
        status = registerBackendId(this->control_rule_ids_by_uuid, rule.uuid, rule.id, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusEntityType::Rule, QStringLiteral("Control rule"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = addPatternTime(pattern);
        collectBuildFailure(this->project, status, first_failure);
    }

    status = configureConstantDemandPattern(request);
    collectBuildFailure(this->project, status, first_failure);

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

    for (const HydraulicCurveGeneric &curve : request.curves_generic)
    {
        status = addCurveGeneric(curve);
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
        status = addLinkPipe(pipe);
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

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        if (!this->indices.nodes_junctions.contains(junction.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_junctions.value(junction.uuid), junction.metadata, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("junction"));
        collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        if (!this->indices.nodes_reservoirs.contains(reservoir.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_reservoirs.value(reservoir.uuid), reservoir.metadata, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("reservoir"));
        collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        if (!this->indices.nodes_tanks.contains(tank.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_tanks.value(tank.uuid), tank.metadata, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("tank"));
        collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        if (!this->indices.links_pipes.contains(pipe.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_pipes.value(pipe.uuid), pipe.metadata, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("pipe"));
        collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        if (!this->indices.links_pumps.contains(pump.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_pumps.value(pump.uuid), pump.metadata, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("pump"));
        collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        if (!this->indices.links_valves.contains(valve.uuid))
            continue;
        status = setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_valves.value(valve.uuid), valve.metadata, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("valve"));
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlSimple &control : request.controls_simple)
    {
        status = addControlSimple(control);
        collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlRule &rule : request.controls_rules)
    {
        status = addControlRule(rule);
        collectBuildFailure(this->project, status, first_failure);
    }

    return first_failure;
}

HydraulicSimulationStatus EpanetNetworkBuilder::addPatternTime(const HydraulicPatternTime &pattern)
{
    if (pattern.id.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Pattern, QString(), pattern.uuid, QStringLiteral("Time pattern has no ID"));

    if (pattern.multipliers.isEmpty())
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

    QList<double> multipliers = pattern.multipliers;
    error = EN_setpattern(this->project.handle(), pattern_index, multipliers.data(), multipliers.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to set time pattern multipliers"));
        if (!status.success)
        {
            status.entity.index = pattern_index;
            return status;
        }
    }

    HydraulicSimulationStatus status = setObjectComment(this->project, EN_TIMEPAT, pattern_index, pattern.comment, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("time pattern"));
    if (!status.success)
        return status;

    this->indices.patterns_time.insert(pattern.uuid, pattern_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureConstantDemandPattern(const NetworkHydraulic &request)
{
    bool needs_constant_pattern = false;
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
        {
            if (demand.pattern_mode == HydraulicTimePatternMode::Constant)
            {
                needs_constant_pattern = true;
                break;
            }
        }
        if (needs_constant_pattern)
            break;
    }

    if (!needs_constant_pattern)
        return makeEpanetSuccess();

    QString pattern_id = QStringLiteral("__AOWIS_CONSTANT");
    int suffix = 1;
    bool id_in_use = true;
    while (id_in_use)
    {
        id_in_use = false;
        for (const HydraulicPatternTime &pattern : request.patterns_time)
        {
            if (pattern.id == pattern_id)
            {
                id_in_use = true;
                pattern_id = QStringLiteral("__AOWIS_CONSTANT_%1").arg(suffix++);
                break;
            }
        }
    }

    const QByteArray pattern_id_utf8 = pattern_id.toUtf8();
    int error = EN_addpattern(this->project.handle(), pattern_id_utf8.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_addpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to add internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int pattern_index = 0;
    error = EN_getpatternindex(this->project.handle(), pattern_id_utf8.constData(), &pattern_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to resolve internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setpatternvalue(this->project.handle(), pattern_index, 1, 1.0);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpatternvalue"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to set internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    this->constant_demand_pattern_id = pattern_id;
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

    error = EN_setcurvetype(this->project.handle(), curve_index, EN_VOLUME_CURVE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcurvetype"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to set tank volume curve type"));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    HydraulicSimulationStatus status = setObjectComment(this->project, EN_CURVE, curve_index, curve.comment, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("tank volume curve"));
    if (!status.success)
        return status;

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

    return addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, heads, EN_PUMP_CURVE, this->indices.curves_pump_head, QStringLiteral("pump head curve"));
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

    return addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, efficiencies, EN_EFFIC_CURVE, this->indices.curves_pump_efficiency, QStringLiteral("pump efficiency curve"));
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

    return addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, head_losses, EN_HLOSS_CURVE, this->indices.curves_valve_headloss, QStringLiteral("valve head-loss curve"));
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

    return addCurveData(this->project, curve.id, curve.uuid, curve.comment, positions, relative_flows, EN_VALVE_CURVE, this->indices.curves_valve_characteristic, QStringLiteral("valve characteristic curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveGeneric(const HydraulicCurveGeneric &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Generic curve requires at least one point"));

    QList<double> x_values;
    QList<double> y_values;
    x_values.reserve(curve.points.size());
    y_values.reserve(curve.points.size());

    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveGenericPoint &point = curve.points.at(index);
        if (index > 0 && point.x <= curve.points.at(index - 1).x)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Generic curve x values must increase"));
        x_values.append(point.x);
        y_values.append(point.y);
    }

    return addCurveData(this->project, curve.id, curve.uuid, curve.comment, x_values, y_values, EN_GENERIC_CURVE, this->indices.curves_generic, QStringLiteral("generic curve"));
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
        if (first_demand.pattern_mode == HydraulicTimePatternMode::Constant)
        {
            if (this->constant_demand_pattern_id.isEmpty())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Internal constant-demand pattern is unavailable"));
            first_pattern_id = this->constant_demand_pattern_id.toUtf8();
        }
        else if (!resolveBackendId(this->pattern_ids_by_uuid, first_demand.pattern_uuid, first_pattern_id))
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
            else if (!resolveBackendId(this->pattern_ids_by_uuid, demand.pattern_uuid, pattern_id))
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

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkPipe(const HydraulicLinkPipe &pipe)
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
    // Static network construction is intentionally independent of the requested
    // headloss formula. The actual formula-specific roughness is applied by
    // configureEpanetHydraulicRun() before hydraulics or INP export.
    constexpr double construction_roughness = 1.0;
    error = EN_setpipedata(this->project.handle(), pipe_index, length_m, pipe.diameter_mm, construction_roughness, pipe.minor_loss_coefficient);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_setpipedata"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!std::isfinite(pipe.leak_area_mm2_per_100m) || pipe.leak_area_mm2_per_100m < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe fixed leakage area must be finite and non-negative"));
    error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_LEAK_AREA, pipe.leak_area_mm2_per_100m);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LEAK_AREA)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe fixed leakage area"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!std::isfinite(pipe.leak_area_expansion_per_pressure_head_mm2_per_m) || pipe.leak_area_expansion_per_pressure_head_mm2_per_m < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe leakage expansion coefficient must be finite and non-negative"));
    error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_LEAK_EXPAN, pipe.leak_area_expansion_per_pressure_head_mm2_per_m);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LEAK_EXPAN)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe leakage expansion coefficient"));
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

    if (pump.initial_speed_ratio < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump initial speed cannot be negative"));
    error = EN_setlinkvalue(this->project.handle(), pump_index, EN_INITSETTING, pump.initial_speed_ratio);
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
        status = addCurveData(this->project, curve_id, pump.uuid, QString(), flows, efficiencies, EN_EFFIC_CURVE, synthetic_indices, QStringLiteral("constant pump efficiency curve"));
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
    error = EN_setlinkvalue(this->project.handle(), valve_index, EN_MINORLOSS, valve.minor_loss_coefficient);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_MINORLOSS)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve minor-loss coefficient"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (valve.type == HydraulicLinkValveType::GPV)
    {
        const int curve_index = this->indices.curves_valve_headloss.value(valve.head_loss_curve_uuid, 0);
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
        double initial_setting = 0.0;
        switch (valve.type)
        {
        case HydraulicLinkValveType::PRV:
        case HydraulicLinkValveType::PSV:
        case HydraulicLinkValveType::PBV:
            initial_setting = valve.setting_pressure_head_m;
            break;
        case HydraulicLinkValveType::FCV:
            initial_setting = valve.setting_flow_m3_per_h;
            break;
        case HydraulicLinkValveType::TCV:
            initial_setting = valve.setting_loss_coefficient;
            break;
        case HydraulicLinkValveType::PCV:
            initial_setting = valve.setting_position_percent;
            if (initial_setting < 0.0 || initial_setting > 100.0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV position must be in [0, 100] percent"));
            break;
        case HydraulicLinkValveType::GPV:
            break;
        }

        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_INITSETTING, initial_setting);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSETTING)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve setting"));
            if (!epanet_status.success)
                return epanet_status;
        }

        if (valve.type == HydraulicLinkValveType::PCV && !valve.characteristic_curve_uuid.isNull())
        {
            const int curve_index = this->indices.curves_valve_characteristic.value(valve.characteristic_curve_uuid, 0);
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

HydraulicSimulationStatus EpanetNetworkBuilder::addControlSimple(const HydraulicControlSimple &control)
{
    int backend_type = 0;
    if (!resolveSimpleControlType(control.type, backend_type))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Unsupported simple control type"));

    int link_index = 0;
    if (!resolveLinkIndex(control.link_uuid, link_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Could not resolve the link controlled by the simple control"));

    double setting = 0.0;
    switch (control.action)
    {
    case HydraulicControlActionType::Open:
        if (controlLinkSettingValueCount(control.setting) != 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("OPEN simple control action must not define a numeric setting"));
        setting = EN_SET_OPEN;
        break;
    case HydraulicControlActionType::Close:
        if (controlLinkSettingValueCount(control.setting) != 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("CLOSE simple control action must not define a numeric setting"));
        setting = EN_SET_CLOSED;
        break;
    case HydraulicControlActionType::Setting:
        if (!resolveControlLinkSetting(control.link_uuid, control.setting, setting) || setting < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control setting does not match the controlled pump or valve"));
        break;
    }

    int trigger_node_index = 0;
    double trigger_value = 0.0;
    if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
    {
        trigger_node_index = this->indices.nodes_junctions.value(control.trigger_node_uuid, 0);
        if (trigger_node_index != 0)
        {
            if (!std::isfinite(control.trigger_pressure_head_m))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger pressure head must be finite"));
            trigger_value = control.trigger_pressure_head_m;
        }
        else
        {
            trigger_node_index = this->indices.nodes_tanks.value(control.trigger_node_uuid, 0);
            if (trigger_node_index == 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("A level control trigger must reference a junction or tank"));
            if (!std::isfinite(control.trigger_water_level_m))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger water level must be finite"));
            trigger_value = control.trigger_water_level_m;
        }
    }
    else
    {
        const quint64 trigger_time_s = control.type == HydraulicControlSimpleType::Timer
            ? control.trigger_elapsed_time_s
            : control.trigger_time_of_day_s;
        if (trigger_time_s > static_cast<quint64>(std::numeric_limits<long>::max()))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Simple control trigger time exceeds the EPANET time range"));
        if (control.type == HydraulicControlSimpleType::TimeOfDay && trigger_time_s >= 24 * 60 * 60)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Time-of-day control trigger must be within one day"));
        trigger_value = static_cast<double>(trigger_time_s);
    }

    int control_index = 0;
    int error = EN_addcontrol(this->project.handle(), backend_type, link_index, setting, trigger_node_index, trigger_value, &control_index);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::AddControl, QStringLiteral("EN_addcontrol"), HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Failed to add simple hydraulic control"));
        if (!status.success)
        {
            status.entity.index = control_index;
            return status;
        }
    }

    error = EN_setcontrolenabled(this->project.handle(), control_index, control.enabled ? EN_TRUE : EN_FALSE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcontrolenabled"), HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("Failed to set simple hydraulic control enabled state"));
        if (!status.success)
        {
            status.entity.index = control_index;
            return status;
        }
    }

    this->indices.controls_simple.insert(control.uuid, control_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::buildControlRuleText(const HydraulicControlRule &rule, QString &rule_text) const
{
    if (rule.premises.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule requires at least one premise"));
    if (rule.actions_then.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule requires at least one THEN action"));
    if (!std::isfinite(rule.priority))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule priority must be finite"));

    QStringList lines;
    lines.append(QStringLiteral("RULE %1").arg(rule.id));

    for (int premise_index = 0; premise_index < rule.premises.size(); premise_index++)
    {
        const HydraulicControlRulePremise &premise = rule.premises.at(premise_index);
        if ((premise_index == 0 && premise.logical_operator != HydraulicControlRuleLogicalOperator::If)
            || (premise_index > 0 && premise.logical_operator == HydraulicControlRuleLogicalOperator::If))
        {
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("The first control-rule premise must use IF and later premises must use AND or OR"));
        }

        const QString logical_operator = ruleLogicalOperatorText(premise.logical_operator);
        const QString variable = ruleVariableText(premise.variable);
        const QString comparison = ruleComparisonText(premise.comparison);
        if (logical_operator.isEmpty() || variable.isEmpty() || comparison.isEmpty())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control rule contains an unsupported premise enum value"));

        QString object_clause;
        bool variable_supported = false;
        if (premise.object == HydraulicControlRuleObject::Node)
        {
            const QString node_id = this->node_ids_by_uuid.value(premise.object_uuid);
            if (node_id.isEmpty())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a node referenced by a control-rule premise"));
            object_clause = QStringLiteral("NODE %1").arg(node_id);
            variable_supported = premise.variable == HydraulicControlRuleVariable::Demand
                || premise.variable == HydraulicControlRuleVariable::Head
                || premise.variable == HydraulicControlRuleVariable::Grade
                || premise.variable == HydraulicControlRuleVariable::Level
                || premise.variable == HydraulicControlRuleVariable::Pressure
                || premise.variable == HydraulicControlRuleVariable::FillTime
                || premise.variable == HydraulicControlRuleVariable::DrainTime;
            if ((premise.variable == HydraulicControlRuleVariable::FillTime || premise.variable == HydraulicControlRuleVariable::DrainTime)
                && !this->indices.nodes_tanks.contains(premise.object_uuid))
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("FILLTIME and DRAINTIME premises require a tank"));
            }
        }
        else if (premise.object == HydraulicControlRuleObject::Link)
        {
            QString link_id;
            if (!resolveLinkId(premise.object_uuid, link_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a link referenced by a control-rule premise"));
            if (premise.variable == HydraulicControlRuleVariable::Power)
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::AddRule,
                    HydraulicSimulationStatusOperation::AddRule,
                    HydraulicSimulationStatusEntityType::Rule,
                    rule.id,
                    rule.uuid,
                    QStringLiteral("Pump POWER control-rule premises are not supported by the bundled EPANET 2.3 rule engine"));
            }
            object_clause = QStringLiteral("LINK %1").arg(link_id);
            variable_supported = premise.variable == HydraulicControlRuleVariable::Flow
                || premise.variable == HydraulicControlRuleVariable::Status
                || premise.variable == HydraulicControlRuleVariable::Setting;
        }
        else if (premise.object == HydraulicControlRuleObject::System)
        {
            if (!premise.object_uuid.isNull())
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A SYSTEM premise must not reference an entity UUID"));
            object_clause = QStringLiteral("SYSTEM");
            variable_supported = premise.variable == HydraulicControlRuleVariable::Demand
                || premise.variable == HydraulicControlRuleVariable::Time
                || premise.variable == HydraulicControlRuleVariable::ClockTime;
        }

        if (!variable_supported)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control-rule variable is not valid for its object type"));

        QString value_text;
        if (premise.variable == HydraulicControlRuleVariable::Status)
        {
            if (!premise.status.has_value() || rulePremiseNumericValueCount(premise) != 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A STATUS premise requires one status value and no numeric value"));
            if (premise.comparison != HydraulicControlRuleOperator::Equal
                && premise.comparison != HydraulicControlRuleOperator::NotEqual
                && premise.comparison != HydraulicControlRuleOperator::Is
                && premise.comparison != HydraulicControlRuleOperator::IsNot)
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A STATUS premise only supports equality or inequality comparisons"));
            }
            value_text = ruleStatusText(premise.status.value());
        }
        else
        {
            double premise_value = 0.0;
            if (premise.status.has_value() || !resolveRulePremiseValue(premise, premise_value) || !std::isfinite(premise_value))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A numeric control-rule premise requires the quantity-specific value for its selected variable"));
            if (isTimeRuleVariable(premise.variable))
            {
                if ((premise.variable == HydraulicControlRuleVariable::Time || premise.variable == HydraulicControlRuleVariable::ClockTime)
                    && premise_value > static_cast<double>(std::numeric_limits<long>::max()))
                {
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Control-rule time value exceeds the EPANET time range"));
                }
                if (premise.variable == HydraulicControlRuleVariable::ClockTime && premise_value >= 24.0 * 60.0 * 60.0)
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("CLOCKTIME premise value must be within one day"));
                value_text = QString::number(premise_value / 3600.0, 'g', 17);
            }
            else
            {
                value_text = QString::number(premise_value, 'g', 17);
            }
        }

        lines.append(QStringLiteral("%1 %2 %3 %4 %5").arg(logical_operator, object_clause, variable, comparison, value_text));
    }

    const std::function<HydraulicSimulationStatus(const QList<HydraulicControlRuleAction> &, const QString &)> append_actions = [this, &rule, &lines](const QList<HydraulicControlRuleAction> &actions, const QString &first_keyword) -> HydraulicSimulationStatus
    {
        for (int action_index = 0; action_index < actions.size(); action_index++)
        {
            const HydraulicControlRuleAction &action = actions.at(action_index);
            QString link_id;
            if (!resolveLinkId(action.link_uuid, link_id))
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Could not resolve a link referenced by a control-rule action"));
            double action_setting = 0.0;
            const int setting_value_count = controlLinkSettingValueCount(action.setting);
            const bool has_setting = resolveControlLinkSetting(action.link_uuid, action.setting, action_setting);
            if ((action.status.has_value() && setting_value_count != 0)
                || (!action.status.has_value() && (!has_setting || setting_value_count != 1)))
            {
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A control-rule action must define exactly one status or quantity-specific setting"));
            }

            QString variable;
            QString value;
            if (action.status.has_value())
            {
                if (action.status.value() == HydraulicControlRuleStatus::Active
                    && !this->indices.links_valves.contains(action.link_uuid))
                {
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("An ACTIVE rule action requires a valve"));
                }
                variable = QStringLiteral("STATUS");
                value = ruleStatusText(action.status.value());
            }
            else
            {
                if (!std::isfinite(action_setting) || action_setting < 0.0)
                    return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("A control-rule action setting must be finite and non-negative"));
                variable = QStringLiteral("SETTING");
                value = QString::number(action_setting, 'g', 17);
            }

            const QString keyword = action_index == 0 ? first_keyword : QStringLiteral("AND");
            lines.append(QStringLiteral("%1 LINK %2 %3 = %4").arg(keyword, link_id, variable, value));
        }

        return makeEpanetSuccess();
    };

    HydraulicSimulationStatus status = append_actions(rule.actions_then, QStringLiteral("THEN"));
    if (!status.success)
        return status;
    status = append_actions(rule.actions_else, QStringLiteral("ELSE"));
    if (!status.success)
        return status;

    lines.append(QStringLiteral("PRIORITY %1").arg(QString::number(rule.priority, 'g', 17)));
    rule_text = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addControlRule(const HydraulicControlRule &rule)
{
    QString rule_text;
    const HydraulicSimulationStatus build_status = buildControlRuleText(rule, rule_text);
    if (!build_status.success)
        return build_status;

    int rule_count_before = 0;
    int error = EN_getcount(this->project.handle(), EN_RULECOUNT, &rule_count_before);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcount(EN_RULECOUNT)"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to read the control-rule count before adding a rule"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QByteArray backend_rule_text = rule_text.toUtf8();
    error = EN_addrule(this->project.handle(), backend_rule_text.data());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, QStringLiteral("EN_addrule"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to add hydraulic control rule"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int rule_count_after = 0;
    error = EN_getcount(this->project.handle(), EN_RULECOUNT, &rule_count_after);
    if (error != 0 || rule_count_after != rule_count_before + 1)
    {
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcount(EN_RULECOUNT)"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to resolve the newly added control-rule index"));
            if (!epanet_status.success)
                return epanet_status;
        }
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("EPANET did not add exactly one control rule"));
    }

    const int rule_index = rule_count_after;
    char backend_rule_id[EN_MAXID + 1] = {};
    error = EN_getruleID(this->project.handle(), rule_index, backend_rule_id);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getruleID"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to read the newly added control-rule ID"));
        if (!status.success)
        {
            status.entity.index = rule_index;
            return status;
        }
    }
    if (QString::fromUtf8(backend_rule_id) != rule.id)
    {
        EN_deleterule(this->project.handle(), rule_index);
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::AddRule, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("EPANET returned a control-rule ID different from the model rule ID"));
    }

    error = EN_setruleenabled(this->project.handle(), rule_index, rule.enabled ? EN_TRUE : EN_FALSE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setruleenabled"), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("Failed to set hydraulic control-rule enabled state"));
        if (!status.success)
        {
            status.entity.index = rule_index;
            return status;
        }
    }

    this->indices.controls_rules.insert(rule.uuid, rule_index);
    return makeEpanetSuccess();
}

bool EpanetNetworkBuilder::resolveLinkId(const QUuid &uuid, QString &id) const
{
    id = this->pipe_ids_by_uuid.value(uuid);
    if (id.isEmpty())
        id = this->pump_ids_by_uuid.value(uuid);
    if (id.isEmpty())
        id = this->valve_ids_by_uuid.value(uuid);
    return !id.isEmpty();
}

bool EpanetNetworkBuilder::resolveLinkIndex(const QUuid &uuid, int &index) const
{
    index = this->indices.links_pipes.value(uuid, 0);
    if (index == 0)
        index = this->indices.links_pumps.value(uuid, 0);
    if (index == 0)
        index = this->indices.links_valves.value(uuid, 0);
    return index > 0;
}

bool EpanetNetworkBuilder::resolveControlLinkSetting(const QUuid &link_uuid, const HydraulicControlLinkSetting &setting, double &backend_setting) const
{
    if (controlLinkSettingValueCount(setting) != 1)
        return false;

    if (this->pump_ids_by_uuid.contains(link_uuid))
    {
        if (!setting.pump_speed_ratio.has_value())
            return false;
        backend_setting = setting.pump_speed_ratio.value();
        return std::isfinite(backend_setting);
    }

    if (!this->valve_types_by_uuid.contains(link_uuid))
        return false;

    switch (this->valve_types_by_uuid.value(link_uuid))
    {
    case HydraulicLinkValveType::PRV:
    case HydraulicLinkValveType::PSV:
    case HydraulicLinkValveType::PBV:
        if (!setting.valve_pressure_head_m.has_value())
            return false;
        backend_setting = setting.valve_pressure_head_m.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::FCV:
        if (!setting.valve_flow_m3_per_h.has_value())
            return false;
        backend_setting = setting.valve_flow_m3_per_h.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::TCV:
        if (!setting.valve_loss_coefficient.has_value())
            return false;
        backend_setting = setting.valve_loss_coefficient.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::PCV:
        if (!setting.valve_position_percent.has_value())
            return false;
        backend_setting = setting.valve_position_percent.value();
        return std::isfinite(backend_setting);
    case HydraulicLinkValveType::GPV:
        return false;
    }

    return false;
}

bool EpanetNetworkBuilder::resolveRulePremiseValue(const HydraulicControlRulePremise &premise, double &value) const
{
    if (rulePremiseNumericValueCount(premise) != 1)
        return false;

    switch (premise.variable)
    {
    case HydraulicControlRuleVariable::Demand:
        if (!premise.demand_m3_per_h.has_value())
            return false;
        value = premise.demand_m3_per_h.value();
        return true;
    case HydraulicControlRuleVariable::Head:
    case HydraulicControlRuleVariable::Grade:
        if (!premise.hydraulic_head_m.has_value())
            return false;
        value = premise.hydraulic_head_m.value();
        return true;
    case HydraulicControlRuleVariable::Level:
        if (!premise.water_level_m.has_value())
            return false;
        value = premise.water_level_m.value();
        return true;
    case HydraulicControlRuleVariable::Pressure:
        if (!premise.pressure_head_m.has_value())
            return false;
        value = premise.pressure_head_m.value();
        return true;
    case HydraulicControlRuleVariable::Flow:
        if (!premise.flow_m3_per_h.has_value())
            return false;
        value = premise.flow_m3_per_h.value();
        return true;
    case HydraulicControlRuleVariable::Status:
        return false;
    case HydraulicControlRuleVariable::Setting:
        return resolveControlLinkSetting(premise.object_uuid, premise.link_setting, value);
    case HydraulicControlRuleVariable::Power:
        if (!premise.power_kw.has_value())
            return false;
        value = premise.power_kw.value();
        return true;
    case HydraulicControlRuleVariable::Time:
        if (!premise.elapsed_time_s.has_value())
            return false;
        value = static_cast<double>(premise.elapsed_time_s.value());
        return true;
    case HydraulicControlRuleVariable::ClockTime:
        if (!premise.time_of_day_s.has_value())
            return false;
        value = static_cast<double>(premise.time_of_day_s.value());
        return true;
    case HydraulicControlRuleVariable::FillTime:
        if (!premise.fill_time_s.has_value())
            return false;
        value = static_cast<double>(premise.fill_time_s.value());
        return true;
    case HydraulicControlRuleVariable::DrainTime:
        if (!premise.drain_time_s.has_value())
            return false;
        value = static_cast<double>(premise.drain_time_s.value());
        return true;
    }

    return false;
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
