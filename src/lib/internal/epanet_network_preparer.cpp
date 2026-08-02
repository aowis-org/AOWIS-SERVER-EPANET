#include "epanet_network_preparer.h"
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

HydraulicSimulationStatus validateEnabledLinkEndpoint(
    const QSet<QUuid> &all_node_uuids,
    const QSet<QUuid> &enabled_node_uuids,
    const QUuid &node_uuid,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &link_id,
    const QUuid &link_uuid,
    const QString &link_name,
    const QString &endpoint_name)
{
    if (!all_node_uuids.contains(node_uuid))
    {
        return makeEpanetStatus(
            stage,
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            link_id,
            link_uuid,
            QStringLiteral("Enabled %1 references an unknown %2-node UUID").arg(link_name, endpoint_name));
    }

    if (!enabled_node_uuids.contains(node_uuid))
    {
        return makeEpanetStatus(
            stage,
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            link_id,
            link_uuid,
            QStringLiteral("Enabled %1 references a disabled %2 node").arg(link_name, endpoint_name));
    }

    return makeEpanetSuccess();
}

template<typename Link>
HydraulicSimulationStatus validateEnabledLinkEndpoints(
    const QList<Link> &links,
    const QSet<QUuid> &all_node_uuids,
    const QSet<QUuid> &enabled_node_uuids,
    HydraulicSimulationStatusStage stage,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &link_name)
{
    for (const Link &link : links)
    {
        HydraulicSimulationStatus status = validateEnabledLinkEndpoint(
            all_node_uuids,
            enabled_node_uuids,
            link.node_uuid_from,
            stage,
            entity_type,
            link.id,
            link.uuid,
            link_name,
            QStringLiteral("start"));
        if (!status.success)
            return status;

        status = validateEnabledLinkEndpoint(
            all_node_uuids,
            enabled_node_uuids,
            link.node_uuid_to,
            stage,
            entity_type,
            link.id,
            link.uuid,
            link_name,
            QStringLiteral("end"));
        if (!status.success)
            return status;
    }

    return makeEpanetSuccess();
}
}

HydraulicSimulationStatus prepareEpanetNetwork(const NetworkHydraulic &source, NetworkHydraulic &prepared)
{
    prepared = source;

    prepared.nodes_junctions = enabledEntities(source.nodes_junctions);
    prepared.nodes_reservoirs = enabledEntities(source.nodes_reservoirs);
    prepared.nodes_tanks = enabledEntities(source.nodes_tanks);

    prepared.links_pipes = enabledEntities(source.links_pipes);
    prepared.links_pumps = enabledEntities(source.links_pumps);
    prepared.links_valves = enabledEntities(source.links_valves);

    prepared.controls_simple.clear();
    prepared.controls_simple.reserve(source.controls_simple.size());
    for (const HydraulicControlSimple &control : source.controls_simple)
    {
        if (control.enabled)
            prepared.controls_simple.append(control);
    }

    prepared.controls_rules.clear();
    prepared.controls_rules.reserve(source.controls_rules.size());
    for (const HydraulicControlRule &rule : source.controls_rules)
    {
        if (rule.enabled)
            prepared.controls_rules.append(rule);
    }

    const QSet<QUuid> all_node_uuids = nodeUuids(source);
    const QSet<QUuid> enabled_node_uuids = nodeUuids(prepared);

    HydraulicSimulationStatus status = validateEnabledLinkEndpoints(
        prepared.links_pipes,
        all_node_uuids,
        enabled_node_uuids,
        HydraulicSimulationStatusStage::AddPipe,
        HydraulicSimulationStatusEntityType::Pipe,
        QStringLiteral("pipe"));
    if (!status.success)
        return status;

    status = validateEnabledLinkEndpoints(
        prepared.links_pumps,
        all_node_uuids,
        enabled_node_uuids,
        HydraulicSimulationStatusStage::AddPump,
        HydraulicSimulationStatusEntityType::Pump,
        QStringLiteral("pump"));
    if (!status.success)
        return status;

    status = validateEnabledLinkEndpoints(
        prepared.links_valves,
        all_node_uuids,
        enabled_node_uuids,
        HydraulicSimulationStatusStage::AddValve,
        HydraulicSimulationStatusEntityType::Valve,
        QStringLiteral("valve"));
    if (!status.success)
        return status;

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
