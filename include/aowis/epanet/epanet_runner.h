#ifndef AOWIS_EPANET_RUNNER_H
#define AOWIS_EPANET_RUNNER_H

#include <aowis/epanet/epanet_result_run.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

class EpanetRunner
{
public:
    EpanetResultRun run(const NetworkHydraulic &request) const;
};

#endif
