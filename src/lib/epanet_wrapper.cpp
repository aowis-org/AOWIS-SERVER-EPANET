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

bool EpanetWrapper::addReservoir(const Reservoir &reservoir)
{
    QByteArray reservoir_id = reservoir.id.toUtf8();
    int reservoir_index = 0;
    
    int error = EN_addnode(
        this->epanet_project,
        reservoir_id.constData(),
        EN_RESERVOIR,
        &reservoir_index
    );
    if (error != 0)
    {
        qWarning() << "EN_addnode reservoir failed:" << error;
        return false;
    }
    
    error = EN_setnodevalue(this->epanet_project,
                            reservoir_index,
                            EN_ELEVATION,
                            reservoir.head_m
    );
    if (error != 0)
    {
        qWarning() << "EN_setnodevalue reservoir head failed:" << error;
        return false;
    }
    
    return true;
}

bool EpanetWrapper::addJunction(const Junction &junction)
{
    QByteArray junction_id = junction.id.toUtf8();
    int junction_index = 0;
    
    int error = EN_addnode(
        this->epanet_project,
        junction_id.constData(),
        EN_JUNCTION,
        &junction_index
    );
    if (error != 0)
    {
        qWarning() << "EN_addnode junction failed:" << error;
        return false;
    }
    
    error = EN_setjuncdata(
        this->epanet_project,
        junction_index,
        junction.elevation_m,
        junction.demand_lps,
        ""
    );
    if (error != 0)
    {
        qWarning() << "EN_setjuncdata junction failed:" << error;
        return false;
    }
    
    return true;
}

bool EpanetWrapper::addPipe(const Pipe &pipe)
{
    QByteArray pipe_id = pipe.id.toUtf8();
    QByteArray node_id_from = pipe.node_id_from.toUtf8();
    QByteArray node_id_to = pipe.node_id_to.toUtf8();
    
    int pipe_index = 0;
    
    int error = EN_addlink(
        this->epanet_project,
        pipe_id.constData(),
        EN_PIPE,
        node_id_from.constData(),
        node_id_to.constData(),
        &pipe_index
    );
    if (error != 0)
    {
        qWarning() << "EN_addlink pipe failed:" << error;
        return false;
    }
    
    error = EN_setpipedata(
        this->epanet_project,
        pipe_index,
        pipe.length_m,
        pipe.diameter_mm,
        pipe.roughness_hw,
        pipe.minor_loss
    );
    if (error != 0)
    {
        qWarning() << "EN_setpipedata pipe failed:" << error;
        return false;
    }
    
    error = EN_setlinkvalue(
        this->epanet_project,
        pipe_index,
        EN_INITSTATUS,
        pipe.open ? EN_OPEN : EN_CLOSED
    );
    if (error != 0)
    {
        qWarning() << "EN_setlinkvalue pipe status failed:" << error;
        return false;
    }
    
    return true;
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

bool EpanetWrapper::readResults()
{
    if (!readResultsJunctions())
        return false;
    
    if (!readResultsPipes())
        return false;
    
    return true;
}
bool EpanetWrapper::readResultsJunctions()
{
    for (const Junction &junction : std::as_const(this->simulation_request.junctions))
    {
        int junction_index = 0;
        QByteArray junction_id = junction.id.toUtf8();
        
        int error = EN_getnodeindex(
            this->epanet_project,
            junction_id.constData(),
            &junction_index
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getnodeindex failed for junction"
                       << junction.id
                       << "error =" << error;
            return false;
        }
        
        double junction_head_m = 0.0;
        double junction_pressure_m = 0.0;
        
        error = EN_getnodevalue(
            this->epanet_project,
            junction_index,
            EN_HEAD,
            &junction_head_m
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getnodevalue head failed for junction"
                       << junction.id
                       << "error =" << error;
            return false;
        }
        
        error = EN_getnodevalue(
            this->epanet_project,
            junction_index,
            EN_PRESSURE,
            &junction_pressure_m
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getnodevalue pressure failed for junction"
                       << junction.id
                       << "error =" << error;
            return false;
        }
        
        JunctionResult junction_result;
        junction_result.id = junction.id;
        junction_result.head_m = junction_head_m;
        junction_result.pressure_m = junction_pressure_m;
        
        this->simulation_result.junctions.append(junction_result);
    }
    
    return true;
}
bool EpanetWrapper::readResultsPipes()
{
    for (const Pipe &pipe : std::as_const(this->simulation_request.pipes))
    {
        int pipe_index = 0;
        QByteArray pipe_id = pipe.id.toUtf8();
        
        int error = EN_getlinkindex(
            this->epanet_project,
            pipe_id.constData(),
            &pipe_index
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getlinkindex failed for pipe"
                       << pipe.id
                       << "error =" << error;
            return false;
        }
        
        double pipe_flow_lps = 0.0;
        double pipe_velocity_mps = 0.0;
        double pipe_headloss = 0.0;
        
        error = EN_getlinkvalue(
            this->epanet_project,
            pipe_index,
            EN_FLOW,
            &pipe_flow_lps
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getlinkvalue flow failed for pipe"
                       << pipe.id
                       << "error =" << error;
            return false;
        }
        
        error = EN_getlinkvalue(
            this->epanet_project,
            pipe_index,
            EN_VELOCITY,
            &pipe_velocity_mps
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getlinkvalue velocity failed for pipe"
                       << pipe.id
                       << "error =" << error;
            return false;
        }
        
        error = EN_getlinkvalue(
            this->epanet_project,
            pipe_index,
            EN_HEADLOSS,
            &pipe_headloss
            );
        
        if (error != 0)
        {
            qWarning() << "EN_getlinkvalue headloss failed for pipe"
                       << pipe.id
                       << "error =" << error;
            return false;
        }
        
        PipeResult pipe_result;
        pipe_result.id = pipe.id;
        pipe_result.flow_lps = pipe_flow_lps;
        pipe_result.velocity_mps = pipe_velocity_mps;
        pipe_result.headloss = pipe_headloss;
        
        this->simulation_result.pipes.append(pipe_result);
    }
    
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
