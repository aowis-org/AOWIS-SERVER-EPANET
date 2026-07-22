#include "dummy_networks.h"

NetworkHydraulic DummyNetworks::networkSimple()
{
    Reservoir reservoir;
    reservoir.uuid = QUuid::createUuid();
    reservoir.id = "R1";
    reservoir.head_m = 30.0;
    
    Junction junction;
    junction.uuid = QUuid::createUuid();
    junction.id = "J1";
    junction.elevation_m = 0.0;
    junction.demand_lps = 1.0;
    
    Pipe pipe;
    pipe.uuid = QUuid::createUuid();
    pipe.id = "P1";
    pipe.node_id_from = reservoir.id;
    pipe.node_id_to = junction.id;
    pipe.length_calculated_m = 100.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hw = 130.0;
    pipe.minor_loss = 0.0;
    pipe.open = true;
    
    NetworkHydraulic network;
    network.reservoirs.append(reservoir);
    network.junctions.append(junction);
    network.pipes.append(pipe);
    
    return network;
}

NetworkHydraulic DummyNetworks::networkOnMap()
{
    Tank tank_1;
    tank_1.uuid = QUuid::createUuid();
    tank_1.id = "T1";

    tank_1.latitude = 11.98385;
    tank_1.longitude = 18.20381;
    
    tank_1.bottom_elevation_m = 533.0;
    
    tank_1.initial_level_m = 6.0;
    tank_1.minimum_level_m = 2.0;
    tank_1.maximum_level_m = 16.0;
    
    tank_1.geometry_input_type = TankGeometryInputType::Cylindrical;
    
    tank_1.diameter_m = 8.0;
    tank_1.minimum_volume_m3 = 0.0;
    tank_1.can_overflow = false;
    
    Junction junction_1;
    junction_1.uuid = QUuid::createUuid();
    junction_1.id = "J1";
    
    junction_1.latitude = 11.98108;
    junction_1.longitude = 18.20373;
    
    junction_1.elevation_m = 477.0;
    junction_1.demand_lps = 1.0;
    
    Pipe pipe_1;
    pipe_1.uuid = QUuid::createUuid();
    pipe_1.id = "P1";
    
    pipe_1.node_id_from = tank_1.id;
    pipe_1.node_id_to = junction_1.id;
    
    pipe_1.length_calculated_m = 250.0;
    pipe_1.diameter_mm = 300.0;
    pipe_1.roughness_hw = 130.0;
    pipe_1.minor_loss = 0.0;
    pipe_1.open = true;
    
    NetworkHydraulic network;
    network.tanks.append(tank_1);
    network.junctions.append(junction_1);
    network.pipes.append(pipe_1);
    
    return network;
}

