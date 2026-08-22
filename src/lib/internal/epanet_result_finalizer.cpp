#include "epanet_result_finalizer.h"

namespace
{
bool diagnosticInvalidatesResults(const HydraulicSimulationDiagnostic &diagnostic)
{
    if (diagnostic.severity != HydraulicSimulationDiagnosticSeverity::Error
        && diagnostic.severity != HydraulicSimulationDiagnosticSeverity::Fatal)
    {
        return false;
    }

    switch (diagnostic.stage)
    {
    case HydraulicSimulationStatusStage::CloseHydraulics:
    case HydraulicSimulationStatusStage::CloseQuality:
    case HydraulicSimulationStatusStage::SaveHydraulics:
    case HydraulicSimulationStatusStage::GenerateReport:
    case HydraulicSimulationStatusStage::Cleanup:
        return false;
    default:
        return true;
    }
}

bool hasInvalidatingDiagnostic(const QList<HydraulicSimulationDiagnostic> &diagnostics)
{
    for (const HydraulicSimulationDiagnostic &diagnostic : diagnostics)
    {
        if (diagnosticInvalidatesResults(diagnostic))
            return true;
    }

    return false;
}
}


void finalizeEpanetHydraulicResultValidity(HydraulicSimulationResultTimeline &timeline)
{
    if (hasInvalidatingDiagnostic(timeline.diagnostics))
    {
        timeline.validity = timeline.results.isEmpty()
            ? HydraulicSimulationResultValidity::Invalid
            : HydraulicSimulationResultValidity::Partial;
        return;
    }

    timeline.validity = timeline.results.isEmpty()
        ? HydraulicSimulationResultValidity::Invalid
        : HydraulicSimulationResultValidity::Valid;
}

void finalizeEpanetQualityResultValidity(WaterQualitySimulationResultTimeline &timeline)
{
    if (timeline.analysis == WaterQualityAnalysisType::None)
    {
        timeline.validity = WaterQualitySimulationResultValidity::NotRun;
        return;
    }

    if (hasInvalidatingDiagnostic(timeline.diagnostics))
    {
        timeline.validity = timeline.results.isEmpty()
            ? WaterQualitySimulationResultValidity::Invalid
            : WaterQualitySimulationResultValidity::Partial;
        return;
    }

    timeline.validity = timeline.results.isEmpty()
        ? WaterQualitySimulationResultValidity::Invalid
        : WaterQualitySimulationResultValidity::Valid;
}

void markEpanetHydraulicResultCancelled(HydraulicSimulationResultTimeline &timeline)
{
    timeline.validity = timeline.results.isEmpty()
        ? HydraulicSimulationResultValidity::Invalid
        : HydraulicSimulationResultValidity::Partial;
}

void markEpanetQualityResultCancelled(WaterQualitySimulationResultTimeline &timeline)
{
    timeline.validity = timeline.results.isEmpty()
        ? WaterQualitySimulationResultValidity::Invalid
        : WaterQualitySimulationResultValidity::Partial;
}
