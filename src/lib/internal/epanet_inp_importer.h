#ifndef AOWIS_EPANET_INP_IMPORTER_H
#define AOWIS_EPANET_INP_IMPORTER_H

#include <QString>

#include <aowis/epanet/epanet_result_import.h>

EpanetResultImport importEpanetInp(const QString &input_file_path);

#endif // AOWIS_EPANET_INP_IMPORTER_H
