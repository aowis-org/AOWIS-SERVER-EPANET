#ifndef SIMULATION_REQUEST_H
#define SIMULATION_REQUEST_H

#include <QString>
#include <QList>


enum class TankElevationInputType
{
    BottomElevation,
    TerrainElevationAndOffset
};
enum class TankGeometryInputType
{
    Cylindrical,
    UniformArea,
    VolumeAtMaximumLevel,
    VolumeCurve
};
struct TankVolumeCurvePoint
{
    double level_m = 0.0;
    double volume_m3 = 0.0;
};
struct TankVolumeCurve
{
    QString id;
    QList<TankVolumeCurvePoint> points;
};
struct Tank
{
    QString id;
    
    TankElevationInputType elevation_input_type = TankElevationInputType::BottomElevation;
    
    double bottom_elevation_m = 0.0;
    double terrain_elevation_m = 0.0;
    double bottom_offset_m = 0.0;
    
    // Water levels measured above elevation_m.
    double initial_level_m = 0.0;
    double minimum_level_m = 0.0;
    double maximum_level_m = 0.0;
    
    TankGeometryInputType geometry_input_type = TankGeometryInputType::Cylindrical;
    
    // Used for Cylindrical.
    double diameter_m = 0.0;
    
    // Used for UniformArea.
    double cross_section_area_m2 = 0.0;
    
    // Used for VolumeAtMaximumLevel.
    double volume_at_maximum_level_m3 = 0.0;
    
    // EPANET volume stored when the tank is at minimum_level_m.
    double minimum_volume_m3 = 0.0;
    
    // Used for VolumeCurve.
    QString volume_curve_id;
    
    // EPANET 2.2 option.
    bool can_overflow = false;
};



struct Reservoir
{
    QString id;
    double head_m;
};

struct Junction
{
    QString id;
    double elevation_m;
    double demand_lps;
};

struct Pipe
{
    QString id;
    QString node_id_from;
    QString node_id_to;
    double length_m;
    double diameter_mm;
    double roughness_hw;
    double minor_loss;
    bool open;
};

struct SimulationRequest
{
    long duration_s = 0;
    long hydraulic_timestep_s = 3600;
    
    QList<Reservoir> reservoirs;
    QList<Junction> junctions;
    QList<Pipe> pipes;
    
    QList<Tank> tanks;
    QList<TankVolumeCurve> tank_volume_curves;
};

#endif // SIMULATION_REQUEST_H
