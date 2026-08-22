#include "epanet_network_builder.h"

#include "epanet_index_registry.h"
#include "epanet_network_builder_support.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
template<typename Entity>
HydraulicSimulationStatus rebuildNodeIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getnodeindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getnodeindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET node reindexing").arg(entity_name));
            if (!epanet_status.success)
            {
                EpanetNetworkBuilderSupport::collectBuildFailure(project, epanet_status, first_failure);
                continue;
            }
        }

        indices.insert(entity.uuid, entity_index);
    }

    return first_failure;
}

template<typename Entity>
HydraulicSimulationStatus rebuildLinkIndices(EpanetProject &project, const QList<Entity> &entities, QHash<QUuid, int> &indices, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusEntityType entity_type, const QString &entity_name)
{
    indices.clear();
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    for (const Entity &entity : entities)
    {
        const QByteArray entity_id = entity.id.toUtf8();
        int entity_index = 0;
        const int error = EN_getlinkindex(project.handle(), entity_id.constData(), &entity_index);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, stage, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getlinkindex"), entity_type, entity.id, entity.uuid, QStringLiteral("Failed to rebuild %1 index after EPANET link reindexing").arg(entity_name));
            if (!epanet_status.success)
            {
                EpanetNetworkBuilderSupport::collectBuildFailure(project, epanet_status, first_failure);
                continue;
            }
        }

        indices.insert(entity.uuid, entity_index);
    }

    return first_failure;
}
}

EpanetNetworkBuilder::EpanetNetworkBuilder(EpanetProject &project, EpanetIndexRegistry &indices)
    : project(project), indices(indices)
{
}

