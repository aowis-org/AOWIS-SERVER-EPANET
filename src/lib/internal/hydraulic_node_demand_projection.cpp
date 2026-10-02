#include "hydraulic_node_demand_projection.h"

#include <QHash>
#include <QStringList>

#include <cmath>

namespace
{
HydraulicSimulationStatus successStatus()
{
    HydraulicSimulationStatus status;
    status.success = true;
    return status;
}

HydraulicSimulationStatus demandPointFailure(
    const HydraulicDemandPoint &demand_point,
    HydraulicSimulationStatusOperation operation,
    const QString &message,
    const QStringList &details = QStringList())
{
    HydraulicSimulationStatus status;
    status.success = false;
    status.stage = HydraulicSimulationStatusStage::BuildNetwork;
    status.operation = operation;
    status.entity.type = HydraulicSimulationStatusEntityType::DemandPoint;
    status.entity.id = demand_point.id;
    status.entity.uuid = demand_point.uuid;
    status.message = message;
    status.details = details;
    return status;
}

const HydraulicLinkPipe *pipeByUuid(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.uuid == uuid)
            return &pipe;
    }

    return nullptr;
}

HydraulicSimulationStatus appendScaledDemands(
    const HydraulicDemandPoint &demand_point,
    NetworkHydraulic &projected,
    const QHash<QUuid, qsizetype> &enabled_junction_indices,
    const QUuid &junction_uuid,
    double fraction)
{
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction > 1.0)
    {
        return demandPointFailure(
            demand_point,
            HydraulicSimulationStatusOperation::AddDemand,
            QStringLiteral("Demand point contains an invalid derived allocation fraction"));
    }

    if (fraction == 0.0)
        return successStatus();

    if (!enabled_junction_indices.contains(junction_uuid))
    {
        return demandPointFailure(
            demand_point,
            HydraulicSimulationStatusOperation::ResolveEntity,
            QStringLiteral("Demand point attachment does not resolve to an enabled hydraulic junction"),
            {QStringLiteral("junction_uuid: %1").arg(junction_uuid.toString(QUuid::WithoutBraces))});
    }

    const qsizetype junction_index = enabled_junction_indices.value(junction_uuid);
    for (qsizetype demand_index = 0; demand_index < demand_point.demands.size(); demand_index++)
    {
        const HydraulicDemand &demand = demand_point.demands.at(demand_index);
        if (!std::isfinite(demand.base_demand_m3_per_h))
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::AddDemand,
                QStringLiteral("Demand point contains an invalid hydraulic demand"),
                {QStringLiteral("demands[%1].base_demand_m3_per_h must be finite").arg(demand_index)});
        }

        HydraulicDemand projected_demand = demand;
        projected_demand.base_demand_m3_per_h *= fraction;
        if (!std::isfinite(projected_demand.base_demand_m3_per_h))
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::AddDemand,
                QStringLiteral("Demand point attachment overflows the projected junction demand"));
        }

        projected.nodes_junctions[junction_index].demands.append(projected_demand);
    }

    return successStatus();
}
}

