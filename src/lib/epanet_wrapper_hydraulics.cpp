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
        status.entity.type = EpanetEntityType::None;
        status.message = "EN_openH failed";
        status.details << "Opening EPANET hydraulics analysis system failed";
        
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
        status.entity.type = EpanetEntityType::None;
        status.message = "EN_initH failed";
        status.details << "Initializing EPANET hydraulics analysis system failed";
        
        return status;
    }
    
    long current_time_s = 0;
    long next_step_s = 0;
    
    do
    {
        error = EN_runH(this->epanet_project, &current_time_s);
        if (error != 0)
        {
            EN_closeH(this->epanet_project);
            
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::RunHydraulics;
            status.operation = EpanetOperation::EN_runH;
            status.entity.type = EpanetEntityType::None;
            status.message = "EN_runH failed";
            status.details << "Running EPANET hydraulics analysis system failed";
            
            return status;
        }
        
        qDebug() << "hydraulics calculated at time =" << current_time_s << "s";
        
        error = EN_nextH(this->epanet_project, &next_step_s);
        if (error != 0)
        {
            EN_closeH(this->epanet_project);
            
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::RunHydraulics;
            status.operation = EpanetOperation::EN_nextH;
            status.entity.type = EpanetEntityType::None;
            status.message = "EN_nextH failed";
            status.details << "EPANET hydraulics analysis system failed";
            
            return status;
        }
        
    } while (next_step_s > 0);
    
    EpanetStatus status;
    status.success = true;
    return status;
}
