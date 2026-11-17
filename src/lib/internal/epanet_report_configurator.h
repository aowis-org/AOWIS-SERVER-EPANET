#ifndef AOWIS_EPANET_REPORT_CONFIGURATOR_H
#define AOWIS_EPANET_REPORT_CONFIGURATOR_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct NetworkHydraulicDistribution;

HydraulicSimulationStatus configureEpanetReport(
    EpanetProject &project,
    const NetworkHydraulicDistribution &request);

#endif // AOWIS_EPANET_REPORT_CONFIGURATOR_H
