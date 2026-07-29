#ifndef AOWIS_EPANET_RESOLVERS_H
#define AOWIS_EPANET_RESOLVERS_H

#include <aowis/model/hydraulic/network_hydraulic.h>

class EpanetResolvers
{
public:
    static double resolveNodeTankBottomElevation(const HydraulicNodeTank &tank);
    static double resolveNodeTankDiameter(const HydraulicNodeTank &tank);
};

#endif
