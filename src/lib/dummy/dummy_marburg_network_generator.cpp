#include <aowis/epanet/dummy/dummy_marburg_network_generator.h>

#include <QChar>
#include <QSet>
#include <QString>
#include <QVector>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double marburg_center_latitude_deg = 50.8075;
constexpr double marburg_center_longitude_deg = 8.7708;
constexpr double metres_per_degree_latitude = 111320.0;
constexpr int trunk_junction_count = 72;
constexpr int tank_count = 8;
constexpr double district_spread_factor = 0.65;
constexpr double target_total_demand_m3_per_h = 630.0;

struct DistrictDefinition
{
    double center_x_m;
    double center_y_m;
    double sigma_x_m;
    double sigma_y_m;
    int junction_count;
    double demand_factor;
};

constexpr std::array<DistrictDefinition, 17> districts = {
    DistrictDefinition{0.0, 0.0, 850.0, 1100.0, 210, 1.55},
    DistrictDefinition{0.0, -1700.0, 900.0, 900.0, 150, 1.25},
    DistrictDefinition{0.0, 1800.0, 850.0, 900.0, 125, 1.15},
    DistrictDefinition{500.0, -200.0, 450.0, 700.0, 90, 1.35},
    DistrictDefinition{-1800.0, -1800.0, 850.0, 900.0, 120, 1.05},
    DistrictDefinition{-2500.0, 1000.0, 800.0, 850.0, 115, 1.00},
    DistrictDefinition{1500.0, 900.0, 700.0, 850.0, 110, 1.10},
    DistrictDefinition{2200.0, -2300.0, 850.0, 1100.0, 150, 1.30},
    DistrictDefinition{1000.0, -4300.0, 900.0, 1050.0, 135, 1.05},
    DistrictDefinition{-400.0, 4300.0, 950.0, 1000.0, 120, 1.05},
    DistrictDefinition{-4300.0, 3600.0, 800.0, 850.0, 85, 0.75},
    DistrictDefinition{4400.0, 1600.0, 750.0, 800.0, 70, 0.80},
    DistrictDefinition{2500.0, 5200.0, 650.0, 700.0, 60, 0.70},
    DistrictDefinition{5600.0, -3000.0, 700.0, 800.0, 65, 0.75},
    DistrictDefinition{4300.0, -6500.0, 650.0, 700.0, 50, 0.65},
    DistrictDefinition{-4300.0, -4300.0, 700.0, 800.0, 58, 0.65},
    DistrictDefinition{-1700.0, -6500.0, 600.0, 750.0, 45, 0.60}
};

constexpr std::array<std::array<int, 2>, 23> inter_district_connections = {
    std::array<int, 2>{0, 1},
    std::array<int, 2>{0, 2},
    std::array<int, 2>{0, 3},
    std::array<int, 2>{0, 4},
    std::array<int, 2>{0, 5},
    std::array<int, 2>{0, 6},
    std::array<int, 2>{1, 4},
    std::array<int, 2>{1, 7},
    std::array<int, 2>{1, 8},
    std::array<int, 2>{2, 5},
    std::array<int, 2>{2, 6},
    std::array<int, 2>{2, 9},
    std::array<int, 2>{4, 15},
    std::array<int, 2>{5, 10},
    std::array<int, 2>{6, 7},
    std::array<int, 2>{6, 11},
    std::array<int, 2>{7, 8},
    std::array<int, 2>{8, 13},
    std::array<int, 2>{8, 14},
    std::array<int, 2>{8, 16},
    std::array<int, 2>{9, 10},
    std::array<int, 2>{9, 12},
    std::array<int, 2>{11, 12}
};

struct GeneratedJunction
{
    HydraulicNodeJunction node;
    double x_m;
    double y_m;
    double raw_demand;
};

struct NearestNodes
{
    int first_index = -1;
    int second_index = -1;
    double first_distance_squared = std::numeric_limits<double>::max();
    double second_distance_squared = std::numeric_limits<double>::max();
};

