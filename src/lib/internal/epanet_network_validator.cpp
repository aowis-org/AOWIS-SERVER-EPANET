#include "epanet_network_validator.h"
#include "epanet_status_helpers.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>
#include <functional>
#include <optional>

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

HydraulicSimulationStatus validationStatus(
    HydraulicSimulationStatusOperation operation,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &message,
    const QStringList &details = {})
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        HydraulicSimulationStatusStage::BuildNetwork,
        operation,
        entity_type,
        entity_id,
        entity_uuid,
        message);
    status.details = details;
    return status;
}

void collectIdentityFailures(
    QList<HydraulicSimulationStatus> &failures,
    QSet<QUuid> &all_uuids,
    QSet<QString> &ids,
    const QString &namespace_name,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_name,
    const QString &id,
    const QUuid &uuid)
{
    const bool uuid_missing = uuid.isNull();
    const bool id_missing = id.isEmpty();

    if (uuid_missing)
    {
        failures.append(validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no UUID").arg(entity_name)));
    }

    if (id_missing)
    {
        failures.append(validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no ID").arg(entity_name)));
    }

    if (!uuid_missing)
    {
        if (all_uuids.contains(uuid))
        {
            failures.append(validationStatus(
                HydraulicSimulationStatusOperation::ResolveEntity,
                entity_type,
                id,
                uuid,
                QStringLiteral("%1 UUID is duplicated across the hydraulic model").arg(entity_name),
                {QStringLiteral("Duplicate UUID: %1").arg(uuid.toString(QUuid::WithoutBraces))}));
        }
        else
        {
            all_uuids.insert(uuid);
        }
    }

    if (!id_missing)
    {
        if (ids.contains(id))
        {
            failures.append(validationStatus(
                HydraulicSimulationStatusOperation::ResolveEntity,
                entity_type,
                id,
                uuid,
                QStringLiteral("%1 ID is duplicated in the EPANET %2 namespace").arg(entity_name, namespace_name),
                {QStringLiteral("Duplicate ID: %1").arg(id)}));
        }
        else
        {
            ids.insert(id);
        }
    }
}

HydraulicSimulationStatusOperation numericValidationOperation(HydraulicSimulationStatusEntityType entity_type)
{
    switch (entity_type)
    {
    case HydraulicSimulationStatusEntityType::Pattern:
        return HydraulicSimulationStatusOperation::AddPattern;
    case HydraulicSimulationStatusEntityType::Curve:
        return HydraulicSimulationStatusOperation::AddCurve;
    case HydraulicSimulationStatusEntityType::HydraulicSolver:
        return HydraulicSimulationStatusOperation::ConfigureHydraulics;
    case HydraulicSimulationStatusEntityType::QualitySolver:
        return HydraulicSimulationStatusOperation::ConfigureQuality;
    case HydraulicSimulationStatusEntityType::Report:
        return HydraulicSimulationStatusOperation::ConfigureReport;
    case HydraulicSimulationStatusEntityType::Control:
        return HydraulicSimulationStatusOperation::AddControl;
    case HydraulicSimulationStatusEntityType::Rule:
        return HydraulicSimulationStatusOperation::AddRule;
    default:
        return HydraulicSimulationStatusOperation::SetEntityMetadata;
    }
}

HydraulicSimulationStatus invalidNumeric(
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name,
    const QString &requirement)
{
    return validationStatus(
        numericValidationOperation(entity_type),
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 contains an invalid numeric value").arg(entity_id),
        {QStringLiteral("%1: %2").arg(field_name, requirement)});
}

HydraulicSimulationStatus validateFinite(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value))
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite"));
}

HydraulicSimulationStatus validateLongitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= -180.0 && value <= 180.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and between -180 and 180 degrees"));
}

HydraulicSimulationStatus validateLatitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= -90.0 && value <= 90.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and between -90 and 90 degrees"));
}

HydraulicSimulationStatus validateFiniteNonNegative(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= 0.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and non-negative"));
}

HydraulicSimulationStatus validateFinitePositive(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value > 0.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and positive"));
}

