#include "epanet_quality_solver.h"

#include "epanet_project.h"
#include "epanet_quality_result_reader.h"
#include "epanet_status_helpers.h"

namespace
{
bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

HydraulicSimulationDiagnostic diagnosticFromQualityStatus(const HydraulicSimulationStatus &status, HydraulicSimulationDiagnosticSeverity severity)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.stage = status.stage;
    diagnostic.operation = status.operation;
    diagnostic.property = status.property;
    diagnostic.entity = status.entity;
    diagnostic.message = status.message;
    diagnostic.details = status.details;
    diagnostic.backend_name = status.backend_name;
    diagnostic.backend_error_code = status.backend_error_code;
    diagnostic.backend_operation = status.backend_operation;
    diagnostic.message_backend = status.message_backend;
    return diagnostic;
}

void collectQualityFailure(WaterQualitySimulationResultTimeline &timeline, HydraulicSimulationStatus status, HydraulicSimulationStatus &first_failure, HydraulicSimulationDiagnosticSeverity severity, long simulation_time_s = -1)
{
    if (status.success)
        return;

    if (simulation_time_s >= 0)
        status.details.append(QStringLiteral("Simulation time: %1 s").arg(simulation_time_s));

    timeline.diagnostics.append(diagnosticFromQualityStatus(status, severity));
    if (first_failure.success)
        first_failure = status;
}
}

EpanetQualitySolver::EpanetQualitySolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetQualityResultReader &result_reader)
    : project(project), network(network), result_reader(result_reader)
{
}

HydraulicSimulationStatus EpanetQualitySolver::run(
    WaterQualitySimulationResultTimeline &timeline,
    const std::function<bool()> &cancellation_requested,
    bool &cancelled)
{
    cancelled = false;

    if (this->network.options_quality.analysis == WaterQualityAnalysisType::None)
    {
        timeline.status = makeEpanetSuccess();
        timeline.validity = WaterQualitySimulationResultValidity::NotRun;
        return makeEpanetSuccess();
    }

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        return makeEpanetSuccess();
    }

    HydraulicSimulationStatus first_failure = makeEpanetSuccess();
    HydraulicSimulationStatus status = makeEpanetSuccess();

    int error = EN_openQ(this->project.handle());
    if (error != 0)
    {
        status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::OpenQuality, QStringLiteral("EN_openQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to open EPANET water-quality analysis"));
        if (!status.success)
        {
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            timeline.status = first_failure;
            return first_failure;
        }
    }

    error = EN_initQ(this->project.handle(), EN_SAVE);
    if (error != 0)
    {
        status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::InitializeQuality, QStringLiteral("EN_initQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to initialize EPANET water-quality analysis"));
        if (!status.success)
        {
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);

            const int close_error = EN_closeQ(this->project.handle());
            if (close_error != 0)
            {
                const HydraulicSimulationStatus close_status = processEpanetReturnCode(this->project, close_error, HydraulicSimulationStatusStage::CloseQuality, HydraulicSimulationStatusOperation::CloseQuality, QStringLiteral("EN_closeQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to close EPANET water-quality analysis after initialization failure"));
                if (!close_status.success)
                    collectQualityFailure(timeline, close_status, first_failure, HydraulicSimulationDiagnosticSeverity::Error);
            }
            timeline.status = first_failure;
            return first_failure;
        }
    }

    long current_time_s = 0;
    long previous_time_s = -1;
    long time_left_s = 0;

    while (!cancelled)
    {
        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        error = EN_runQ(this->project.handle(), &current_time_s);

        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        if (error != 0)
        {
            status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::RunQuality, QStringLiteral("EN_runQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("EPANET water-quality analysis returned a diagnostic"));
            if (!status.success)
            {
                collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal, current_time_s);
                break;
            }
        }

        if (current_time_s < 0)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::RunQuality, HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("EPANET returned a negative water-quality simulation time"));
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }

        if (previous_time_s >= 0 && current_time_s <= previous_time_s)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::RunQuality, HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("EPANET water-quality simulation time did not advance"));
            status.details.append(QStringLiteral("Previous simulation time: %1 s").arg(previous_time_s));
            status.details.append(QStringLiteral("Current simulation time: %1 s").arg(current_time_s));
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }
        previous_time_s = current_time_s;

        WaterQualitySimulationResult result;
        result.time_elapsed_s = static_cast<quint64>(current_time_s);
        status = this->result_reader.read(result);
        if (!status.success)
        {
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Error, current_time_s);
        }
        else
        {
            result.status = makeEpanetSuccess();
            timeline.results.append(result);
        }

        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        error = EN_stepQ(this->project.handle(), &time_left_s);
        if (error != 0)
        {
            status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::StepQuality, QStringLiteral("EN_stepQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to advance EPANET water-quality timestep"));
            if (!status.success)
            {
                collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal, current_time_s);
                break;
            }
        }

        if (time_left_s < 0)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunQuality, HydraulicSimulationStatusOperation::StepQuality, HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("EPANET returned a negative remaining water-quality simulation time"));
            status.details.append(QStringLiteral("Time left: %1 s").arg(time_left_s));
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal, current_time_s);
            break;
        }

        if (time_left_s <= 0)
            break;
    }

    const int close_error = EN_closeQ(this->project.handle());
    if (close_error != 0)
    {
        status = processEpanetReturnCode(this->project, close_error, HydraulicSimulationStatusStage::CloseQuality, HydraulicSimulationStatusOperation::CloseQuality, QStringLiteral("EN_closeQ"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to close EPANET water-quality analysis"));
        if (!status.success)
            collectQualityFailure(timeline, status, first_failure, HydraulicSimulationDiagnosticSeverity::Error, current_time_s);
    }

    if (cancelled)
    {
        timeline.status = makeEpanetSuccess();
        return makeEpanetSuccess();
    }

    timeline.status = first_failure.success ? makeEpanetSuccess() : first_failure;
    return timeline.status;
}
