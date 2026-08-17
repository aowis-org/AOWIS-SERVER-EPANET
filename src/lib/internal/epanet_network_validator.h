#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

HydraulicSimulationStatus validateEpanetNetwork(const NetworkHydraulic &network);

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_H
