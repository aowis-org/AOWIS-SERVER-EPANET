#include "epanet_wrapper.h"

EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    
}

void EpanetWrapper::run(SimulationRequest request)
{
    this->epanet_report.clear();
    
    EN_createproject(&this->epanet_project);
    EN_setreportcallbackuserdata(this->epanet_project, this);
    EN_setreportcallback(this->epanet_project, &EpanetWrapper::epanetReportCallback);
    
    EN_init(this->epanet_project, "", "", EN_LPS, EN_HW);
    
    // Reservoirs
    for (int i=0; i < request.reservoirs.length(); i++)
    {
        Reservoir reservoir = request.reservoirs.at(i);
        addReservoir(reservoir);
    }
    
    // Junctions
    for (int i=0; i < request.junctions.length(); i++)
    {
        Junction junction = request.junctions.at(i);
        addJunction(junction);
    }
    
    // Pipes
    for (int i=0; i < request.pipes.length(); i++)
    {
        Pipe pipe = request.pipes.at(i);
        addPipe(pipe);
    }
    
    runHydraulics();
    readResults();
    
    EN_deleteproject(this->epanet_project);
    this->epanet_project = nullptr;
}

void EpanetWrapper::addReservoir(Reservoir reservoir)
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
        return;
    }
    
    error = EN_setnodevalue(this->epanet_project,
                            reservoir_index,
                            EN_ELEVATION,
                            reservoir.head_m
    );
    if (error != 0)
    {
        qWarning() << "EN_setnodevalue reservoir head failed:" << error;
        return;
    }
}

void EpanetWrapper::addJunction(Junction junction)
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
        return;
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
        return;
    }
}

void EpanetWrapper::addPipe(Pipe pipe)
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
        return;
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
        return;
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
        return;
    }
}

void EpanetWrapper::runHydraulics()
{
    int error = EN_openH(this->epanet_project);
    if (error != 0)
    {
        qWarning() << "EN_openH failed:" << error;
        return;
    }
    
    error = EN_initH(this->epanet_project, EN_NOSAVE | EN_INITFLOW);
    if (error != 0)
    {
        qWarning() << "EN_initH failed:" << error;
        EN_closeH(this->epanet_project);
        return;
    }
    
    long current_time_s = 0;
    
    error = EN_runH(this->epanet_project, &current_time_s);
    if (error != 0)
    {
        qWarning() << "EN_runH failed:" << error;
        EN_closeH(this->epanet_project);
        return;
    }
    
    qDebug() << "hydraulics calculated at time =" << current_time_s << "s";    
}

void EpanetWrapper::readResults()
{
    int junction_index = 0;
    int pipe_index = 0;
    
    QByteArray junction_id = QString("J1").toUtf8();
    QByteArray pipe_id = QString("P1").toUtf8();
    
    int error = EN_getnodeindex(
        this->epanet_project,
        junction_id.constData(),
        &junction_index
        );
    if (error != 0)
    {
        qWarning() << "EN_getnodeindex failed:" << error;
        return;
    }
    
    error = EN_getlinkindex(
        this->epanet_project,
        pipe_id.constData(),
        &pipe_index
        );
    if (error != 0)
    {
        qWarning() << "EN_getlinkindex failed:" << error;
        return;
    }
    
    double junction_head_m = 0.0;
    double junction_pressure_m = 0.0;
    double pipe_flow_lps = 0.0;
    double pipe_velocity_mps = 0.0;
    double pipe_headloss = 0.0;
    
    error = EN_getnodevalue(
        this->epanet_project,
        junction_index,
        EN_HEAD,
        &junction_head_m
        );
    if (error != 0)
    {
        qWarning() << "EN_getnodevalue head failed:" << error;
        return;
    }
    
    error = EN_getnodevalue(
        this->epanet_project,
        junction_index,
        EN_PRESSURE,
        &junction_pressure_m
        );
    if (error != 0)
    {
        qWarning() << "EN_getnodevalue pressure failed:" << error;
        return;
    }
    
    error = EN_getlinkvalue(
        this->epanet_project,
        pipe_index,
        EN_FLOW,
        &pipe_flow_lps
        );
    if (error != 0)
    {
        qWarning() << "EN_getlinkvalue flow failed:" << error;
        return;
    }
    
    error = EN_getlinkvalue(
        this->epanet_project,
        pipe_index,
        EN_VELOCITY,
        &pipe_velocity_mps
        );
    if (error != 0)
    {
        qWarning() << "EN_getlinkvalue velocity failed:" << error;
        return;
    }
    
    error = EN_getlinkvalue(
        this->epanet_project,
        pipe_index,
        EN_HEADLOSS,
        &pipe_headloss
        );
    if (error != 0)
    {
        qWarning() << "EN_getlinkvalue headloss failed:" << error;
        return;
    }
    
    qDebug() << "EPANET results:";
    qDebug() << "  J1 head =" << junction_head_m << "m";
    qDebug() << "  J1 pressure =" << junction_pressure_m << "m";
    qDebug() << "  P1 flow =" << pipe_flow_lps << "L/s";
    qDebug() << "  P1 velocity =" << pipe_velocity_mps << "m/s";
    qDebug() << "  P1 headloss =" << pipe_headloss;
}

QStringList EpanetWrapper::reportTextList()
{
    return this->epanet_report;
}
QString EpanetWrapper::reportText()
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

