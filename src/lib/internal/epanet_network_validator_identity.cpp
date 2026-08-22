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
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"), entity.id, entity.uuid);
    }
    for (const HydraulicNodeReservoir &entity : network.nodes_reservoirs)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"), entity.id, entity.uuid);
    }
    for (const HydraulicNodeTank &entity : network.nodes_tanks)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, node_ids, QStringLiteral("node"), HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkPipe &entity : network.links_pipes)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkPump &entity : network.links_pumps)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Pump, QStringLiteral("Pump"), entity.id, entity.uuid);
    }
    for (const HydraulicLinkValve &entity : network.links_valves)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, link_ids, QStringLiteral("link"), HydraulicSimulationStatusEntityType::Valve, QStringLiteral("Valve"), entity.id, entity.uuid);
    }
    for (const HydraulicPatternTime &entity : network.patterns_time)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, pattern_ids, QStringLiteral("pattern"), HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveTankVolume &entity : network.curves_tank_volume)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurvePumpHead &entity : network.curves_pump_head)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump head curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurvePumpEfficiency &entity : network.curves_pump_efficiency)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump efficiency curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveValveHeadloss &entity : network.curves_valve_headloss)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve head-loss curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveValveCharacteristic &entity : network.curves_valve_characteristic)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve characteristic curve"), entity.id, entity.uuid);
    }
    for (const HydraulicCurveGeneric &entity : network.curves_generic)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, curve_ids, QStringLiteral("curve"), HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Generic curve"), entity.id, entity.uuid);
    }
    for (const HydraulicControlSimple &entity : network.controls_simple)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, simple_control_ids, QStringLiteral("simple-control"), HydraulicSimulationStatusEntityType::Control, QStringLiteral("Simple control"), entity.id, entity.uuid);
    }
    for (const HydraulicControlRule &entity : network.controls_rules)
    {
        EpanetNetworkValidatorSupport::collectIdentityFailures(failures, all_uuids, rule_ids, QStringLiteral("rule"), HydraulicSimulationStatusEntityType::Rule, QStringLiteral("Control rule"), entity.id, entity.uuid);
    }

    return failures;
}
}
