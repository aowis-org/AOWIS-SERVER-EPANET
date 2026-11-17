#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_H

#include <QList>

#include <aowis/model/hydraulic/hydraulic_simulation_options.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

HydraulicSimulationStatus validateEpanetNetwork(
    const NetworkHydraulicDistribution &network,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

HydraulicSimulationStatus validateEpanetQualityRun(
    const NetworkHydraulicDistribution &network,
    const WaterQualitySolverOptions &options,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_H
