#ifndef AOWIS_EPANET_RUN_REQUEST_H
#define AOWIS_EPANET_RUN_REQUEST_H

#include <optional>

#include <QList>

#include <aowis/model/hydraulic/network_hydraulic_distribution.h>
#include <aowis/model/map/map_annotation.h>

struct EpanetRunRequest
{
    NetworkHydraulicDistribution network;
    QList<WaterQualitySolverOptions> quality_runs;

    // Standard EPANET chemical/age/trace quality_runs and the multi-species
    // reaction model are independent quality analyses. EpanetRunner executes
    // them sequentially against the same hydraulic solution; MSX consumes the
    // hydraulic file persisted from that one EPANET hydraulic solve. Within
    // MultiSpeciesRunOptions, output_species_uuids is an output filter only: MSX
    // always solves the complete NetworkHydraulicDistribution::multi_species chemistry.
    // nullopt means this run does not execute NetworkHydraulicDistribution::multi_species
    // even if that model is defined.
    std::optional<MultiSpeciesRunOptions> multi_species_run;
    // Members of this network's Distribution map-annotation layer (MODEL A3;
    // annotations are not part of the hydraulic network). EPANET INP keeps
    // them as [LABELS]: EpanetRunner::importInp() fills this list from
    // [LABELS] and retrieveInp() writes it back. run() ignores annotations,
    // which have no hydraulic effect. Anchors must resolve to a junction,
    // reservoir, tank or demand point of `network` (checked by retrieveInp).
    QList<MapAnnotation> map_annotations;
};

#endif // AOWIS_EPANET_RUN_REQUEST_H
