#ifndef AOWIS_EPANET_INTERNAL_EPANET_MSX_MASS_BALANCE_BRIDGE_H
#define AOWIS_EPANET_INTERNAL_EPANET_MSX_MASS_BALANCE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// EPANET-MSX computes per-species mass-balance ratios internally but does not
// expose them through epanetmsx.h. Keep this pinned-backend dependency behind
// one tiny bridge instead of leaking msxtypes.h into the C++ adapter.
int aowisMsxGetMassBalanceRatio(int species_index, double *ratio);

#ifdef __cplusplus
}
#endif

#endif // AOWIS_EPANET_INTERNAL_EPANET_MSX_MASS_BALANCE_BRIDGE_H
