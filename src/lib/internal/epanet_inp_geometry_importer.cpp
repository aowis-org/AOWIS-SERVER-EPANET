#include "epanet_inp_geometry_importer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>

#include <GeographicLib/LocalCartesian.hpp>

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
constexpr double meters_per_foot = 0.3048;
constexpr int missing_coordinate_error = 254;

struct SourcePoint
{
    double x = 0.0;
    double y = 0.0;
};

enum class SourceMapUnits
{
    None,
    Meters,
    Feet,
    Degrees
};

struct SourceMapLabel
{
    SourcePoint point;
    QString text;
    QString anchor_node_id;
};

struct SourceBackdrop
{
    bool present = false;
    std::optional<SourcePoint> lower_left;
    std::optional<SourcePoint> upper_right;
    QString file;
    SourcePoint offset;
};

struct SourceMapMetadata
{
    SourceMapUnits units = SourceMapUnits::None;
    bool units_declared = false;
    QList<SourceMapLabel> labels;
    SourceBackdrop backdrop;
};

struct ImportedNodeCoordinate
{
    QString id;
    std::optional<SourcePoint> point;
};

struct ImportedLinkVertices
{
    QString id;
    QList<SourcePoint> points;
};

HydraulicSimulationStatus geometryReadFailure(
    const EpanetProject &project,
    int error,
    const QString &backend_operation,
    const QString &message,
    HydraulicSimulationStatusEntityType entity_type)
{
    return processEpanetReturnCode(
        project,
        error,
        HydraulicSimulationStatusStage::ReadInput,
        HydraulicSimulationStatusOperation::ReadInput,
        backend_operation,
        entity_type,
        QString(),
        message);
}

void appendGeometryDiagnostic(
    EpanetResultImport &result,
    HydraulicSimulationDiagnosticSeverity severity,
    const QString &message,
    const QStringList &details = QStringList(),
    bool mark_incomplete = false)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.stage = HydraulicSimulationStatusStage::ReadInput;
    diagnostic.operation = HydraulicSimulationStatusOperation::SetEntityGeometry;
    diagnostic.entity.type = HydraulicSimulationStatusEntityType::Network;
    diagnostic.message = message;
    diagnostic.details = details;
    diagnostic.backend_name = QStringLiteral("EPANET");
    diagnostic.backend_operation = QStringLiteral("INP geometry import");
    appendEpanetDiagnosticIfUnique(result.diagnostics, diagnostic);
    if (mark_incomplete)
        result.complete = false;
}

bool parseDouble(const QString &text, double &value)
{
    bool ok = false;
    value = text.toDouble(&ok);
    return ok && std::isfinite(value);
}

QString stripComment(const QString &line)
{
    bool in_quotes = false;
    for (qsizetype index = 0; index < line.size(); index++)
    {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"'))
            in_quotes = !in_quotes;
        else if (character == QLatin1Char(';') && !in_quotes)
            return line.left(index);
    }
    return line;
}

