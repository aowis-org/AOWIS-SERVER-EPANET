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
        //emit signalSimulationFailed("EPANET project is already running");
        return;
    }
    
    this->simulation_request = request;
    
    this->simulation_result = SimulationResult();
    this->epanet_report.clear();
    
    int error = EN_createproject(&this->epanet_project);
    if (error != 0)
    {
        //emit signalSimulationFailed("EN_createproject failed");
        return;
    }
    
    EN_setreportcallbackuserdata(this->epanet_project, this);
    EN_setreportcallback(this->epanet_project, &EpanetWrapper::epanetReportCallback);
    
    error = EN_init(this->epanet_project, "", "", EN_LPS, EN_HW);
    if (error != 0)
    {
        cleanupProject();
        
        //emit signalSimulationFailed("EN_init failed");
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
        EpanetStatus status = addReservoir(reservoir);
        if (!status.success)
        {
            emit signalSimulationFailed(status);
            cleanupProject();
            return;
        }
    }
    
    // Junctions
    for (int i=0; i < request.junctions.length(); i++)
    {
        Junction junction = request.junctions.at(i);
        EpanetStatus status = addJunction(junction);
        if (!status.success)
        {
            emit signalSimulationFailed(status);
            cleanupProject();
            return;
        }
    }
    
    // Pipes
    for (int i=0; i < request.pipes.length(); i++)
    {
        Pipe pipe = request.pipes.at(i);
        EpanetStatus status = addPipe(pipe);
        if (!status.success)
        {
            emit signalSimulationFailed(status);
            cleanupProject();
            return;
        }
    }
    
    
    
    if (!runHydraulics())
    {
        cleanupProject();
        
        //emit signalSimulationFailed("Hydraulic simulation failed");
        return;
    }
    
    if (!readResults())
    {
        EN_closeH(this->epanet_project);
        cleanupProject();
        
        //emit signalSimulationFailed("Failed to read simulation results");
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
