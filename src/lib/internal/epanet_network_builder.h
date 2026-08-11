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
    HydraulicSimulationStatus configureDefaultDemandPattern(const NetworkHydraulic &request);
    HydraulicSimulationStatus configureGlobalEnergyPattern(const NetworkHydraulic &request);
    HydraulicSimulationStatus addCurveTankVolume(const HydraulicCurveTankVolume &curve);
    HydraulicSimulationStatus addCurvePumpHead(const HydraulicCurvePumpHead &curve);
    HydraulicSimulationStatus addCurvePumpEfficiency(const HydraulicCurvePumpEfficiency &curve);
    HydraulicSimulationStatus addCurveValveHeadloss(const HydraulicCurveValveHeadloss &curve);
    HydraulicSimulationStatus addCurveValveCharacteristic(const HydraulicCurveValveCharacteristic &curve);
    HydraulicSimulationStatus addNodeReservoir(const HydraulicNodeReservoir &reservoir);
    HydraulicSimulationStatus addNodeJunction(const HydraulicNodeJunction &junction);
    HydraulicSimulationStatus addNodeTank(const HydraulicNodeTank &tank);
    HydraulicSimulationStatus addLinkPipe(const HydraulicLinkPipe &pipe, HydraulicHeadlossFormula headloss_formula);
    HydraulicSimulationStatus addLinkPump(const HydraulicLinkPump &pump);
    HydraulicSimulationStatus addLinkValve(const HydraulicLinkValve &valve);
    HydraulicSimulationStatus refreshNodeIndices(const NetworkHydraulic &request);
    HydraulicSimulationStatus refreshLinkIndices(const NetworkHydraulic &request);

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
    QHash<QUuid, QString> pipe_ids_by_uuid;
    QHash<QUuid, QString> pump_ids_by_uuid;
    QHash<QUuid, QString> valve_ids_by_uuid;
};

#endif // AOWIS_EPANET_NETWORK_BUILDER_H
