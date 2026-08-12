#ifndef AOWIS_EPANET_RESULT_READER_H
#define AOWIS_EPANET_RESULT_READER_H

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetResultReader
{
public:
    EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices);

    HydraulicSimulationStatus read(HydraulicSimulationResult &result) const;

private:
    HydraulicSimulationStatus readNodesJunctions(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readNodesReservoirs(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readNodesTanks(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readLinksPipes(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readLinksPumps(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readLinksValves(HydraulicSimulationResult &result) const;
    HydraulicSimulationStatus readStatistics(HydraulicSimulationResult &result) const;

    const EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetIndexRegistry &indices;
};

#endif // AOWIS_EPANET_RESULT_READER_H
