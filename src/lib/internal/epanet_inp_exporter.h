#ifndef AOWIS_EPANET_INP_EXPORTER_H
#define AOWIS_EPANET_INP_EXPORTER_H

#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct NetworkHydraulic;

HydraulicSimulationStatus retrieveEpanetInpText(
    EpanetProject &project,
    const NetworkHydraulic &request,
    QString &inp_text);

#endif // AOWIS_EPANET_INP_EXPORTER_H