HydraulicSimulationStatus parseSourceMapMetadata(
    const QString &input_file_path,
    EpanetResultImport &result,
    SourceMapMetadata &metadata)
{
    QFile file(input_file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Network,
            QString(),
            QStringLiteral("Could not read EPANET map metadata: %1").arg(file.errorString()));
    }

    enum class Section
    {
        Other,
        Labels,
        Backdrop
    };

    Section section = Section::Other;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    const QRegularExpression label_expression(
        QStringLiteral(R"REGEX(^\s*([^\s]+)\s+([^\s]+)\s+"([^"]*)"(?:\s+([^\s]+))?\s*$)REGEX"));

    for (QString line : lines)
    {
        line = stripComment(line).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('[')))
        {
            if (line.compare(QStringLiteral("[LABELS]"), Qt::CaseInsensitive) == 0)
                section = Section::Labels;
            else if (line.compare(QStringLiteral("[BACKDROP]"), Qt::CaseInsensitive) == 0)
            {
                section = Section::Backdrop;
                metadata.backdrop.present = true;
            }
            else
                section = Section::Other;
            continue;
        }

        if (section == Section::Labels)
        {
            const QRegularExpressionMatch match = label_expression.match(line);
            if (!match.hasMatch())
            {
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("An EPANET map label could not be parsed and was skipped."),
                    {line},
                    true);
                continue;
            }

            SourceMapLabel label;
            if (!parseDouble(match.captured(1), label.point.x)
                || !parseDouble(match.captured(2), label.point.y))
            {
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("An EPANET map label contains invalid coordinates and was skipped."),
                    {line},
                    true);
                continue;
            }
            label.text = match.captured(3);
            label.anchor_node_id = match.captured(4);
            metadata.labels.append(label);
            continue;
        }

        if (section != Section::Backdrop)
            continue;

        const QStringList tokens = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            continue;

        const QString command = tokens.first().toUpper();
        if (command == QStringLiteral("UNITS") && tokens.size() >= 2)
        {
            metadata.units_declared = true;
            const QString value = tokens.at(1).toUpper();
            if (value == QStringLiteral("METERS"))
                metadata.units = SourceMapUnits::Meters;
            else if (value == QStringLiteral("FEET"))
                metadata.units = SourceMapUnits::Feet;
            else if (value == QStringLiteral("DEGREES"))
                metadata.units = SourceMapUnits::Degrees;
            else if (value == QStringLiteral("NONE"))
                metadata.units = SourceMapUnits::None;
            else
            {
                metadata.units = SourceMapUnits::None;
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("Unknown EPANET backdrop units were treated as arbitrary map units."),
                    {tokens.at(1)},
                    true);
            }
        }
        else if (command == QStringLiteral("DIMENSIONS") && tokens.size() >= 5)
        {
            SourcePoint lower_left;
            SourcePoint upper_right;
            if (parseDouble(tokens.at(1), lower_left.x)
                && parseDouble(tokens.at(2), lower_left.y)
                && parseDouble(tokens.at(3), upper_right.x)
                && parseDouble(tokens.at(4), upper_right.y))
            {
                metadata.backdrop.lower_left = lower_left;
                metadata.backdrop.upper_right = upper_right;
            }
            else
            {
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("EPANET backdrop dimensions contain invalid coordinates and were skipped."),
                    {line},
                    true);
            }
        }
        else if (command == QStringLiteral("FILE") && tokens.size() >= 2)
        {
            metadata.backdrop.file = line.mid(line.indexOf(QLatin1Char(' ')) + 1).trimmed();
        }
        else if (command == QStringLiteral("OFFSET") && tokens.size() >= 3)
        {
            if (!parseDouble(tokens.at(1), metadata.backdrop.offset.x)
                || !parseDouble(tokens.at(2), metadata.backdrop.offset.y))
            {
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("EPANET backdrop offset contains invalid coordinates and was ignored."),
                    {line},
                    true);
                metadata.backdrop.offset = SourcePoint();
            }
        }
    }

    return makeEpanetSuccess();
}

bool validWgs84Point(const SourcePoint &point)
{
    return point.x >= -180.0 && point.x <= 180.0
        && point.y >= -90.0 && point.y <= 90.0;
}

CoordinateWGS84 directWgs84(const SourcePoint &point)
{
    CoordinateWGS84 coordinate;
    coordinate.longitude_deg = point.x;
    coordinate.latitude_deg = point.y;
    return coordinate;
}

struct GeometryTransform
{
    bool direct_degrees = false;
    double scale_to_m = 1.0;
    double center_x = 0.0;
    double center_y = 0.0;
    GeographicLib::LocalCartesian local_cartesian{0.0, 0.0, 0.0};

    CoordinateWGS84 transform(const SourcePoint &point) const
    {
        if (this->direct_degrees)
            return directWgs84(point);

        double latitude = 0.0;
        double longitude = 0.0;
        double height = 0.0;
        this->local_cartesian.Reverse(
            (point.x - this->center_x) * this->scale_to_m,
            (point.y - this->center_y) * this->scale_to_m,
            0.0,
            latitude,
            longitude,
            height);

        CoordinateWGS84 coordinate;
        coordinate.longitude_deg = longitude;
        coordinate.latitude_deg = latitude;
        return coordinate;
    }