enum class PipeClass
{
    Trunk,
    DistrictFeed,
    Local,
    InterDistrict,
    StorageConnection
};

double randomReal(std::mt19937_64 &generator, double minimum, double maximum)
{
    std::uniform_real_distribution<double> distribution(minimum, maximum);
    return distribution(generator);
}

bool randomChance(std::mt19937_64 &generator, double probability)
{
    std::bernoulli_distribution distribution(probability);
    return distribution(generator);
}

double randomNormal(std::mt19937_64 &generator, double mean, double standard_deviation)
{
    std::normal_distribution<double> distribution(mean, standard_deviation);
    return distribution(generator);
}

double clamp(double value, double minimum, double maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

double distanceSquared(double x_1_m, double y_1_m, double x_2_m, double y_2_m)
{
    const double delta_x_m = x_2_m - x_1_m;
    const double delta_y_m = y_2_m - y_1_m;
    return delta_x_m * delta_x_m + delta_y_m * delta_y_m;
}

double distance(double x_1_m, double y_1_m, double x_2_m, double y_2_m)
{
    return std::sqrt(distanceSquared(x_1_m, y_1_m, x_2_m, y_2_m));
}

double longitudeMetresPerDegree(double latitude_deg)
{
    return metres_per_degree_latitude * std::cos(latitude_deg * pi / 180.0);
}

template<typename Coordinate>
void setCoordinate(Coordinate &coordinate, double x_m, double y_m)
{
    coordinate.latitude_deg = marburg_center_latitude_deg + y_m / metres_per_degree_latitude;
    coordinate.longitude_deg = marburg_center_longitude_deg + x_m / longitudeMetresPerDegree(coordinate.latitude_deg);
}

double syntheticTerrainElevation(double x_m, double y_m, std::mt19937_64 &generator)
{
    const double lahn_x_m = 180.0 * std::sin(y_m / 2200.0) - 80.0;
    const double distance_from_lahn_m = std::abs(x_m - lahn_x_m);
    const double valley_gradient_m = 0.0008 * y_m;
    const double hill_rise_m = 0.012 * distance_from_lahn_m + 0.0000013 * distance_from_lahn_m * distance_from_lahn_m;
    const double local_relief_m = 5.0 * std::sin(x_m / 1050.0) + 4.0 * std::cos(y_m / 1350.0);
    const double noise_m = randomNormal(generator, 0.0, 3.0);
    return clamp(181.0 + valley_gradient_m + hill_rise_m + local_relief_m + noise_m, 174.0, 268.0);
}

QString nodeId(const QString &prefix, int index, int width)
{
    return QStringLiteral("%1%2").arg(prefix).arg(index + 1, width, 10, QChar('0'));
}

GeneratedJunction createJunction(int index, double x_m, double y_m, double demand_factor, std::mt19937_64 &generator)
{
    GeneratedJunction generated;
    generated.node.uuid = QUuid::createUuid();
    generated.node.id = nodeId(QStringLiteral("J"), index, 4);
    setCoordinate(generated.node.coordinate_wgs84, x_m, y_m);
    generated.node.elevation_m = syntheticTerrainElevation(x_m, y_m, generator);
    generated.x_m = x_m;
    generated.y_m = y_m;
    generated.raw_demand = demand_factor * randomReal(generator, 0.55, 1.45);
    return generated;
}

quint64 edgeKey(int node_index_1, int node_index_2)
{
    const quint32 lower_index = static_cast<quint32>(std::min(node_index_1, node_index_2));
    const quint32 upper_index = static_cast<quint32>(std::max(node_index_1, node_index_2));
    return (static_cast<quint64>(lower_index) << 32U) | static_cast<quint64>(upper_index);
}

NearestNodes findNearestOtherNodes(const QVector<GeneratedJunction> &junctions, const QVector<int> &candidate_indices, int current_index)
{
    NearestNodes nearest;
    const GeneratedJunction &current = junctions.at(current_index);

    for (int candidate_position = 0; candidate_position < candidate_indices.length(); candidate_position++)
    {
        const int candidate_index = candidate_indices.at(candidate_position);
        if (candidate_index == current_index)
            continue;

        const GeneratedJunction &candidate = junctions.at(candidate_index);
        const double candidate_distance_squared = distanceSquared(current.x_m, current.y_m, candidate.x_m, candidate.y_m);

        if (candidate_distance_squared < nearest.first_distance_squared)
        {
            nearest.second_index = nearest.first_index;
            nearest.second_distance_squared = nearest.first_distance_squared;
            nearest.first_index = candidate_index;
            nearest.first_distance_squared = candidate_distance_squared;
        }
        else if (candidate_distance_squared < nearest.second_distance_squared)
        {
            nearest.second_index = candidate_index;
            nearest.second_distance_squared = candidate_distance_squared;
        }
    }

    return nearest;
}

int findNearestJunction(const QVector<GeneratedJunction> &junctions, double x_m, double y_m)
{
    int nearest_index = -1;
    double nearest_distance_squared = std::numeric_limits<double>::max();

    for (int index = 0; index < junctions.length(); index++)
    {
        const GeneratedJunction &junction = junctions.at(index);
        const double candidate_distance_squared = distanceSquared(x_m, y_m, junction.x_m, junction.y_m);
        if (candidate_distance_squared < nearest_distance_squared)
        {
            nearest_index = index;
            nearest_distance_squared = candidate_distance_squared;
        }
    }

    return nearest_index;
}

std::array<int, 2> findClosestDistrictPair(const QVector<GeneratedJunction> &junctions, const QVector<int> &first_district_indices, const QVector<int> &second_district_indices)
{
    std::array<int, 2> closest_pair = {-1, -1};
    double closest_distance_squared = std::numeric_limits<double>::max();

    for (int first_position = 0; first_position < first_district_indices.length(); first_position++)
    {
        const int first_index = first_district_indices.at(first_position);
        const GeneratedJunction &first = junctions.at(first_index);

        for (int second_position = 0; second_position < second_district_indices.length(); second_position++)
        {
            const int second_index = second_district_indices.at(second_position);
            const GeneratedJunction &second = junctions.at(second_index);
            const double candidate_distance_squared = distanceSquared(first.x_m, first.y_m, second.x_m, second.y_m);

            if (candidate_distance_squared < closest_distance_squared)
            {
                closest_pair = {first_index, second_index};
                closest_distance_squared = candidate_distance_squared;
            }
        }
    }

    return closest_pair;
}

double choosePipeDiameterMm(PipeClass pipe_class, double length_m, std::mt19937_64 &generator)
{
    switch (pipe_class)
    {
    case PipeClass::Trunk:
        return randomChance(generator, 0.30) ? 500.0 : 400.0;
    case PipeClass::DistrictFeed:
        return length_m > 2500.0 ? 350.0 : 300.0;
    case PipeClass::InterDistrict:
        return length_m > 1800.0 ? 300.0 : 250.0;
    case PipeClass::StorageConnection:
        return randomChance(generator, 0.35) ? 300.0 : 250.0;
    case PipeClass::Local:
        if (length_m > 600.0)
            return 200.0;
        if (length_m > 350.0)
            return randomChance(generator, 0.45) ? 200.0 : 150.0;
        if (length_m > 180.0)
            return randomChance(generator, 0.55) ? 150.0 : 125.0;
        return randomChance(generator, 0.55) ? 100.0 : 80.0;
    }

    return 100.0;
}

void appendPipe(NetworkHydraulic &network, const QUuid &from_uuid, const QUuid &to_uuid, double straight_length_m, PipeClass pipe_class, int &pipe_index, std::mt19937_64 &generator)
{
    HydraulicLinkPipe pipe;
    pipe.uuid = QUuid::createUuid();
    pipe.id = nodeId(QStringLiteral("P"), pipe_index, 5);
    pipe.node_uuid_from = from_uuid;
    pipe.node_uuid_to = to_uuid;
    pipe.length_calculated_m = std::max(25.0, straight_length_m * randomReal(generator, 1.04, 1.22));
    pipe.diameter_mm = choosePipeDiameterMm(pipe_class, pipe.length_calculated_m, generator);
    pipe.roughness_hw = randomReal(generator, 122.0, 140.0);
    pipe.minor_loss = 0.0;
    pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
    network.links_pipes.append(pipe);
    pipe_index++;
}

void appendJunctionPipe(NetworkHydraulic &network, const QVector<GeneratedJunction> &junctions, int from_index, int to_index, PipeClass pipe_class, int &pipe_index, QSet<quint64> &edges, std::mt19937_64 &generator)
{
    if (from_index < 0 || to_index < 0 || from_index == to_index)
        return;

    const quint64 key = edgeKey(from_index, to_index);
    if (edges.contains(key))
        return;

    const GeneratedJunction &from = junctions.at(from_index);
    const GeneratedJunction &to = junctions.at(to_index);
    appendPipe(network, from.node.uuid, to.node.uuid, distance(from.x_m, from.y_m, to.x_m, to.y_m), pipe_class, pipe_index, generator);
    edges.insert(key);
}

void appendDistrictPipes(NetworkHydraulic &network, const QVector<GeneratedJunction> &junctions, const QVector<int> &indices, int &pipe_index, QSet<quint64> &edges, std::mt19937_64 &generator)
{
    if (indices.length() < 2)
        return;

    QVector<int> connected;
    QVector<int> nearest_connected_position;
    QVector<double> nearest_connected_distance_squared;
    connected.resize(indices.length());
    nearest_connected_position.resize(indices.length());
    nearest_connected_distance_squared.resize(indices.length());

    connected[0] = 1;
    nearest_connected_position[0] = -1;
    nearest_connected_distance_squared[0] = 0.0;

    const GeneratedJunction &gateway = junctions.at(indices.first());
    for (int position = 1; position < indices.length(); position++)
    {
        const GeneratedJunction &candidate = junctions.at(indices.at(position));
        nearest_connected_position[position] = 0;
        nearest_connected_distance_squared[position] = distanceSquared(gateway.x_m, gateway.y_m, candidate.x_m, candidate.y_m);
    }

    for (int added_count = 1; added_count < indices.length(); added_count++)
    {
        int next_position = -1;
        double next_distance_squared = std::numeric_limits<double>::max();

        for (int position = 1; position < indices.length(); position++)
        {
            if (connected.at(position) == 0 && nearest_connected_distance_squared.at(position) < next_distance_squared)
            {
                next_position = position;
                next_distance_squared = nearest_connected_distance_squared.at(position);
            }
        }

        if (next_position < 0)
            break;

        const int parent_position = nearest_connected_position.at(next_position);
        appendJunctionPipe(network, junctions, indices.at(parent_position), indices.at(next_position), PipeClass::Local, pipe_index, edges, generator);
        connected[next_position] = 1;

        const GeneratedJunction &next = junctions.at(indices.at(next_position));
        for (int position = 1; position < indices.length(); position++)
        {
            if (connected.at(position) != 0)
                continue;

            const GeneratedJunction &candidate = junctions.at(indices.at(position));
            const double candidate_distance_squared = distanceSquared(next.x_m, next.y_m, candidate.x_m, candidate.y_m);
            if (candidate_distance_squared < nearest_connected_distance_squared.at(position))
            {
                nearest_connected_position[position] = next_position;
                nearest_connected_distance_squared[position] = candidate_distance_squared;
            }
        }
    }

    for (int position = 0; position < indices.length(); position++)
    {
        const int current_index = indices.at(position);
        const NearestNodes nearest = findNearestOtherNodes(junctions, indices, current_index);
        if (nearest.second_index >= 0 && randomChance(generator, 0.34))
            appendJunctionPipe(network, junctions, current_index, nearest.second_index, PipeClass::Local, pipe_index, edges, generator);
    }
}

HydraulicCurveTankVolume createTankVolumeCurve(int tank_index, double maximum_level_m, double minimum_level_m, double nominal_area_m2, double &minimum_volume_m3)
{
    HydraulicCurveTankVolume curve;
    curve.uuid = QUuid::createUuid();
    curve.id = nodeId(QStringLiteral("VC_T"), tank_index, 2);

    const int point_count = 7;
    minimum_volume_m3 = 0.0;

    for (int point_index = 0; point_index < point_count; point_index++)
    {
        const double fraction = static_cast<double>(point_index) / static_cast<double>(point_count - 1);
        const double level_m = maximum_level_m * fraction;
        const double widening_factor = 0.72 + 0.28 * fraction;

        HydraulicCurveTankVolumePoint point;
        point.water_level_m = level_m;
        point.volume_m3 = nominal_area_m2 * level_m * widening_factor;
        curve.points.append(point);

        if (point_index > 0)
        {
            const HydraulicCurveTankVolumePoint &previous_point = curve.points.at(point_index - 1);
            if (minimum_level_m >= previous_point.water_level_m && minimum_level_m <= point.water_level_m)
            {
                const double interval_fraction = (minimum_level_m - previous_point.water_level_m) / (point.water_level_m - previous_point.water_level_m);
                minimum_volume_m3 = previous_point.volume_m3 + interval_fraction * (point.volume_m3 - previous_point.volume_m3);
            }
        }
    }

    return curve;
}
}

