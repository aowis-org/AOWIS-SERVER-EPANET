#ifndef DUMMY_NETWORKS_H
#define DUMMY_NETWORKS_H

#include <aowis/model/hydraulic/simulation_request.h>

class DummyNetworks
{
public:
    static SimulationRequest networkSimple();
    static SimulationRequest networkTanks();
    
    static SimulationRequest networkSimpleTimeline();
    static SimulationRequest networkTanksTimeline();
};

#endif // DUMMY_NETWORKS_H
