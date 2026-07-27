#ifndef AOWIS_EPANET_RUN_RESULT_H
#define AOWIS_EPANET_RUN_RESULT_H

#include <QStringList>
#include <aowis/model/hydraulic/simulation_result.h>

struct EpanetRunResult
{
    SimulationResultTimeline timeline;
    QStringList report;
};

#endif
