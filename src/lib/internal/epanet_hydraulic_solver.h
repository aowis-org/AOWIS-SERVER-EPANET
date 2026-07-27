#ifndef AOWIS_EPANET_HYDRAULIC_SOLVER_H
#define AOWIS_EPANET_HYDRAULIC_SOLVER_H

#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;
class EpanetResultReader;

class EpanetHydraulicSolver
{
public:
    EpanetHydraulicSolver(EpanetProject &project, const EpanetResultReader &result_reader);

    EpanetStatus run(SimulationResultTimeline &timeline);

private:
    EpanetProject &project;
    const EpanetResultReader &result_reader;
};

#endif
