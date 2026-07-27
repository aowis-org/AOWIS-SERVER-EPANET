#include <aowis/epanet/dummy/dummy_networks.h>

namespace
{
Reservoir makeReservoir(const QString &id, double head_m, double longitude, double latitude)
{
    Reservoir reservoir;
    reservoir.id = id;
    reservoir.uuid = QUuid::createUuid();
    reservoir.head_m = head_m;
    reservoir.longitude = longitude;
    reservoir.latitude = latitude;
    return reservoir;
}

Junction makeJunction(const QString &id, double elevation_m, double demand_m3h, double longitude, double latitude)
{
    Junction junction;
    junction.id = id;
    junction.uuid = QUuid::createUuid();
    junction.elevation_m = elevation_m;
    junction.longitude = longitude;
    junction.latitude = latitude;
    if (demand_m3h > 0.0)
    {
        JunctionDemand demand;
        demand.base_demand_m3h = demand_m3h;
        junction.demands.append(demand);
    }
    return junction;
}

Pipe makePipe(const QString &id, const QString &from, const QString &to, double length_m, double diameter_mm)
{
    Pipe pipe;
    pipe.id = id;
    pipe.uuid = QUuid::createUuid();
    pipe.node_id_from = from;
    pipe.node_id_to = to;
    pipe.length_calculated_m = length_m;
    pipe.diameter_mm = diameter_mm;
    pipe.roughness_hw = 130.0;
    return pipe;
}

NetworkHydraulic asTimeline(NetworkHydraulic network)
{
    network.duration_s = 24 * 60 * 60;
    network.hydraulic_timestep_s = 60 * 60;
    return network;
}
}

NetworkHydraulic DummyNetworks::networkSimple()
{
    NetworkHydraulic network;
    network.uuid = QUuid::createUuid();
    network.reservoirs.append(makeReservoir(QStringLiteral("R1"), 110.0, 14.00, 12.00));
    network.junctions.append(makeJunction(QStringLiteral("J1"), 90.0, 18.0, 14.01, 12.00));
    network.junctions.append(makeJunction(QStringLiteral("J2"), 87.0, 12.0, 14.02, 12.00));
    network.pipes.append(makePipe(QStringLiteral("P1"), QStringLiteral("R1"), QStringLiteral("J1"), 500.0, 200.0));
    network.pipes.append(makePipe(QStringLiteral("P2"), QStringLiteral("J1"), QStringLiteral("J2"), 400.0, 150.0));
    return network;
}

NetworkHydraulic DummyNetworks::networkOnMap()
{
    NetworkHydraulic network = networkSimple();
    network.reservoirs[0].longitude = 18.2115;
    network.reservoirs[0].latitude = 11.9800;
    network.junctions[0].longitude = 18.2180;
    network.junctions[0].latitude = 11.9760;
    network.junctions[1].longitude = 18.2240;
    network.junctions[1].latitude = 11.9720;
    return network;
}

NetworkHydraulic DummyNetworks::networkTanks()
{
    NetworkHydraulic network = networkSimple();

    Tank tank;
    tank.id = QStringLiteral("T1");
    tank.uuid = QUuid::createUuid();
    tank.bottom_elevation_m = 95.0;
    tank.initial_level_m = 4.0;
    tank.minimum_level_m = 1.0;
    tank.maximum_level_m = 8.0;
    tank.diameter_m = 12.0;
    tank.longitude = 14.03;
    tank.latitude = 12.00;
    network.tanks.append(tank);
    network.pipes.append(makePipe(QStringLiteral("P3"), QStringLiteral("J2"), QStringLiteral("T1"), 600.0, 150.0));
    return network;
}

NetworkHydraulic DummyNetworks::networkFull()
{
    NetworkHydraulic network = networkTanks();

    TankVolumeCurve curve;
    curve.id = QStringLiteral("VC1");
    curve.uuid = QUuid::createUuid();
    curve.points.append({0.0, 0.0});
    curve.points.append({2.0, 180.0});
    curve.points.append({5.0, 520.0});
    curve.points.append({8.0, 920.0});
    network.tank_volume_curves.append(curve);

    Tank curve_tank;
    curve_tank.id = QStringLiteral("T2");
    curve_tank.uuid = QUuid::createUuid();
    curve_tank.bottom_elevation_m = 92.0;
    curve_tank.initial_level_m = 3.0;
    curve_tank.minimum_level_m = 0.0;
    curve_tank.maximum_level_m = 8.0;
    curve_tank.geometry_input_type = TankGeometryInputType::VolumeCurve;
    curve_tank.volume_curve_id = curve.id;
    network.tanks.append(curve_tank);
    network.pipes.append(makePipe(QStringLiteral("P4"), QStringLiteral("J1"), QStringLiteral("T2"), 800.0, 125.0));
    return network;
}

NetworkHydraulic DummyNetworks::networkSimpleTimeline()
{
    return asTimeline(networkSimple());
}

NetworkHydraulic DummyNetworks::networkTanksTimeline()
{
    return asTimeline(networkTanks());
}

NetworkHydraulic DummyNetworks::networkFullTimeline()
{
    return asTimeline(networkFull());
}
