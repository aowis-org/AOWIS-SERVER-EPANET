#include "epanet_hydraulic_solver.h"
#include "epanet_project.h"
#include "epanet_result_reader.h"
#include "epanet_status_helpers.h"

#include <array>

EpanetHydraulicSolver::EpanetHydraulicSolver(EpanetProject &project, const EpanetResultReader &result_reader)
    : project(project), result_reader(result_reader)
{
}

EpanetStatus EpanetHydraulicSolver::run(EpanetResultTimeline &timeline)
{
    constexpr std::array<const char *, 4> report_commands = {"STATUS YES", "SUMMARY YES", "NODES ALL", "LINKS ALL"};
    for (const char *command : report_commands)
    {
        const int report_error = EN_setreport(this->project.handle(), command);
        if (report_error != 0)
            return makeEpanetError(this->project, report_error, EpanetStatusStage::RunHydraulics, EpanetStatusOperation::EN_setreport, EpanetStatusEntityType::Report, QString(), "Failed to configure the EPANET report");
    }

    int error = EN_openH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::RunHydraulics, EpanetStatusOperation::EN_openH, EpanetStatusEntityType::HydraulicSolver, QString(), "Failed to open EPANET hydraulics");

    error = EN_initH(this->project.handle(), EN_SAVE_AND_INIT);
    if (error != 0)
    {
        EN_closeH(this->project.handle());
        return makeEpanetError(this->project, error, EpanetStatusStage::RunHydraulics, EpanetStatusOperation::EN_initH, EpanetStatusEntityType::HydraulicSolver, QString(), "Failed to initialize EPANET hydraulics");
    }

    long current_time_s = 0;
    long next_step_s = 0;
    do
    {
        error = EN_runH(this->project.handle(), &current_time_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, EpanetStatusStage::RunHydraulics, EpanetStatusOperation::EN_runH, EpanetStatusEntityType::HydraulicSolver, QString(), "Failed to run EPANET hydraulics");
        }

        EpanetResult result;
        result.time_elapsed_s = current_time_s;
        EpanetStatus status = this->result_reader.read(result);
        if (!status.success)
        {
            const int close_error = EN_closeH(this->project.handle());
            if (close_error != 0)
                status.details << QString("Additionally, EN_closeH failed with error code %1: %2").arg(close_error).arg(this->project.errorMessage(close_error));
            return status;
        }

        timeline.results.append(result);
        error = EN_nextH(this->project.handle(), &next_step_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, EpanetStatusStage::RunHydraulics, EpanetStatusOperation::EN_nextH, EpanetStatusEntityType::HydraulicSolver, QString(), "Failed to advance EPANET hydraulics");
        }
    }
    while (next_step_s > 0);

    error = EN_closeH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, EpanetStatusStage::CloseHydraulics, EpanetStatusOperation::EN_closeH, EpanetStatusEntityType::HydraulicSolver, QString(), "Failed to close EPANET hydraulics");

    return makeEpanetSuccess();
}
