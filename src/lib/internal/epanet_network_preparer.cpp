#include "epanet_network_preparer.h"
#include "hydraulic_node_demand_projection.h"
#include "epanet_network_validator.h"
#include "epanet_status_helpers.h"

#include <QList>
#include <QSet>

namespace
{
template<typename Entity>
QList<Entity> enabledEntities(const QList<Entity> &entities)
{
    QList<Entity> enabled_entities;
    enabled_entities.reserve(entities.size());

    for (const Entity &entity : entities)
    {
        if (entity.metadata.enabled)
            enabled_entities.append(entity);
    }

    return enabled_entities;
}

template<typename Entity>
QSet<QUuid> entityUuids(const QList<Entity> &entities)
{
    QSet<QUuid> uuids;
    uuids.reserve(entities.size());

    for (const Entity &entity : entities)
        uuids.insert(entity.uuid);

    return uuids;
}

QSet<QUuid> nodeUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> uuids = entityUuids(network.nodes_junctions);
    uuids.unite(entityUuids(network.nodes_reservoirs));
    uuids.unite(entityUuids(network.nodes_tanks));
    return uuids;
}

QSet<QUuid> linkUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> uuids = entityUuids(network.links_pipes);
    uuids.unite(entityUuids(network.links_pumps));
    uuids.unite(entityUuids(network.links_valves));
    return uuids;
}

void removeDisabledReportSelections(HydraulicSimulationReportSelection &selection, const QSet<QUuid> &all_uuids, const QSet<QUuid> &enabled_uuids)
{
    if (selection.mode != HydraulicSimulationReportSelectionMode::Selected)
        return;

    QList<QUuid> retained_uuids;
    retained_uuids.reserve(selection.uuids.size());

    for (const QUuid &uuid : selection.uuids)
    {
        if (!all_uuids.contains(uuid) || enabled_uuids.contains(uuid))
            retained_uuids.append(uuid);
    }

    selection.uuids = retained_uuids;
    if (selection.uuids.isEmpty())
        selection.mode = HydraulicSimulationReportSelectionMode::None;
}

}

HydraulicSimulationStatus prepareEpanetNetwork(
    const NetworkHydraulic &source,
    NetworkHydraulic &prepared,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    HydraulicSimulationStatus status = validateEpanetNetwork(source, validation_failures);
    if (!status.success)
        return status;

    NetworkHydraulic enabled_network = source;

    enabled_network.nodes_junctions = enabledEntities(source.nodes_junctions);
    enabled_network.nodes_reservoirs = enabledEntities(source.nodes_reservoirs);
    enabled_network.nodes_tanks = enabledEntities(source.nodes_tanks);
    enabled_network.demand_points = enabledEntities(source.demand_points);

    enabled_network.links_pipes = enabledEntities(source.links_pipes);
    enabled_network.links_pumps = enabledEntities(source.links_pumps);
    enabled_network.links_valves = enabledEntities(source.links_valves);

    status = buildHydraulicNodeDemandProjection(enabled_network, prepared);
    if (!status.success)
    {
        if (validation_failures != nullptr)
            validation_failures->append(status);
        return status;
    }

    // Validate the projected junction demands as ordinary solver-facing demands.
    // This catches pattern references and numeric constraints after projection.
    QList<HydraulicSimulationStatus> projected_validation_failures;
    status = validateEpanetNetwork(prepared, &projected_validation_failures);
    if (!status.success)
    {
        if (validation_failures != nullptr)
            validation_failures->append(projected_validation_failures);
        return status;
    }

    const QSet<QUuid> all_node_uuids = nodeUuids(source);
    const QSet<QUuid> enabled_node_uuids = nodeUuids(prepared);

    removeDisabledReportSelections(
        prepared.options_report.selection_nodes,
        all_node_uuids,
        enabled_node_uuids);
    removeDisabledReportSelections(
        prepared.options_report.selection_links,
        linkUuids(source),
        linkUuids(prepared));

    return makeEpanetSuccess();
}