HydraulicSimulationStatus validateReference(
    const QSet<QUuid> &all_uuids,
    const QSet<QUuid> &enabled_uuids,
    const QUuid &target_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null = false)
{
    if (target_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (!all_uuids.contains(target_uuid))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
            {QStringLiteral("Referenced UUID: %1").arg(target_uuid.toString(QUuid::WithoutBraces))});

    if (!enabled_uuids.contains(target_uuid))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 references a disabled %2").arg(entity_id, relationship),
            {QStringLiteral("Referenced UUID: %1").arg(target_uuid.toString(QUuid::WithoutBraces))});

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validatePatternReference(
    const QSet<QUuid> &pattern_uuids,
    const QUuid &pattern_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null)
{
    if (pattern_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (pattern_uuids.contains(pattern_uuid))
        return makeEpanetSuccess();

    return validationStatus(
        HydraulicSimulationStatusOperation::ResolveEntity,
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
        {QStringLiteral("Referenced UUID: %1").arg(pattern_uuid.toString(QUuid::WithoutBraces))});
}

HydraulicSimulationStatus validateCurveReference(
    const QSet<QUuid> &curve_uuids,
    const QUuid &curve_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null)
{
    if (curve_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (curve_uuids.contains(curve_uuid))
        return makeEpanetSuccess();

    return validationStatus(
        HydraulicSimulationStatusOperation::ResolveEntity,
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
        {QStringLiteral("Referenced UUID: %1").arg(curve_uuid.toString(QUuid::WithoutBraces))});
}

void appendValidationFailure(QList<HydraulicSimulationStatus> &failures, const HydraulicSimulationStatus &status)
{
    if (!status.success)
        failures.append(status);
}

void appendValidationFailures(
    QList<HydraulicSimulationStatus> &failures,
    const QList<HydraulicSimulationStatus> &additional_failures)
{
    for (const HydraulicSimulationStatus &failure : additional_failures)
        failures.append(failure);
}

QList<HydraulicSimulationStatus> validateIdentities(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    QSet<QUuid> all_uuids;
    QSet<QString> node_ids;
    QSet<QString> link_ids;
    QSet<QString> pattern_ids;
    QSet<QString> curve_ids;
    QSet<QString> simple_control_ids;
    QSet<QString> rule_ids;

    for (const HydraulicNodeJunction &entity : network.nodes_junctions)
    {
        collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"), entity.id, entity.uuid);
    }
    for (const HydraulicNodeReservoir &entity : network.nodes_reservoirs)
    {
        collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"), entity.id, entity.uuid);
    }
    for (const HydraulicNodeTank &entity : network.nodes_tanks)
    {
        collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkPipe &entity : network.links_pipes)
    {
        collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkPump &entity : network.links_pumps)
    {
        collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pump, QStringLiteral("Pump"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkValve &entity : network.links_valves)
    {
        collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Valve, QStringLiteral("Valve"), entity.id, entity.uuid);
    }
    for (const HydraulicPatternTime &entity : network.patterns_time)
    {
        collectIdentityFailures(failures, all_uuids, pattern_ids, QStringLiteral("pattern"), HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveTankVolume &entity : network.curves_tank_volume)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurvePumpHead &entity : network.curves_pump_head)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump head curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurvePumpEfficiency &entity : network.curves_pump_efficiency)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump efficiency curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveValveHeadloss &entity : network.curves_valve_headloss)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve head-loss curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveValveCharacteristic &entity : network.curves_valve_characteristic)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve characteristic curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveGeneric &entity : network.curves_generic)
    {
        collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Generic curve"), entity.id, entity.uuid);
    }
    for (const HydraulicControlSimple &entity : network.controls_simple)
    {
        collectIdentityFailures(failures, all_uuids, simple_control_ids, QStringLiteral("simple-control"), HydraulicSimulationStatusEntityType::Control, QStringLiteral("Simple control"), entity.id, entity.uuid);
    }
    for (const HydraulicControlRule &entity : network.controls_rules)
    {
        collectIdentityFailures(failures, all_uuids, rule_ids, QStringLiteral("rule"), HydraulicSimulationStatusEntityType::Rule, QStringLiteral("Control rule"), entity.id, entity.uuid);
    }

    return failures;
}