    CoordinateWGS84 transformOffset(const SourcePoint &point) const
    {
        if (this->direct_degrees)
            return directWgs84(point);

        double latitude = 0.0;
        double longitude = 0.0;
        double height = 0.0;
        this->local_cartesian.Reverse(
            point.x * this->scale_to_m,
            point.y * this->scale_to_m,
            0.0,
            latitude,
            longitude,
            height);

        CoordinateWGS84 coordinate;
        coordinate.longitude_deg = longitude;
        coordinate.latitude_deg = latitude;
        return coordinate;
    }
};

void includePointBounds(
    const SourcePoint &point,
    bool &has_bounds,
    double &minimum_x,
    double &minimum_y,
    double &maximum_x,
    double &maximum_y)
{
    if (!has_bounds)
    {
        minimum_x = maximum_x = point.x;
        minimum_y = maximum_y = point.y;
        has_bounds = true;
        return;
    }
    minimum_x = std::min(minimum_x, point.x);
    minimum_y = std::min(minimum_y, point.y);
    maximum_x = std::max(maximum_x, point.x);
    maximum_y = std::max(maximum_y, point.y);
}

GeometryTransform makeGeometryTransform(
    SourceMapUnits units,
    const QList<ImportedNodeCoordinate> &nodes,
    const QList<ImportedLinkVertices> &links,
    const SourceMapMetadata &metadata,
    EpanetResultImport &result)
{
    QList<SourcePoint> network_points;
    for (const ImportedNodeCoordinate &node : nodes)
    {
        if (node.point.has_value())
            network_points.append(node.point.value());
    }
    for (const ImportedLinkVertices &link : links)
    {
        for (const SourcePoint &point : link.points)
            network_points.append(point);
    }

    QList<SourcePoint> all_points = network_points;
    for (const SourceMapLabel &label : metadata.labels)
        all_points.append(label.point);
    if (metadata.backdrop.lower_left.has_value())
        all_points.append(metadata.backdrop.lower_left.value());
    if (metadata.backdrop.upper_right.has_value())
        all_points.append(metadata.backdrop.upper_right.value());

    GeometryTransform transform;
    if (units == SourceMapUnits::Degrees)
    {
        bool all_valid = true;
        for (const SourcePoint &point : all_points)
            all_valid = all_valid && validWgs84Point(point);

        if (all_valid)
        {
            transform.direct_degrees = true;
            appendGeometryDiagnostic(
                result,
                HydraulicSimulationDiagnosticSeverity::Information,
                QStringLiteral("EPANET map coordinates declared as degrees were interpreted as WGS84 longitude/latitude."));
            return transform;
        }

        appendGeometryDiagnostic(
            result,
            HydraulicSimulationDiagnosticSeverity::Warning,
            QStringLiteral("EPANET map coordinates were declared as degrees but fall outside valid longitude/latitude ranges; they were imported as synthetic local geometry instead."),
            QStringList(),
            true);
        units = SourceMapUnits::None;
    }

    if (units == SourceMapUnits::Feet)
        transform.scale_to_m = meters_per_foot;
    else
        transform.scale_to_m = 1.0;

    bool has_bounds = false;
    double minimum_x = 0.0;
    double minimum_y = 0.0;
    double maximum_x = 0.0;
    double maximum_y = 0.0;
    const QList<SourcePoint> &centering_points = network_points.isEmpty() ? all_points : network_points;
    for (const SourcePoint &point : centering_points)
        includePointBounds(point, has_bounds, minimum_x, minimum_y, maximum_x, maximum_y);
    if (has_bounds)
    {
        transform.center_x = (minimum_x + maximum_x) / 2.0;
        transform.center_y = (minimum_y + maximum_y) / 2.0;
    }

    if (units == SourceMapUnits::Meters)
    {
        appendGeometryDiagnostic(
            result,
            HydraulicSimulationDiagnosticSeverity::Information,
            QStringLiteral("EPANET metric map geometry was centered at WGS84 0°,0° while preserving metric offsets."));
    }
    else if (units == SourceMapUnits::Feet)
    {
        appendGeometryDiagnostic(
            result,
            HydraulicSimulationDiagnosticSeverity::Information,
            QStringLiteral("EPANET map geometry in feet was converted to metres and centered at WGS84 0°,0°."));
    }
    else
    {
        appendGeometryDiagnostic(
            result,
            HydraulicSimulationDiagnosticSeverity::Warning,
            QStringLiteral("EPANET map geometry has no usable coordinate units; AOWIS interpreted one map unit as one metre and centered the network at WGS84 0°,0°."));
    }
    return transform;
}

