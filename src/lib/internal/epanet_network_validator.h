#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_H

#include <QList>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

HydraulicSimulationStatus validateEpanetNetwork(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_H
