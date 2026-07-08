#include "epanet_wrapper.h"

EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    
}

void EpanetWrapper::run()
{
    qDebug() << "running ...";
    
    // create the network for now here as dummies
    Reservoir reservoir;
    reservoir.id = "R1";
    reservoir.head_m = 30.0;
    
    Junction junction;
    junction.id = "J1";
    junction.elevation_m = 0.0;
    junction.demand_lps = 1.0;
    
    Pipe pipe;
    pipe.id = "P1";
    pipe.node_id_from = reservoir.id;
    pipe.node_id_to = junction.id;
    pipe.length_m = 100.0;
    pipe.diameter_mm = 150.0;
    pipe.roughness_hw = 130.0;
    pipe.minor_loss = 0.0;
    pipe.open = true;
    
    // start the epanet thing
    
    EN_createproject(&this->project);
    EN_init(this->project, "", "", EN_LPS, EN_HW);
    
    addReservoir(reservoir);
    addJunction(junction);
    addPipe(pipe);
    
    runHydraulics();
    readResults();
    
    EN_deleteproject(this->project);
    this->project = nullptr;
}

void EpanetWrapper::addReservoir(Reservoir reservoir)
{
    QByteArray reservoir_id = reservoir.id.toUtf8();
    int reservoir_index = 0;
    
    int error = EN_addnode(
        this->project,
        reservoir_id.constData(),
        EN_RESERVOIR,
        &reservoir_index
    );
    if (error != 0)
    {
        qWarning() << "EN_addnode reservoir failed:" << error;
        return;
    }
    
    error = EN_setnodevalue(this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
    int error = EN_openH(this->project);
    if (error != 0)
    {
        qWarning() << "EN_openH failed:" << error;
        return;
    }
    
    error = EN_initH(this->project, EN_NOSAVE | EN_INITFLOW);
    if (error != 0)
    {
        qWarning() << "EN_initH failed:" << error;
        EN_closeH(this->project);
        return;
    }
    
    long current_time_s = 0;
    
    error = EN_runH(this->project, &current_time_s);
    if (error != 0)
    {
        qWarning() << "EN_runH failed:" << error;
        EN_closeH(this->project);
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
        this->project,
        junction_id.constData(),
        &junction_index
        );
    if (error != 0)
    {
        qWarning() << "EN_getnodeindex failed:" << error;
        return;
    }
    
    error = EN_getlinkindex(
        this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
        this->project,
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
