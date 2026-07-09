#include "epanet_wrapper.h"

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
