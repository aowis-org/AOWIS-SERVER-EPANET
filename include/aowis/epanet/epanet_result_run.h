#ifndef AOWIS_EPANET_RESULT_RUN_H
#define AOWIS_EPANET_RESULT_RUN_H

#include <optional>

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/hydraulic_simulation_options.h>
#include <aowis/model/hydraulic/multi_species.h>
#include <aowis/model/hydraulic/multi_species_results.h>
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
    QString report_text;
    EpanetRunState state = EpanetRunState::Pending;
};

// Runs through the legacy single-threaded EN/MSX toolkit rather than the
// handle-based EN_ API the rest of the adapter uses, so it executes as its
// own serialized sub-step -- see EPANET_BACKEND_SEMANTICS.md for the
// concurrency contract this implies.
struct EpanetMultiSpeciesResult
{
    MultiSpeciesRunOptions options;
    MultiSpeciesSimulationResultTimeline result_timeline;
    QStringList report_lines;
    QString report_text;
    EpanetRunState state = EpanetRunState::Pending;
};

struct EpanetResultRun
{
    HydraulicSimulationResultTimeline result_timeline;
    QList<EpanetQualityResult> quality_results;
    std::optional<EpanetMultiSpeciesResult> multi_species_result;

    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    QStringList report_lines;
    QString report_text;
    EpanetRunState state = EpanetRunState::Pending;
    bool cancelled = false;
};

Q_DECLARE_METATYPE(EpanetResultRun)

#endif // AOWIS_EPANET_RESULT_RUN_H
