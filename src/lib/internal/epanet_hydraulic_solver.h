#ifndef AOWIS_EPANET_HYDRAULIC_SOLVER_H
#define AOWIS_EPANET_HYDRAULIC_SOLVER_H

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <functional>

class EpanetProject;
class EpanetResultReader;

class EpanetHydraulicSolver
{
public:
    EpanetHydraulicSolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetResultReader &result_reader);

    HydraulicSimulationStatus run(
        HydraulicSimulationResultTimeline &timeline,
        const std::function<bool()> &cancellation_requested,
        bool &cancelled);

private:
    EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetResultReader &result_reader;
};

#endif // AOWIS_EPANET_HYDRAULIC_SOLVER_H
