#include <aowis/epanet/epanet_resolvers.h>

#include <QtMath>

double EpanetResolvers::resolveNodeTankBottomElevation(const EpanetNodeTank &tank)
{
    switch (tank.elevation_input_type)
    {
    case EpanetNodeTankElevationInputType::BottomElevation:
        return tank.bottom_elevation_m;
    case EpanetNodeTankElevationInputType::TerrainElevationAndOffset:
        return tank.terrain_elevation_m + tank.bottom_offset_m;
    }

    return 0.0;
}

double EpanetResolvers::resolveNodeTankDiameter(const EpanetNodeTank &tank)
{
    switch (tank.geometry_input_type)
    {
    case EpanetNodeTankGeometryInputType::Cylindrical:
        return tank.diameter_m;
    case EpanetNodeTankGeometryInputType::UniformArea:
        return qSqrt(4.0 * tank.cross_section_area_m2 / M_PI);
    case EpanetNodeTankGeometryInputType::VolumeAtMaximumLevel:
    {
        const double usable_height_m = tank.water_level_maximum_m - tank.water_level_minimum_m;
        const double usable_volume_m3 = tank.volume_at_maximum_level_m3 - tank.minimum_volume_m3;
        if (usable_height_m <= 0.0 || usable_volume_m3 <= 0.0)
            return 0.0;

        return qSqrt(4.0 * (usable_volume_m3 / usable_height_m) / M_PI);
    }
    case EpanetNodeTankGeometryInputType::VolumeCurve:
        return 0.0;
    }

    return 0.0;
}