template<typename NodeType>
NodeType *nodeById(QList<NodeType> &nodes, const QString &id)
{
    for (NodeType &node : nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

template<typename LinkType>
LinkType *linkById(QList<LinkType> &links, const QString &id)
{
    for (LinkType &link : links)
    {
        if (link.id == id)
            return &link;
    }
    return nullptr;
}

QUuid nodeUuidById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &node : network.nodes_junctions)
    {
        if (node.id == id)
            return node.uuid;
    }
    for (const HydraulicNodeReservoir &node : network.nodes_reservoirs)
    {
        if (node.id == id)
            return node.uuid;
    }
    for (const HydraulicNodeTank &node : network.nodes_tanks)
    {
        if (node.id == id)
            return node.uuid;
    }
    return QUuid();
}

void assignNodeCoordinate(NetworkHydraulic &network, const QString &id, const CoordinateWGS84 &coordinate)
{
    HydraulicNodeJunction *junction = nodeById(network.nodes_junctions, id);
    if (junction != nullptr)
    {
        junction->coordinate_wgs84 = coordinate;
        return;
    }
    HydraulicNodeReservoir *reservoir = nodeById(network.nodes_reservoirs, id);
    if (reservoir != nullptr)
    {
        reservoir->coordinate_wgs84 = coordinate;
        return;
    }
    HydraulicNodeTank *tank = nodeById(network.nodes_tanks, id);
    if (tank != nullptr)
        tank->coordinate_wgs84 = coordinate;
}

void assignLinkVertices(NetworkHydraulic &network, const QString &id, const QList<HydraulicLinkVertex> &vertices)
{
    HydraulicLinkPipe *pipe = linkById(network.links_pipes, id);
    if (pipe != nullptr)
    {
        pipe->vertices = vertices;
        return;
    }
    HydraulicLinkPump *pump = linkById(network.links_pumps, id);
    if (pump != nullptr)
    {
        pump->vertices = vertices;
        return;
    }
    HydraulicLinkValve *valve = linkById(network.links_valves, id);
    if (valve != nullptr)
        valve->vertices = vertices;
}

