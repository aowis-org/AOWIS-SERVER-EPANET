#include "epanet_coordinate_reference_transform.h"

#include <GeographicLib/LambertConformalConic.hpp>

#include <cmath>

namespace
{
constexpr double grs80_a_m = 6378137.0;
constexpr double grs80_inverse_flattening = 298.257222101;
constexpr double kentucky_latitude_false_origin_deg = 36.3333333333333;
constexpr double kentucky_longitude_false_origin_deg = -85.75;
constexpr double kentucky_standard_parallel_1_deg = 37.0833333333333;
constexpr double kentucky_standard_parallel_2_deg = 38.6666666666667;
constexpr double kentucky_false_easting_m = 1500000.0;
constexpr double kentucky_false_northing_m = 1000000.0;
constexpr double kentucky_false_easting_ft_us = 4921250.0;
constexpr double kentucky_false_northing_ft_us = 3280833.333;
constexpr double meters_per_us_survey_foot = 1200.0 / 3937.0;

bool validGeographicCoordinate(double longitude_deg, double latitude_deg)
{
    return std::isfinite(longitude_deg)
        && std::isfinite(latitude_deg)
        && longitude_deg >= -180.0
        && longitude_deg <= 180.0
        && latitude_deg >= -90.0
        && latitude_deg <= 90.0;
}

const GeographicLib::LambertConformalConic &kentuckySingleZoneProjection()
{
    static const GeographicLib::LambertConformalConic projection(
        grs80_a_m,
        1.0 / grs80_inverse_flattening,
        kentucky_standard_parallel_1_deg,
        kentucky_standard_parallel_2_deg,
        1.0);
    return projection;
}

bool reverseKentuckySingleZone(
    double easting_m,
    double northing_m,
    double false_easting_m,
    double false_northing_m,
    CoordinateWGS84 &coordinate)
{
    double origin_x_m = 0.0;
    double origin_y_m = 0.0;
    kentuckySingleZoneProjection().Forward(
        kentucky_longitude_false_origin_deg,
        kentucky_latitude_false_origin_deg,
        kentucky_longitude_false_origin_deg,
        origin_x_m,
        origin_y_m);
    origin_x_m -= false_easting_m;
    origin_y_m -= false_northing_m;

    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    kentuckySingleZoneProjection().Reverse(
        kentucky_longitude_false_origin_deg,
        easting_m + origin_x_m,
        northing_m + origin_y_m,
        latitude_deg,
        longitude_deg);
    if (!validGeographicCoordinate(longitude_deg, latitude_deg))
        return false;

    coordinate.longitude_deg = longitude_deg;
    coordinate.latitude_deg = latitude_deg;
    return true;
}
}

bool epanetCoordinateReferenceDefinition(
    int epsg_code,
    EpanetCoordinateReferenceDefinition &definition)
{
    definition = EpanetCoordinateReferenceDefinition();
    definition.epsg_code = epsg_code;

    switch (epsg_code)
    {
    case 4326:
        definition.name = "WGS 84";
        definition.unit = EpanetCoordinateReferenceUnit::Degrees;
        return true;
    case 4269:
        definition.name = "NAD83";
        definition.unit = EpanetCoordinateReferenceUnit::Degrees;
        definition.datum_approximated_as_wgs84 = true;
        return true;
    case 3088:
        definition.name = "NAD83 / Kentucky Single Zone";
        definition.unit = EpanetCoordinateReferenceUnit::Meters;
        definition.datum_approximated_as_wgs84 = true;
        return true;
    case 3089:
        definition.name = "NAD83 / Kentucky Single Zone (ftUS)";
        definition.unit = EpanetCoordinateReferenceUnit::UsSurveyFeet;
        definition.datum_approximated_as_wgs84 = true;
        return true;
    default:
        return false;
    }
}

bool transformEpanetEpsgToWgs84(
    int epsg_code,
    double x,
    double y,
    CoordinateWGS84 &coordinate)
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;

    switch (epsg_code)
    {
    case 4326:
    case 4269:
        if (!validGeographicCoordinate(x, y))
            return false;
        coordinate.longitude_deg = x;
        coordinate.latitude_deg = y;
        return true;
    case 3088:
        return reverseKentuckySingleZone(
            x,
            y,
            kentucky_false_easting_m,
            kentucky_false_northing_m,
            coordinate);
    case 3089:
        return reverseKentuckySingleZone(
            x * meters_per_us_survey_foot,
            y * meters_per_us_survey_foot,
            kentucky_false_easting_ft_us * meters_per_us_survey_foot,
            kentucky_false_northing_ft_us * meters_per_us_survey_foot,
            coordinate);
    default:
        return false;
    }
}
