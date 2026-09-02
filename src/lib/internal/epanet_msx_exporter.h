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
// run_options.species_uuids, if non-empty, restricts [SPECIES], [PIPES],
// [TANKS], [SOURCES], and [QUALITY] to the selected species. [COEFFICIENTS],
// [TERMS], [PARAMETERS], and [PATTERNS] are always exported in full: symbols
// they define may be referenced from a selected species' reaction expression
// text, which this function does not parse, so dropping them on the basis of
// species selection risks silently breaking a reaction that is still active.
// An unused symbol is harmless; a missing one is a parse error.
HydraulicSimulationStatus retrieveEpanetMsxText(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    QString &msx_text);

#endif // AOWIS_EPANET_MSX_EXPORTER_H
