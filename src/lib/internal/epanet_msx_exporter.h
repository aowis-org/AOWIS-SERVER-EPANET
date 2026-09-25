#ifndef AOWIS_EPANET_MSX_EXPORTER_H
#define AOWIS_EPANET_MSX_EXPORTER_H

#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/multi_species.h>

struct NetworkHydraulic;

// Formats NetworkHydraulic::multi_species as MSX 2.0 input file text.
//
// Unlike retrieveEpanetInpText(), this does not call into the EPANET Toolkit
// at all: MSX has no in-memory project or native writer equivalent to
// EN_saveinpfile(), so the text is built directly from the Model. That also
// means this function cannot itself detect whether the produced file is
// something the real MSXopen() parser accepts -- it is written to the
// documented MSX 2.0 grammar, not proven against the native parser, which
// requires the vendored EPANETMSX submodule this repository does not have
// yet.
//
// run_options.output_species_uuids is validated here but never used to prune the
// generated reaction model. MSX chemistry is coupled: an output species can
// reference another species in an expression, so removing an unrequested
// species would change or invalidate the chemistry. EpanetMsxProject applies
// output_species_uuids only when reading results after the complete model is loaded
// and solved. An empty list means return every species.
HydraulicSimulationStatus retrieveEpanetMsxText(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    QString &msx_text);

#endif // AOWIS_EPANET_MSX_EXPORTER_H
