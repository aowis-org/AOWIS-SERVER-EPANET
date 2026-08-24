#ifndef AOWIS_EPANET_COORDINATE_REFERENCE_TRANSFORM_H
#define AOWIS_EPANET_COORDINATE_REFERENCE_TRANSFORM_H

#include <aowis/model/gis.h>


enum class EpanetCoordinateReferenceUnit
{
    Degrees,
    Meters,
    UsSurveyFeet
};

struct EpanetCoordinateReferenceDefinition
{
    int epsg_code = 0;
    const char *name = "";
    EpanetCoordinateReferenceUnit unit = EpanetCoordinateReferenceUnit::Degrees;
    bool datum_approximated_as_wgs84 = false;
};

bool epanetCoordinateReferenceDefinition(
    int epsg_code,
    EpanetCoordinateReferenceDefinition &definition);

bool transformEpanetEpsgToWgs84(
    int epsg_code,
    double x,
    double y,
    CoordinateWGS84 &coordinate);

#endif // AOWIS_EPANET_COORDINATE_REFERENCE_TRANSFORM_H
