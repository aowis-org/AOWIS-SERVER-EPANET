#include "epanet_network_preparer.h"
#include "hydraulic_node_demand_projection.h"
#include "epanet_network_validator.h"
#include "epanet_status_helpers.h"

#include <QDate>
#include <QList>
#include <QSet>

#include <cmath>

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

const HydraulicPipeMaterial *pipeMaterialByUuid(
    const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicPipeMaterial &material : network.pipe_materials)
    {
        if (material.uuid == uuid)
            return &material;
    }

    return nullptr;
}

HydraulicSimulationStatus pipeMaterialFailure(
    const HydraulicLinkPipe &pipe,
    const QString &message,
    const QStringList &details = QStringList())
{
    HydraulicSimulationStatus status;
    status.success = false;
    status.stage = HydraulicSimulationStatusStage::BuildNetwork;
    status.operation = HydraulicSimulationStatusOperation::ResolveEntity;
    status.entity.type = HydraulicSimulationStatusEntityType::Pipe;
    status.entity.id = pipe.id;
    status.entity.uuid = pipe.uuid;
    status.message = message;
    status.details = details;
    return status;
}

HydraulicSimulationStatus resolveMaterialLibraryRoughness(
    const NetworkHydraulic &source, NetworkHydraulic &resolved)
{
    resolved = source;
    const QDate reference_date = QDate::currentDate();

    for (HydraulicLinkPipe &pipe : resolved.links_pipes)
    {
        if (!pipe.metadata.enabled
            || pipe.roughness_mode != HydraulicPipeRoughnessMode::MaterialLibrary)
        {
            continue;
        }

        if (pipe.material_uuid.isNull())
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("Pipe material-library roughness requires a material"));
        }

        const HydraulicPipeMaterial *material = pipeMaterialByUuid(source, pipe.material_uuid);
        if (material == nullptr)
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("Pipe references a material that is not present in the network material library"),
                {QStringLiteral("material_uuid: %1")
                     .arg(pipe.material_uuid.toString(QUuid::WithoutBraces))});
        }

        if (!pipe.metadata.date_installed.has_value())
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("Pipe material-library roughness requires an installation date"),
                {QStringLiteral("material: %1").arg(material->id)});
        }

        const std::optional<int> age_years = hydraulicPipeAgeYears(
            pipe.metadata.date_installed.value(), reference_date);
        if (!age_years.has_value())
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("Pipe installation date is invalid for material-library roughness"),
                {QStringLiteral("material: %1").arg(material->id),
                 QStringLiteral("date_installed: %1")
                     .arg(pipe.metadata.date_installed->toString(Qt::ISODate)),
                 QStringLiteral("reference_date: %1").arg(reference_date.toString(Qt::ISODate))});
        }

        const std::optional<double> roughness = resolveHydraulicPipeMaterialRoughness(
            *material, source.options_hydraulic.headloss_formula, age_years.value());
        if (!roughness.has_value())
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("No applicable material roughness is defined for the pipe age and active headloss formula"),
                {QStringLiteral("material: %1").arg(material->id),
                 QStringLiteral("pipe_age_years: %1").arg(age_years.value())});
        }

        if (!std::isfinite(roughness.value()) || roughness.value() <= 0.0)
        {
            return pipeMaterialFailure(
                pipe,
                QStringLiteral("Resolved material roughness must be finite and greater than zero"),
                {QStringLiteral("material: %1").arg(material->id),
                 QStringLiteral("pipe_age_years: %1").arg(age_years.value()),
                 QStringLiteral("roughness: %1").arg(roughness.value(), 0, 'g', 17)});
        }

        switch (source.options_hydraulic.headloss_formula)
        {
        case HydraulicHeadlossFormula::HazenWilliams:
            pipe.roughness_hazen_williams = roughness.value();
            break;
        case HydraulicHeadlossFormula::DarcyWeisbach:
            pipe.roughness_darcy_weisbach_mm = roughness.value();
            break;
        case HydraulicHeadlossFormula::ChezyManning:
            pipe.roughness_chezy_manning = roughness.value();
            break;
        }
    }

    return makeEpanetSuccess();
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
    NetworkHydraulic resolved_source;
    HydraulicSimulationStatus status = resolveMaterialLibraryRoughness(source, resolved_source);
    if (!status.success)
    {
        if (validation_failures != nullptr)
            validation_failures->append(status);
        return status;
    }

    status = validateEpanetNetwork(resolved_source, validation_failures);
    if (!status.success)
        return status;

    NetworkHydraulic enabled_network = resolved_source;

    enabled_network.nodes_junctions = enabledEntities(resolved_source.nodes_junctions);
    enabled_network.nodes_reservoirs = enabledEntities(resolved_source.nodes_reservoirs);
    enabled_network.nodes_tanks = enabledEntities(resolved_source.nodes_tanks);
    enabled_network.demand_points = enabledEntities(resolved_source.demand_points);

    enabled_network.links_pipes = enabledEntities(resolved_source.links_pipes);
    enabled_network.links_pumps = enabledEntities(resolved_source.links_pumps);
    enabled_network.links_valves = enabledEntities(resolved_source.links_valves);

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
