#ifndef DUMMY_NETWORKS_H
#define DUMMY_NETWORKS_H

#include <QDateTime>
#include <QUuid>

#include <aowis/model/hydraulic/network.h>

class DummyNetworks
{
public:
    static NetworkHydraulic networkSimple();
    static NetworkHydraulic networkOnMap();
    static NetworkHydraulic networkTanks();
    static NetworkHydraulic networkFull();
    
    static NetworkHydraulic networkSimpleTimeline();
    static NetworkHydraulic networkTanksTimeline();
    static NetworkHydraulic networkFullTimeline();
};

#endif // DUMMY_NETWORKS_H
