#ifndef AOWIS_EPANET_QUALITY_SOLVER_H
#define AOWIS_EPANET_QUALITY_SOLVER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

#include <functional>

class EpanetProject;
class EpanetQualityResultReader;

class EpanetQualitySolver
{
public:
    EpanetQualitySolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetQualityResultReader &result_reader);

    HydraulicSimulationStatus run(
        WaterQualitySimulationResultTimeline &timeline,
        const std::function<bool()> &cancellation_requested,
        bool &cancelled);

private:
    EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetQualityResultReader &result_reader;
};

#endif // AOWIS_EPANET_QUALITY_SOLVER_H
