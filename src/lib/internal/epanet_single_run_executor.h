#ifndef AOWIS_EPANET_SINGLE_RUN_EXECUTOR_H
#define AOWIS_EPANET_SINGLE_RUN_EXECUTOR_H

#include <aowis/epanet/epanet_result_run.h>

#include <functional>

class EpanetPreparedProject;

class EpanetSingleRunExecutor
{
public:
    explicit EpanetSingleRunExecutor(EpanetPreparedProject &prepared_project);

    EpanetResultRun run(
        EpanetResultRun result,
        const std::function<bool()> &cancellation_requested = std::function<bool()>());

private:
    EpanetPreparedProject &prepared_project_;
};

#endif // AOWIS_EPANET_SINGLE_RUN_EXECUTOR_H
