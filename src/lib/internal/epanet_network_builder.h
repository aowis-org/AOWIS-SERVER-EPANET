#ifndef AOWIS_EPANET_NETWORK_BUILDER_H
#define AOWIS_EPANET_NETWORK_BUILDER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QHash>
#include <QString>
#include <QUuid>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetNetworkBuilder
{
public:
    EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices);

    HydraulicSimulationStatus build(const NetworkHydraulic &request);

private:
    HydraulicSimulationStatus addPatternTime(const HydraulicPatternTime &pattern);
    HydraulicSimulationStatus configureConstantDemandPattern(const NetworkHydraulic &request);
    HydraulicSimulationStatus configureDefaultDemandPattern(const NetworkHydraulic &request);
    HydraulicSimulationStatus configureGlobalEnergyPattern(const NetworkHydraulic &request);
    HydraulicSimulationStatus addCurveTankVolume(const HydraulicCurveTankVolume &curve);
    HydraulicSimulationStatus addCurvePumpHead(const HydraulicCurvePumpHead &curve);
    HydraulicSimulationStatus addCurvePumpEfficiency(const HydraulicCurvePumpEfficiency &curve);
    HydraulicSimulationStatus addCurveValveHeadloss(const HydraulicCurveValveHeadloss &curve);
    HydraulicSimulationStatus addCurveValveCharacteristic(const HydraulicCurveValveCharacteristic &curve);
    HydraulicSimulationStatus addCurveGeneric(const HydraulicCurveGeneric &curve);
    HydraulicSimulationStatus addNodeReservoir(const HydraulicNodeReservoir &reservoir);
    HydraulicSimulationStatus addNodeJunction(const HydraulicNodeJunction &junction);
    HydraulicSimulationStatus addNodeTank(const HydraulicNodeTank &tank);
    HydraulicSimulationStatus addLinkPipe(const HydraulicLinkPipe &pipe);
    HydraulicSimulationStatus addLinkPump(const HydraulicLinkPump &pump);
    HydraulicSimulationStatus addLinkValve(const HydraulicLinkValve &valve);
    HydraulicSimulationStatus addControlSimple(const HydraulicControlSimple &control);
    HydraulicSimulationStatus addControlRule(const HydraulicControlRule &rule);
    HydraulicSimulationStatus buildControlRuleText(const HydraulicControlRule &rule, QString &rule_text) const;
    HydraulicSimulationStatus refreshNodeIndices(const NetworkHydraulic &request);
    HydraulicSimulationStatus refreshLinkIndices(const NetworkHydraulic &request);

    bool resolveLinkId(const QUuid &uuid, QString &id) const;
    bool resolveLinkIndex(const QUuid &uuid, int &index) const;
    bool resolveControlLinkSetting(const QUuid &link_uuid, const HydraulicControlLinkSetting &setting, double &backend_setting) const;
    bool resolveRulePremiseValue(const HydraulicControlRulePremise &premise, double &value) const;

    EpanetProject &project;
    EpanetIndexRegistry &indices;
    QHash<QUuid, QString> node_ids_by_uuid;
    QHash<QUuid, QString> pattern_ids_by_uuid;
    QHash<QUuid, QString> tank_volume_curve_ids_by_uuid;
    QHash<QUuid, QString> pump_head_curve_ids_by_uuid;
    QHash<QUuid, int> pump_head_curve_point_counts_by_uuid;
    QHash<QUuid, QString> pump_efficiency_curve_ids_by_uuid;
    QHash<QUuid, QString> valve_headloss_curve_ids_by_uuid;
    QHash<QUuid, QString> valve_characteristic_curve_ids_by_uuid;
    QHash<QUuid, QString> generic_curve_ids_by_uuid;
    QHash<QUuid, QString> pipe_ids_by_uuid;
    QHash<QUuid, QString> pump_ids_by_uuid;
    QHash<QUuid, QString> valve_ids_by_uuid;
    QHash<QUuid, HydraulicLinkValveType> valve_types_by_uuid;
    QHash<QUuid, QString> control_simple_ids_by_uuid;
    QHash<QUuid, QString> control_rule_ids_by_uuid;
    QString constant_demand_pattern_id;
};

#endif // AOWIS_EPANET_NETWORK_BUILDER_H
