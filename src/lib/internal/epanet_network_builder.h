#ifndef AOWIS_EPANET_NETWORK_BUILDER_H
#define AOWIS_EPANET_NETWORK_BUILDER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

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
};

#endif // AOWIS_EPANET_NETWORK_BUILDER_H