HydraulicSimulationStatus readNativeGeometry(
    EpanetProject &project,
    QList<ImportedNodeCoordinate> &nodes,
    QList<ImportedLinkVertices> &links)
{
    int node_count = 0;
    int error = EN_getcount(project.handle(), EN_NODECOUNT, &node_count);
    if (error != 0)
    {
        return geometryReadFailure(
            project, error, QStringLiteral("EN_getcount(EN_NODECOUNT)"),
            QStringLiteral("Failed to inspect EPANET node geometry"),
            HydraulicSimulationStatusEntityType::Node);
    }

    for (int node_index = 1; node_index <= node_count; node_index++)
    {
        char node_id[EN_MAXID + 1]{};
        error = EN_getnodeid(project.handle(), node_index, node_id);
        if (error != 0)
        {
            return geometryReadFailure(
                project, error, QStringLiteral("EN_getnodeid"),
                QStringLiteral("Failed to read an EPANET node identifier for geometry import"),
                HydraulicSimulationStatusEntityType::Node);
        }

        ImportedNodeCoordinate imported;
        imported.id = QString::fromUtf8(node_id);
        SourcePoint point;
        error = EN_getcoord(project.handle(), node_index, &point.x, &point.y);
        if (error == 0)
            imported.point = point;
        else if (error != missing_coordinate_error)
        {
            return geometryReadFailure(
                project, error, QStringLiteral("EN_getcoord"),
                QStringLiteral("Failed to read EPANET node coordinates"),
                HydraulicSimulationStatusEntityType::Node);
        }
        nodes.append(imported);
    }

    int link_count = 0;
    error = EN_getcount(project.handle(), EN_LINKCOUNT, &link_count);
    if (error != 0)
    {
        return geometryReadFailure(
            project, error, QStringLiteral("EN_getcount(EN_LINKCOUNT)"),
            QStringLiteral("Failed to inspect EPANET link geometry"),
            HydraulicSimulationStatusEntityType::Link);
    }

    for (int link_index = 1; link_index <= link_count; link_index++)
    {
        char link_id[EN_MAXID + 1]{};
        error = EN_getlinkid(project.handle(), link_index, link_id);
        if (error != 0)
        {
            return geometryReadFailure(
                project, error, QStringLiteral("EN_getlinkid"),
                QStringLiteral("Failed to read an EPANET link identifier for geometry import"),
                HydraulicSimulationStatusEntityType::Link);
        }

        ImportedLinkVertices imported;
        imported.id = QString::fromUtf8(link_id);
        int vertex_count = 0;
        error = EN_getvertexcount(project.handle(), link_index, &vertex_count);
        if (error != 0)
        {
            return geometryReadFailure(
                project, error, QStringLiteral("EN_getvertexcount"),
                QStringLiteral("Failed to read EPANET link vertex count"),
                HydraulicSimulationStatusEntityType::Link);
        }
        for (int vertex_index = 1; vertex_index <= vertex_count; vertex_index++)
        {
            SourcePoint point;
            error = EN_getvertex(project.handle(), link_index, vertex_index, &point.x, &point.y);
            if (error != 0)
            {
                return geometryReadFailure(
                    project, error, QStringLiteral("EN_getvertex"),
                    QStringLiteral("Failed to read an EPANET link vertex"),
                    HydraulicSimulationStatusEntityType::Link);
            }
            imported.points.append(point);
        }
        links.append(imported);
    }

    return makeEpanetSuccess();
}

void generateMissingNodeCoordinates(
    NetworkHydraulic &network,
    const QList<ImportedNodeCoordinate> &nodes,
    EpanetResultImport &result)
{
    QStringList missing_ids;
    double longitude_sum = 0.0;
    double latitude_sum = 0.0;
    int positioned_count = 0;

    for (const ImportedNodeCoordinate &node : nodes)
    {
        if (!node.point.has_value())
        {
            missing_ids.append(node.id);
            continue;
        }

        const HydraulicNodeJunction *junction = nodeById(network.nodes_junctions, node.id);
        const HydraulicNodeReservoir *reservoir = nodeById(network.nodes_reservoirs, node.id);
        const HydraulicNodeTank *tank = nodeById(network.nodes_tanks, node.id);
        const CoordinateWGS84 *coordinate = nullptr;
        if (junction != nullptr)
            coordinate = &junction->coordinate_wgs84;
        else if (reservoir != nullptr)
            coordinate = &reservoir->coordinate_wgs84;
        else if (tank != nullptr)
            coordinate = &tank->coordinate_wgs84;

        if (coordinate != nullptr)
        {
            longitude_sum += coordinate->longitude_deg;
            latitude_sum += coordinate->latitude_deg;
            positioned_count++;
        }
    }

    if (missing_ids.isEmpty())
        return;

    const double origin_longitude = positioned_count > 0
        ? longitude_sum / static_cast<double>(positioned_count) : 0.0;
    const double origin_latitude = positioned_count > 0
        ? latitude_sum / static_cast<double>(positioned_count) : 0.0;
    GeographicLib::LocalCartesian layout(origin_latitude, origin_longitude, 0.0);

    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(
        static_cast<double>(missing_ids.size())))));
    const int rows = static_cast<int>(std::ceil(
        static_cast<double>(missing_ids.size()) / static_cast<double>(columns)));
    constexpr double spacing_m = 100.0;

    for (int index = 0; index < missing_ids.size(); index++)
    {
        const int column = index % columns;
        const int row = index / columns;
        const double x_offset = positioned_count > 0 ? 200.0 : 0.0;
        const double x = x_offset
            + (static_cast<double>(column) - static_cast<double>(columns - 1) / 2.0) * spacing_m;
        const double y = (static_cast<double>(rows - 1) / 2.0 - static_cast<double>(row)) * spacing_m;

        double latitude = 0.0;
        double longitude = 0.0;
        double height = 0.0;
        layout.Reverse(x, y, 0.0, latitude, longitude, height);

        CoordinateWGS84 coordinate;
        coordinate.longitude_deg = longitude;
        coordinate.latitude_deg = latitude;
        assignNodeCoordinate(network, missing_ids.at(index), coordinate);
    }

    appendGeometryDiagnostic(
        result,
        HydraulicSimulationDiagnosticSeverity::Information,
        QStringLiteral("EPANET nodes without source coordinates received a deterministic schematic WGS84 layout."),
        {QStringLiteral("Generated coordinates for %1 node(s).").arg(missing_ids.size())});
}
}

