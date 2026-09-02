#ifndef AOWIS_EPANET_RUN_REQUEST_H
#define AOWIS_EPANET_RUN_REQUEST_H

#include <optional>

#include <QList>

#include <aowis/model/hydraulic/network_hydraulic.h>

struct EpanetRunRequest
{
    NetworkHydraulic network;
    QList<WaterQualitySolverOptions> quality_runs;

    // A network's chemical/age/trace quality_runs and its multi-species
    // reaction model are two independent EPANET solvers that both write into
    // the one active INP quality slot, so a request may carry one or the
    // other but not both -- EpanetRunner rejects a request setting both.
    // nullopt means this run does not execute NetworkHydraulic::multi_species
    // even if that model is defined.
    std::optional<MultiSpeciesRunOptions> multi_species_run;
};

#endif // AOWIS_EPANET_RUN_REQUEST_H
