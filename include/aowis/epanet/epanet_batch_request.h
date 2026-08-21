#ifndef AOWIS_EPANET_BATCH_REQUEST_H
#define AOWIS_EPANET_BATCH_REQUEST_H

#include <QList>

#include <aowis/model/hydraulic/network_hydraulic.h>

struct EpanetBatchPlan
{
    QList<HydraulicHeadlossFormula> headloss_formulas;
    QList<WaterQualitySolverOptions> quality_runs;
};

struct EpanetBatchRequest
{
    NetworkHydraulic network;
    EpanetBatchPlan plan;
};

#endif // AOWIS_EPANET_BATCH_REQUEST_H
