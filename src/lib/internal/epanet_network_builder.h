#ifndef AOWIS_EPANET_NETWORK_BUILDER_H
#define AOWIS_EPANET_NETWORK_BUILDER_H

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetNetworkBuilder
{
public:
    EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices);

    EpanetStatus build(const NetworkHydraulic &request);

private:
    EpanetStatus addCurveTankVolume(const EpanetCurveTankVolume &curve);
    EpanetStatus addNodeReservoir(const EpanetNodeReservoir &reservoir);
    EpanetStatus addNodeJunction(const EpanetNodeJunction &junction);
    EpanetStatus addNodeTank(const EpanetNodeTank &tank);
    EpanetStatus addLinkPipe(const EpanetLinkPipe &pipe);

    EpanetProject &project;
    EpanetIndexRegistry &indices;
};

#endif
