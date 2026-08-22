#include "epanet_network_validator_parts.h"
#include "epanet_network_validator_support.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>
#include <functional>
#include <optional>

namespace EpanetNetworkValidatorParts
{
QList<HydraulicSimulationStatus> validateReferences(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    const QSet<QUuid> all_nodes = EpanetNetworkValidatorSupport::nodeUuids(network);
    const QSet<QUuid> all_links = EpanetNetworkValidatorSupport::linkUuids(network);

    NetworkHydraulic enabled_network = network;
    enabled_network.nodes_junctions = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_junctions);
    enabled_network.nodes_reservoirs = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_reservoirs);
    enabled_network.nodes_tanks = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_tanks);
    enabled_network.links_pipes = EpanetNetworkValidatorSupport::enabledEntities(network.links_pipes);
    enabled_network.links_pumps = EpanetNetworkValidatorSupport::enabledEntities(network.links_pumps);
    enabled_network.links_valves = EpanetNetworkValidatorSupport::enabledEntities(network.links_valves);

    const QSet<QUuid> enabled_nodes = EpanetNetworkValidatorSupport::nodeUuids(enabled_network);
    const QSet<QUuid> enabled_links = EpanetNetworkValidatorSupport::linkUuids(enabled_network);
    const QSet<QUuid> patterns = EpanetNetworkValidatorSupport::entityUuids(network.patterns_time);
    const QSet<QUuid> tank_curves = EpanetNetworkValidatorSupport::entityUuids(network.curves_tank_volume);
    const QSet<QUuid> pump_head_curves = EpanetNetworkValidatorSupport::entityUuids(network.curves_pump_head);
    const QSet<QUuid> pump_efficiency_curves = EpanetNetworkValidatorSupport::entityUuids(network.curves_pump_efficiency);
    const QSet<QUuid> valve_headloss_curves = EpanetNetworkValidatorSupport::entityUuids(network.curves_valve_headloss);
    const QSet<QUuid> valve_characteristic_curves = EpanetNetworkValidatorSupport::entityUuids(network.curves_valve_characteristic);

    HydraulicSimulationStatus status;

    const std::function<void(const HydraulicNodeQualitySource &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> validate_quality_source_pattern = [&failures, &patterns](const HydraulicNodeQualitySource &source, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        if (source.type == HydraulicNodeQualitySourceType::None || source.pattern_uuid.isNull())
            return;
        const HydraulicSimulationStatus source_pattern_status = EpanetNetworkValidatorSupport::validatePatternReference(patterns, source.pattern_uuid, entity_type, id, uuid, QStringLiteral("quality-source pattern"), true);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, source_pattern_status);
    };

    for (const HydraulicNodeJunction &junction : enabled_network.nodes_junctions)
    {
        validate_quality_source_pattern(junction.quality_source, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid);
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
        {
            if (demand.pattern_mode != HydraulicTimePatternMode::TimePattern)
                continue;
            status = EpanetNetworkValidatorSupport::validatePatternReference(patterns, demand.pattern_uuid, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demand pattern"), true);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicNodeReservoir &reservoir : enabled_network.nodes_reservoirs)
    {
        validate_quality_source_pattern(reservoir.quality_source, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid);
        if (reservoir.head_pattern_mode != HydraulicTimePatternMode::TimePattern)
            continue;
        status = EpanetNetworkValidatorSupport::validatePatternReference(patterns, reservoir.head_pattern_uuid, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("head pattern"), false);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeTank &tank : enabled_network.nodes_tanks)
    {
        validate_quality_source_pattern(tank.quality_source, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid);
        if (tank.geometry_input_type != HydraulicNodeTankGeometryInputType::VolumeCurve)
            continue;
        status = EpanetNetworkValidatorSupport::validateCurveReference(tank_curves, tank.volume_curve_uuid, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("tank volume curve"), false);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPipe &pipe : enabled_network.links_pipes)
    {
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, pipe.node_uuid_from, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("start node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, pipe.node_uuid_to, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("end node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPump &pump : enabled_network.links_pumps)
    {
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, pump.node_uuid_from, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("start node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, pump.node_uuid_to, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("end node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        if (pump.definition_type != HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = EpanetNetworkValidatorSupport::validateCurveReference(pump_head_curves, pump.head_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("head curve"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }

        if (!pump.speed_pattern_uuid.isNull())
        {
            status = EpanetNetworkValidatorSupport::validatePatternReference(patterns, pump.speed_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("speed pattern"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }

        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve)
        {
            status = EpanetNetworkValidatorSupport::validateCurveReference(pump_efficiency_curves, pump.efficiency_curve_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("efficiency curve"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }

        if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
        {
            status = EpanetNetworkValidatorSupport::validatePatternReference(patterns, pump.price_pattern_uuid, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy-price pattern"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkValve &valve : enabled_network.links_valves)
    {
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, valve.node_uuid_from, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("start node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, valve.node_uuid_to, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("end node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        if (valve.type == HydraulicLinkValveType::GPV)
        {
            status = EpanetNetworkValidatorSupport::validateCurveReference(valve_headloss_curves, valve.head_loss_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("GPV head-loss curve"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        if (valve.type == HydraulicLinkValveType::PCV && !valve.characteristic_curve_uuid.isNull())
        {
            status = EpanetNetworkValidatorSupport::validateCurveReference(valve_characteristic_curves, valve.characteristic_curve_uuid, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV characteristic curve"), false);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    if (!network.options_hydraulic.default_demand_pattern_uuid.isNull())
    {
        status = EpanetNetworkValidatorSupport::validatePatternReference(
            patterns,
            network.options_hydraulic.default_demand_pattern_uuid,
            HydraulicSimulationStatusEntityType::Pattern,
            QStringLiteral("default-demand-pattern"),
            network.options_hydraulic.default_demand_pattern_uuid,
            QStringLiteral("default demand pattern"),
            false);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    if (!network.options_energy.global_energy_price_pattern_uuid.isNull())
    {
        status = EpanetNetworkValidatorSupport::validatePatternReference(
            patterns,
            network.options_energy.global_energy_price_pattern_uuid,
            HydraulicSimulationStatusEntityType::Pattern,
            QStringLiteral("global-energy-price-pattern"),
            network.options_energy.global_energy_price_pattern_uuid,
            QStringLiteral("global energy-price pattern"),
            false);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicMapLabel &label : network.map_labels)
    {
        if (label.anchor_node_uuid.isNull())
            continue;
        status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, label.anchor_node_uuid, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map-label anchor node"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        status = EpanetNetworkValidatorSupport::validateReference(all_links, enabled_links, control.link_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("controlled link"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, control.trigger_node_uuid, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger node"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        for (const HydraulicControlRulePremise &premise : rule.premises)
        {
            if (premise.object == HydraulicControlRuleObject::Node)
            {
                status = EpanetNetworkValidatorSupport::validateReference(all_nodes, enabled_nodes, premise.object_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premise node"));
                EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            }
            else if (premise.object == HydraulicControlRuleObject::Link)
            {
                status = EpanetNetworkValidatorSupport::validateReference(all_links, enabled_links, premise.object_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premise link"));
                EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            }
        }

        for (const HydraulicControlRuleAction &action : rule.actions_then)
        {
            status = EpanetNetworkValidatorSupport::validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("THEN-action link"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        for (const HydraulicControlRuleAction &action : rule.actions_else)
        {
            status = EpanetNetworkValidatorSupport::validateReference(all_links, enabled_links, action.link_uuid, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("ELSE-action link"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    return failures;
}
}
