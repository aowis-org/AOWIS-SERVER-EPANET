#ifndef AOWIS_EPANET_MSX_PROJECT_H
#define AOWIS_EPANET_MSX_PROJECT_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/multi_species.h>
#include <aowis/model/hydraulic/multi_species_results.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <functional>

#include <QString>

// Drives the vendored EPANET-MSX toolkit against a self-contained
// temporary INP+MSX file pair generated from NetworkHydraulic, while
// consuming hydraulics already solved by AOWIS/EPANET through a .hyd file.
// MSXENopen is still required to give the legacy MSX API its network
// topology and identifiers, but this class deliberately never calls
// MSXsolveH: chemistry is advanced exclusively against the hydraulic
// solution supplied by the caller through MSXusehydfile.
//
// Unlike the rest of this adapter, MSX's API has no per-instance handle:
// only one MSX project can be open in this process at a time. This class
// serializes that itself with a static mutex, held only around calls that
// touch MSX's global state. Exporting/preparing the matching INP+MSX model
// happens before the mutex is acquired.
class EpanetMsxProject
{
public:
    HydraulicSimulationStatus run(
        const NetworkHydraulic &network,
        const MultiSpeciesRunOptions &run_options,
        const QString &hydraulic_file_path,
        MultiSpeciesSimulationResultTimeline &timeline,
        const std::function<bool()> &cancellation_requested,
        bool &cancelled);
};

#endif // AOWIS_EPANET_MSX_PROJECT_H
