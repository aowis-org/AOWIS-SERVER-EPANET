#include <aowis/epanet/dummy/dummy_networks.h>

// @ChatGPT: Do not change the structure of this file.
// only change class and field names, if needed to match the MODEL

NetworkHydraulic DummyNetworks::networkSimple()
{
    EpanetNodeReservoir reservoir;
    reservoir.uuid = QUuid::createUuid();
    reservoir.id = "R1";
    reservoir.head_m = 30.0;
    
    EpanetNodeJunction junction;
    junction.uuid = QUuid::createUuid();
    junction.id = "J1";
    junction.elevation_m = 0.0;
    EpanetNodeJunctionDemand junction_demand;
    junction_demand.base_demand_m3_per_h = 3.6;
    junction.demands.append(junction_demand);
    
    EpanetLinkPipe pipe;
    pipe.uuid = QUuid::createUuid();
    pipe.id = "P1";
    pipe.node_id_from = reservoir.id;
    pipe.node_id_to = junction.id;
    pipe.length_calculated_m = 100.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hw = 130.0;
    pipe.minor_loss = 0.0;
    pipe.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    NetworkHydraulic network;
    network.nodes_reservoirs.append(reservoir);
    network.nodes_junctions.append(junction);
    network.links_pipes.append(pipe);
    
    return network;
}

NetworkHydraulic DummyNetworks::networkOnMap()
{
    EpanetNodeTank tank_1;
    tank_1.uuid = QUuid::createUuid();
    tank_1.id = "T34";
    
    tank_1.latitude_deg = 11.98385;
    tank_1.longitude_deg = 18.20381;
    
    tank_1.bottom_elevation_m = 533.0;
    
    tank_1.water_level_initial_m = 6.0;
    tank_1.water_level_minimum_m = 2.0;
    tank_1.water_level_maximum_m = 16.0;
    
    tank_1.geometry_input_type = EpanetNodeTankGeometryInputType::Cylindrical;
    
    tank_1.diameter_m = 8.0;
    tank_1.minimum_volume_m3 = 0.0;
    tank_1.can_overflow = false;
    
    EpanetNodeJunction junction_1;
    junction_1.uuid = QUuid::createUuid();
    junction_1.id = "J1";
    
    junction_1.latitude_deg = 11.98108;
    junction_1.longitude_deg = 18.20373;
    
    junction_1.elevation_m = 477.0;
    EpanetNodeJunctionDemand junction_1_demand;
    junction_1_demand.base_demand_m3_per_h = 3.6;
    junction_1.demands.append(junction_1_demand);
    
    EpanetLinkPipe pipe_1;
    pipe_1.uuid = QUuid::createUuid();
    pipe_1.id = "P1";
    
    pipe_1.node_id_from = tank_1.id;
    pipe_1.node_id_to = junction_1.id;
    
    pipe_1.length_calculated_m = 250.0;
    pipe_1.diameter_mm = 300.0;
    pipe_1.roughness_hw = 130.0;
    pipe_1.minor_loss = 0.0;
    pipe_1.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    NetworkHydraulic network;
    network.nodes_tanks.append(tank_1);
    network.nodes_junctions.append(junction_1);
    network.links_pipes.append(pipe_1);
    
    return network;
}

