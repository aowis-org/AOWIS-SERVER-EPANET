#ifndef AOWIS_EPANET_RESULT_IMPORT_H
#define AOWIS_EPANET_RESULT_IMPORT_H

#include <QList>
#include <QMetaType>

#include <aowis/epanet/epanet_run_request.h>
#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

struct EpanetResultImport
{
    EpanetRunRequest request;
    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    bool complete = false;
};

Q_DECLARE_METATYPE(EpanetResultImport)

#endif // AOWIS_EPANET_RESULT_IMPORT_H
