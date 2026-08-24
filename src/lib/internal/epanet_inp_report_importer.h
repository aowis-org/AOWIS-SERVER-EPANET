#ifndef AOWIS_SERVER_EPANET_INP_REPORT_IMPORTER_H
#define AOWIS_SERVER_EPANET_INP_REPORT_IMPORTER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

#include <QString>

class EpanetProject;
struct EpanetResultImport;

struct EpanetInpSourceUnits
{
    int flow_units = 0;
    int pressure_units = 0;
    double specific_gravity = 1.0;
};

HydraulicSimulationStatus importEpanetInpReport(
    const EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result,
    const EpanetInpSourceUnits &source_units);

#endif // AOWIS_SERVER_EPANET_INP_REPORT_IMPORTER_H
