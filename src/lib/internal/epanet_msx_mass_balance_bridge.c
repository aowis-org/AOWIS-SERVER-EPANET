#include "epanet_msx_mass_balance_bridge.h"

#include <stddef.h>
#include <stdio.h>

#include "msxtypes.h"

extern MSXproject MSX;

int aowisMsxGetMassBalanceRatio(int species_index, double *ratio)
{
    if (ratio == NULL)
        return 1;
    if (species_index <= 0 || species_index > MSX.Nobjects[SPECIES])
        return 2;
    if (MSX.MassBalance.ratio == NULL)
        return 3;

    *ratio = MSX.MassBalance.ratio[species_index];
    return 0;
}
