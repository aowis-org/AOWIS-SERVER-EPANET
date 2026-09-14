#ifndef AOWIS_EPANET_MSX_PROJECT_H
#define AOWIS_EPANET_MSX_PROJECT_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/multi_species.h>
#include <aowis/model/hydraulic/multi_species_results.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <functional>

#include <QString>

// Drives the vendored EPANET-MSX toolkit against a temporary INP+MSX pair
// while consuming hydraulics already solved by AOWIS/EPANET through a .hyd
// file. The INP text must be the snapshot of the exact configured EPANET
// project that produced the supplied .hyd file; this class deliberately does
// not rebuild or reconfigure a second EPANET project. MSXENopen is still
// required to give the legacy MSX API its topology/identifiers, but this
// class never calls MSXsolveH: chemistry is advanced exclusively against the
// caller-supplied hydraulic solution through MSXusehydfile.
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
        const QString &configured_inp_text,
        const QString &hydraulic_file_path,
        MultiSpeciesSimulationResultTimeline &timeline,
        const std::function<bool()> &cancellation_requested,
        bool &cancelled);
};

#endif // AOWIS_EPANET_MSX_PROJECT_H
