#include "epanet_result_finalizer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_prepared_project.h"

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


EpanetResultRun finalizeEpanetCancelledRun(
    EpanetResultRun result,
    const EpanetPreparedProject &prepared_project,
    bool hydraulics_complete,
    bool quality_attempted,
    bool quality_complete)
{
    result.cancelled = true;
    result.report_lines = prepared_project.reportCollector().lines();

    if (!hydraulics_complete)
        markEpanetHydraulicResultCancelled(result.result_timeline);

    if (quality_attempted && !quality_complete && result.quality_result_timeline.analysis != WaterQualityAnalysisType::None)
        markEpanetQualityResultCancelled(result.quality_result_timeline);

    return result;
}

EpanetResultRun finalizeEpanetFailedRun(
    EpanetResultRun result,
    const HydraulicSimulationStatus &status,
    const EpanetPreparedProject &prepared_project)
{
    result.result_timeline.status = status;
    result.report_lines = prepared_project.reportCollector().lines();

    appendEpanetDiagnostics(result.result_timeline.diagnostics, prepared_project.project().diagnostics());

    if (!status.success)
        appendEpanetDiagnosticIfUnique(result.result_timeline.diagnostics, epanetDiagnosticFromStatus(status));

    appendEpanetReportDiagnostics(result.result_timeline.diagnostics, result.report_lines);
    finalizeEpanetHydraulicResultValidity(result.result_timeline);
    return result;
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
