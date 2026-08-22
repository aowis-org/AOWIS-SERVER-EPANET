#ifndef AOWIS_EPANET_RESULT_FINALIZER_H
#define AOWIS_EPANET_RESULT_FINALIZER_H

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>


void finalizeEpanetHydraulicResultValidity(HydraulicSimulationResultTimeline &timeline);
void finalizeEpanetQualityResultValidity(WaterQualitySimulationResultTimeline &timeline);
void markEpanetHydraulicResultCancelled(HydraulicSimulationResultTimeline &timeline);
void markEpanetQualityResultCancelled(WaterQualitySimulationResultTimeline &timeline);

#endif // AOWIS_EPANET_RESULT_FINALIZER_H
