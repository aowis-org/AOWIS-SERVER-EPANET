#ifndef AOWIS_EPANET_BATCH_EXECUTOR_H
#define AOWIS_EPANET_BATCH_EXECUTOR_H

#include <aowis/epanet/epanet_result_batch.h>

#include <functional>

class EpanetPreparedProject;

class EpanetBatchExecutor
{
public:
    explicit EpanetBatchExecutor(EpanetPreparedProject &prepared_project);

    EpanetResultBatch run(
        EpanetResultBatch result,
        const std::function<bool()> &cancellation_requested = std::function<bool()>());

private:
    EpanetPreparedProject &prepared_project_;
};

#endif // AOWIS_EPANET_BATCH_EXECUTOR_H
