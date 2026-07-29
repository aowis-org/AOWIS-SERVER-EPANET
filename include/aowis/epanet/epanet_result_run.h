#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>

struct EpanetResultRun
{
    HydraulicSimulationResultTimeline result_timeline;
    QStringList report_lines;
};

#endif // AOWIS_EPANET_RESULT_RUN_H
