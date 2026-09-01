#ifndef AOWIS_EPANET_NETWORK_ADVISORIES_H
#define AOWIS_EPANET_NETWORK_ADVISORIES_H

#include <QList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

// Non-blocking advisories about a hydraulic network's static configuration.
//
// EpanetNetworkValidatorParts (see epanet_network_validator_parts.h) rejects
// networks that EPANET cannot simulate at all - broken references, missing
// IDs, non-finite numbers, and the like. Simulation never runs when one of
// those checks fails.
//
// This file is for a different, deliberately weaker category: configurations
// that EPANET accepts and can simulate just fine, but that are unusual
// enough to almost always indicate a data-entry mistake rather than an
// intentional design choice (e.g. a tank whose MinLevel/MaxLevel leave it no
// room to ever fill or drain, which EPANET quietly satisfies by keeping
// whatever link feeds it closed for the entire run). Since a legitimate,
// intentional use is possible, callers should surface these as
// Warning-severity diagnostics and let the simulation proceed - never as a
// validation failure that blocks it.
QList<HydraulicSimulationDiagnostic> collectEpanetNetworkAdvisories(const NetworkHydraulic &network);

#endif // AOWIS_EPANET_NETWORK_ADVISORIES_H
