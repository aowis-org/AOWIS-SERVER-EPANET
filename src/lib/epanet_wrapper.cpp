#include "epanet_wrapper.h"

#include <utility>

EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    
}

void EpanetWrapper::run(const SimulationRequest &request)
{
    if (this->epanet_project != nullptr)
    {
        emit signalSimulationFailed("EPANET project is already running");
        return;
    }
    
    this->simulation_request = request;
    
    this->simulation_result = SimulationResult();
    this->epanet_report.clear();
    
    int error = EN_createproject(&this->epanet_project);
    if (error != 0)
    {
        emit signalSimulationFailed("EN_createproject failed");
        return;
    }
    
    EN_setreportcallbackuserdata(this->epanet_project, this);
    EN_setreportcallback(this->epanet_project, &EpanetWrapper::epanetReportCallback);
    
    error = EN_init(this->epanet_project, "", "", EN_LPS, EN_HW);
    if (error != 0)
    {
        cleanupProject();
        
        emit signalSimulationFailed("EN_init failed");
        return;
    }
    
    // Important: EN_init can reset report-related project state.
    // Register the callback again after EN_init.
    EN_setreportcallbackuserdata(this->epanet_project, this);
    EN_setreportcallback(this->epanet_project, &EpanetWrapper::epanetReportCallback);
    
    // Reservoirs
    for (int i=0; i < request.reservoirs.length(); i++)
    {
        Reservoir reservoir = request.reservoirs.at(i);
        if (!addReservoir(reservoir))
        {
            emit signalSimulationFailed("Failed to add reservoir");
            cleanupProject();
            return;
        }
    }
    
    // Junctions
    for (int i=0; i < request.junctions.length(); i++)
    {
        Junction junction = request.junctions.at(i);
        if (!addJunction(junction))
        {
            emit signalSimulationFailed("Failed to add junction");
            cleanupProject();
            return;
        }
    }
    
    // Pipes
    for (int i=0; i < request.pipes.length(); i++)
    {
        Pipe pipe = request.pipes.at(i);
        if (!addPipe(pipe))
        {
            emit signalSimulationFailed("Failed to add pipe");
            cleanupProject();
            return;
        }
    }
    
    
    
    if (!runHydraulics())
    {
        cleanupProject();
        
        emit signalSimulationFailed("Hydraulic simulation failed");
        return;
    }
    
    if (!readResults())
    {
        EN_closeH(this->epanet_project);
        cleanupProject();
        
        emit signalSimulationFailed("Failed to read simulation results");
        return;
    }
    
    
    
    error = EN_closeH(this->epanet_project);
    if (error != 0)
    {
        qWarning() << "EN_closeH failed:" << error;
    }
    
    error = EN_saveH(this->epanet_project);
    if (error != 0)
    {
        qWarning() << "EN_saveH failed:" << error;
    }
    
    error = EN_report(this->epanet_project);
    if (error != 0)
    {
        qWarning() << "EN_report failed:" << error;
    }
    
    cleanupProject();
    
    emit signalSimulationFinished(this->simulation_result);
}



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



QStringList EpanetWrapper::reportTextList() const
{
    return this->epanet_report;
}
QString EpanetWrapper::reportText() const
{
    return this->epanet_report.join('\n');
}

void EpanetWrapper::epanetReportCallback(
    void *user_data,
    void *project_handle,
    const char *line
)
{
    Q_UNUSED(project_handle)
    
    EpanetWrapper *wrapper = static_cast<EpanetWrapper *>(user_data);
    
    if (wrapper == nullptr)
        return;
    
    if (line == nullptr)
        return;
    
    wrapper->epanet_report.append(QString::fromUtf8(line));
}

void EpanetWrapper::cleanupProject()
{
    if (this->epanet_project != nullptr)
    {
        EN_deleteproject(this->epanet_project);
        this->epanet_project = nullptr;
    }
}
