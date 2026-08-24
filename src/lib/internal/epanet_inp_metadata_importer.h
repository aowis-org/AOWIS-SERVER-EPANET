#ifndef AOWIS_SERVER_EPANET_INP_METADATA_IMPORTER_H
#define AOWIS_SERVER_EPANET_INP_METADATA_IMPORTER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct EpanetResultImport;

HydraulicSimulationStatus importEpanetInpEntityMetadata(
    const EpanetProject &project, EpanetResultImport &result);

#endif // AOWIS_SERVER_EPANET_INP_METADATA_IMPORTER_H
