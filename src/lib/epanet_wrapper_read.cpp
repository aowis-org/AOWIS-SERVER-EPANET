#include "epanet_wrapper.h"

EpanetStatus EpanetWrapper::readResults()
{
    EpanetStatus status_junctions = readResultsJunctions();
    if (!status_junctions.success)
        return status_junctions;
    
    EpanetStatus status_pipes = readResultsPipes();
    if (!status_pipes.success)
        return status_pipes;
    
    EpanetStatus status;
    status.success = true;
    return status;
}
EpanetStatus EpanetWrapper::readResultsJunctions()
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
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadJunctionResults;
            status.operation = EpanetOperation::EN_getnodeindex;
            status.entity.type = EpanetEntityType::Junction;
            status.entity.id = junction.id;
            status.message = "Failed to get Junction Index";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getnodeindex failed for : " + junction.id;
            
            return status;
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
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadJunctionResults;
            status.operation = EpanetOperation::EN_getnodevalue;
            status.property = EpanetProperty::Headloss;
            status.entity.type = EpanetEntityType::Junction;
            status.entity.id = junction.id;
            status.message = "Failed to get Head for Junction";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getnodevalue EN_HEAD failed for : " + junction.id;
            
            return status;
        }
        
        error = EN_getnodevalue(
            this->epanet_project,
            junction_index,
            EN_PRESSURE,
            &junction_pressure_m
        );
        if (error != 0)
        {
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadJunctionResults;
            status.operation = EpanetOperation::EN_getnodevalue;
            status.property = EpanetProperty::Pressure;
            status.entity.type = EpanetEntityType::Junction;
            status.entity.id = junction.id;
            status.message = "Failed to get Pressure for Junction";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getnodevalue EN_PRESSURE failed for : " + junction.id;
            
            return status;
        }
        
        JunctionResult junction_result;
        junction_result.id = junction.id;
        junction_result.head_m = junction_head_m;
        junction_result.pressure_m = junction_pressure_m;
        
        this->simulation_result.junctions.append(junction_result);
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}
EpanetStatus EpanetWrapper::readResultsPipes()
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
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadPipeResults;
            status.operation = EpanetOperation::EN_getlinkindex;
            status.entity.type = EpanetEntityType::Pipe;
            status.entity.id = pipe.id;
            status.message = "Failed to get Link Index for Pipe";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getlinkindex failed for Pipe: " + pipe.id;
            
            return status;
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
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadPipeResults;
            status.operation = EpanetOperation::EN_getlinkvalue;
            status.property = EpanetProperty::Flow;
            status.entity.type = EpanetEntityType::Pipe;
            status.entity.id = pipe.id;
            status.message = "Failed to get Flow for Pipe";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getlinkvalue EN_FLOW failed for Pipe: " + pipe.id;
            
            return status;
        }
        
        error = EN_getlinkvalue(
            this->epanet_project,
            pipe_index,
            EN_VELOCITY,
            &pipe_velocity_mps
        );
        if (error != 0)
        {
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadPipeResults;
            status.operation = EpanetOperation::EN_getlinkvalue;
            status.property = EpanetProperty::Velocity;
            status.entity.type = EpanetEntityType::Pipe;
            status.entity.id = pipe.id;
            status.message = "Failed to get Velocity for Pipe";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getlinkvalue EN_VELOCITY failed for Pipe: " + pipe.id;
            
            return status;
        }
        
        error = EN_getlinkvalue(
            this->epanet_project,
            pipe_index,
            EN_HEADLOSS,
            &pipe_headloss
        );
        if (error != 0)
        {
            EpanetStatus status;
            status.success = false;
            status.epanet_error_code = error;
            status.stage = EpanetStage::ReadPipeResults;
            status.operation = EpanetOperation::EN_getlinkvalue;
            status.property = EpanetProperty::Headloss;
            status.entity.type = EpanetEntityType::Pipe;
            status.entity.id = pipe.id;
            status.message = "Failed to get Headloss for Pipe";
            status.message_epanet = getEpanetErrorMessage(error);
            status.details << "EN_getlinkvalue EN_HEADLOSS failed for Pipe: " + pipe.id;
            
            return status;
        }
        
        PipeResult pipe_result;
        pipe_result.id = pipe.id;
        pipe_result.flow_lps = pipe_flow_lps;
        pipe_result.velocity_mps = pipe_velocity_mps;
        pipe_result.headloss = pipe_headloss;
        
        this->simulation_result.pipes.append(pipe_result);
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}
