#include "epanet_network_builder_support.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QStringList>

#include <cmath>

namespace EpanetNetworkBuilderSupport
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
}
