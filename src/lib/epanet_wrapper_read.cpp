#include "epanet_wrapper.h"

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
