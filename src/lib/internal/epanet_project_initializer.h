#ifndef AOWIS_EPANET_PROJECT_INITIALIZER_H
#define AOWIS_EPANET_PROJECT_INITIALIZER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
class EpanetReportCollector;
struct NetworkHydraulic;

HydraulicSimulationStatus initializeEpanetProject(
    EpanetProject &project,
    const NetworkHydraulic &request,
    EpanetReportCollector &report_collector);

#endif // AOWIS_EPANET_PROJECT_INITIALIZER_H
