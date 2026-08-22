#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <QList>
#include <QMetaType>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/hydraulic_simulation_options.h>
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

    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    QStringList report_lines;
    EpanetRunState state = EpanetRunState::Pending;
    bool cancelled = false;
};

Q_DECLARE_METATYPE(EpanetResultRun)

#endif // AOWIS_EPANET_RESULT_RUN_H