HydraulicSimulationStatus buildHydraulicNodeDemandProjection(
    const NetworkHydraulic &source,
    NetworkHydraulic &projected)
{
    projected = source;

    QHash<QUuid, qsizetype> enabled_junction_indices;
    enabled_junction_indices.reserve(projected.nodes_junctions.size());
    for (qsizetype junction_index = 0; junction_index < projected.nodes_junctions.size(); junction_index++)
    {
        const HydraulicNodeJunction &junction = projected.nodes_junctions.at(junction_index);
        if (junction.metadata.enabled)
            enabled_junction_indices.insert(junction.uuid, junction_index);
    }

    for (const HydraulicDemandPoint &demand_point : source.demand_points)
    {
        if (!demand_point.metadata.enabled || demand_point.demands.isEmpty())
            continue;

        if (demand_point.attachment.type == HydraulicDemandPointAttachmentType::None)
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Enabled demand point with hydraulic demands requires a network attachment"));
        }

        if (demand_point.attachment.type == HydraulicDemandPointAttachmentType::Junction)
        {
            HydraulicSimulationStatus status = appendScaledDemands(
                demand_point,
                projected,
                enabled_junction_indices,
                demand_point.attachment.junction_uuid,
                1.0);
            if (!status.success)
                return status;
            continue;
        }

        if (demand_point.attachment.type != HydraulicDemandPointAttachmentType::Pipe)
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Demand point has an unsupported network attachment type"));
        }

        const double position = demand_point.attachment.pipe_position;
        if (!std::isfinite(position) || position < 0.0 || position > 1.0)
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Demand point pipe attachment position must be between 0 and 1"));
        }

        const HydraulicLinkPipe *pipe = pipeByUuid(source, demand_point.attachment.pipe_uuid);
        if (pipe == nullptr || !pipe->metadata.enabled)
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Demand point pipe attachment does not reference an enabled hydraulic pipe"),
                {QStringLiteral("pipe_uuid: %1")
                    .arg(demand_point.attachment.pipe_uuid.toString(QUuid::WithoutBraces))});
        }

        if (demand_point.attachment.pipe_allocation_mode
            == HydraulicDemandPointPipeAllocationMode::AssignedJunction)
        {
            const QUuid assigned_junction_uuid =
                demand_point.attachment.pipe_assigned_junction_uuid;
            if (assigned_junction_uuid.isNull())
            {
                return demandPointFailure(
                    demand_point,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    QStringLiteral("Assigned-junction pipe demand point requires an assigned junction"),
                    {QStringLiteral("pipe_uuid: %1")
                         .arg(pipe->uuid.toString(QUuid::WithoutBraces))});
            }

            if (assigned_junction_uuid != pipe->node_uuid_from
                && assigned_junction_uuid != pipe->node_uuid_to)
            {
                return demandPointFailure(
                    demand_point,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    QStringLiteral("Assigned-junction pipe demand point must reference one of the attached pipe endpoints"),
                    {QStringLiteral("pipe_uuid: %1")
                         .arg(pipe->uuid.toString(QUuid::WithoutBraces)),
                     QStringLiteral("assigned_junction_uuid: %1")
                         .arg(assigned_junction_uuid.toString(QUuid::WithoutBraces))});
            }

            HydraulicSimulationStatus status = appendScaledDemands(
                demand_point,
                projected,
                enabled_junction_indices,
                assigned_junction_uuid,
                1.0);
            if (!status.success)
                return status;
            continue;
        }

        if (demand_point.attachment.pipe_allocation_mode
            != HydraulicDemandPointPipeAllocationMode::InterpolateByPosition)
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Demand point has an unsupported pipe demand allocation mode"));
        }

        if (!enabled_junction_indices.contains(pipe->node_uuid_from)
            || !enabled_junction_indices.contains(pipe->node_uuid_to))
        {
            return demandPointFailure(
                demand_point,
                HydraulicSimulationStatusOperation::ResolveEntity,
                QStringLiteral("Position-interpolated pipe demand point requires both pipe endpoints to be enabled junctions"),
                {QStringLiteral("pipe_uuid: %1").arg(pipe->uuid.toString(QUuid::WithoutBraces))});
        }

        HydraulicSimulationStatus status = appendScaledDemands(
            demand_point,
            projected,
            enabled_junction_indices,
            pipe->node_uuid_from,
            1.0 - position);
        if (!status.success)
            return status;

        status = appendScaledDemands(
            demand_point,
            projected,
            enabled_junction_indices,
            pipe->node_uuid_to,
            position);
        if (!status.success)
            return status;
    }

    // The projection is solver-facing. Demand points remain only in the source model
    // and must never be forwarded as duplicate solver entities.
    projected.demand_points.clear();

    return successStatus();
}
