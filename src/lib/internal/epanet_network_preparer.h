#ifndef AOWIS_EPANET_NETWORK_PREPARER_H
#define AOWIS_EPANET_NETWORK_PREPARER_H

#include <QList>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

HydraulicSimulationStatus prepareEpanetNetwork(
    const NetworkHydraulicDistribution &source,
    NetworkHydraulicDistribution &prepared,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

#endif // AOWIS_EPANET_NETWORK_PREPARER_H
