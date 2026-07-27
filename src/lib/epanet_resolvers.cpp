#include <aowis/epanet/epanet_resolvers.h>

#include <QtMath>

double EpanetResolvers::resolveTankBottomElevation(const Tank &tank)
{
    switch (tank.elevation_input_type)
    {
    case TankElevationInputType::BottomElevation:
        return tank.bottom_elevation_m;
    case TankElevationInputType::TerrainElevationAndOffset:
        return tank.terrain_elevation_m + tank.bottom_offset_m;
    }

    return 0.0;
}

double EpanetResolvers::resolveTankDiameter(const Tank &tank)
{
    switch (tank.geometry_input_type)
    {
    case TankGeometryInputType::Cylindrical:
        return tank.diameter_m;
    case TankGeometryInputType::UniformArea:
        return qSqrt(4.0 * tank.cross_section_area_m2 / M_PI);
    case TankGeometryInputType::VolumeAtMaximumLevel:
    {
        const double usable_height_m = tank.maximum_level_m - tank.minimum_level_m;
        const double usable_volume_m3 = tank.volume_at_maximum_level_m3 - tank.minimum_volume_m3;
        if (usable_height_m <= 0.0 || usable_volume_m3 <= 0.0)
            return 0.0;

        return qSqrt(4.0 * (usable_volume_m3 / usable_height_m) / M_PI);
    }
    case TankGeometryInputType::VolumeCurve:
        return 0.0;
    }

    return 0.0;
}
