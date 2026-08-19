#ifndef AOWIS_EPANET_QUALITY_RESULT_READER_H
#define AOWIS_EPANET_QUALITY_RESULT_READER_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetQualityResultReader
{
public:
    EpanetQualityResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices);

    HydraulicSimulationStatus read(WaterQualitySimulationResult &result) const;

private:
    HydraulicSimulationStatus readNodesJunctions(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readNodesReservoirs(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readNodesTanks(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readLinksPipes(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readLinksPumps(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readLinksValves(WaterQualitySimulationResult &result) const;
    HydraulicSimulationStatus readStatistics(WaterQualitySimulationResult &result) const;

    const EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetIndexRegistry &indices;
};

#endif // AOWIS_EPANET_QUALITY_RESULT_READER_H
