#ifndef AOWIS_EPANET_RUN_REQUEST_H
#define AOWIS_EPANET_RUN_REQUEST_H

#include <QList>

#include <aowis/model/hydraulic/network_hydraulic.h>

struct EpanetRunRequest
{
    NetworkHydraulic network;
    QList<WaterQualitySolverOptions> quality_runs;
};

#endif // AOWIS_EPANET_RUN_REQUEST_H
