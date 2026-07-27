#ifndef AOWIS_EPANET_RUNNER_H
#define AOWIS_EPANET_RUNNER_H

#include <aowis/epanet/epanet_run_result.h>
#include <aowis/model/hydraulic/network.h>

class EpanetRunner
{
public:
    EpanetRunResult run(const NetworkHydraulic &request) const;
};

#endif
