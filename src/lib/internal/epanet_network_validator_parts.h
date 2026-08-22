#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QList>

namespace EpanetNetworkValidatorParts
{
QList<HydraulicSimulationStatus> validateIdentities(const NetworkHydraulic &network);
QList<HydraulicSimulationStatus> validateReferences(const NetworkHydraulic &network);
QList<HydraulicSimulationStatus> validateNumerics(const NetworkHydraulic &network);
}

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_PARTS_H
