#include "epanet_hydraulic_solver.h"
#include "epanet_project.h"
#include "epanet_result_reader.h"
#include "epanet_status_helpers.h"

EpanetHydraulicSolver::EpanetHydraulicSolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetResultReader &result_reader)
    : project(project), network(network), result_reader(result_reader)
{
}

HydraulicSimulationStatus EpanetHydraulicSolver::configureReport() const
{
    return this->project.configureReport(this->network);
}

HydraulicSimulationStatus EpanetHydraulicSolver::run(HydraulicSimulationResultTimeline &timeline)
{
    HydraulicSimulationStatus status = this->configureReport();
    if (!status.success)
        return status;

    int error = EN_openH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::OpenHydraulics, QStringLiteral("EN_openH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to open EPANET hydraulics"));

    error = EN_initH(this->project.handle(), EN_SAVE_AND_INIT);
    if (error != 0)
    {
        EN_closeH(this->project.handle());
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::InitializeHydraulics, QStringLiteral("EN_initH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to initialize EPANET hydraulics"));
    }

    long current_time_s = 0;
    long next_step_s = 0;
    do
    {
        error = EN_runH(this->project.handle(), &current_time_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, QStringLiteral("EN_runH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to run EPANET hydraulics"));
        }

        if (current_time_s < 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetStatus(HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET returned a negative elapsed simulation time"));
        }

        HydraulicSimulationResult result;
        result.time_elapsed_s = static_cast<quint64>(current_time_s);
        status = this->result_reader.read(result);
        if (!status.success)
        {
            const int close_error = EN_closeH(this->project.handle());
            if (close_error != 0)
                status.details << QStringLiteral("Additionally, EN_closeH failed with error code %1: %2").arg(close_error).arg(this->project.errorMessage(close_error));
            return status;
        }

        timeline.results.append(result);
        error = EN_nextH(this->project.handle(), &next_step_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::AdvanceHydraulics, QStringLiteral("EN_nextH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to advance EPANET hydraulics"));
        }
    }
    while (next_step_s > 0);

    error = EN_closeH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::CloseHydraulics, HydraulicSimulationStatusOperation::CloseHydraulics, QStringLiteral("EN_closeH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to close EPANET hydraulics"));

    return makeEpanetSuccess();
}
