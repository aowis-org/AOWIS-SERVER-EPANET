#include "epanet_network_validator.h"
#include "epanet_status_helpers.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
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

HydraulicSimulationStatus registerIdentity(
    QSet<QUuid> &all_uuids,
    QSet<QString> &ids,
    const QString &namespace_name,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_name,
    const QString &id,
    const QUuid &uuid)
{
    if (uuid.isNull())
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no UUID").arg(entity_name));

    if (id.isEmpty())
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no ID").arg(entity_name));

    if (all_uuids.contains(uuid))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 UUID is duplicated across the hydraulic model").arg(entity_name),
            {QStringLiteral("Duplicate UUID: %1").arg(uuid.toString(QUuid::WithoutBraces))});

    if (ids.contains(id))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 ID is duplicated in the EPANET %2 namespace").arg(entity_name, namespace_name),
            {QStringLiteral("Duplicate ID: %1").arg(id)});

    all_uuids.insert(uuid);
    ids.insert(id);
    return makeEpanetSuccess();
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

HydraulicSimulationStatus validateIdentities(const NetworkHydraulic &network)
{
    QSet<QUuid> all_uuids;
    QSet<QString> node_ids;
    QSet<QString> link_ids;
    QSet<QString> pattern_ids;
    QSet<QString> curve_ids;
    QSet<QString> simple_control_ids;
    QSet<QString> rule_ids;

    HydraulicSimulationStatus status;

    for (const HydraulicNodeJunction &entity : network.nodes_junctions)
    {
        status = registerIdentity(all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicNodeReservoir &entity : network.nodes_reservoirs)
    {
        status = registerIdentity(all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicNodeTank &entity : network.nodes_tanks)
    {
        status = registerIdentity(all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicLinkPipe &entity : network.links_pipes)
    {
        status = registerIdentity(all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicLinkPump &entity : network.links_pumps)
    {
        status = registerIdentity(all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pump, QStringLiteral("Pump"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicLinkValve &entity : network.links_valves)
    {
        status = registerIdentity(all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Valve, QStringLiteral("Valve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicPatternTime &entity : network.patterns_time)
    {
        status = registerIdentity(all_uuids, pattern_ids, QStringLiteral("pattern"), HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurveTankVolume &entity : network.curves_tank_volume)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurvePumpHead &entity : network.curves_pump_head)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump head curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurvePumpEfficiency &entity : network.curves_pump_efficiency)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump efficiency curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurveValveHeadloss &entity : network.curves_valve_headloss)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve head-loss curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurveValveCharacteristic &entity : network.curves_valve_characteristic)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve characteristic curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicCurveGeneric &entity : network.curves_generic)
    {
        status = registerIdentity(all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Generic curve"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicControlSimple &entity : network.controls_simple)
    {
        status = registerIdentity(all_uuids, simple_control_ids, QStringLiteral("simple-control"), HydraulicSimulationStatusEntityType::Control, QStringLiteral("Simple control"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }
    for (const HydraulicControlRule &entity : network.controls_rules)
    {
        status = registerIdentity(all_uuids, rule_ids, QStringLiteral("rule"), HydraulicSimulationStatusEntityType::Rule, QStringLiteral("Control rule"), entity.id, entity.uuid);
        if (!status.success)
            return status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateReferences(const NetworkHydraulic &network)
{
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

    for (const HydraulicNodeJunction &junction : enabled_network.nodes_junctions)
    {
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
        {
            if (demand.pattern_mode != HydraulicTimePatternMode::TimePattern)
                continue;
            status = validatePatternReference(patterns, demand.pattern_uuid, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demand pattern"), true);
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicNodeReservoir &reservoir : enabled_network.nodes_reservoirs)
    {
        if (reservoir.head_pattern_mode != HydraulicTimePatternMode::TimePattern)
            continue;
        status = validatePatternReference(patterns, reservoir.head_pattern_uuid, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("head pattern"), false);
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeTank &tank : enabled_network.nodes_tanks)
    {
        if (tank.geometry_input_type != HydraulicNodeTankGeometryInputType::VolumeCurve)
            continue;
        status = validateCurveReference(tank_curves, tank.volume_curve_uuid, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("tank volume curve"), false);
        if (!status.success)
            return status;
    }

    for (const HydraulicLinkPipe &pipe : enabled_network.links_pipes)
    {
        status = validateReference(all_nodes, enabled_nodes, pipe.node_uuid_from, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("start node"));
        if (!status.success)
            return status;
        status = validateReference(all_nodes, enabled_nodes, pipe.node_uuid_to, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("end node"));
        if (!status.success)
            return status;
    }

    for (const HydraulicLinkPump &pump : enabled_network.links_pumps)
    {
        status = validateReference(all_nodes, enabled_nodes, pump.node_uuid_from, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("start node"));
        if (!status.success)
            return status;
        status = validateReference(all_nodes, enabled_nodes, pump.node_uuid_to, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("end node"));
        if (!status.success)
            return status;

        if (pump.definition_type != HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = validateCurveReference(pump_head_curves, pump.head_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("head curve"), false);
            if (!status.success)
                return status;
        }

        if (!pump.speed_pattern_uuid.isNull())
        {
            status = validatePatternReference(patterns, pump.speed_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("speed pattern"), false);
            if (!status.success)
                return status;
        }

        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve)
        {
            status = validateCurveReference(pump_efficiency_curves, pump.efficiency_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("efficiency curve"), false);
            if (!status.success)
                return status;
        }

        if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
        {
            status = validatePatternReference(patterns, pump.price_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy-price pattern"), false);
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicLinkValve &valve : enabled_network.links_valves)
    {
        status = validateReference(all_nodes, enabled_nodes, valve.node_uuid_from, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("start node"));
        if (!status.success)
            return status;
        status = validateReference(all_nodes, enabled_nodes, valve.node_uuid_to, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("end node"));
        if (!status.success)
            return status;

        if (valve.type == HydraulicLinkValveType::GPV)
        {
            status = validateCurveReference(valve_headloss_curves, valve.setting_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("GPV head-loss curve"), false);
            if (!status.success)
                return status;
        }
        if (valve.type == HydraulicLinkValveType::PCV && !valve.setting_curve_uuid.isNull())
        {
            status = validateCurveReference(valve_characteristic_curves, valve.setting_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV characteristic curve"), false);
            if (!status.success)
                return status;
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
        if (!status.success)
            return status;
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
        if (!status.success)
            return status;
    }

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        status = validateReference(all_links, enabled_links, control.link_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("controlled link"));
        if (!status.success)
            return status;

        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = validateReference(all_nodes, enabled_nodes, control.trigger_node_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger node"));
            if (!status.success)
                return status;
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
                if (!status.success)
                    return status;
            }
            else if (premise.object == HydraulicControlRuleObject::Link)
            {
                status = validateReference(all_links, enabled_links, premise.object_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premise link"));
                if (!status.success)
                    return status;
            }
        }

        for (const HydraulicControlRuleAction &action : rule.actions_then)
        {
            status = validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("THEN-action link"));
            if (!status.success)
                return status;
        }
        for (const HydraulicControlRuleAction &action : rule.actions_else)
        {
            status = validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("ELSE-action link"));
            if (!status.success)
                return status;
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateReportField(
    const HydraulicSimulationReportField &field,
    const QString &field_name,
    const NetworkHydraulic &network)
{
    if (field.below.has_value() && !std::isfinite(field.below.value()))
        return invalidNumeric(
            HydraulicSimulationStatusEntityType::Report,
            network.id,
            network.uuid,
            field_name + QStringLiteral(".below"),
            QStringLiteral("must be finite when configured"));

    if (field.above.has_value() && !std::isfinite(field.above.value()))
        return invalidNumeric(
            HydraulicSimulationStatusEntityType::Report,
            network.id,
            network.uuid,
            field_name + QStringLiteral(".above"),
            QStringLiteral("must be finite when configured"));

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateNumerics(const NetworkHydraulic &network)
{
    HydraulicSimulationStatus status;

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
        {QStringLiteral("emitter_exponent"), hydraulic.emitter_exponent},
        {QStringLiteral("specific_gravity"), hydraulic.specific_gravity},
        {QStringLiteral("relative_viscosity"), hydraulic.relative_viscosity}
    };
    for (const QPair<QString, double> &field : hydraulic_values)
    {
        status = validateFinite(field.second, HydraulicSimulationStatusEntityType::HydraulicSolver, network.id, network.uuid, field.first);
        if (!status.success)
            return status;
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
        if (!status.success)
            return status;
    }

    struct NamedReportField
    {
        const char *name;
        const HydraulicSimulationReportField *field;
    };

    const std::array<NamedReportField, 14> report_fields = {{
        {"fields_node.elevation", &network.options_report.fields_node.elevation},
        {"fields_node.demand", &network.options_report.fields_node.demand},
        {"fields_node.head", &network.options_report.fields_node.head},
        {"fields_node.pressure", &network.options_report.fields_node.pressure},
        {"fields_node.quality", &network.options_report.fields_node.quality},
        {"fields_link.length", &network.options_report.fields_link.length},
        {"fields_link.diameter", &network.options_report.fields_link.diameter},
        {"fields_link.flow", &network.options_report.fields_link.flow},
        {"fields_link.velocity", &network.options_report.fields_link.velocity},
        {"fields_link.headloss", &network.options_report.fields_link.headloss},
        {"fields_link.position", &network.options_report.fields_link.position},
        {"fields_link.setting", &network.options_report.fields_link.setting},
        {"fields_link.reaction", &network.options_report.fields_link.reaction},
        {"fields_link.friction", &network.options_report.fields_link.friction}
    }};

    for (const NamedReportField &report_field : report_fields)
    {
        status = validateReportField(*report_field.field, QString::fromLatin1(report_field.name), network);
        if (!status.success)
            return status;
    }

    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        for (int index = 0; index < pattern.factors.size(); index++)
        {
            status = validateFinite(pattern.factors.at(index), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("factors[%1]").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).water_level_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].water_level_m").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).volume_m3, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].volume_m3").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).head_gain_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_gain_m").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).efficiency_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].efficiency_percent").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).head_loss_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_loss_m").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).position_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].position_percent").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).relative_flow_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].relative_flow_percent").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicCurveGeneric &curve : network.curves_generic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = validateFinite(curve.points.at(index).x, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].x").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(curve.points.at(index).y, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].y").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (!junction.metadata.enabled)
            continue;
        if (junction.elevation_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(junction.terrain_elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("terrain_elevation_m"));
            if (!status.success)
                return status;
            status = validateFinite(junction.elevation_offset_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_offset_m"));
            if (!status.success)
                return status;
        }
        else
        {
            status = validateFinite(junction.elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_m"));
            if (!status.success)
                return status;
        }
        status = validateFiniteNonNegative(junction.emitter_coefficient_m3_per_h_per_m_exponent, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("emitter_coefficient_m3_per_h_per_m_exponent"));
        if (!status.success)
            return status;
        status = validateFinite(junction.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        if (!status.success)
            return status;
        status = validateFinite(junction.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        if (!status.success)
            return status;

        for (int index = 0; index < junction.demands.size(); index++)
        {
            status = validateFinite(junction.demands.at(index).base_demand_m3_per_h, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demands[%1].base_demand_m3_per_h").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (!reservoir.metadata.enabled)
            continue;
        if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(reservoir.terrain_elevation_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("terrain_elevation_m"));
            if (!status.success)
                return status;
            status = validateFinite(reservoir.head_offset_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("head_offset_m"));
            if (!status.success)
                return status;
        }
        else
        {
            status = validateFinite(reservoir.head_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("head_m"));
            if (!status.success)
                return status;
        }
        status = validateFinite(reservoir.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        if (!status.success)
            return status;
        status = validateFinite(reservoir.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (!tank.metadata.enabled)
            continue;

        if (tank.elevation_input_type == HydraulicNodeTankElevationInputType::TerrainElevationAndOffset)
        {
            status = validateFinite(tank.terrain_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("terrain_elevation_m"));
            if (!status.success)
                return status;
            status = validateFinite(tank.bottom_offset_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_offset_m"));
            if (!status.success)
                return status;
        }
        else
        {
            status = validateFinite(tank.bottom_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_elevation_m"));
            if (!status.success)
                return status;
        }

        status = validateFinite(tank.water_level_initial_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_initial_m"));
        if (!status.success)
            return status;
        status = validateFinite(tank.water_level_minimum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_minimum_m"));
        if (!status.success)
            return status;
        status = validateFinite(tank.water_level_maximum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_maximum_m"));
        if (!status.success)
            return status;
        status = validateFiniteNonNegative(tank.minimum_volume_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("minimum_volume_m3"));
        if (!status.success)
            return status;

        if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::Cylindrical)
            status = validateFinitePositive(tank.diameter_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("diameter_m"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::UniformArea)
            status = validateFinitePositive(tank.cross_section_area_m2, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("cross_section_area_m2"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel)
            status = validateFinitePositive(tank.volume_at_maximum_level_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("volume_at_maximum_level_m3"));
        else
            status = makeEpanetSuccess();
        if (!status.success)
            return status;

        status = validateFinite(tank.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        if (!status.success)
            return status;
        status = validateFinite(tank.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        if (!status.success)
            return status;
    }

    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (!pipe.metadata.enabled)
            continue;
        const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
        status = validateFinitePositive(length_m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("length_m"));
        if (!status.success)
            return status;
        status = validateFinitePositive(pipe.diameter_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("diameter_mm"));
        if (!status.success)
            return status;
        if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::HazenWilliams)
            status = validateFinite(pipe.roughness_hw, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_hw"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach)
            status = validateFinite(pipe.roughness_dw_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_dw_mm"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::ChezyManning)
            status = validateFinite(pipe.roughness_cm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_cm"));
        else
            status = makeEpanetSuccess();
        if (!status.success)
            return status;
        status = validateFiniteNonNegative(pipe.minor_loss, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("minor_loss"));
        if (!status.success)
            return status;
        status = validateFiniteNonNegative(pipe.leak_area_mm2_per_100m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_area_mm2_per_100m"));
        if (!status.success)
            return status;
        status = validateFiniteNonNegative(pipe.leak_expansion_mm2_per_m_head, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_expansion_mm2_per_m_head"));
        if (!status.success)
            return status;
        for (int index = 0; index < pipe.vertices.size(); index++)
        {
            status = validateFinite(pipe.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(pipe.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (!pump.metadata.enabled)
            continue;
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = validateFinitePositive(pump.constant_power_kw, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_power_kw"));
            if (!status.success)
                return status;
        }
        status = validateFiniteNonNegative(pump.initial_speed, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("initial_speed"));
        if (!status.success)
            return status;
        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Constant)
        {
            status = validateFinitePositive(pump.constant_efficiency_percent, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_efficiency_percent"));
            if (!status.success)
                return status;
        }
        if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
        {
            status = validateFinite(pump.energy_price_per_kw_h, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy_price_per_kw_h"));
            if (!status.success)
                return status;
        }
        for (int index = 0; index < pump.vertices.size(); index++)
        {
            status = validateFinite(pump.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(pump.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (!valve.metadata.enabled)
            continue;
        status = validateFinitePositive(valve.diameter_mm, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("diameter_mm"));
        if (!status.success)
            return status;
        status = validateFiniteNonNegative(valve.minor_loss, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("minor_loss"));
        if (!status.success)
            return status;
        if (valve.type != HydraulicLinkValveType::GPV)
        {
            status = validateFinite(valve.setting, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting"));
            if (!status.success)
                return status;
        }
        for (int index = 0; index < valve.vertices.size(); index++)
        {
            status = validateFinite(valve.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            if (!status.success)
                return status;
            status = validateFinite(valve.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        if (control.action == HydraulicControlActionType::Setting)
        {
            status = validateFinite(control.setting, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("setting"));
            if (!status.success)
                return status;
        }
        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = validateFinite(control.trigger_level_or_pressure_head_m, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger_level_or_pressure_head_m"));
            if (!status.success)
                return status;
        }
    }

    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        status = validateFinite(rule.priority, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("priority"));
        if (!status.success)
            return status;

        if (!rule.source_text.isEmpty())
            continue;

        for (int index = 0; index < rule.premises.size(); index++)
        {
            const HydraulicControlRulePremise &premise = rule.premises.at(index);
            if (premise.value.has_value())
            {
                status = validateFinite(premise.value.value(), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premises[%1].value").arg(index));
                if (!status.success)
                    return status;
            }
        }

        const QList<QList<HydraulicControlRuleAction>> action_groups = {rule.actions_then, rule.actions_else};
        for (const QList<HydraulicControlRuleAction> &actions : action_groups)
        {
            for (const HydraulicControlRuleAction &action : actions)
            {
                if (action.setting.has_value())
                {
                    status = validateFinite(action.setting.value(), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("action.setting"));
                    if (!status.success)
                        return status;
                }
            }
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validateNetwork(const NetworkHydraulic &network)
{
    HydraulicSimulationStatus status = validateIdentities(network);
    if (!status.success)
        return status;

    status = validateReferences(network);
    if (!status.success)
        return status;

    return validateNumerics(network);
}
}

HydraulicSimulationStatus validateEpanetNetwork(const NetworkHydraulic &network)
{
    return validateNetwork(network);
}
