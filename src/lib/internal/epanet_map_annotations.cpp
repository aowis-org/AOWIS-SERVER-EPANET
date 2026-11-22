#include "epanet_map_annotations.h"

#include "epanet_status_helpers.h"

#include <cmath>

#include <GeographicLib/Geodesic.hpp>

#include <QSet>

#include <aowis/model/hydraulic/network_hydraulic_distribution.h>
#include <aowis/model/serialization/model_validation.h>

namespace
{
constexpr double degreesPerRadian = 57.295779513082320876798154814105;

template <typename Node>
std::optional<EpanetAnnotationAnchorTarget> findNode(
    const QList<Node> &nodes, const QUuid &uuid, const QString &type)
{
    for (const Node &node : nodes)
    {
        if (node.uuid != uuid)
            continue;
        EpanetAnnotationAnchorTarget target;
        target.entity_type = type;
        target.node_id = node.id;
        target.position = node.coordinate_wgs84;
        return target;
    }
    return std::nullopt;
}

QString annotationEntityId(const MapAnnotation &annotation)
{
    return annotation.uuid.toString(QUuid::WithoutBraces);
}

HydraulicSimulationStatus annotationFailure(const MapAnnotation &annotation, const QString &message)
{
    return makeEpanetStatus(
        HydraulicSimulationStatusStage::BuildNetwork,
        HydraulicSimulationStatusOperation::SetEntityGeometry,
        HydraulicSimulationStatusEntityType::Network,
        annotationEntityId(annotation),
        annotation.uuid,
        message);
}
}

std::optional<EpanetAnnotationAnchorTarget> epanetAnnotationAnchorTarget(
    const NetworkHydraulicDistribution &network, const QUuid &entity_uuid)
{
    if (entity_uuid.isNull())
        return std::nullopt;
    std::optional<EpanetAnnotationAnchorTarget> target =
        findNode(network.nodes_junctions, entity_uuid, QStringLiteral("HydraulicNodeJunction"));
    if (!target)
        target = findNode(network.nodes_reservoirs, entity_uuid, QStringLiteral("HydraulicNodeReservoir"));
    if (!target)
        target = findNode(network.nodes_tanks, entity_uuid, QStringLiteral("HydraulicNodeTank"));
    if (target)
        return target;
    for (const HydraulicDemandPoint &point : network.demand_points)
    {
        if (point.uuid != entity_uuid)
            continue;
        // Demand points are not EPANET nodes: position only, no INP anchor.
        EpanetAnnotationAnchorTarget demand_point;
        demand_point.entity_type = QStringLiteral("HydraulicDemandPoint");
        demand_point.position = point.coordinate_wgs84;
        return demand_point;
    }
    return std::nullopt;
}

CoordinateWGS84 epanetAnnotationOffsetPosition(
    const CoordinateWGS84 &anchor, double offset_east_m, double offset_north_m)
{
    CoordinateWGS84 position;
    const double distance_m = std::hypot(offset_east_m, offset_north_m);
    if (distance_m == 0.0)
    {
        position.longitude_deg = anchor.longitude_deg;
        position.latitude_deg = anchor.latitude_deg;
        return position;
    }
    const double azimuth_deg = std::atan2(offset_east_m, offset_north_m) * degreesPerRadian;
    GeographicLib::Geodesic::WGS84().Direct(
        anchor.latitude_deg, anchor.longitude_deg, azimuth_deg, distance_m,
        position.latitude_deg, position.longitude_deg);
    return position;
}

void epanetAnnotationOffset(
    const CoordinateWGS84 &anchor, const CoordinateWGS84 &position,
    double &offset_east_m, double &offset_north_m)
{
    double distance_m = 0.0;
    double azimuth_start_deg = 0.0;
    double azimuth_end_deg = 0.0;
    GeographicLib::Geodesic::WGS84().Inverse(
        anchor.latitude_deg, anchor.longitude_deg,
        position.latitude_deg, position.longitude_deg,
        distance_m, azimuth_start_deg, azimuth_end_deg);
    const double azimuth = azimuth_start_deg / degreesPerRadian;
    offset_east_m = distance_m * std::sin(azimuth);
    offset_north_m = distance_m * std::cos(azimuth);
}

CoordinateWGS84 epanetAnnotationDisplayPosition(
    const NetworkHydraulicDistribution &network, const MapAnnotation &annotation)
{
    if (annotation.anchor)
    {
        const std::optional<EpanetAnnotationAnchorTarget> target =
            epanetAnnotationAnchorTarget(network, annotation.anchor->entity_uuid);
        if (target)
            return epanetAnnotationOffsetPosition(
                target->position, annotation.anchor->offset_east_m, annotation.anchor->offset_north_m);
    }
    CoordinateWGS84 position;
    position.longitude_deg = annotation.position_wgs84.longitude_deg;
    position.latitude_deg = annotation.position_wgs84.latitude_deg;
    return position;
}

HydraulicSimulationStatus validateEpanetMapAnnotations(
    const NetworkHydraulicDistribution &network, const QList<MapAnnotation> &annotations)
{
    QSet<QUuid> seen;
    for (const MapAnnotation &annotation : annotations)
    {
        const aowis::model::ModelValidationReport report = aowis::model::validateMapAnnotation(annotation);
        if (!report.valid())
            return annotationFailure(annotation,
                QStringLiteral("Invalid map annotation: %1").arg(report.summary()));
        if (seen.contains(annotation.uuid))
            return annotationFailure(annotation, QStringLiteral("Duplicate map annotation UUID"));
        seen.insert(annotation.uuid);
        if (!annotation.anchor)
            continue;
        const std::optional<EpanetAnnotationAnchorTarget> target =
            epanetAnnotationAnchorTarget(network, annotation.anchor->entity_uuid);
        if (!target)
            return annotationFailure(annotation,
                QStringLiteral("Missing map annotation anchor: no point entity with this UUID in the network"));
        if (target->entity_type != annotation.anchor->entity_type)
            return annotationFailure(annotation,
                QStringLiteral("Map annotation anchor type %1 does not match the network entity type %2")
                    .arg(annotation.anchor->entity_type, target->entity_type));
    }
    return makeEpanetSuccess();
}
