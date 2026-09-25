#ifndef AOWIS_EPANET_RUN_REQUEST_H
#define AOWIS_EPANET_RUN_REQUEST_H

#include <optional>

#include <QList>

#include <aowis/model/hydraulic/network_hydraulic.h>

struct EpanetRunRequest
{
    NetworkHydraulic network;
    QList<WaterQualitySolverOptions> quality_runs;

    // Standard EPANET chemical/age/trace quality_runs and the multi-species
    // reaction model are independent quality analyses. EpanetRunner executes
    // them sequentially against the same hydraulic solution; MSX consumes the
    // hydraulic file persisted from that one EPANET hydraulic solve. Within
    // MultiSpeciesRunOptions, output_species_uuids is an output filter only: MSX
    // always solves the complete NetworkHydraulic::multi_species chemistry.
    // nullopt means this run does not execute NetworkHydraulic::multi_species
    // even if that model is defined.
    std::optional<MultiSpeciesRunOptions> multi_species_run;
};

#endif // AOWIS_EPANET_RUN_REQUEST_H