QList<HydraulicSimulationStatus> validateReferences(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    const QSet<QUuid> all_nodes = nodeUuids(network);
    const QSet<QUuid> all_links = linkUuids(network);

    NetworkHydraulic enabled_network = network;
    enabled_network.nodes_junctions = enabledEntities(network.nodes_junctions);
    enabled_network.nodes_reservoirs = enabledEntities(network.nodes_reservoirs);
    enabled_network.nodes_tanks = enabledEntities(network.nodes_tanks);
    enabled_network.links_pipes = enabledEntities(network.links_pipes);
    enabled_network.links_pumps = enabledEntities(network.links_pumps);
    enabled_network.links_valves = enabledEntities(network.links_valves);

    const QSet<QUuid> enabled_nodes = nodeUuids(enabled_network);
    const QSet<QUuid> enabled_links = linkUuids(enabled_network);
    const QSet<QUuid> patterns = entityUuids(network.patterns_time);
    const QSet<QUuid> tank_curves = entityUuids(network.curves_tank_volume);
    const QSet<QUuid> pump_head_curves = entityUuids(network.curves_pump_head);
    const QSet<QUuid> pump_efficiency_curves = entityUuids(network.curves_pump_efficiency);
    const QSet<QUuid> valve_headloss_curves = entityUuids(network.curves_valve_headloss);
    const QSet<QUuid> valve_characteristic_curves = entityUuids(network.curves_valve_characteristic);

    HydraulicSimulationStatus status;

    if (network.options_quality.analysis == WaterQualityAnalysisType::SourceTrace)
    {
        status = validateReference(all_nodes, enabled_nodes, network.options_quality.trace_node_uuid, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("source-trace node"));
        appendValidationFailure(failures, status);
    }

    const std::function<void(const HydraulicNodeQualitySource &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> validate_quality_source_pattern = [&failures, &patterns](const HydraulicNodeQualitySource &source, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        if (source.type == HydraulicNodeQualitySourceType::None || source.pattern_uuid.isNull())
            return;
        const HydraulicSimulationStatus source_pattern_status = validatePatternReference(patterns, source.pattern_uuid, entity_type, id, uuid, QStringLiteral("quality-source pattern"), true);
        appendValidationFailure(failures, source_pattern_status);
    };

    for (const HydraulicNodeJunction &junction : enabled_network.nodes_junctions)
    {
        validate_quality_source_pattern(junction.quality_source, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid);
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
        {
            if (demand.pattern_mode != HydraulicTimePatternMode::TimePattern)
                continue;
            status = validatePatternReference(patterns, demand.pattern_uuid, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demand pattern"), true);
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicNodeReservoir &reservoir : enabled_network.nodes_reservoirs)
    {
        validate_quality_source_pattern(reservoir.quality_source, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid);
        if (reservoir.head_pattern_mode != HydraulicTimePatternMode::TimePattern)
            continue;
        status = validatePatternReference(patterns, reservoir.head_pattern_uuid, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("head pattern"), false);
        appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeTank &tank : enabled_network.nodes_tanks)
    {
        validate_quality_source_pattern(tank.quality_source, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid);
        if (tank.geometry_input_type != HydraulicNodeTankGeometryInputType::VolumeCurve)
            continue;
        status = validateCurveReference(tank_curves, tank.volume_curve_uuid, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("tank volume curve"), false);
        appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPipe &pipe : enabled_network.links_pipes)
    {
        status = validateReference(all_nodes, enabled_nodes, pipe.node_uuid_from, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("start node"));
        appendValidationFailure(failures, status);
        status = validateReference(all_nodes, enabled_nodes, pipe.node_uuid_to, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("end node"));
        appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPump &pump : enabled_network.links_pumps)
    {
        status = validateReference(all_nodes, enabled_nodes, pump.node_uuid_from, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("start node"));
        appendValidationFailure(failures, status);
        status = validateReference(all_nodes, enabled_nodes, pump.node_uuid_to, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("end node"));
        appendValidationFailure(failures, status);

        if (pump.definition_type != HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = validateCurveReference(pump_head_curves, pump.head_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("head curve"), false);
            appendValidationFailure(failures, status);
        }

        if (!pump.speed_pattern_uuid.isNull())
        {
            status = validatePatternReference(patterns, pump.speed_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("speed pattern"), false);
            appendValidationFailure(failures, status);
        }

        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve)
        {
            status = validateCurveReference(pump_efficiency_curves, pump.efficiency_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("efficiency curve"), false);
            appendValidationFailure(failures, status);
        }

        if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
        {
            status = validatePatternReference(patterns, pump.price_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy-price pattern"), false);
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkValve &valve : enabled_network.links_valves)
    {
        status = validateReference(all_nodes, enabled_nodes, valve.node_uuid_from, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("start node"));
        appendValidationFailure(failures, status);
        status = validateReference(all_nodes, enabled_nodes, valve.node_uuid_to, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("end node"));
        appendValidationFailure(failures, status);

        if (valve.type == HydraulicLinkValveType::GPV)
        {
            status = validateCurveReference(valve_headloss_curves, valve.head_loss_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("GPV head-loss curve"), false);
            appendValidationFailure(failures, status);
        }
        if (valve.type == HydraulicLinkValveType::PCV && !valve.characteristic_curve_uuid.isNull())
        {
            status = validateCurveReference(valve_characteristic_curves, valve.characteristic_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV characteristic curve"), false);
            appendValidationFailure(failures, status);
        }
    }

    if (!network.options_hydraulic.default_demand_pattern_uuid.isNull())
    {
        status = validatePatternReference(
            patterns,
            network.options_hydraulic.default_demand_pattern_uuid,
            HydraulicSimulationStatusEntityType::Pattern,
            QStringLiteral("default-demand-pattern"),
            network.options_hydraulic.default_demand_pattern_uuid,
            QStringLiteral("default demand pattern"),
            false);
        appendValidationFailure(failures, status);
    }

    if (!network.options_energy.global_energy_price_pattern_uuid.isNull())
    {
        status = validatePatternReference(
            patterns,
            network.options_energy.global_energy_price_pattern_uuid,
            HydraulicSimulationStatusEntityType::Pattern,
            QStringLiteral("global-energy-price-pattern"),
            network.options_energy.global_energy_price_pattern_uuid,
            QStringLiteral("global energy-price pattern"),
            false);
        appendValidationFailure(failures, status);
    }

    for (const HydraulicMapLabel &label : network.map_labels)
    {
        if (label.anchor_node_uuid.isNull())
            continue;
        status = validateReference(all_nodes, enabled_nodes, label.anchor_node_uuid, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map-label anchor node"));
        appendValidationFailure(failures, status);
    }

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        status = validateReference(all_links, enabled_links, control.link_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("controlled link"));
        appendValidationFailure(failures, status);

        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = validateReference(all_nodes, enabled_nodes, control.trigger_node_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger node"));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        if (!rule.source_text.isEmpty())
            continue;

        for (const HydraulicControlRulePremise &premise : rule.premises)
        {
            if (premise.object == HydraulicControlRuleObject::Node)
            {
                status = validateReference(all_nodes, enabled_nodes, premise.object_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premise node"));
                appendValidationFailure(failures, status);
            }
            else if (premise.object == HydraulicControlRuleObject::Link)
            {
                status = validateReference(all_links, enabled_links, premise.object_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premise link"));
                appendValidationFailure(failures, status);
            }
        }

        for (const HydraulicControlRuleAction &action : rule.actions_then)
        {
            status = validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("THEN-action link"));
            appendValidationFailure(failures, status);
        }
        for (const HydraulicControlRuleAction &action : rule.actions_else)
        {
            status = validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("ELSE-action link"));
            appendValidationFailure(failures, status);
        }
    }

    return failures;
}

void collectReportThresholdFailures(
    QList<HydraulicSimulationStatus> &failures,
    const std::optional<double> &threshold,
    const QString &field_name,
    const NetworkHydraulic &network)
{
    if (!threshold.has_value() || std::isfinite(threshold.value()))
        return;

    failures.append(invalidNumeric(
        HydraulicSimulationStatusEntityType::Report,
        network.id,
        network.uuid,
        field_name,
        QStringLiteral("must be finite when configured")));
}

QList<HydraulicSimulationStatus> validateNumerics(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    HydraulicSimulationStatus status;


    const WaterQualitySolverOptions &quality = network.options_quality;
    const WaterQualityReactionOptions &reactions = network.options_reaction;
    status = validateFiniteNonNegative(quality.chemical_tolerance_mg_per_l, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_quality.chemical_tolerance_mg_per_l"));
    appendValidationFailure(failures, status);
    status = validateFiniteNonNegative(quality.water_age_tolerance_h, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_quality.water_age_tolerance_h"));
    appendValidationFailure(failures, status);
    status = validateFiniteNonNegative(quality.source_trace_tolerance_percent, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_quality.source_trace_tolerance_percent"));
    appendValidationFailure(failures, status);
    status = validateFiniteNonNegative(quality.relative_diffusivity, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_quality.relative_diffusivity"));
    appendValidationFailure(failures, status);
    status = validateFiniteNonNegative(reactions.global_pipe_bulk_reaction.order, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_bulk_reaction.order"));
    appendValidationFailure(failures, status);
    status = validateFiniteNonNegative(reactions.global_tank_bulk_reaction.order, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_tank_bulk_reaction.order"));
    appendValidationFailure(failures, status);
    status = validateFinite(reactions.global_pipe_bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_bulk_reaction.coefficient"));
    appendValidationFailure(failures, status);
    status = validateFinite(reactions.global_tank_bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_tank_bulk_reaction.coefficient"));
    appendValidationFailure(failures, status);
    status = validateFinite(reactions.global_pipe_wall_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_wall_reaction.coefficient"));
    appendValidationFailure(failures, status);
    if (!std::isfinite(reactions.global_pipe_wall_reaction.order) || (reactions.global_pipe_wall_reaction.order != 0.0 && reactions.global_pipe_wall_reaction.order != 1.0))
    {
        failures.append(invalidNumeric(HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_wall_reaction.order"), QStringLiteral("must be either 0 or 1 for EPANET")));
    }
    status = validateFiniteNonNegative(reactions.limiting_concentration_mg_per_l, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.limiting_concentration_mg_per_l"));
    appendValidationFailure(failures, status);
    status = validateFinite(reactions.roughness_reaction_factor, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.roughness_reaction_factor"));
    appendValidationFailure(failures, status);

    if (quality.analysis == WaterQualityAnalysisType::Chemical && quality.chemical_name.trimmed().isEmpty())
    {
        failures.append(validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("Chemical water-quality analysis requires a chemical name")));
    }

    const std::function<void(const HydraulicNodeQualitySource &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> validate_quality_source = [&failures, &quality](const HydraulicNodeQualitySource &source, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        HydraulicSimulationStatus source_status = validateFiniteNonNegative(source.chemical_concentration_mg_per_l, entity_type, id, uuid, QStringLiteral("quality_source.chemical_concentration_mg_per_l"));
        appendValidationFailure(failures, source_status);
        source_status = validateFiniteNonNegative(source.chemical_mass_flow_mg_per_min, entity_type, id, uuid, QStringLiteral("quality_source.chemical_mass_flow_mg_per_min"));
        appendValidationFailure(failures, source_status);
        if (source.type != HydraulicNodeQualitySourceType::None && quality.analysis != WaterQualityAnalysisType::Chemical)
        {
            failures.append(validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, entity_type, id, uuid, QStringLiteral("Water-quality sources require chemical analysis mode")));
        }
    };

    const HydraulicSolverOptions &hydraulic = network.options_hydraulic;
    const QList<QPair<QString, double>> hydraulic_values = {
        {QStringLiteral("minimum_pressure_head_m"), hydraulic.minimum_pressure_head_m},
        {QStringLiteral("required_pressure_head_m"), hydraulic.required_pressure_head_m},
        {QStringLiteral("pressure_exponent"), hydraulic.pressure_exponent},
        {QStringLiteral("accuracy"), hydraulic.accuracy},
        {QStringLiteral("damping_limit"), hydraulic.damping_limit},
        {QStringLiteral("maximum_head_error_m"), hydraulic.maximum_head_error_m},
        {QStringLiteral("maximum_flow_change_m3_per_h"), hydraulic.maximum_flow_change_m3_per_h},
        {QStringLiteral("demand_multiplier"), hydraulic.demand_multiplier},
        {QStringLiteral("specific_gravity"), hydraulic.specific_gravity},
        {QStringLiteral("relative_viscosity"), hydraulic.relative_viscosity}
    };
    for (const QPair<QString, double> &field : hydraulic_values)
    {
        status = validateFinite(field.second, HydraulicSimulationStatusEntityType::HydraulicSolver, network.id, network.uuid, field.first);
        appendValidationFailure(failures, status);
    }

    const PumpEnergyOptions &energy = network.options_energy;
    const QList<QPair<QString, double>> energy_values = {
        {QStringLiteral("global_pump_efficiency_percent"), energy.global_pump_efficiency_percent},
        {QStringLiteral("global_energy_price_per_kw_h"), energy.global_energy_price_per_kw_h},
        {QStringLiteral("demand_charge_per_kw"), energy.demand_charge_per_kw}
    };
    for (const QPair<QString, double> &field : energy_values)
    {
        status = validateFinite(field.second, HydraulicSimulationStatusEntityType::HydraulicSolver, network.id, network.uuid, field.first);
        appendValidationFailure(failures, status);
    }

    struct NamedReportThreshold
    {
        const char *name;
        const std::optional<double> *threshold;
    };

    const std::array<NamedReportThreshold, 20> report_thresholds = {{
        {"fields_node.elevation.below_m", &network.options_report.fields_node.elevation.below_m},
        {"fields_node.elevation.above_m", &network.options_report.fields_node.elevation.above_m},
        {"fields_node.demand.below_m3_per_h", &network.options_report.fields_node.demand.below_m3_per_h},
        {"fields_node.demand.above_m3_per_h", &network.options_report.fields_node.demand.above_m3_per_h},
        {"fields_node.head.below_m", &network.options_report.fields_node.head.below_m},
        {"fields_node.head.above_m", &network.options_report.fields_node.head.above_m},
        {"fields_node.pressure.below_m", &network.options_report.fields_node.pressure.below_m},
        {"fields_node.pressure.above_m", &network.options_report.fields_node.pressure.above_m},
        {"fields_link.length.below_m", &network.options_report.fields_link.length.below_m},
        {"fields_link.length.above_m", &network.options_report.fields_link.length.above_m},
        {"fields_link.diameter.below_mm", &network.options_report.fields_link.diameter.below_mm},
        {"fields_link.diameter.above_mm", &network.options_report.fields_link.diameter.above_mm},
        {"fields_link.flow.below_m3_per_h", &network.options_report.fields_link.flow.below_m3_per_h},
        {"fields_link.flow.above_m3_per_h", &network.options_report.fields_link.flow.above_m3_per_h},
        {"fields_link.velocity.below_m_per_s", &network.options_report.fields_link.velocity.below_m_per_s},
        {"fields_link.velocity.above_m_per_s", &network.options_report.fields_link.velocity.above_m_per_s},
        {"fields_link.headloss.below_m_per_km", &network.options_report.fields_link.headloss.below_m_per_km},
        {"fields_link.headloss.above_m_per_km", &network.options_report.fields_link.headloss.above_m_per_km},
        {"fields_link.friction.below_friction_factor", &network.options_report.fields_link.friction.below_friction_factor},
        {"fields_link.friction.above_friction_factor", &network.options_report.fields_link.friction.above_friction_factor}
    }};

    for (const NamedReportThreshold &report_threshold : report_thresholds)
    {
        collectReportThresholdFailures(
            failures,
            *report_threshold.threshold,
            QString::fromLatin1(report_threshold.name),
            network);
    }

    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        for (int index = 0; index < pattern.multipliers.size(); index++)
        {
            status = validateFinite(pattern.multipliers.at(index), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("multipliers[%1]").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).water_level_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].water_level_m").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).volume_m3, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].volume_m3").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).head_gain_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_gain_m").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).efficiency_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].efficiency_percent").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).head_loss_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_loss_m").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).position_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].position_percent").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).relative_flow_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].relative_flow_percent").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveGeneric &curve : network.curves_generic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).x, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].x").arg(index));
            appendValidationFailure(failures, status);
            status = validateFinite(curve.points.at(index).y, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].y").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicMapLabel &label : network.map_labels)
    {
        status = validateLongitudeDeg(label.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map_labels.coordinate_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(label.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map_labels.coordinate_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);
    }

    if (network.map_backdrop.enabled)
    {
        status = validateLongitudeDeg(network.map_backdrop.lower_left_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.lower_left_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(network.map_backdrop.lower_left_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.lower_left_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLongitudeDeg(network.map_backdrop.upper_right_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.upper_right_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(network.map_backdrop.upper_right_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.upper_right_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);
        status = validateFinite(network.map_backdrop.offset_longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.offset_longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateFinite(network.map_backdrop.offset_latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.offset_latitude_deg"));
        appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (!junction.metadata.enabled)
            continue;
        status = validateFiniteNonNegative(junction.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(junction.initial_water_age_h, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("initial_water_age_h"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(junction.initial_source_trace_percent, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("initial_source_trace_percent"));
        appendValidationFailure(failures, status);
        validate_quality_source(junction.quality_source, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid);
        if (junction.elevation_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(junction.terrain_elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("terrain_elevation_m"));
            appendValidationFailure(failures, status);
            status = validateFinite(junction.elevation_offset_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_offset_m"));
            appendValidationFailure(failures, status);
        }
        else
        {
            status = validateFinite(junction.elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_m"));
            appendValidationFailure(failures, status);
        }
        status = validateFiniteNonNegative(junction.emitter.coefficient, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("emitter.coefficient"));
        appendValidationFailure(failures, status);
        status = validateFinitePositive(junction.emitter.pressure_exponent, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("emitter.pressure_exponent"));
        appendValidationFailure(failures, status);
        status = validateLongitudeDeg(junction.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(junction.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);

        for (int index = 0; index < junction.demands.size(); index++)
        {
            status = validateFinite(junction.demands.at(index).base_demand_m3_per_h, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demands[%1].base_demand_m3_per_h").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    std::optional<double> active_emitter_pressure_exponent;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (!junction.metadata.enabled || junction.emitter.coefficient <= 0.0 || !std::isfinite(junction.emitter.pressure_exponent))
            continue;

        if (!active_emitter_pressure_exponent.has_value())
        {
            active_emitter_pressure_exponent = junction.emitter.pressure_exponent;
            continue;
        }

        if (std::abs(active_emitter_pressure_exponent.value() - junction.emitter.pressure_exponent) > 1.0e-12)
        {
            failures.append(validationStatus(
                HydraulicSimulationStatusOperation::ConfigureHydraulics,
                HydraulicSimulationStatusEntityType::Junction,
                junction.id,
                junction.uuid,
                QStringLiteral("Enabled junction emitters must use one common pressure exponent because EPANET exposes a network-wide emitter exponent"),
                {QStringLiteral("emitter.pressure_exponent: %1; expected %2")
                    .arg(junction.emitter.pressure_exponent, 0, 'g', 17)
                    .arg(active_emitter_pressure_exponent.value(), 0, 'g', 17)}));
        }
    }

    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (!reservoir.metadata.enabled)
            continue;
        status = validateFiniteNonNegative(reservoir.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(reservoir.initial_water_age_h, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("initial_water_age_h"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(reservoir.initial_source_trace_percent, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("initial_source_trace_percent"));
        appendValidationFailure(failures, status);
        validate_quality_source(reservoir.quality_source, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid);
        if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(reservoir.terrain_elevation_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("terrain_elevation_m"));
            appendValidationFailure(failures, status);
            status = validateFinite(reservoir.hydraulic_head_offset_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("hydraulic_head_offset_m"));
            appendValidationFailure(failures, status);
        }
        else
        {
            status = validateFinite(reservoir.hydraulic_head_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("hydraulic_head_m"));
            appendValidationFailure(failures, status);
        }
        status = validateLongitudeDeg(reservoir.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(reservoir.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (!tank.metadata.enabled)
            continue;
        status = validateFiniteNonNegative(tank.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(tank.initial_water_age_h, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("initial_water_age_h"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(tank.initial_source_trace_percent, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("initial_source_trace_percent"));
        appendValidationFailure(failures, status);
        validate_quality_source(tank.quality_source, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid);
        if (!std::isfinite(tank.mixing_fraction) || tank.mixing_fraction < 0.0 || tank.mixing_fraction > 1.0)
            failures.append(invalidNumeric(HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("mixing_fraction"), QStringLiteral("must be finite and between 0 and 1")));
        status = validateFinite(tank.bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bulk_reaction.coefficient"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(tank.bulk_reaction.order, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bulk_reaction.order"));
        appendValidationFailure(failures, status);
        if (tank.override_bulk_reaction && std::isfinite(tank.bulk_reaction.order) && std::abs(tank.bulk_reaction.order - reactions.global_tank_bulk_reaction.order) > 1.0e-12)
            failures.append(validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Tank bulk reaction order must match the network-wide EPANET tank reaction order")));

        if (tank.elevation_input_type == HydraulicNodeTankElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(tank.terrain_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("terrain_elevation_m"));
            appendValidationFailure(failures, status);
            status = validateFinite(tank.bottom_offset_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_offset_m"));
            appendValidationFailure(failures, status);
        }
        else
        {
            status = validateFinite(tank.bottom_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_elevation_m"));
            appendValidationFailure(failures, status);
        }

        status = validateFinite(tank.water_level_initial_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_initial_m"));
        appendValidationFailure(failures, status);
        status = validateFinite(tank.water_level_minimum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_minimum_m"));
        appendValidationFailure(failures, status);
        status = validateFinite(tank.water_level_maximum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_maximum_m"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(tank.minimum_volume_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("minimum_volume_m3"));
        appendValidationFailure(failures, status);

        if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::Cylindrical)
            status = validateFinitePositive(tank.diameter_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("diameter_m"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::UniformArea)
            status = validateFinitePositive(tank.cross_section_area_m2, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("cross_section_area_m2"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel)
            status = validateFinitePositive(tank.volume_at_maximum_level_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("volume_at_maximum_level_m3"));
        else
            status = makeEpanetSuccess();
        appendValidationFailure(failures, status);

        status = validateLongitudeDeg(tank.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        appendValidationFailure(failures, status);
        status = validateLatitudeDeg(tank.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (!pipe.metadata.enabled)
            continue;
        status = validateFinite(pipe.bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("bulk_reaction.coefficient"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(pipe.bulk_reaction.order, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("bulk_reaction.order"));
        appendValidationFailure(failures, status);
        status = validateFinite(pipe.wall_reaction.coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("wall_reaction.coefficient"));
        appendValidationFailure(failures, status);
        if (!std::isfinite(pipe.wall_reaction.order) || (pipe.wall_reaction.order != 0.0 && pipe.wall_reaction.order != 1.0))
            failures.append(invalidNumeric(HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("wall_reaction.order"), QStringLiteral("must be either 0 or 1 for EPANET")));
        if (pipe.override_reactions && std::isfinite(pipe.bulk_reaction.order) && std::abs(pipe.bulk_reaction.order - reactions.global_pipe_bulk_reaction.order) > 1.0e-12)
            failures.append(validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe bulk reaction order must match the network-wide EPANET bulk reaction order")));
        if (pipe.override_reactions && std::isfinite(pipe.wall_reaction.order) && std::abs(pipe.wall_reaction.order - reactions.global_pipe_wall_reaction.order) > 1.0e-12)
            failures.append(validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe wall reaction order must match the network-wide EPANET wall reaction order")));
        const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
        status = validateFinitePositive(length_m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("length_m"));
        appendValidationFailure(failures, status);
        status = validateFinitePositive(pipe.diameter_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("diameter_mm"));
        appendValidationFailure(failures, status);
        if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::HazenWilliams)
            status = validateFinite(pipe.roughness_hazen_williams, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_hazen_williams"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach)
            status = validateFinite(pipe.roughness_darcy_weisbach_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_darcy_weisbach_mm"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::ChezyManning)
            status = validateFinite(pipe.roughness_chezy_manning, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_chezy_manning"));
        else
            status = makeEpanetSuccess();
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(pipe.minor_loss_coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("minor_loss_coefficient"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(pipe.leak_area_mm2_per_100m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_area_mm2_per_100m"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(pipe.leak_area_expansion_per_pressure_head_mm2_per_m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_area_expansion_per_pressure_head_mm2_per_m"));
        appendValidationFailure(failures, status);
        for (int index = 0; index < pipe.vertices.size(); index++)
        {
            status = validateLongitudeDeg(pipe.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            appendValidationFailure(failures, status);
            status = validateLatitudeDeg(pipe.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (!pump.metadata.enabled)
            continue;
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = validateFinitePositive(pump.constant_power_kw, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_power_kw"));
            appendValidationFailure(failures, status);
        }
        status = validateFiniteNonNegative(pump.initial_speed_ratio, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("initial_speed_ratio"));
        appendValidationFailure(failures, status);
        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Constant)
        {
            status = validateFinitePositive(pump.constant_efficiency_percent, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_efficiency_percent"));
            appendValidationFailure(failures, status);
        }
        if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
        {
            status = validateFinite(pump.energy_price_per_kw_h, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy_price_per_kw_h"));
            appendValidationFailure(failures, status);
        }
        for (int index = 0; index < pump.vertices.size(); index++)
        {
            status = validateLongitudeDeg(pump.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            appendValidationFailure(failures, status);
            status = validateLatitudeDeg(pump.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (!valve.metadata.enabled)
            continue;
        status = validateFinitePositive(valve.diameter_mm, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("diameter_mm"));
        appendValidationFailure(failures, status);
        status = validateFiniteNonNegative(valve.minor_loss_coefficient, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("minor_loss_coefficient"));
        appendValidationFailure(failures, status);
        switch (valve.type)
        {
        case HydraulicLinkValveType::PRV:
        case HydraulicLinkValveType::PSV:
        case HydraulicLinkValveType::PBV:
            status = validateFinite(valve.setting_pressure_head_m, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_pressure_head_m"));
            appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::FCV:
            status = validateFinite(valve.setting_flow_m3_per_h, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_flow_m3_per_h"));
            appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::TCV:
            status = validateFinite(valve.setting_loss_coefficient, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_loss_coefficient"));
            appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::PCV:
            status = validateFinite(valve.setting_position_percent, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_position_percent"));
            appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::GPV:
            break;
        }
        for (int index = 0; index < valve.vertices.size(); index++)
        {
            status = validateLongitudeDeg(valve.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            appendValidationFailure(failures, status);
            status = validateLatitudeDeg(valve.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            appendValidationFailure(failures, status);
        }
    }

    const std::function<void(const HydraulicControlLinkSetting &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &, const QString &)> validate_control_setting = [&failures](const HydraulicControlLinkSetting &setting, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid, const QString &prefix)
    {
        const QList<QPair<QString, std::optional<double>>> values = {
            {QStringLiteral("pump_speed_ratio"), setting.pump_speed_ratio},
            {QStringLiteral("valve_pressure_head_m"), setting.valve_pressure_head_m},
            {QStringLiteral("valve_flow_m3_per_h"), setting.valve_flow_m3_per_h},
            {QStringLiteral("valve_loss_coefficient"), setting.valve_loss_coefficient},
            {QStringLiteral("valve_position_percent"), setting.valve_position_percent}
        };
        for (const QPair<QString, std::optional<double>> &entry : values)
        {
            if (!entry.second.has_value())
                continue;
            HydraulicSimulationStatus setting_status = validateFinite(entry.second.value(), entity_type, id, uuid, prefix + entry.first);
            appendValidationFailure(failures, setting_status);
        }
    };

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        if (control.action == HydraulicControlActionType::Setting)
            validate_control_setting(control.setting, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("setting."));
        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = validateFinite(control.trigger_water_level_m, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger_water_level_m"));
            appendValidationFailure(failures, status);
            status = validateFinite(control.trigger_pressure_head_m, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger_pressure_head_m"));
            appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        status = validateFinite(rule.priority, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("priority"));
        appendValidationFailure(failures, status);

        if (!rule.source_text.isEmpty())
            continue;

        for (int index = 0; index < rule.premises.size(); index++)
        {
            const HydraulicControlRulePremise &premise = rule.premises.at(index);
            const QList<QPair<QString, std::optional<double>>> values = {
                {QStringLiteral("demand_m3_per_h"), premise.demand_m3_per_h},
                {QStringLiteral("hydraulic_head_m"), premise.hydraulic_head_m},
                {QStringLiteral("water_level_m"), premise.water_level_m},
                {QStringLiteral("pressure_head_m"), premise.pressure_head_m},
                {QStringLiteral("flow_m3_per_h"), premise.flow_m3_per_h},
                {QStringLiteral("power_kw"), premise.power_kw}
            };
            for (const QPair<QString, std::optional<double>> &entry : values)
            {
                if (!entry.second.has_value())
                    continue;
                status = validateFinite(entry.second.value(), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premises[%1].").arg(index) + entry.first);
                appendValidationFailure(failures, status);
            }
            validate_control_setting(premise.link_setting, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premises[%1].link_setting.").arg(index));
        }

        const QList<QList<HydraulicControlRuleAction>> action_groups = {rule.actions_then, rule.actions_else};
        for (const QList<HydraulicControlRuleAction> &actions : action_groups)
        {
            for (const HydraulicControlRuleAction &action : actions)
                validate_control_setting(action.setting, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("action.setting."));
        }
    }

    return failures;
}

HydraulicSimulationStatus validateNetwork(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    QList<HydraulicSimulationStatus> failures = validateIdentities(network);
    appendValidationFailures(failures, validateReferences(network));
    appendValidationFailures(failures, validateNumerics(network));

    if (validation_failures != nullptr)
        *validation_failures = failures;

    return failures.isEmpty() ? makeEpanetSuccess() : failures.first();
}
}

HydraulicSimulationStatus validateEpanetNetwork(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    return validateNetwork(network, validation_failures);
}
