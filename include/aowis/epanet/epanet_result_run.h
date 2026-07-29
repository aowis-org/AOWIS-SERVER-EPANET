#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <QStringList>
#include <aowis/model/hydraulic/epanet_results.h>

struct EpanetResultRun
{
    EpanetResultTimeline result_timeline;
    QStringList report_lines;
};

#endif
