#ifndef AOWIS_EPANET_NETWORK_BUILDER_SUPPORT_H
#define AOWIS_EPANET_NETWORK_BUILDER_SUPPORT_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUuid>

class EpanetProject;

namespace EpanetNetworkBuilderSupport
{
bool resolveBackendId(const QHash<QUuid, QString> &ids_by_uuid, const QUuid &uuid, QByteArray &backend_id);
bool resolveBackendIndex(const QHash<QUuid, int> &indices_by_uuid, const QUuid &uuid, int &backend_index);
HydraulicSimulationStatus registerBackendId(
    QHash<QUuid, QString> &ids_by_uuid,
    const QUuid &uuid,
    const QString &id,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_name);
HydraulicSimulationStatus validateSupportedFeatures(const NetworkHydraulic &request);
int controlLinkSettingValueCount(const HydraulicControlLinkSetting &setting);
int rulePremiseNumericValueCount(const HydraulicControlRulePremise &premise);
bool resolveSimpleControlType(HydraulicControlSimpleType type, int &backend_type);
QString ruleLogicalOperatorText(HydraulicControlRuleLogicalOperator logical_operator);
QString ruleVariableText(HydraulicControlRuleVariable variable);
QString ruleComparisonText(HydraulicControlRuleOperator comparison);
QString ruleStatusText(HydraulicControlRuleStatus status);
bool isTimeRuleVariable(HydraulicControlRuleVariable variable);
void collectBuildFailure(
    EpanetProject &project,
    const HydraulicSimulationStatus &status,
    HydraulicSimulationStatus &first_failure);
bool resolveValveType(HydraulicLinkValveType type, int &backend_type);
HydraulicSimulationStatus setLinkVertices(
    EpanetProject &project,
    int link_index,
    const QList<HydraulicLinkVertex> &vertices,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &entity_name);
HydraulicSimulationStatus setObjectComment(
    EpanetProject &project,
    int object_type,
    int object_index,
    const QString &comment,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &entity_name);
HydraulicSimulationStatus setNodeOrLinkMetadata(
    EpanetProject &project,
    int object_type,
    int object_index,
    const HydraulicEntityMetadata &metadata,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &entity_name);
HydraulicSimulationStatus addCurveData(
    EpanetProject &project,
    const QString &curve_id_string,
    const QUuid &curve_uuid,
    const QString &comment,
    const QList<double> &x_values,
    const QList<double> &y_values,
    int backend_curve_type,
    QHash<QUuid, int> &indices,
    const QString &curve_name);
}

#endif // AOWIS_EPANET_NETWORK_BUILDER_SUPPORT_H
