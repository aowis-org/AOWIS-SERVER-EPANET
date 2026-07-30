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
    HydraulicSimulationStatus addCurveTankVolume(const HydraulicCurveTankVolume &curve);
    HydraulicSimulationStatus addNodeReservoir(const HydraulicNodeReservoir &reservoir);
    HydraulicSimulationStatus addNodeJunction(const HydraulicNodeJunction &junction);
    HydraulicSimulationStatus addNodeTank(const HydraulicNodeTank &tank);
    HydraulicSimulationStatus addLinkPipe(const HydraulicLinkPipe &pipe);

    EpanetProject &project;
    EpanetIndexRegistry &indices;
    QHash<QUuid, QString> node_ids_by_uuid;
    QHash<QUuid, QString> pattern_ids_by_uuid;
    QHash<QUuid, QString> tank_volume_curve_ids_by_uuid;
};

#endif // AOWIS_EPANET_NETWORK_BUILDER_H