HydraulicSimulationStatus EpanetNetworkBuilder::build(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::validateSupportedFeatures(request);
    EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);

    this->node_ids_by_uuid.clear();
    this->pattern_ids_by_uuid.clear();
    this->tank_volume_curve_ids_by_uuid.clear();
    this->pump_head_curve_ids_by_uuid.clear();
    this->pump_head_curve_point_counts_by_uuid.clear();
    this->pump_efficiency_curve_ids_by_uuid.clear();
    this->valve_headloss_curve_ids_by_uuid.clear();
    this->valve_characteristic_curve_ids_by_uuid.clear();
    this->generic_curve_ids_by_uuid.clear();
    this->pipe_ids_by_uuid.clear();
    this->pump_ids_by_uuid.clear();
    this->valve_ids_by_uuid.clear();
    this->valve_types_by_uuid.clear();
    this->control_simple_ids_by_uuid.clear();
    this->control_rule_ids_by_uuid.clear();
    this->constant_demand_pattern_id.clear();

    this->indices.patterns_time.clear();
    this->indices.curves_tank_volume.clear();
    this->indices.curves_pump_head.clear();
    this->indices.curves_pump_efficiency.clear();
    this->indices.curves_valve_headloss.clear();
    this->indices.curves_valve_characteristic.clear();
    this->indices.curves_generic.clear();
    this->indices.nodes_reservoirs.clear();
    this->indices.nodes_junctions.clear();
    this->indices.nodes_tanks.clear();
    this->indices.links_pipes.clear();
    this->indices.links_pumps.clear();
    this->indices.links_valves.clear();
    this->indices.controls_simple.clear();
    this->indices.controls_rules.clear();

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->node_ids_by_uuid, junction.uuid, junction.id, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("Junction"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->node_ids_by_uuid, reservoir.uuid, reservoir.id, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("Reservoir"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->node_ids_by_uuid, tank.uuid, tank.id, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("Tank"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->pattern_ids_by_uuid, pattern.uuid, pattern.id, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusEntityType::Pattern, QStringLiteral("Time pattern"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->tank_volume_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Tank volume curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpHead &curve : request.curves_pump_head)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->pump_head_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump head curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
        if (status.success)
            this->pump_head_curve_point_counts_by_uuid.insert(curve.uuid, static_cast<int>(curve.points.size()));
    }

    for (const HydraulicCurvePumpEfficiency &curve : request.curves_pump_efficiency)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->pump_efficiency_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Pump efficiency curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveHeadloss &curve : request.curves_valve_headloss)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->valve_headloss_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve head-loss curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveCharacteristic &curve : request.curves_valve_characteristic)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->valve_characteristic_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Valve characteristic curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveGeneric &curve : request.curves_generic)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->generic_curve_ids_by_uuid, curve.uuid, curve.id, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, QStringLiteral("Generic curve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->pipe_ids_by_uuid, pipe.uuid, pipe.id, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("Pipe"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->pump_ids_by_uuid, pump.uuid, pump.id, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, QStringLiteral("Pump"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->valve_ids_by_uuid, valve.uuid, valve.id, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, QStringLiteral("Valve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
        if (status.success)
            this->valve_types_by_uuid.insert(valve.uuid, valve.type);
    }

    for (const HydraulicControlSimple &control : request.controls_simple)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->control_simple_ids_by_uuid, control.uuid, control.id, HydraulicSimulationStatusStage::AddControl, HydraulicSimulationStatusEntityType::Control, QStringLiteral("Simple control"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlRule &rule : request.controls_rules)
    {
        status = EpanetNetworkBuilderSupport::registerBackendId(this->control_rule_ids_by_uuid, rule.uuid, rule.id, HydraulicSimulationStatusStage::AddRule, HydraulicSimulationStatusEntityType::Rule, QStringLiteral("Control rule"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicPatternTime &pattern : request.patterns_time)
    {
        status = addPatternTime(pattern);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    status = configureConstantDemandPattern(request);
    EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);

    status = configureDefaultDemandPattern(request);
    EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);

    status = configureGlobalEnergyPattern(request);
    EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);

    for (const HydraulicCurveTankVolume &curve : request.curves_tank_volume)
    {
        status = addCurveTankVolume(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpHead &curve : request.curves_pump_head)
    {
        status = addCurvePumpHead(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurvePumpEfficiency &curve : request.curves_pump_efficiency)
    {
        status = addCurvePumpEfficiency(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveHeadloss &curve : request.curves_valve_headloss)
    {
        status = addCurveValveHeadloss(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveValveCharacteristic &curve : request.curves_valve_characteristic)
    {
        status = addCurveValveCharacteristic(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicCurveGeneric &curve : request.curves_generic)
    {
        status = addCurveGeneric(curve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    // EPANET keeps all junctions before tanks and reservoirs. Adding a junction after
    // a reservoir or tank can therefore change previously returned node indices.
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        status = addNodeJunction(junction);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        status = addNodeReservoir(reservoir);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        status = addNodeTank(tank);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    status = refreshNodeIndices(request);
    if (!status.success && first_failure.success)
        first_failure = status;

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        status = addLinkPipe(pipe);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        status = addLinkPump(pump);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        status = addLinkValve(valve);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    status = refreshLinkIndices(request);
    if (!status.success && first_failure.success)
        first_failure = status;

    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        if (!this->indices.nodes_junctions.contains(junction.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_junctions.value(junction.uuid), junction.metadata, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("junction"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicNodeReservoir &reservoir : request.nodes_reservoirs)
    {
        if (!this->indices.nodes_reservoirs.contains(reservoir.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_reservoirs.value(reservoir.uuid), reservoir.metadata, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("reservoir"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        if (!this->indices.nodes_tanks.contains(tank.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_NODE, this->indices.nodes_tanks.value(tank.uuid), tank.metadata, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("tank"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        if (!this->indices.links_pipes.contains(pipe.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_pipes.value(pipe.uuid), pipe.metadata, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("pipe"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkPump &pump : request.links_pumps)
    {
        if (!this->indices.links_pumps.contains(pump.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_pumps.value(pump.uuid), pump.metadata, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("pump"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }
    for (const HydraulicLinkValve &valve : request.links_valves)
    {
        if (!this->indices.links_valves.contains(valve.uuid))
            continue;
        status = EpanetNetworkBuilderSupport::setNodeOrLinkMetadata(this->project, EN_LINK, this->indices.links_valves.value(valve.uuid), valve.metadata, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("valve"));
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlSimple &control : request.controls_simple)
    {
        status = addControlSimple(control);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    for (const HydraulicControlRule &rule : request.controls_rules)
    {
        status = addControlRule(rule);
        EpanetNetworkBuilderSupport::collectBuildFailure(this->project, status, first_failure);
    }

    return first_failure;
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshNodeIndices(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = rebuildNodeIndices(this->project, request.nodes_junctions, this->indices.nodes_junctions, HydraulicSimulationStatusStage::AddJunction, HydraulicSimulationStatusEntityType::Junction, QStringLiteral("junction"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildNodeIndices(this->project, request.nodes_reservoirs, this->indices.nodes_reservoirs, HydraulicSimulationStatusStage::AddReservoir, HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("reservoir"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildNodeIndices(this->project, request.nodes_tanks, this->indices.nodes_tanks, HydraulicSimulationStatusStage::AddTank, HydraulicSimulationStatusEntityType::Tank, QStringLiteral("tank"));
    if (!status.success && first_failure.success)
        first_failure = status;

    return first_failure;
}

HydraulicSimulationStatus EpanetNetworkBuilder::refreshLinkIndices(const NetworkHydraulic &request)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = rebuildLinkIndices(this->project, request.links_pipes, this->indices.links_pipes, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, QStringLiteral("pipe"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildLinkIndices(this->project, request.links_pumps, this->indices.links_pumps, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, QStringLiteral("pump"));
    if (!status.success && first_failure.success)
        first_failure = status;

    status = rebuildLinkIndices(this->project, request.links_valves, this->indices.links_valves, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, QStringLiteral("valve"));
    if (!status.success && first_failure.success)
        first_failure = status;

    return first_failure;
}
