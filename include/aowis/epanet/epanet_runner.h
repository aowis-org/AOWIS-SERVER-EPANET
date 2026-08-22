#ifndef AOWIS_EPANET_RUNNER_H
#define AOWIS_EPANET_RUNNER_H

#include <aowis/epanet/epanet_result_inp.h>
#include <aowis/epanet/epanet_result_run.h>
#include <aowis/epanet/epanet_run_request.h>

#include <functional>

class EpanetRunner
{
public:
    EpanetResultInp retrieveInp(const EpanetRunRequest &request) const;

    EpanetResultRun run(const EpanetRunRequest &request) const;
    EpanetResultRun run(const EpanetRunRequest &request, const std::function<bool()> &cancellation_requested) const;
};

#endif
