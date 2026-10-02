#ifndef AOWIS_HYDRAULIC_NODE_DEMAND_PROJECTION_H
#define AOWIS_HYDRAULIC_NODE_DEMAND_PROJECTION_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

// Builds a transient node-demand solver representation without modifying source.
// Enabled demand points are resolved from their network attachment: junction
// attachments map 100% to that junction, while pipe attachments are distributed
// between the pipe endpoint junctions according to normalized pipe position.
// The returned projection contains no demand points.
HydraulicSimulationStatus buildHydraulicNodeDemandProjection(
    const NetworkHydraulic &source,
    NetworkHydraulic &projected);

#endif // AOWIS_HYDRAULIC_NODE_DEMAND_PROJECTION_H
