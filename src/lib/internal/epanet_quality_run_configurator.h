#ifndef AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H
#define AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H

#include <aowis/model/hydraulic/hydraulic_simulation_options.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct EpanetIndexRegistry;
struct NetworkHydraulicDistribution;

HydraulicSimulationStatus configureEpanetQualityRun(
    EpanetProject &project,
    const NetworkHydraulicDistribution &network,
    const EpanetIndexRegistry &indices,
    const WaterQualitySolverOptions &options);

#endif // AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H
