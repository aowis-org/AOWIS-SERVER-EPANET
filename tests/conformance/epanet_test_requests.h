#ifndef AOWIS_EPANET_TEST_REQUESTS_H
#define AOWIS_EPANET_TEST_REQUESTS_H

#include <aowis/epanet/epanet_run_request.h>

#include <utility>

namespace AowisEpanetTests
{
inline EpanetRunRequest makeRunRequest(NetworkHydraulic network)
{
    EpanetRunRequest request;
    request.network = std::move(network);
    return request;
}

inline EpanetRunRequest makeRunRequest(
    NetworkHydraulic network,
    const WaterQualitySolverOptions &quality_options)
{
    EpanetRunRequest request = makeRunRequest(std::move(network));
    request.quality_runs.append(quality_options);
    return request;
}

inline EpanetRunRequest makeRunRequest(
    NetworkHydraulic network,
    const QList<WaterQualitySolverOptions> &quality_runs)
{
    EpanetRunRequest request = makeRunRequest(std::move(network));
    request.quality_runs = quality_runs;
    return request;
}
}

#endif // AOWIS_EPANET_TEST_REQUESTS_H