NetworkHydraulic DummyMarburgNetworkGenerator::generate()
{
    std::random_device random_device;
    const quint64 high = static_cast<quint64>(random_device()) << 32U;
    const quint64 low = static_cast<quint64>(random_device());
    return generate(high ^ low);
}

NetworkHydraulic DummyMarburgNetworkGenerator::generate(quint64 seed)
{
    std::mt19937_64 generator(seed);
    NetworkHydraulic network;
    QVector<GeneratedJunction> junctions;
    QVector<QVector<int>> district_junction_indices;
    district_junction_indices.resize(static_cast<int>(districts.size()));

    int total_junction_count = trunk_junction_count;
    for (int district_index = 0; district_index < static_cast<int>(districts.size()); district_index++)
        total_junction_count += districts.at(static_cast<std::size_t>(district_index)).junction_count;

    junctions.reserve(total_junction_count);

    for (int trunk_index = 0; trunk_index < trunk_junction_count; trunk_index++)
    {
        const double fraction = static_cast<double>(trunk_index) / static_cast<double>(trunk_junction_count - 1);
        const double y_m = -7600.0 + fraction * 15200.0;
        const double x_m = 180.0 * std::sin(y_m / 2200.0) - 80.0 + randomNormal(generator, 0.0, 55.0);
        junctions.append(createJunction(junctions.length(), x_m, y_m, 0.55, generator));
    }

    for (int district_index = 0; district_index < static_cast<int>(districts.size()); district_index++)
    {
        const DistrictDefinition &district = districts.at(static_cast<std::size_t>(district_index));
        QVector<int> &indices = district_junction_indices[district_index];
        indices.reserve(district.junction_count);

        for (int district_node_index = 0; district_node_index < district.junction_count; district_node_index++)
        {
            double x_m = district.center_x_m;
            double y_m = district.center_y_m;

            if (district_node_index == 0)
            {
                x_m += randomNormal(generator, 0.0, 65.0);
                y_m += randomNormal(generator, 0.0, 65.0);
            }
            else
            {
                const double sigma_x_m = district.sigma_x_m * district_spread_factor;
                const double sigma_y_m = district.sigma_y_m * district_spread_factor;
                x_m += clamp(randomNormal(generator, 0.0, sigma_x_m), -2.4 * sigma_x_m, 2.4 * sigma_x_m);
                y_m += clamp(randomNormal(generator, 0.0, sigma_y_m), -2.4 * sigma_y_m, 2.4 * sigma_y_m);
            }

            const int junction_index = junctions.length();
            junctions.append(createJunction(junction_index, x_m, y_m, district.demand_factor, generator));
            indices.append(junction_index);
        }
    }

    double raw_total_demand = 0.0;
    for (int junction_index = 0; junction_index < junctions.length(); junction_index++)
        raw_total_demand += junctions.at(junction_index).raw_demand;

    const double demand_scale = target_total_demand_m3_per_h / raw_total_demand;
    for (int junction_index = 0; junction_index < junctions.length(); junction_index++)
    {
        GeneratedJunction &junction = junctions[junction_index];
        HydraulicNodeJunctionDemand demand;
        demand.base_demand_m3_per_h = junction.raw_demand * demand_scale;
        junction.node.demands.append(demand);
        network.nodes_junctions.append(junction.node);
    }

    QSet<quint64> edges;
    int pipe_index = 0;

    for (int trunk_index = 1; trunk_index < trunk_junction_count; trunk_index++)
        appendJunctionPipe(network, junctions, trunk_index - 1, trunk_index, PipeClass::Trunk, pipe_index, edges, generator);

    for (int district_index = 0; district_index < district_junction_indices.length(); district_index++)
    {
        const QVector<int> &indices = district_junction_indices.at(district_index);
        if (indices.isEmpty())
            continue;

        const int gateway_index = indices.first();
        const GeneratedJunction &gateway = junctions.at(gateway_index);
        int nearest_trunk_index = 0;
        double nearest_trunk_distance_squared = std::numeric_limits<double>::max();

        for (int trunk_index = 0; trunk_index < trunk_junction_count; trunk_index++)
        {
            const GeneratedJunction &trunk = junctions.at(trunk_index);
            const double candidate_distance_squared = distanceSquared(gateway.x_m, gateway.y_m, trunk.x_m, trunk.y_m);
            if (candidate_distance_squared < nearest_trunk_distance_squared)
            {
                nearest_trunk_index = trunk_index;
                nearest_trunk_distance_squared = candidate_distance_squared;
            }
        }

        appendJunctionPipe(network, junctions, nearest_trunk_index, gateway_index, PipeClass::DistrictFeed, pipe_index, edges, generator);

        appendDistrictPipes(network, junctions, indices, pipe_index, edges, generator);
    }

    for (int connection_index = 0; connection_index < static_cast<int>(inter_district_connections.size()); connection_index++)
    {
        const std::array<int, 2> connection = inter_district_connections.at(static_cast<std::size_t>(connection_index));
        const std::array<int, 2> closest_pair = findClosestDistrictPair(junctions, district_junction_indices.at(connection[0]), district_junction_indices.at(connection[1]));
        appendJunctionPipe(network, junctions, closest_pair[0], closest_pair[1], PipeClass::InterDistrict, pipe_index, edges, generator);
    }

    const std::array<std::array<double, 3>, 2> reservoir_definitions = {
        std::array<double, 3>{-3600.0, 7200.0, 302.0},
        std::array<double, 3>{3900.0, -7300.0, 296.0}
    };

    for (int reservoir_index = 0; reservoir_index < static_cast<int>(reservoir_definitions.size()); reservoir_index++)
    {
        const std::array<double, 3> definition = reservoir_definitions.at(static_cast<std::size_t>(reservoir_index));
        HydraulicNodeReservoir reservoir;
        reservoir.uuid = QUuid::createUuid();
        reservoir.id = nodeId(QStringLiteral("R"), reservoir_index, 1);
        setCoordinate(reservoir.coordinate_wgs84, definition[0], definition[1]);
        reservoir.head_m = definition[2] + randomReal(generator, -2.0, 2.0);
        network.nodes_reservoirs.append(reservoir);

        const int nearest_junction_index = findNearestJunction(junctions, definition[0], definition[1]);
        const GeneratedJunction &nearest_junction = junctions.at(nearest_junction_index);
        appendPipe(network, reservoir.uuid, nearest_junction.node.uuid, distance(definition[0], definition[1], nearest_junction.x_m, nearest_junction.y_m), PipeClass::DistrictFeed, pipe_index, generator);
    }

    const std::array<int, tank_count> tank_districts = {5, 6, 7, 9, 10, 11, 13, 15};
    for (int tank_index = 0; tank_index < tank_count; tank_index++)
    {
        const DistrictDefinition &district = districts.at(static_cast<std::size_t>(tank_districts.at(static_cast<std::size_t>(tank_index))));
        const double direction = tank_index % 2 == 0 ? -1.0 : 1.0;
        const double x_m = district.center_x_m + direction * randomReal(generator, 350.0, 700.0);
        const double y_m = district.center_y_m + randomReal(generator, -500.0, 500.0);
        const double bottom_elevation_m = syntheticTerrainElevation(x_m, y_m, generator) + randomReal(generator, 8.0, 18.0);
        const double minimum_level_m = 2.0;
        const double maximum_level_m = randomReal(generator, 12.0, 18.0);
        const double initial_level_m = randomReal(generator, 0.42 * maximum_level_m, 0.72 * maximum_level_m);
        const double nominal_area_m2 = randomReal(generator, 250.0, 700.0);

        HydraulicNodeTank tank;
        tank.uuid = QUuid::createUuid();
        tank.id = nodeId(QStringLiteral("T"), tank_index, 2);
        setCoordinate(tank.coordinate_wgs84, x_m, y_m);
        tank.bottom_elevation_m = bottom_elevation_m;
        tank.water_level_initial_m = initial_level_m;
        tank.water_level_minimum_m = minimum_level_m;
        tank.water_level_maximum_m = maximum_level_m;
        tank.can_overflow = false;

        const int geometry_index = tank_index % 4;
        if (geometry_index == 0)
        {
            tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;
            tank.diameter_m = std::sqrt(4.0 * nominal_area_m2 / pi);
            tank.minimum_volume_m3 = 0.0;
        }
        else if (geometry_index == 1)
        {
            tank.geometry_input_type = HydraulicNodeTankGeometryInputType::UniformArea;
            tank.cross_section_area_m2 = nominal_area_m2;
            tank.minimum_volume_m3 = nominal_area_m2 * minimum_level_m;
        }
        else if (geometry_index == 2)
        {
            tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel;
            tank.minimum_volume_m3 = nominal_area_m2 * minimum_level_m;
            tank.volume_at_maximum_level_m3 = nominal_area_m2 * maximum_level_m;
        }
        else
        {
            tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeCurve;
            double minimum_volume_m3 = 0.0;
            HydraulicCurveTankVolume curve = createTankVolumeCurve(tank_index, maximum_level_m, minimum_level_m, nominal_area_m2, minimum_volume_m3);
            tank.volume_curve_uuid = curve.uuid;
            tank.minimum_volume_m3 = minimum_volume_m3;
            network.curves_tank_volume.append(curve);
        }

        network.nodes_tanks.append(tank);

        const int nearest_junction_index = findNearestJunction(junctions, x_m, y_m);
        const GeneratedJunction &nearest_junction = junctions.at(nearest_junction_index);
        appendPipe(network, tank.uuid, nearest_junction.node.uuid, distance(x_m, y_m, nearest_junction.x_m, nearest_junction.y_m), PipeClass::StorageConnection, pipe_index, generator);
    }

    return network;
}