HydraulicSimulationStatus importEpanetInpGeometry(
    EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result)
{
    SourceMapMetadata metadata;
    HydraulicSimulationStatus status = parseSourceMapMetadata(input_file_path, result, metadata);
    if (!status.success)
        return status;

    QList<ImportedNodeCoordinate> nodes;
    QList<ImportedLinkVertices> links;
    status = readNativeGeometry(project, nodes, links);
    if (!status.success)
        return status;

    GeometryTransform transform = makeGeometryTransform(
        metadata.units, nodes, links, metadata, result);

    NetworkHydraulic &network = result.request.network;
    for (const ImportedNodeCoordinate &node : nodes)
    {
        if (node.point.has_value())
            assignNodeCoordinate(network, node.id, transform.transform(node.point.value()));
    }

    for (const ImportedLinkVertices &link : links)
    {
        QList<HydraulicLinkVertex> vertices;
        vertices.reserve(link.points.size());
        for (const SourcePoint &point : link.points)
        {
            HydraulicLinkVertex vertex;
            vertex.coordinate_wgs84 = transform.transform(point);
            vertices.append(vertex);
        }
        assignLinkVertices(network, link.id, vertices);
    }

    generateMissingNodeCoordinates(network, nodes, result);

    for (int index = 0; index < metadata.labels.size(); index++)
    {
        const SourceMapLabel &source_label = metadata.labels.at(index);
        HydraulicMapLabel label;
        label.id = QStringLiteral("MapLabel%1").arg(index + 1);
        label.uuid = QUuid::createUuid();
        label.coordinate_wgs84 = transform.transform(source_label.point);
        label.text = source_label.text;
        if (!source_label.anchor_node_id.isEmpty())
        {
            label.anchor_node_uuid = nodeUuidById(network, source_label.anchor_node_id);
            if (label.anchor_node_uuid.isNull())
            {
                appendGeometryDiagnostic(
                    result,
                    HydraulicSimulationDiagnosticSeverity::Warning,
                    QStringLiteral("An EPANET map-label anchor node could not be resolved."),
                    {source_label.anchor_node_id},
                    true);
            }
        }
        network.map_labels.append(label);
    }

    if (metadata.backdrop.present)
    {
        network.map_backdrop.enabled = metadata.backdrop.lower_left.has_value()
            || metadata.backdrop.upper_right.has_value()
            || !metadata.backdrop.file.isEmpty()
            || metadata.backdrop.offset.x != 0.0
            || metadata.backdrop.offset.y != 0.0;
        network.map_backdrop.file = metadata.backdrop.file;
        if (metadata.backdrop.lower_left.has_value())
        {
            network.map_backdrop.lower_left_wgs84 = transform.transform(
                metadata.backdrop.lower_left.value());
        }
        if (metadata.backdrop.upper_right.has_value())
        {
            network.map_backdrop.upper_right_wgs84 = transform.transform(
                metadata.backdrop.upper_right.value());
        }

        const CoordinateWGS84 offset = transform.transformOffset(metadata.backdrop.offset);
        network.map_backdrop.offset_longitude_deg = offset.longitude_deg;
        network.map_backdrop.offset_latitude_deg = offset.latitude_deg;
    }

    if (!metadata.units_declared)
    {
        appendGeometryDiagnostic(
            result,
            HydraulicSimulationDiagnosticSeverity::Information,
            QStringLiteral("The INP file does not declare EPANET map units; geometry therefore follows the AOWIS arbitrary-unit import convention."));
    }

    return makeEpanetSuccess();
}
