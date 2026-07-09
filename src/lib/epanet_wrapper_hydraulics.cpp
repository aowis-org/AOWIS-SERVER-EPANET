#include "epanet_wrapper.h"

bool EpanetWrapper::runHydraulics()
{
    EN_setreport(this->epanet_project, "STATUS YES");
    EN_setreport(this->epanet_project, "SUMMARY YES");
    EN_setreport(this->epanet_project, "NODES ALL");
    EN_setreport(this->epanet_project, "LINKS ALL");
    
    int error = EN_openH(this->epanet_project);
    if (error != 0)
    {
        qWarning() << "EN_openH failed:" << error;
        return false;
    }
    
    error = EN_initH(this->epanet_project, EN_SAVE_AND_INIT);
    if (error != 0)
    {
        qWarning() << "EN_initH failed:" << error;
        EN_closeH(this->epanet_project);
        return false;
    }
    
    long current_time_s = 0;
    long next_step_s = 0;
    
    do
    {
        error = EN_runH(this->epanet_project, &current_time_s);
        if (error != 0)
        {
            qWarning() << "EN_runH failed:" << error;
            EN_closeH(this->epanet_project);
            return false;
        }
        
        qDebug() << "hydraulics calculated at time =" << current_time_s << "s";
        
        error = EN_nextH(this->epanet_project, &next_step_s);
        if (error != 0)
        {
            qWarning() << "EN_nextH failed:" << error;
            EN_closeH(this->epanet_project);
            return false;
        }
        
    } while (next_step_s > 0);
    
    return true;
}
