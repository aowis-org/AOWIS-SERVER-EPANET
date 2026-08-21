#ifndef AOWIS_EPANET_RESULT_BATCH_H
#define AOWIS_EPANET_RESULT_BATCH_H

#include <QList>
#include <QMetaType>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/water_quality_simulation_results.h>

enum class EpanetBatchRunState
{
    Pending,
    Running,
    Success,
    Warning,
    Error,
    Cancelled,
    Skipped
};

struct EpanetBatchQualityResult
{
    WaterQualitySolverOptions options;
    WaterQualitySimulationResultTimeline result_timeline;
    QStringList report_lines;
    EpanetBatchRunState state = EpanetBatchRunState::Pending;
};

struct EpanetBatchHydraulicResult
{
    HydraulicHeadlossFormula headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    HydraulicSimulationResultTimeline result_timeline;
    QList<EpanetBatchQualityResult> quality_results;
    QStringList report_lines;
    EpanetBatchRunState state = EpanetBatchRunState::Pending;
};

struct EpanetResultBatch
{
    QList<EpanetBatchHydraulicResult> hydraulic_runs;
    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    EpanetBatchRunState state = EpanetBatchRunState::Pending;
    bool cancelled = false;
};

Q_DECLARE_METATYPE(EpanetResultBatch)

#endif // AOWIS_EPANET_RESULT_BATCH_H
