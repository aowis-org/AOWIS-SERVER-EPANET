#include <aowis/model/units/conversion.h>

// Compare directly against the actual vendored EPANET definitions, not
// a second table of expected literals maintained by AOWIS.
#include "types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
// The derived SI factors may round by one floating-point step relative to the
// decimal macros. A 1e-15 relative tolerance catches material changes while
// accepting equivalent values across their different evaluation paths.
bool compare(const char *name, double model_value, double epanet_value)
{
    const double tolerance = 1.0e-15 * std::max(1.0, std::abs(epanet_value));
    if (std::isfinite(model_value) && std::isfinite(epanet_value)
        && std::abs(model_value - epanet_value) <= tolerance)
        return true;

    std::fprintf(stderr,
                 "%s: Model %.17g differs from vendored EPANET %.17g (tolerance %.17g)\n",
                 name, model_value, epanet_value, tolerance);
    return false;
}
}

int main()
{
    namespace units = aowis::units;
    bool passed = true;

    // Historical EPANET 2.3 pressure/head and pump-power factors.
    passed &= compare("PSIperFT", units::epanet_psi_per_foot, PSIperFT);
    passed &= compare("KPAperPSI", units::epanet_kpa_per_psi, KPAperPSI);
    passed &= compare("BARperPSI", units::epanet_bar_per_psi, BARperPSI);
    passed &= compare("KWperHP", units::epanet_kw_per_hp, KWperHP);

    // Every flow-unit macro used by the EPANET 2.3 importer/report reader.
    passed &= compare("GPMperCFS", units::epanet_gallons_per_minute_per_cfs, GPMperCFS);
    passed &= compare("AFDperCFS", units::epanet_acre_feet_per_day_per_cfs, AFDperCFS);
    passed &= compare("MGDperCFS", units::epanet_million_gallons_per_day_per_cfs, MGDperCFS);
    passed &= compare("IMGDperCFS", units::epanet_imperial_million_gallons_per_day_per_cfs, IMGDperCFS);
    passed &= compare("LPSperCFS", units::epanet_litres_per_second_per_cfs, LPSperCFS);
    passed &= compare("LPMperCFS", units::epanet_litres_per_minute_per_cfs, LPMperCFS);
    passed &= compare("CMSperCFS", units::epanet_cubic_metres_per_second_per_cfs, CMSperCFS);
    passed &= compare("CMHperCFS", units::epanet_cubic_metres_per_hour_per_cfs, CMHperCFS);
    passed &= compare("CMDperCFS", units::epanet_cubic_metres_per_day_per_cfs, CMDperCFS);
    passed &= compare("MLDperCFS", units::epanet_million_litres_per_day_per_cfs, MLDperCFS);

    // Shared physical factors used by other adapter paths.
    passed &= compare("MperFT", units::metres_per_international_foot, MperFT);
    passed &= compare("M3perFT3", units::cubic_metres_per_cubic_foot, M3perFT3);
    passed &= compare("LperFT3", units::cubic_metres_per_cubic_foot * 1000.0, LperFT3);

    return passed ? 0 : 1;
}
