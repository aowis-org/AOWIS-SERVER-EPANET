#ifndef AOWIS_EPANET_RESOLVERS_H
#define AOWIS_EPANET_RESOLVERS_H

#include <aowis/model/hydraulic/network.h>

class EpanetResolvers
{
public:
    static double resolveTankBottomElevation(const Tank &tank);
    static double resolveTankDiameter(const Tank &tank);
};

#endif
