#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

struct EpanetResultRun
{
    HydraulicSimulationResultTimeline result_timeline;
    WaterQualitySimulationResultTimeline quality_result_timeline;
    QStringList report_lines;
    bool cancelled = false;
};

#endif // AOWIS_EPANET_RESULT_RUN_H