NetworkHydraulic DummyNetworks::networkTanks()
{
    // Create a more complex dummy network.
    
    // ------------------------------------------------------------
    // Reservoir
    // ------------------------------------------------------------
    
    EpanetNodeReservoir reservoir;
    reservoir.uuid = QUuid::createUuid();
    reservoir.id = "R1";
    reservoir.head_m = 75.0;
    
    
    // ------------------------------------------------------------
    // Junctions
    // ------------------------------------------------------------
    
    EpanetNodeJunction junction_1;
    junction_1.uuid = QUuid::createUuid();
    junction_1.id = "J1";
    junction_1.elevation_m = 45.0;
    EpanetNodeJunctionDemand junction_1_demand;
    junction_1_demand.base_demand_m3_per_h = 5.4;
    junction_1.demands.append(junction_1_demand);
    
    EpanetNodeJunction junction_2;
    junction_2.uuid = QUuid::createUuid();
    junction_2.id = "J2";
    junction_2.elevation_m = 50.0;
    EpanetNodeJunctionDemand junction_2_demand;
    junction_2_demand.base_demand_m3_per_h = 7.2;
    junction_2.demands.append(junction_2_demand);
    
    EpanetNodeJunction junction_3;
    junction_3.uuid = QUuid::createUuid();
    junction_3.id = "J3";
    junction_3.elevation_m = 60.0;
    EpanetNodeJunctionDemand junction_3_demand;
    junction_3_demand.base_demand_m3_per_h = 3.6;
    junction_3.demands.append(junction_3_demand);
    
    EpanetNodeJunction junction_4;
    junction_4.uuid = QUuid::createUuid();
    junction_4.id = "J4";
    junction_4.elevation_m = 35.0;
    EpanetNodeJunctionDemand junction_4_demand;
    junction_4_demand.base_demand_m3_per_h = 5.4;
    junction_4.demands.append(junction_4_demand);
    
    EpanetNodeJunction junction_5;
    junction_5.uuid = QUuid::createUuid();
    junction_5.id = "J5";
    junction_5.elevation_m = 55.0;
    EpanetNodeJunctionDemand junction_5_demand;
    junction_5_demand.base_demand_m3_per_h = 3.6;
    junction_5.demands.append(junction_5_demand);
    
    
    // ------------------------------------------------------------
    // Tank 1: cylindrical
    //
    // Initial hydraulic head:
    //     58 m elevation + 6 m level = 64 m
    //
    // This is below the reservoir head of 75 m, so this tank
    // will initially tend to fill.
    // ------------------------------------------------------------
    
    EpanetNodeTank tank_1;
    tank_1.uuid = QUuid::createUuid();
    tank_1.id = "T1";
    
    tank_1.bottom_elevation_m = 58.0;
    
    tank_1.water_level_initial_m = 6.0;
    tank_1.water_level_minimum_m = 2.0;
    tank_1.water_level_maximum_m = 16.0;
    
    tank_1.geometry_input_type = EpanetNodeTankGeometryInputType::Cylindrical;
    
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
    
    EpanetNodeTank tank_2;
    tank_2.uuid = QUuid::createUuid();
    tank_2.id = "T2";
    
    tank_2.bottom_elevation_m = 68.0;
    
    tank_2.water_level_initial_m = 12.0;
    tank_2.water_level_minimum_m = 2.0;
    tank_2.water_level_maximum_m = 18.0;
    
    tank_2.geometry_input_type = EpanetNodeTankGeometryInputType::UniformArea;
    
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
    
    EpanetNodeTank tank_3;
    tank_3.uuid = QUuid::createUuid();
    tank_3.id = "T3";
    
    tank_3.bottom_elevation_m = 45.0;
    
    tank_3.water_level_initial_m = 8.0;
    tank_3.water_level_minimum_m = 2.0;
    tank_3.water_level_maximum_m = 20.0;
    
    tank_3.geometry_input_type = EpanetNodeTankGeometryInputType::VolumeAtMaximumLevel;
    
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
    
    EpanetCurveTankVolume tank_4_curve;
    tank_4_curve.uuid = QUuid::createUuid();
    tank_4_curve.id = "VC_T4";
    
    EpanetCurveTankVolumePoint tank_4_curve_point_1;
    tank_4_curve_point_1.water_level_m = 0.0;
    tank_4_curve_point_1.volume_m3 = 0.0;
    tank_4_curve.points.append(tank_4_curve_point_1);
    
    EpanetCurveTankVolumePoint tank_4_curve_point_2;
    tank_4_curve_point_2.water_level_m = 3.0;
    tank_4_curve_point_2.volume_m3 = 25.0;
    tank_4_curve.points.append(tank_4_curve_point_2);
    
    EpanetCurveTankVolumePoint tank_4_curve_point_3;
    tank_4_curve_point_3.water_level_m = 6.0;
    tank_4_curve_point_3.volume_m3 = 65.0;
    tank_4_curve.points.append(tank_4_curve_point_3);
    
    EpanetCurveTankVolumePoint tank_4_curve_point_4;
    tank_4_curve_point_4.water_level_m = 9.0;
    tank_4_curve_point_4.volume_m3 = 120.0;
    tank_4_curve.points.append(tank_4_curve_point_4);
    
    EpanetCurveTankVolumePoint tank_4_curve_point_5;
    tank_4_curve_point_5.water_level_m = 12.0;
    tank_4_curve_point_5.volume_m3 = 190.0;
    tank_4_curve.points.append(tank_4_curve_point_5);
    
    EpanetCurveTankVolumePoint tank_4_curve_point_6;
    tank_4_curve_point_6.water_level_m = 16.0;
    tank_4_curve_point_6.volume_m3 = 310.0;
    tank_4_curve.points.append(tank_4_curve_point_6);
    
    EpanetNodeTank tank_4;
    tank_4.uuid = QUuid::createUuid();
    tank_4.id = "T4";
    
    tank_4.bottom_elevation_m = 70.0;
    
    tank_4.water_level_initial_m = 12.0;
    tank_4.water_level_minimum_m = 3.0;
    tank_4.water_level_maximum_m = 16.0;
    
    tank_4.geometry_input_type = EpanetNodeTankGeometryInputType::VolumeCurve;
    
    tank_4.volume_curve_id = tank_4_curve.id;
    
    // Keep this consistent with the curve volume at the minimum level.
    tank_4.minimum_volume_m3 = 25.0;
    
    tank_4.can_overflow = false;
    
    
    // ------------------------------------------------------------
    // Distribution pipes
    // ------------------------------------------------------------
    
    // Reservoir connection.
    EpanetLinkPipe pipe_1;
    pipe_1.uuid = QUuid::createUuid();
    pipe_1.id = "P1";
    pipe_1.node_id_from = reservoir.id;
    pipe_1.node_id_to = junction_1.id;
    pipe_1.length_calculated_m = 250.0;
    pipe_1.diameter_mm = 300.0;
    pipe_1.roughness_hw = 130.0;
    pipe_1.minor_loss = 0.0;
    pipe_1.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Upper-left distribution branch.
    EpanetLinkPipe pipe_2;
    pipe_2.uuid = QUuid::createUuid();
    pipe_2.id = "P2";
    pipe_2.node_id_from = junction_1.id;
    pipe_2.node_id_to = junction_2.id;
    pipe_2.length_calculated_m = 300.0;
    pipe_2.diameter_mm = 250.0;
    pipe_2.roughness_hw = 130.0;
    pipe_2.minor_loss = 0.0;
    pipe_2.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Upper-right distribution branch.
    EpanetLinkPipe pipe_3;
    pipe_3.uuid = QUuid::createUuid();
    pipe_3.id = "P3";
    pipe_3.node_id_from = junction_1.id;
    pipe_3.node_id_to = junction_3.id;
    pipe_3.length_calculated_m = 350.0;
    pipe_3.diameter_mm = 250.0;
    pipe_3.roughness_hw = 130.0;
    pipe_3.minor_loss = 0.0;
    pipe_3.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Left-side branch.
    EpanetLinkPipe pipe_4;
    pipe_4.uuid = QUuid::createUuid();
    pipe_4.id = "P4";
    pipe_4.node_id_from = junction_2.id;
    pipe_4.node_id_to = junction_4.id;
    pipe_4.length_calculated_m = 400.0;
    pipe_4.diameter_mm = 200.0;
    pipe_4.roughness_hw = 130.0;
    pipe_4.minor_loss = 0.0;
    pipe_4.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Right-side branch.
    EpanetLinkPipe pipe_5;
    pipe_5.uuid = QUuid::createUuid();
    pipe_5.id = "P5";
    pipe_5.node_id_from = junction_3.id;
    pipe_5.node_id_to = junction_5.id;
    pipe_5.length_calculated_m = 375.0;
    pipe_5.diameter_mm = 200.0;
    pipe_5.roughness_hw = 130.0;
    pipe_5.minor_loss = 0.0;
    pipe_5.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Lower cross-connection creates a loop.
    EpanetLinkPipe pipe_6;
    pipe_6.uuid = QUuid::createUuid();
    pipe_6.id = "P6";
    pipe_6.node_id_from = junction_4.id;
    pipe_6.node_id_to = junction_5.id;
    pipe_6.length_calculated_m = 500.0;
    pipe_6.diameter_mm = 180.0;
    pipe_6.roughness_hw = 130.0;
    pipe_6.minor_loss = 0.0;
    pipe_6.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    // Upper cross-connection creates another loop.
    EpanetLinkPipe pipe_7;
    pipe_7.uuid = QUuid::createUuid();
    pipe_7.id = "P7";
    pipe_7.node_id_from = junction_2.id;
    pipe_7.node_id_to = junction_3.id;
    pipe_7.length_calculated_m = 450.0;
    pipe_7.diameter_mm = 180.0;
    pipe_7.roughness_hw = 130.0;
    pipe_7.minor_loss = 0.0;
    pipe_7.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    
    // ------------------------------------------------------------
    // Tank connection pipes
    // ------------------------------------------------------------
    
    EpanetLinkPipe tank_pipe_1;
    tank_pipe_1.uuid = QUuid::createUuid();
    tank_pipe_1.id = "PT1";
    tank_pipe_1.node_id_from = tank_1.id;
    tank_pipe_1.node_id_to = junction_2.id;
    tank_pipe_1.length_calculated_m = 80.0;
    tank_pipe_1.diameter_mm = 200.0;
    tank_pipe_1.roughness_hw = 130.0;
    tank_pipe_1.minor_loss = 0.0;
    tank_pipe_1.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    EpanetLinkPipe tank_pipe_2;
    tank_pipe_2.uuid = QUuid::createUuid();
    tank_pipe_2.id = "PT2";
    tank_pipe_2.node_id_from = tank_2.id;
    tank_pipe_2.node_id_to = junction_3.id;
    tank_pipe_2.length_calculated_m = 100.0;
    tank_pipe_2.diameter_mm = 200.0;
    tank_pipe_2.roughness_hw = 130.0;
    tank_pipe_2.minor_loss = 0.0;
    tank_pipe_2.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    EpanetLinkPipe tank_pipe_3;
    tank_pipe_3.uuid = QUuid::createUuid();
    tank_pipe_3.id = "PT3";
    tank_pipe_3.node_id_from = tank_3.id;
    tank_pipe_3.node_id_to = junction_4.id;
    tank_pipe_3.length_calculated_m = 120.0;
    tank_pipe_3.diameter_mm = 180.0;
    tank_pipe_3.roughness_hw = 130.0;
    tank_pipe_3.minor_loss = 0.0;
    tank_pipe_3.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    EpanetLinkPipe tank_pipe_4;
    tank_pipe_4.uuid = QUuid::createUuid();
    tank_pipe_4.id = "PT4";
    tank_pipe_4.node_id_from = tank_4.id;
    tank_pipe_4.node_id_to = junction_5.id;
    tank_pipe_4.length_calculated_m = 90.0;
    tank_pipe_4.diameter_mm = 180.0;
    tank_pipe_4.roughness_hw = 130.0;
    tank_pipe_4.minor_loss = 0.0;
    tank_pipe_4.initial_status = EpanetLinkPipeInitialStatus::Open;
    
    
    // ------------------------------------------------------------
    // Simulation request
    // ------------------------------------------------------------
    
    NetworkHydraulic request;
    
    request.nodes_reservoirs.append(reservoir);
    
    request.nodes_junctions.append(junction_1);
    request.nodes_junctions.append(junction_2);
    request.nodes_junctions.append(junction_3);
    request.nodes_junctions.append(junction_4);
    request.nodes_junctions.append(junction_5);
    
    request.curves_tank_volume.append(tank_4_curve);
    
    request.nodes_tanks.append(tank_1);
    request.nodes_tanks.append(tank_2);
    request.nodes_tanks.append(tank_3);
    request.nodes_tanks.append(tank_4);
    
    request.links_pipes.append(pipe_1);
    request.links_pipes.append(pipe_2);
    request.links_pipes.append(pipe_3);
    request.links_pipes.append(pipe_4);
    request.links_pipes.append(pipe_5);
    request.links_pipes.append(pipe_6);
    request.links_pipes.append(pipe_7);
    
    request.links_pipes.append(tank_pipe_1);
    request.links_pipes.append(tank_pipe_2);
    request.links_pipes.append(tank_pipe_3);
    request.links_pipes.append(tank_pipe_4);
    
    return request;
}