NetworkHydraulic DummyNetworks::networkTanks()
{
    // Create a more complex dummy network.
    
    // ------------------------------------------------------------
    // Reservoir
    // ------------------------------------------------------------
    
    Reservoir reservoir;
    reservoir.uuid = QUuid::createUuid();
    reservoir.id = "R1";
    reservoir.head_m = 75.0;
    
    
    // ------------------------------------------------------------
    // Junctions
    // ------------------------------------------------------------
    
    Junction junction_1;
    junction_1.uuid = QUuid::createUuid();
    junction_1.id = "J1";
    junction_1.elevation_m = 45.0;
    junction_1.demand_lps = 1.5;
    
    Junction junction_2;
    junction_2.uuid = QUuid::createUuid();
    junction_2.id = "J2";
    junction_2.elevation_m = 50.0;
    junction_2.demand_lps = 2.0;
    
    Junction junction_3;
    junction_3.uuid = QUuid::createUuid();
    junction_3.id = "J3";
    junction_3.elevation_m = 60.0;
    junction_3.demand_lps = 1.0;
    
    Junction junction_4;
    junction_4.uuid = QUuid::createUuid();
    junction_4.id = "J4";
    junction_4.elevation_m = 35.0;
    junction_4.demand_lps = 1.5;
    
    Junction junction_5;
    junction_5.uuid = QUuid::createUuid();
    junction_5.id = "J5";
    junction_5.elevation_m = 55.0;
    junction_5.demand_lps = 1.0;
    
    
    // ------------------------------------------------------------
    // Tank 1: cylindrical
    //
    // Initial hydraulic head:
    //     58 m elevation + 6 m level = 64 m
    //
    // This is below the reservoir head of 75 m, so this tank
    // will initially tend to fill.
    // ------------------------------------------------------------
    
    Tank tank_1;
    tank_1.uuid = QUuid::createUuid();
    tank_1.id = "T1";
    
    tank_1.bottom_elevation_m = 58.0;
    
    tank_1.initial_level_m = 6.0;
    tank_1.minimum_level_m = 2.0;
    tank_1.maximum_level_m = 16.0;
    
    tank_1.geometry_input_type = TankGeometryInputType::Cylindrical;
    
    tank_1.diameter_m = 8.0;
    tank_1.minimum_volume_m3 = 0.0;
    tank_1.can_overflow = false;
    
    
    // ------------------------------------------------------------
    // Tank 2: uniform cross-sectional area
    //
    // Initial hydraulic head:
    //     68 m elevation + 12 m level = 80 m
    //
    // This is above the reservoir head, so this tank will
    // initially tend to drain.
    // ------------------------------------------------------------
    
    Tank tank_2;
    tank_2.uuid = QUuid::createUuid();
    tank_2.id = "T2";
    
    tank_2.bottom_elevation_m = 68.0;
    
    tank_2.initial_level_m = 12.0;
    tank_2.minimum_level_m = 2.0;
    tank_2.maximum_level_m = 18.0;
    
    tank_2.geometry_input_type = TankGeometryInputType::UniformArea;
    
    tank_2.cross_section_area_m2 = 60.0;
    tank_2.minimum_volume_m3 = 20.0;
    tank_2.can_overflow = false;
    
    
    // ------------------------------------------------------------
    // Tank 3: volume at maximum level
    //
    // Initial hydraulic head:
    //     45 m elevation + 8 m level = 53 m
    //
    // This is well below the reservoir head, so this tank will
    // initially tend to fill.
    //
    // Usable height:
    //     20 m - 2 m = 18 m
    //
    // Usable volume:
    //     490 m³ - 40 m³ = 450 m³
    //
    // Equivalent uniform area:
    //     450 m³ / 18 m = 25 m²
    // ------------------------------------------------------------
    
    Tank tank_3;
    tank_3.uuid = QUuid::createUuid();
    tank_3.id = "T3";
    
    tank_3.bottom_elevation_m = 45.0;
    
    tank_3.initial_level_m = 8.0;
    tank_3.minimum_level_m = 2.0;
    tank_3.maximum_level_m = 20.0;
    
    tank_3.geometry_input_type = TankGeometryInputType::VolumeAtMaximumLevel;
    
    tank_3.minimum_volume_m3 = 40.0;
    tank_3.volume_at_maximum_level_m3 = 490.0;
    tank_3.can_overflow = false;
    
    
    // ------------------------------------------------------------
    // Tank 4: non-uniform tank with volume curve
    //
    // Initial hydraulic head:
    //     70 m elevation + 12 m level = 82 m
    //
    // This is above the reservoir head, so this tank will
    // initially tend to drain.
    // ------------------------------------------------------------
    
    TankVolumeCurve tank_4_curve;
    tank_4_curve.uuid = QUuid::createUuid();
    tank_4_curve.id = "VC_T4";
    
    TankVolumeCurvePoint tank_4_curve_point_1;
    tank_4_curve_point_1.level_m = 0.0;
    tank_4_curve_point_1.volume_m3 = 0.0;
    tank_4_curve.points.append(tank_4_curve_point_1);
    
    TankVolumeCurvePoint tank_4_curve_point_2;
    tank_4_curve_point_2.level_m = 3.0;
    tank_4_curve_point_2.volume_m3 = 25.0;
    tank_4_curve.points.append(tank_4_curve_point_2);
    
    TankVolumeCurvePoint tank_4_curve_point_3;
    tank_4_curve_point_3.level_m = 6.0;
    tank_4_curve_point_3.volume_m3 = 65.0;
    tank_4_curve.points.append(tank_4_curve_point_3);
    
    TankVolumeCurvePoint tank_4_curve_point_4;
    tank_4_curve_point_4.level_m = 9.0;
    tank_4_curve_point_4.volume_m3 = 120.0;
    tank_4_curve.points.append(tank_4_curve_point_4);
    
    TankVolumeCurvePoint tank_4_curve_point_5;
    tank_4_curve_point_5.level_m = 12.0;
    tank_4_curve_point_5.volume_m3 = 190.0;
    tank_4_curve.points.append(tank_4_curve_point_5);
    
    TankVolumeCurvePoint tank_4_curve_point_6;
    tank_4_curve_point_6.level_m = 16.0;
    tank_4_curve_point_6.volume_m3 = 310.0;
    tank_4_curve.points.append(tank_4_curve_point_6);
    
    Tank tank_4;
    tank_4.uuid = QUuid::createUuid();
    tank_4.id = "T4";
    
    tank_4.bottom_elevation_m = 70.0;
    
    tank_4.initial_level_m = 12.0;
    tank_4.minimum_level_m = 3.0;
    tank_4.maximum_level_m = 16.0;
    
    tank_4.geometry_input_type = TankGeometryInputType::VolumeCurve;
    
    tank_4.volume_curve_id = tank_4_curve.id;
    
    // Keep this consistent with the curve volume at the minimum level.
    tank_4.minimum_volume_m3 = 25.0;
    
    tank_4.can_overflow = false;
    
    
    // ------------------------------------------------------------
    // Distribution pipes
    // ------------------------------------------------------------
    
    // Reservoir connection.
    Pipe pipe_1;
    pipe_1.uuid = QUuid::createUuid();
    pipe_1.id = "P1";
    pipe_1.node_id_from = reservoir.id;
    pipe_1.node_id_to = junction_1.id;
    pipe_1.length_calculated_m = 250.0;
    pipe_1.diameter_mm = 300.0;
    pipe_1.roughness_hw = 130.0;
    pipe_1.minor_loss = 0.0;
    pipe_1.open = true;
    
    // Upper-left distribution branch.
    Pipe pipe_2;
    pipe_2.uuid = QUuid::createUuid();
    pipe_2.id = "P2";
    pipe_2.node_id_from = junction_1.id;
    pipe_2.node_id_to = junction_2.id;
    pipe_2.length_calculated_m = 300.0;
    pipe_2.diameter_mm = 250.0;
    pipe_2.roughness_hw = 130.0;
    pipe_2.minor_loss = 0.0;
    pipe_2.open = true;
    
    // Upper-right distribution branch.
    Pipe pipe_3;
    pipe_3.uuid = QUuid::createUuid();
    pipe_3.id = "P3";
    pipe_3.node_id_from = junction_1.id;
    pipe_3.node_id_to = junction_3.id;
    pipe_3.length_calculated_m = 350.0;
    pipe_3.diameter_mm = 250.0;
    pipe_3.roughness_hw = 130.0;
    pipe_3.minor_loss = 0.0;
    pipe_3.open = true;
    
    // Left-side branch.
    Pipe pipe_4;
    pipe_4.uuid = QUuid::createUuid();
    pipe_4.id = "P4";
    pipe_4.node_id_from = junction_2.id;
    pipe_4.node_id_to = junction_4.id;
    pipe_4.length_calculated_m = 400.0;
    pipe_4.diameter_mm = 200.0;
    pipe_4.roughness_hw = 130.0;
    pipe_4.minor_loss = 0.0;
    pipe_4.open = true;
    
    // Right-side branch.
    Pipe pipe_5;
    pipe_5.uuid = QUuid::createUuid();
    pipe_5.id = "P5";
    pipe_5.node_id_from = junction_3.id;
    pipe_5.node_id_to = junction_5.id;
    pipe_5.length_calculated_m = 375.0;
    pipe_5.diameter_mm = 200.0;
    pipe_5.roughness_hw = 130.0;
    pipe_5.minor_loss = 0.0;
    pipe_5.open = true;
    
    // Lower cross-connection creates a loop.
    Pipe pipe_6;
    pipe_6.uuid = QUuid::createUuid();
    pipe_6.id = "P6";
    pipe_6.node_id_from = junction_4.id;
    pipe_6.node_id_to = junction_5.id;
    pipe_6.length_calculated_m = 500.0;
    pipe_6.diameter_mm = 180.0;
    pipe_6.roughness_hw = 130.0;
    pipe_6.minor_loss = 0.0;
    pipe_6.open = true;
    
    // Upper cross-connection creates another loop.
    Pipe pipe_7;
    pipe_7.uuid = QUuid::createUuid();
    pipe_7.id = "P7";
    pipe_7.node_id_from = junction_2.id;
    pipe_7.node_id_to = junction_3.id;
    pipe_7.length_calculated_m = 450.0;
    pipe_7.diameter_mm = 180.0;
    pipe_7.roughness_hw = 130.0;
    pipe_7.minor_loss = 0.0;
    pipe_7.open = true;
    
    
    // ------------------------------------------------------------
    // Tank connection pipes
    // ------------------------------------------------------------
    
    Pipe tank_pipe_1;
    tank_pipe_1.uuid = QUuid::createUuid();
    tank_pipe_1.id = "PT1";
    tank_pipe_1.node_id_from = tank_1.id;
    tank_pipe_1.node_id_to = junction_2.id;
    tank_pipe_1.length_calculated_m = 80.0;
    tank_pipe_1.diameter_mm = 200.0;
    tank_pipe_1.roughness_hw = 130.0;
    tank_pipe_1.minor_loss = 0.0;
    tank_pipe_1.open = true;
    
    Pipe tank_pipe_2;
    tank_pipe_2.uuid = QUuid::createUuid();
    tank_pipe_2.id = "PT2";
    tank_pipe_2.node_id_from = tank_2.id;
    tank_pipe_2.node_id_to = junction_3.id;
    tank_pipe_2.length_calculated_m = 100.0;
    tank_pipe_2.diameter_mm = 200.0;
    tank_pipe_2.roughness_hw = 130.0;
    tank_pipe_2.minor_loss = 0.0;
    tank_pipe_2.open = true;
    
    Pipe tank_pipe_3;
    tank_pipe_3.uuid = QUuid::createUuid();
    tank_pipe_3.id = "PT3";
    tank_pipe_3.node_id_from = tank_3.id;
    tank_pipe_3.node_id_to = junction_4.id;
    tank_pipe_3.length_calculated_m = 120.0;
    tank_pipe_3.diameter_mm = 180.0;
    tank_pipe_3.roughness_hw = 130.0;
    tank_pipe_3.minor_loss = 0.0;
    tank_pipe_3.open = true;
    
    Pipe tank_pipe_4;
    tank_pipe_4.uuid = QUuid::createUuid();
    tank_pipe_4.id = "PT4";
    tank_pipe_4.node_id_from = tank_4.id;
    tank_pipe_4.node_id_to = junction_5.id;
    tank_pipe_4.length_calculated_m = 90.0;
    tank_pipe_4.diameter_mm = 180.0;
    tank_pipe_4.roughness_hw = 130.0;
    tank_pipe_4.minor_loss = 0.0;
    tank_pipe_4.open = true;
    
    
    // ------------------------------------------------------------
    // Simulation request
    // ------------------------------------------------------------
    
    NetworkHydraulic request;
    
    request.reservoirs.append(reservoir);
    
    request.junctions.append(junction_1);
    request.junctions.append(junction_2);
    request.junctions.append(junction_3);
    request.junctions.append(junction_4);
    request.junctions.append(junction_5);
    
    request.tank_volume_curves.append(tank_4_curve);
    
    request.tanks.append(tank_1);
    request.tanks.append(tank_2);
    request.tanks.append(tank_3);
    request.tanks.append(tank_4);
    
    request.pipes.append(pipe_1);
    request.pipes.append(pipe_2);
    request.pipes.append(pipe_3);
    request.pipes.append(pipe_4);
    request.pipes.append(pipe_5);
    request.pipes.append(pipe_6);
    request.pipes.append(pipe_7);
    
    request.pipes.append(tank_pipe_1);
    request.pipes.append(tank_pipe_2);
    request.pipes.append(tank_pipe_3);
    request.pipes.append(tank_pipe_4);
    
    return request;
}

NetworkHydraulic DummyNetworks::networkSimpleTimeline()
{
    NetworkHydraulic request = networkSimple();
    request.duration_s = 24 * 60 * 60;
    request.hydraulic_timestep_s = 60 * 60;
    return request;
}
NetworkHydraulic DummyNetworks::networkTanksTimeline()
{
    NetworkHydraulic request = networkTanks();
    request.duration_s = 24 * 60 * 60;
    request.hydraulic_timestep_s = 60 * 60;
    return request;
}


