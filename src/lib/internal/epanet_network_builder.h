#ifndef AOWIS_EPANET_NETWORK_BUILDER_H
#define AOWIS_EPANET_NETWORK_BUILDER_H

#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetNetworkBuilder
{
public:
    EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices);

    EpanetStatus build(const NetworkHydraulic &request);

private:
    EpanetStatus addTankVolumeCurve(const TankVolumeCurve &curve);
    EpanetStatus addReservoir(const Reservoir &reservoir);
    EpanetStatus addJunction(const Junction &junction);
    EpanetStatus addTank(const Tank &tank);
    EpanetStatus addPipe(const Pipe &pipe);

    EpanetProject &project;
    EpanetIndexRegistry &indices;
};

#endif
