#ifndef AOWIS_EPANET_MSX_VALIDATOR_H
#define AOWIS_EPANET_MSX_VALIDATOR_H

#include <QList>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/multi_species.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

HydraulicSimulationStatus validateEpanetMultiSpeciesModel(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

HydraulicSimulationStatus validateEpanetMultiSpeciesRun(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    QList<HydraulicSimulationStatus> *validation_failures = nullptr);

#endif // AOWIS_EPANET_MSX_VALIDATOR_H
