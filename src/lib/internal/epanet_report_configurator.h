#ifndef AOWIS_EPANET_REPORT_CONFIGURATOR_H
#define AOWIS_EPANET_REPORT_CONFIGURATOR_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct NetworkHydraulic;

HydraulicSimulationStatus configureEpanetReport(
    EpanetProject &project,
    const NetworkHydraulic &request);

#endif // AOWIS_EPANET_REPORT_CONFIGURATOR_H
