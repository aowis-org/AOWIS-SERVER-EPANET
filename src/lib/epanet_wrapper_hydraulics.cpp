#include "epanet_wrapper.h"

EpanetStatus EpanetWrapper::runHydraulics()
{
    EN_setreport(this->epanet_project, "STATUS YES");
    EN_setreport(this->epanet_project, "SUMMARY YES");
    EN_setreport(this->epanet_project, "NODES ALL");
    EN_setreport(this->epanet_project, "LINKS ALL");
    
    int error = EN_openH(this->epanet_project);
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::RunHydraulics;
        status.operation = EpanetOperation::EN_openH;
        status.entity.type = EpanetEntityType::HydraulicSolver;
        status.message = "EN_openH failed";
        status.message_epanet = getEpanetErrorMessage(error);
        status.details
            << "Opening EPANET hydraulics analysis system failed";
        
        return status;
    }
    
    error = EN_initH(this->epanet_project, EN_SAVE_AND_INIT);
    if (error != 0)
    {
        EN_closeH(this->epanet_project);
        
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::RunHydraulics;
        status.operation = EpanetOperation::EN_initH;
        status.entity.type = EpanetEntityType::HydraulicSolver;
        status.message = "EN_initH failed";
        status.message_epanet = getEpanetErrorMessage(error);
        status.details
            << "Initializing EPANET hydraulics analysis system failed";
        
        return status;
    }
    
    long current_time_s = 0;
    long next_step_s = 0;
    
    do
    {
        error = EN_runH(
            this->epanet_project,
            &current_time_s
        );
        if (error != 0)
        {
            EN_closeH(this->epanet_project);
            
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::RunHydraulics;
            status.operation = EpanetOperation::EN_runH;
            status.entity.type = EpanetEntityType::HydraulicSolver;
            status.message = "EN_runH failed";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details
                << "Running EPANET hydraulics analysis system failed"
                << QString(
                       "Hydraulic time: %1 seconds"
                ).arg(current_time_s);
            
            return status;
        }
        
        SimulationResult result;
        result.elapsed_time_s = current_time_s;
        
        EpanetStatus result_status =
            readResults(result);
        if (!result_status.success)
        {
            const int close_error =
                EN_closeH(this->epanet_project);
            
            if (close_error != 0)
            {
                result_status.details
                    << QString(
                        "Additionally, EN_closeH failed "
                        "with error code %1: %2"
                    )
                    .arg(close_error)
                    .arg(
                        getEpanetErrorMessage(
                            close_error
                        )
                    );
            }
            
            return result_status;
        }
        
        this->simulation_result_timeline.results.append(result);
        
        qDebug()
            << "Hydraulics calculated at time ="
            << current_time_s
            << "s";
        
        error = EN_nextH(
            this->epanet_project,
            &next_step_s
        );
        
        if (error != 0)
        {
            EN_closeH(this->epanet_project);
            
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::RunHydraulics;
            status.operation = EpanetOperation::EN_nextH;
            status.entity.type = EpanetEntityType::HydraulicSolver;
            status.message = "EN_nextH failed";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details
                << "Advancing EPANET hydraulics analysis failed"
                << QString(
                    "Hydraulic time: %1 seconds"
                ).arg(current_time_s);
            
            return status;
        }
    }
    while (next_step_s > 0);
    
    EpanetStatus status;
    status.success = true;
    return status;
}
