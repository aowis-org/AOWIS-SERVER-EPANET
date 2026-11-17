#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

#include <QList>

namespace EpanetNetworkValidatorParts
{
QList<HydraulicSimulationStatus> validateIdentities(const NetworkHydraulicDistribution &network);
QList<HydraulicSimulationStatus> validateReferences(const NetworkHydraulicDistribution &network);
QList<HydraulicSimulationStatus> validateTopology(const NetworkHydraulicDistribution &network);
QList<HydraulicSimulationStatus> validateNumerics(const NetworkHydraulicDistribution &network);
}

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H