NetworkHydraulic DummyNetworks::networkFull()
{
    NetworkHydraulic network;
    network.uuid = QUuid::createUuid();
    
    // Curves
    EpanetCurvePumpHead pump_head_curve;
    pump_head_curve.uuid = QUuid::createUuid();
    pump_head_curve.id = "HC_PU1";
    
    EpanetCurvePumpHeadPoint pump_head_point_1;
    pump_head_point_1.flow_m3_per_h = 0.0;
    pump_head_point_1.head_gain_m = 62.0;
    pump_head_curve.points.append(pump_head_point_1);
    
    EpanetCurvePumpHeadPoint pump_head_point_2;
    pump_head_point_2.flow_m3_per_h = 35.0;
    pump_head_point_2.head_gain_m = 50.0;
    pump_head_curve.points.append(pump_head_point_2);
    
    EpanetCurvePumpHeadPoint pump_head_point_3;
    pump_head_point_3.flow_m3_per_h = 75.0;
    pump_head_point_3.head_gain_m = 18.0;
    pump_head_curve.points.append(pump_head_point_3);
    
    EpanetCurveValveHeadloss gpv_headloss_curve;
    gpv_headloss_curve.uuid = QUuid::createUuid();
    gpv_headloss_curve.id = "HC_GPV1";
    
    EpanetCurveValveHeadlossPoint gpv_point_1;
    gpv_point_1.flow_m3_per_h = 0.0;
    gpv_point_1.head_loss_m = 0.0;
    gpv_headloss_curve.points.append(gpv_point_1);
    
    EpanetCurveValveHeadlossPoint gpv_point_2;
    gpv_point_2.flow_m3_per_h = 30.0;
    gpv_point_2.head_loss_m = 2.5;
    gpv_headloss_curve.points.append(gpv_point_2);
    
    EpanetCurveValveHeadlossPoint gpv_point_3;
    gpv_point_3.flow_m3_per_h = 60.0;
    gpv_point_3.head_loss_m = 9.0;
    gpv_headloss_curve.points.append(gpv_point_3);
    
    EpanetCurveTankVolume tank_volume_curve;
    tank_volume_curve.uuid = QUuid::createUuid();
    tank_volume_curve.id = "VC_FULL";
    
    EpanetCurveTankVolumePoint tank_volume_point_1;
    tank_volume_point_1.water_level_m = 0.0;
    tank_volume_point_1.volume_m3 = 0.0;
    tank_volume_curve.points.append(tank_volume_point_1);
    
    EpanetCurveTankVolumePoint tank_volume_point_2;
    tank_volume_point_2.water_level_m = 4.0;
    tank_volume_point_2.volume_m3 = 80.0;
    tank_volume_curve.points.append(tank_volume_point_2);
    
    EpanetCurveTankVolumePoint tank_volume_point_3;
    tank_volume_point_3.water_level_m = 8.0;
    tank_volume_point_3.volume_m3 = 200.0;
    tank_volume_curve.points.append(tank_volume_point_3);
    
    EpanetCurveTankVolumePoint tank_volume_point_4;
    tank_volume_point_4.water_level_m = 14.0;
    tank_volume_point_4.volume_m3 = 460.0;
    tank_volume_curve.points.append(tank_volume_point_4);
    
    network.curves_pump_head.append(pump_head_curve);
    network.curves_valve_headloss.append(gpv_headloss_curve);
    network.curves_tank_volume.append(tank_volume_curve);
    
    // Reservoir
    EpanetNodeReservoir reservoir;
    reservoir.uuid = QUuid::createUuid();
    reservoir.id = "R_FULL";
    reservoir.latitude_deg = 11.9850;
    reservoir.longitude_deg = 18.1980;
    reservoir.head_input_type = EpanetNodeElevationInputType::TerrainElevationAndOffset;
    reservoir.terrain_elevation_m = 82.0;
    reservoir.head_offset_m = 18.0;
    reservoir.head_m = 100.0;
    network.nodes_reservoirs.append(reservoir);
    
    // Junctions
    EpanetNodeJunction junction_pump_out;
    junction_pump_out.uuid = QUuid::createUuid();
    junction_pump_out.id = "J_PUMP_OUT";
    junction_pump_out.latitude_deg = 11.9847;
    junction_pump_out.longitude_deg = 18.2000;
    junction_pump_out.elevation_m = 54.0;
    
    EpanetNodeJunctionDemand junction_pump_out_demand_1;
    junction_pump_out_demand_1.base_demand_m3_per_h = 6.0;
    junction_pump_out_demand_1.source_method = EpanetNodeJunctionDemandSourceMethod::ManualEstimation;
    junction_pump_out_demand_1.note = "Domestic demand";
    junction_pump_out.demands.append(junction_pump_out_demand_1);
    
    EpanetNodeJunctionDemand junction_pump_out_demand_2;
    junction_pump_out_demand_2.base_demand_m3_per_h = 3.0;
    junction_pump_out_demand_2.source_method = EpanetNodeJunctionDemandSourceMethod::MeterData;
    junction_pump_out_demand_2.note = "Measured commercial demand";
    junction_pump_out.demands.append(junction_pump_out_demand_2);
    
    EpanetNodeJunction junction_prv_out;
    junction_prv_out.uuid = QUuid::createUuid();
    junction_prv_out.id = "J_PRV_OUT";
    junction_prv_out.latitude_deg = 11.9842;
    junction_prv_out.longitude_deg = 18.2020;
    junction_prv_out.elevation_input_type = EpanetNodeElevationInputType::TerrainElevationAndOffset;
    junction_prv_out.terrain_elevation_m = 48.0;
    junction_prv_out.elevation_offset_m = 1.5;
    junction_prv_out.elevation_m = 49.5;
    
    EpanetNodeJunctionDemand junction_prv_out_demand;
    junction_prv_out_demand.base_demand_m3_per_h = 5.0;
    junction_prv_out_demand.source_method = EpanetNodeJunctionDemandSourceMethod::Scenario;
    junction_prv_out.demands.append(junction_prv_out_demand);
    
    EpanetNodeJunction junction_psv_out;
    junction_psv_out.uuid = QUuid::createUuid();
    junction_psv_out.id = "J_PSV_OUT";
    junction_psv_out.latitude_deg = 11.9837;
    junction_psv_out.longitude_deg = 18.2040;
    junction_psv_out.elevation_m = 51.0;
    
    EpanetNodeJunction junction_fcv_out;
    junction_fcv_out.uuid = QUuid::createUuid();
    junction_fcv_out.id = "J_FCV_OUT";
    junction_fcv_out.latitude_deg = 11.9832;
    junction_fcv_out.longitude_deg = 18.2060;
    junction_fcv_out.elevation_m = 52.0;
    
    EpanetNodeJunction junction_pbv_out;
    junction_pbv_out.uuid = QUuid::createUuid();
    junction_pbv_out.id = "J_PBV_OUT";
    junction_pbv_out.latitude_deg = 11.9827;
    junction_pbv_out.longitude_deg = 18.2080;
    junction_pbv_out.elevation_m = 53.0;
    
    EpanetNodeJunction junction_tcv_out;
    junction_tcv_out.uuid = QUuid::createUuid();
    junction_tcv_out.id = "J_TCV_OUT";
    junction_tcv_out.latitude_deg = 11.9822;
    junction_tcv_out.longitude_deg = 18.2100;
    junction_tcv_out.elevation_m = 54.0;
    
    EpanetNodeJunction junction_gpv_out;
    junction_gpv_out.uuid = QUuid::createUuid();
    junction_gpv_out.id = "J_GPV_OUT";
    junction_gpv_out.latitude_deg = 11.9817;
    junction_gpv_out.longitude_deg = 18.2120;
    junction_gpv_out.elevation_m = 55.0;
    
    EpanetNodeJunction junction_pcv_out;
    junction_pcv_out.uuid = QUuid::createUuid();
    junction_pcv_out.id = "J_PCV_OUT";
    junction_pcv_out.latitude_deg = 11.9812;
    junction_pcv_out.longitude_deg = 18.2140;
    junction_pcv_out.elevation_m = 56.0;
    
    EpanetNodeJunction junction_loop;
    junction_loop.uuid = QUuid::createUuid();
    junction_loop.id = "J_LOOP";
    junction_loop.latitude_deg = 11.9795;
    junction_loop.longitude_deg = 18.2070;
    junction_loop.elevation_m = 47.0;
    
    EpanetNodeJunctionDemand junction_loop_demand;
    junction_loop_demand.base_demand_m3_per_h = 12.0;
    junction_loop_demand.source_method = EpanetNodeJunctionDemandSourceMethod::ManualEstimation;
    junction_loop.demands.append(junction_loop_demand);
    
    network.nodes_junctions.append(junction_pump_out);
    network.nodes_junctions.append(junction_prv_out);
    network.nodes_junctions.append(junction_psv_out);
    network.nodes_junctions.append(junction_fcv_out);
    network.nodes_junctions.append(junction_pbv_out);
    network.nodes_junctions.append(junction_tcv_out);
    network.nodes_junctions.append(junction_gpv_out);
    network.nodes_junctions.append(junction_pcv_out);
    network.nodes_junctions.append(junction_loop);
    
    // Tank with non-uniform volume curve
    EpanetNodeTank tank;
    tank.uuid = QUuid::createUuid();
    tank.id = "T_FULL";
    tank.latitude_deg = 11.9780;
    tank.longitude_deg = 18.2130;
    tank.elevation_input_type = EpanetNodeElevationInputType::TerrainElevationAndOffset;
    tank.terrain_elevation_m = 58.0;
    tank.bottom_offset_m = 2.0;
    tank.bottom_elevation_m = 60.0;
    tank.water_level_initial_m = 8.0;
    tank.water_level_minimum_m = 4.0;
    tank.water_level_maximum_m = 14.0;
    tank.geometry_input_type = EpanetNodeTankGeometryInputType::VolumeCurve;
    tank.minimum_volume_m3 = 80.0;
    tank.volume_curve_id = tank_volume_curve.id;
    tank.can_overflow = true;
    tank.quality_source.type = EpanetNodeQualitySourceType::MassBooster;
    tank.mixing_model = EpanetNodeTankMixingModel::FirstInFirstOut;
    network.nodes_tanks.append(tank);
    
    // Pumps: one curve-driven and one constant-power pump
    EpanetLinkPump curve_pump;
    curve_pump.uuid = QUuid::createUuid();
    curve_pump.id = "PU_CURVE";
    curve_pump.node_id_from = reservoir.id;
    curve_pump.node_id_to = junction_pump_out.id;
    curve_pump.definition_type = EpanetLinkPumpDefinitionType::ThreePointCurve;
    curve_pump.head_curve_id = pump_head_curve.id;
    curve_pump.initial_speed = 1.0;
    curve_pump.initial_status = EpanetLinkPumpInitialStatus::On;
    curve_pump.control_type = EpanetLinkPumpControlType::LevelBased;
    curve_pump.constant_efficiency_percent = 78.0;
    curve_pump.energy_price_per_kw_h = 0.22;
    network.links_pumps.append(curve_pump);
    
    EpanetLinkPump power_pump;
    power_pump.uuid = QUuid::createUuid();
    power_pump.id = "PU_POWER";
    power_pump.node_id_from = junction_loop.id;
    power_pump.node_id_to = tank.id;
    power_pump.definition_type = EpanetLinkPumpDefinitionType::ConstantPower;
    power_pump.constant_power_kw = 11.0;
    power_pump.initial_speed = 0.85;
    power_pump.initial_status = EpanetLinkPumpInitialStatus::Off;
    power_pump.control_type = EpanetLinkPumpControlType::TimeBased;
    power_pump.constant_efficiency_percent = 72.0;
    power_pump.energy_price_per_kw_h = 0.22;
    network.links_pumps.append(power_pump);
    
    // Every valve type currently represented by the hydraulic model
    EpanetLinkValve prv;
    prv.uuid = QUuid::createUuid();
    prv.id = "V_PRV";
    prv.node_id_from = junction_pump_out.id;
    prv.node_id_to = junction_prv_out.id;
    prv.type = EpanetLinkValveType::PRV;
    prv.setting = 38.0;
    prv.initial_status = EpanetLinkValveInitialStatus::Active;
    prv.diameter_mm = 200.0;
    prv.minor_loss = 0.2;
    
    EpanetLinkValve psv;
    psv.uuid = QUuid::createUuid();
    psv.id = "V_PSV";
    psv.node_id_from = junction_prv_out.id;
    psv.node_id_to = junction_psv_out.id;
    psv.type = EpanetLinkValveType::PSV;
    psv.setting = 42.0;
    psv.initial_status = EpanetLinkValveInitialStatus::Active;
    psv.diameter_mm = 200.0;
    psv.minor_loss = 0.2;
    
    EpanetLinkValve fcv;
    fcv.uuid = QUuid::createUuid();
    fcv.id = "V_FCV";
    fcv.node_id_from = junction_psv_out.id;
    fcv.node_id_to = junction_fcv_out.id;
    fcv.type = EpanetLinkValveType::FCV;
    fcv.setting = 30.0;
    fcv.initial_status = EpanetLinkValveInitialStatus::Active;
    fcv.diameter_mm = 180.0;
    fcv.minor_loss = 0.1;
    
    EpanetLinkValve pbv;
    pbv.uuid = QUuid::createUuid();
    pbv.id = "V_PBV";
    pbv.node_id_from = junction_fcv_out.id;
    pbv.node_id_to = junction_pbv_out.id;
    pbv.type = EpanetLinkValveType::PBV;
    pbv.setting = 5.0;
    pbv.initial_status = EpanetLinkValveInitialStatus::Active;
    pbv.diameter_mm = 180.0;
    pbv.minor_loss = 0.1;
    
    EpanetLinkValve tcv;
    tcv.uuid = QUuid::createUuid();
    tcv.id = "V_TCV";
    tcv.node_id_from = junction_pbv_out.id;
    tcv.node_id_to = junction_tcv_out.id;
    tcv.type = EpanetLinkValveType::TCV;
    tcv.setting = 2.5;
    tcv.initial_status = EpanetLinkValveInitialStatus::Open;
    tcv.diameter_mm = 160.0;
    tcv.minor_loss = 0.0;
    
    EpanetLinkValve gpv;
    gpv.uuid = QUuid::createUuid();
    gpv.id = "V_GPV";
    gpv.node_id_from = junction_tcv_out.id;
    gpv.node_id_to = junction_gpv_out.id;
    gpv.type = EpanetLinkValveType::GPV;
    gpv.setting_curve_id = gpv_headloss_curve.id;
    gpv.initial_status = EpanetLinkValveInitialStatus::Active;
    gpv.diameter_mm = 160.0;
    gpv.minor_loss = 0.0;
    
    EpanetLinkValve pcv;
    pcv.uuid = QUuid::createUuid();
    pcv.id = "V_PCV";
    pcv.node_id_from = junction_gpv_out.id;
    pcv.node_id_to = junction_pcv_out.id;
    pcv.type = EpanetLinkValveType::PCV;
    pcv.setting = 65.0;
    pcv.initial_status = EpanetLinkValveInitialStatus::Closed;
    pcv.diameter_mm = 150.0;
    pcv.minor_loss = 0.0;
    
    network.links_valves.append(prv);
    network.links_valves.append(psv);
    network.links_valves.append(fcv);
    network.links_valves.append(pbv);
    network.links_valves.append(tcv);
    network.links_valves.append(gpv);
    network.links_valves.append(pcv);
    
    // Pipes exercise open, closed and check-valve states and reaction overrides.
    EpanetLinkPipe loop_pipe_1;
    loop_pipe_1.uuid = QUuid::createUuid();
    loop_pipe_1.id = "P_LOOP_1";
    loop_pipe_1.node_id_from = junction_pcv_out.id;
    loop_pipe_1.node_id_to = junction_loop.id;
    loop_pipe_1.length_calculated_m = 420.0;
    loop_pipe_1.length_measured_m = 418.6;
    loop_pipe_1.initial_status = EpanetLinkPipeInitialStatus::Open;
    loop_pipe_1.diameter_mm = 200.0;
    loop_pipe_1.material_id = "DI";
    loop_pipe_1.roughness_hw = 125.0;
    loop_pipe_1.roughness_dw_mm = 0.26;
    loop_pipe_1.roughness_cm = 0.014;
    loop_pipe_1.minor_loss = 0.4;
    loop_pipe_1.override_reaction_coefficients = true;
    loop_pipe_1.bulk_reaction_coefficient_per_day = -0.15;
    loop_pipe_1.wall_reaction_coefficient_m_per_day = -0.05;
    
    EpanetLinkPipe loop_pipe_2;
    loop_pipe_2.uuid = QUuid::createUuid();
    loop_pipe_2.id = "P_LOOP_2";
    loop_pipe_2.node_id_from = junction_loop.id;
    loop_pipe_2.node_id_to = junction_pump_out.id;
    loop_pipe_2.length_calculated_m = 650.0;
    loop_pipe_2.initial_status = EpanetLinkPipeInitialStatus::Open;
    loop_pipe_2.diameter_mm = 250.0;
    loop_pipe_2.material_id = "PVC";
    loop_pipe_2.roughness_hw = 145.0;
    loop_pipe_2.roughness_dw_mm = 0.0015;
    loop_pipe_2.roughness_cm = 0.010;
    
    EpanetLinkPipe check_pipe;
    check_pipe.uuid = QUuid::createUuid();
    check_pipe.id = "P_CHECK";
    check_pipe.node_id_from = junction_fcv_out.id;
    check_pipe.node_id_to = junction_loop.id;
    check_pipe.length_calculated_m = 280.0;
    check_pipe.initial_status = EpanetLinkPipeInitialStatus::CheckValve;
    check_pipe.diameter_mm = 150.0;
    check_pipe.roughness_hw = 130.0;
    check_pipe.roughness_dw_mm = 0.1;
    check_pipe.roughness_cm = 0.013;
    check_pipe.minor_loss = 0.3;
    
    EpanetLinkPipe closed_pipe;
    closed_pipe.uuid = QUuid::createUuid();
    closed_pipe.id = "P_CLOSED";
    closed_pipe.node_id_from = junction_pbv_out.id;
    closed_pipe.node_id_to = junction_loop.id;
    closed_pipe.length_calculated_m = 210.0;
    closed_pipe.initial_status = EpanetLinkPipeInitialStatus::Closed;
    closed_pipe.diameter_mm = 125.0;
    closed_pipe.roughness_hw = 130.0;
    closed_pipe.roughness_dw_mm = 0.1;
    closed_pipe.roughness_cm = 0.013;
    
    EpanetLinkPipe tank_pipe;
    tank_pipe.uuid = QUuid::createUuid();
    tank_pipe.id = "P_TANK";
    tank_pipe.node_id_from = junction_psv_out.id;
    tank_pipe.node_id_to = tank.id;
    tank_pipe.length_calculated_m = 360.0;
    tank_pipe.initial_status = EpanetLinkPipeInitialStatus::Open;
    tank_pipe.diameter_mm = 200.0;
    tank_pipe.roughness_hw = 130.0;
    tank_pipe.roughness_dw_mm = 0.1;
    tank_pipe.roughness_cm = 0.013;
    
    network.links_pipes.append(loop_pipe_1);
    network.links_pipes.append(loop_pipe_2);
    network.links_pipes.append(check_pipe);
    network.links_pipes.append(closed_pipe);
    network.links_pipes.append(tank_pipe);
    
    NetworkHydraulicCustomerPoint customer_point;
    customer_point.uuid = QUuid::createUuid();
    customer_point.id = "CP_FULL";
    network.customer_points.append(customer_point);
    
    return network;
}

NetworkHydraulic DummyNetworks::networkSimpleTimeline()
{
    NetworkHydraulic request = networkSimple();
    request.duration_s = 24 * 60 * 60;
    request.timestep_hydraulic_s = 60 * 60;
    return request;
}
NetworkHydraulic DummyNetworks::networkTanksTimeline()
{
    NetworkHydraulic request = networkTanks();
    request.duration_s = 24 * 60 * 60;
    request.timestep_hydraulic_s = 60 * 60;
    return request;
}

NetworkHydraulic DummyNetworks::networkFullTimeline()
{
    NetworkHydraulic request = networkFull();
    request.duration_s = 48 * 60 * 60;
    request.timestep_hydraulic_s = 15 * 60;
    return request;
}
