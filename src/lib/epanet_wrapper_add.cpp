#include "epanet_wrapper.h"

EpanetStatus EpanetWrapper::addReservoir(const Reservoir &reservoir)
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
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddReservoir;
        status.operation = EpanetOperation::EN_addnode;
        status.entity.type = EpanetEntityType::Reservoir;
        status.entity.id = reservoir.id;
        status.message = "Failed to add Reservoir";
        status.details << "Node: " + reservoir.id;
        return status;
    }
    
    error = EN_setnodevalue(this->epanet_project,
                            reservoir_index,
                            EN_ELEVATION,
                            reservoir.head_m
                            );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddReservoir;
        status.operation = EpanetOperation::EN_setnodevalue;
        status.entity.type = EpanetEntityType::Reservoir;
        status.entity.id = reservoir.id;
        status.message = "Failed to add Reservoir Head";
        status.details << "Node: " + reservoir.id;
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}

EpanetStatus EpanetWrapper::addJunction(const Junction &junction)
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
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddJunction;
        status.operation = EpanetOperation::EN_addnode;
        status.entity.type = EpanetEntityType::Pipe;
        status.entity.id = junction.id;
        status.message = "Failed to add Junction";
        status.details << "Node: " + junction.id;
        return status;
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
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddJunction;
        status.operation = EpanetOperation::EN_setjuncdata;
        status.entity.type = EpanetEntityType::Pipe;
        status.entity.id = junction.id;
        status.message = "Failed to add Junction Data";
        status.details << "Node: " + junction.id;
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}

EpanetStatus EpanetWrapper::addPipe(const Pipe &pipe)
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
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddPipe;
        status.operation = EpanetOperation::EN_addlink;
        status.entity.type = EpanetEntityType::Pipe;
        status.entity.id = pipe.id;
        status.message = "Failed to add pipe";
        status.details << "From node: " + pipe.node_id_from;
        status.details << "To node: " + pipe.node_id_to;
        return status;
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
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddPipe;
        status.operation = EpanetOperation::EN_setpipedata;
        status.entity.type = EpanetEntityType::Pipe;
        status.entity.id = pipe.id;
        status.message = "Failed to add pipe data";
        status.details << "From node: " + pipe.node_id_from;
        status.details << "To node: " + pipe.node_id_to;
        return status;
    }
    
    error = EN_setlinkvalue(
        this->epanet_project,
        pipe_index,
        EN_INITSTATUS,
        pipe.open ? EN_OPEN : EN_CLOSED
        );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddPipe;
        status.operation = EpanetOperation::EN_setlinkvalue;
        status.entity.type = EpanetEntityType::Pipe;
        status.entity.id = pipe.id;
        status.message = "Failed to add pipe status";
        status.details << "From node: " + pipe.node_id_from;
        status.details << "To node: " + pipe.node_id_to;
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}
