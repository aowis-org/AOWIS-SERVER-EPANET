#ifndef AOWIS_EPANET_RESULT_READER_H
#define AOWIS_EPANET_RESULT_READER_H

#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetResultReader
{
public:
    EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices);

    EpanetStatus read(SimulationResult &result) const;

private:
    EpanetStatus readJunctions(SimulationResult &result) const;
    EpanetStatus readTanks(SimulationResult &result) const;
    EpanetStatus readPipes(SimulationResult &result) const;

    const EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetIndexRegistry &indices;
};

#endif
