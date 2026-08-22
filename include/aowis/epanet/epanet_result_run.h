#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <QList>
#include <QMetaType>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

enum class EpanetRunState
{
    Pending,
    Running,
    Success,
    Warning,
    Error,
    Cancelled,
    Skipped
};

struct EpanetQualityResult
{
    WaterQualitySolverOptions options;
    WaterQualitySimulationResultTimeline result_timeline;
    QStringList report_lines;
    EpanetRunState state = EpanetRunState::Pending;
};

struct EpanetResultRun
{
    HydraulicSimulationResultTimeline result_timeline;
    QList<EpanetQualityResult> quality_results;

    // Compatibility view for the original single-quality run API. New multi-quality
    // callers should use quality_results instead.
    WaterQualitySimulationResultTimeline quality_result_timeline;

    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    QStringList report_lines;
    EpanetRunState state = EpanetRunState::Pending;
    bool cancelled = false;
};

Q_DECLARE_METATYPE(EpanetResultRun)

#endif // AOWIS_EPANET_RESULT_RUN_H
