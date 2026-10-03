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

bool demandProjectionKeyMatches(
    const HydraulicDemand &left,
    const HydraulicDemand &right)
{
    if (left.category_name != right.category_name
        || left.pattern_mode != right.pattern_mode)
    {
        return false;
    }

    if (left.pattern_mode == HydraulicTimePatternMode::Constant)
        return true;
    return left.pattern_uuid == right.pattern_uuid;
}

HydraulicSimulationStatus aggregateProjectedDemands(
    const NetworkHydraulic &source,
    NetworkHydraulic &projected)
{
    QHash<QUuid, qsizetype> original_demand_counts;
    original_demand_counts.reserve(source.nodes_junctions.size());
    for (const HydraulicNodeJunction &junction : source.nodes_junctions)
        original_demand_counts.insert(junction.uuid, junction.demands.size());

    for (HydraulicNodeJunction &junction : projected.nodes_junctions)
    {
        const qsizetype original_count = original_demand_counts.value(
            junction.uuid, junction.demands.size());
        if (junction.demands.size() <= original_count + 1)
            continue;

        QList<HydraulicDemand> compacted;
        compacted.reserve(junction.demands.size());
        for (qsizetype demand_index = 0; demand_index < original_count; ++demand_index)
            compacted.append(junction.demands.at(demand_index));

        for (qsizetype demand_index = original_count;
             demand_index < junction.demands.size(); ++demand_index)
        {
            const HydraulicDemand &candidate = junction.demands.at(demand_index);
            bool merged = false;
            for (qsizetype compacted_index = original_count;
                 compacted_index < compacted.size(); ++compacted_index)
            {
                HydraulicDemand &aggregate = compacted[compacted_index];
                if (!demandProjectionKeyMatches(aggregate, candidate))
                    continue;

                aggregate.base_demand_m3_per_h += candidate.base_demand_m3_per_h;
                if (!std::isfinite(aggregate.base_demand_m3_per_h))
                {
                    HydraulicSimulationStatus status;
                    status.success = false;
                    status.stage = HydraulicSimulationStatusStage::BuildNetwork;
                    status.operation = HydraulicSimulationStatusOperation::AddDemand;
                    status.entity.type = HydraulicSimulationStatusEntityType::Junction;
                    status.entity.id = junction.id;
                    status.entity.uuid = junction.uuid;
                    status.message = QStringLiteral(
                        "Projected demand aggregation overflowed a junction demand");
                    return status;
                }
                merged = true;
                break;
            }

            if (!merged)
                compacted.append(candidate);
        }

        junction.demands = compacted;
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

    HydraulicSimulationStatus aggregation_status = aggregateProjectedDemands(
        source, projected);
    if (!aggregation_status.success)
        return aggregation_status;

    // The projection is solver-facing. Demand points remain only in the source model
    // and must never be forwarded as duplicate solver entities.
    projected.demand_points.clear();

    return successStatus();
}
