#ifndef AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H
#define AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct EpanetIndexRegistry;
struct NetworkHydraulic;

HydraulicSimulationStatus configureEpanetQualityRun(
    EpanetProject &project,
    const NetworkHydraulic &request,
    const EpanetIndexRegistry &indices);

#endif // AOWIS_EPANET_QUALITY_RUN_CONFIGURATOR_H
