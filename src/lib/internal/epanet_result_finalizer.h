#ifndef AOWIS_EPANET_RESULT_FINALIZER_H
#define AOWIS_EPANET_RESULT_FINALIZER_H

#include <aowis/epanet/epanet_result_run.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

class EpanetPreparedProject;

EpanetResultRun finalizeEpanetCancelledRun(
    EpanetResultRun result,
    const EpanetPreparedProject &prepared_project,
    bool hydraulics_complete = false,
    bool quality_attempted = false,
    bool quality_complete = false);

EpanetResultRun finalizeEpanetFailedRun(
    EpanetResultRun result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project);

void finalizeEpanetHydraulicResultValidity(HydraulicSimulationResultTimeline &timeline);
void finalizeEpanetQualityResultValidity(WaterQualitySimulationResultTimeline &timeline);
void markEpanetHydraulicResultCancelled(HydraulicSimulationResultTimeline &timeline);
void markEpanetQualityResultCancelled(WaterQualitySimulationResultTimeline &timeline);

#endif // AOWIS_EPANET_RESULT_FINALIZER_H
