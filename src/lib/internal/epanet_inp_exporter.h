#ifndef AOWIS_EPANET_INP_EXPORTER_H
#define AOWIS_EPANET_INP_EXPORTER_H

#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;
struct NetworkHydraulicDistribution;

HydraulicSimulationStatus retrieveEpanetInpText(
    EpanetProject &project,
    const NetworkHydraulicDistribution &request,
    QString &inp_text);

#endif // AOWIS_EPANET_INP_EXPORTER_H
