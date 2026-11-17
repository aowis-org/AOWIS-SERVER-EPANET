#ifndef DUMMY_NETWORKS_H
#define DUMMY_NETWORKS_H

#include <QDateTime>
#include <QUuid>

#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

class DummyNetworks
{
public:
    static NetworkHydraulicDistribution networkSimple();
    static NetworkHydraulicDistribution networkOnMap();
    static NetworkHydraulicDistribution networkTanks();
    static NetworkHydraulicDistribution networkFull();
    
    static NetworkHydraulicDistribution networkSimpleTimeline();
    static NetworkHydraulicDistribution networkTanksTimeline();
    static NetworkHydraulicDistribution networkFullTimeline();
};

#endif // DUMMY_NETWORKS_H
