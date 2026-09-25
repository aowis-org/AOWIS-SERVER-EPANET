#ifndef AOWIS_EPANET_INTERNAL_EPANET_MSX_UNITS_H
#define AOWIS_EPANET_INTERNAL_EPANET_MSX_UNITS_H

#include <aowis/model/hydraulic/multi_species.h>

namespace EpanetMsxUnits
{
constexpr double square_metres_per_square_foot = 0.09290304;
constexpr double square_metres_per_square_centimetre = 0.0001;
constexpr double msx_reference_diffusivity_ft2_per_s = 1.29e-8;

inline double solverMassScale(MultiSpeciesUnits units)
{
    switch (units)
    {
    case MultiSpeciesUnits::Milligrams:
        return 1.0;
    case MultiSpeciesUnits::Micrograms:
        return 1000.0;
    case MultiSpeciesUnits::Moles:
        return 0.001;
    case MultiSpeciesUnits::Millimoles:
        return 1.0;
    }
    return 1.0;
}

inline double solverAreaScale(MultiSpeciesAreaUnits area_units)
{
    switch (area_units)
    {
    case MultiSpeciesAreaUnits::SquareFeet:
        return square_metres_per_square_foot;
    case MultiSpeciesAreaUnits::SquareMetres:
        return 1.0;
    case MultiSpeciesAreaUnits::SquareCentimetres:
        return square_metres_per_square_centimetre;
    }
    return 1.0;
}

inline double speciesValueToSolver(
    double canonical_value,
    MultiSpeciesSpeciesType species_type,
    MultiSpeciesUnits units,
    MultiSpeciesAreaUnits area_units)
{
    double factor = solverMassScale(units);
    if (species_type == MultiSpeciesSpeciesType::Wall)
        factor *= solverAreaScale(area_units);
    return canonical_value * factor;
}

inline double speciesValueFromSolver(
    double solver_value,
    MultiSpeciesSpeciesType species_type,
    MultiSpeciesUnits units,
    MultiSpeciesAreaUnits area_units)
{
    double factor = solverMassScale(units);
    if (species_type == MultiSpeciesSpeciesType::Wall)
        factor *= solverAreaScale(area_units);
    return solver_value / factor;
}

inline double bulkSourceValueToSolver(double canonical_value, MultiSpeciesUnits units)
{
    return canonical_value * solverMassScale(units);
}

inline double diffusivityToSolverRatio(double m2_per_s)
{
    const double ft2_per_s = m2_per_s / square_metres_per_square_foot;
    return ft2_per_s / msx_reference_diffusivity_ft2_per_s;
}
}

#endif // AOWIS_EPANET_INTERNAL_EPANET_MSX_UNITS_H
