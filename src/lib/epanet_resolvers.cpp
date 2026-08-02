#include <aowis/epanet/epanet_resolvers.h>

#include <QtMath>

double EpanetResolvers::resolveNodeTankBottomElevation(const HydraulicNodeTank &tank)
{
    switch (tank.elevation_input_type)
    {
    case HydraulicNodeTankElevationInputType::BottomElevation:
        return tank.bottom_elevation_m;
    case HydraulicNodeTankElevationInputType::TerrainElevationAndOffset:
        return tank.terrain_elevation_m + tank.bottom_offset_m;
    }

    return 0.0;
}

double EpanetResolvers::resolveNodeTankDiameter(const HydraulicNodeTank &tank)
{
    switch (tank.geometry_input_type)
    {
    case HydraulicNodeTankGeometryInputType::Cylindrical:
        return tank.diameter_m;
    case HydraulicNodeTankGeometryInputType::UniformArea:
        return qSqrt(4.0 * tank.cross_section_area_m2 / M_PI);
    case HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel:
    {
        double reference_height_m = 0.0;
        double reference_volume_m3 = 0.0;
        if (tank.minimum_volume_m3 > 0.0)
        {
            reference_height_m = tank.water_level_maximum_m - tank.water_level_minimum_m;
            reference_volume_m3 =
                tank.volume_at_maximum_level_m3 - tank.minimum_volume_m3;
        }
        else if (tank.minimum_volume_m3 == 0.0)
        {
            reference_height_m = tank.water_level_maximum_m;
            reference_volume_m3 = tank.volume_at_maximum_level_m3;
        }

        if (reference_height_m <= 0.0 || reference_volume_m3 <= 0.0)
            return 0.0;

        return qSqrt(4.0 * (reference_volume_m3 / reference_height_m) / M_PI);
    }
    case HydraulicNodeTankGeometryInputType::VolumeCurve:
        return 0.0;
    }

    return 0.0;
}
