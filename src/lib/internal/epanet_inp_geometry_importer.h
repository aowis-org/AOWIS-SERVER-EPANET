#ifndef AOWIS_EPANET_INP_GEOMETRY_IMPORTER_H
#define AOWIS_EPANET_INP_GEOMETRY_IMPORTER_H

#include <QString>

#include <aowis/epanet/epanet_result_import.h>

class EpanetProject;

HydraulicSimulationStatus importEpanetInpGeometry(
    EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result);

#endif // AOWIS_EPANET_INP_GEOMETRY_IMPORTER_H
