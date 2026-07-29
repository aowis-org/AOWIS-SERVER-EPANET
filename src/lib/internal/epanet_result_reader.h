#ifndef AOWIS_EPANET_RESULT_READER_H
#define AOWIS_EPANET_RESULT_READER_H

#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/epanet_results.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;
struct EpanetIndexRegistry;

class EpanetResultReader
{
public:
    EpanetResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices);

    EpanetStatus read(EpanetResult &result) const;

private:
    EpanetStatus readNodesJunctions(EpanetResult &result) const;
    EpanetStatus readNodesTanks(EpanetResult &result) const;
    EpanetStatus readLinksPipes(EpanetResult &result) const;

    const EpanetProject &project;
    const NetworkHydraulic &network;
    const EpanetIndexRegistry &indices;
};

#endif
