#ifndef AOWIS_EPANET_MAP_ANNOTATIONS_H
#define AOWIS_EPANET_MAP_ANNOTATIONS_H

#include <optional>

#include <QList>
#include <QString>
#include <QUuid>

#include <aowis/model/gis.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/map/map_annotation.h>

struct NetworkHydraulicDistribution;

// EPANET [LABELS] <-> MODEL map annotations (MODEL A3 replaced the hydraulic
// HydraulicMapLabel collection with annotations in a separate layer).
//
// An anchored annotation is drawn at its anchor's WGS84 position plus a signed
// east/north offset in metres. As required by the MODEL annotation contract,
// the offset is a WGS84 geodesic: distance hypot(east, north) along azimuth
// atan2(east, north) from the anchor (GeographicLib Geodesic::Direct), and
// the inverse operation when an offset is derived (Geodesic::Inverse).

struct EpanetAnnotationAnchorTarget
{
    // Canonical MODEL type key (HydraulicNodeJunction, ...).
    QString entity_type;
    // EPANET node ID; empty for targets that are not EPANET nodes.
    QString node_id;
    CoordinateWGS84 position;
};

// Point-position target with this UUID in the network, if any.
std::optional<EpanetAnnotationAnchorTarget> epanetAnnotationAnchorTarget(
    const NetworkHydraulicDistribution &network, const QUuid &entity_uuid);

// The anchor position moved by a signed east/north offset in metres.
CoordinateWGS84 epanetAnnotationOffsetPosition(
    const CoordinateWGS84 &anchor, double offset_east_m, double offset_north_m);

// The signed east/north offset in metres from anchor to position.
void epanetAnnotationOffset(
    const CoordinateWGS84 &anchor, const CoordinateWGS84 &position,
    double &offset_east_m, double &offset_north_m);

// Where the annotation is drawn: the anchor plus its offset when the anchor
// resolves in `network`, otherwise its stored position.
CoordinateWGS84 epanetAnnotationDisplayPosition(
    const NetworkHydraulicDistribution &network, const MapAnnotation &annotation);

// MODEL validation plus closure against the network: every anchor must
// resolve to an entity of its declared type in this network. Success status
// when all annotations are valid.
HydraulicSimulationStatus validateEpanetMapAnnotations(
    const NetworkHydraulicDistribution &network, const QList<MapAnnotation> &annotations);

#endif // AOWIS_EPANET_MAP_ANNOTATIONS_H
