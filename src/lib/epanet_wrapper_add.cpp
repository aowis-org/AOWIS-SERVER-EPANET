#include "epanet_wrapper.h"

EpanetStatus EpanetWrapper::addEntities(const NetworkHydraulic &request)
{
    // Tank volume curves
    for (int i = 0; i < request.tank_volume_curves.length(); i++)
    {
        const TankVolumeCurve &curve =
            request.tank_volume_curves.at(i);
        
        EpanetStatus status = addTankVolumeCurve(curve);
        
        if (!status.success)
            return status;
    }
    
    // Reservoirs
    for (int i=0; i < request.reservoirs.length(); i++)
    {
        Reservoir reservoir = request.reservoirs.at(i);
        EpanetStatus status = addReservoir(reservoir);
        if (!status.success)
        {
            return status;
        }
    }
    
    // Junctions
    for (int i=0; i < request.junctions.length(); i++)
    {
        Junction junction = request.junctions.at(i);
        EpanetStatus status = addJunction(junction);
        if (!status.success)
        {
            return status;
        }
    }
    
    // Tanks
    for (int i = 0; i < request.tanks.length(); i++)
    {
        const Tank &tank = request.tanks.at(i);
        
        EpanetStatus status = addTank(tank);
        
        if (!status.success)
            return status;
    }
    
    // Pipes must be added after all nodes.
    for (int i=0; i < request.pipes.length(); i++)
    {
        Pipe pipe = request.pipes.at(i);
        EpanetStatus status = addPipe(pipe);
        if (!status.success)
        {
            return status;
        }
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}

EpanetStatus EpanetWrapper::addTankVolumeCurve(
    const TankVolumeCurve &curve
    )
{
    if (curve.id.isEmpty())
    {
        EpanetStatus status;
        status.success = false;
        status.stage = EpanetStage::AddTankVolumeCurve;
        status.entity.type = EpanetEntityType::Curve;
        status.message = "Tank volume curve has no ID";
        return status;
    }
    
    if (curve.points.length() < 2)
    {
        EpanetStatus status;
        status.success = false;
        status.stage = EpanetStage::AddTankVolumeCurve;
        status.entity.type = EpanetEntityType::Curve;
        status.entity.id = curve.id;
        status.message =
            "Tank volume curve requires at least two points";
        return status;
    }
    
    QByteArray curve_id = curve.id.toUtf8();
    
    int error = EN_addcurve(
        this->epanet_project,
        curve_id.data()
        );
    
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTankVolumeCurve;
        status.operation = EpanetOperation::EN_addcurve;
        status.entity.type = EpanetEntityType::Curve;
        status.entity.id = curve.id;
        status.message = "Failed to add tank volume curve";
        status.message_epanet =
            getEpanetErrorMessage(error);
        return status;
    }
    
    int curve_index = 0;
    
    error = EN_getcurveindex(
        this->epanet_project,
        curve_id.data(),
        &curve_index
        );
    
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTankVolumeCurve;
        status.operation = EpanetOperation::EN_getcurveindex;
        status.entity.type = EpanetEntityType::Curve;
        status.entity.id = curve.id;
        status.message = "Failed to get tank volume curve index";
        status.message_epanet = getEpanetErrorMessage(error);
        return status;
    }
    
    QList<double> levels_m;
    QList<double> volumes_m3;
    
    levels_m.reserve(curve.points.length());
    volumes_m3.reserve(curve.points.length());
    
    for (int i = 0; i < curve.points.length(); i++)
    {
        const TankVolumeCurvePoint &point =
            curve.points.at(i);
        
        if (i > 0)
        {
            const TankVolumeCurvePoint &previous_point =
                curve.points.at(i - 1);
            
            if (point.level_m <= previous_point.level_m)
            {
                EpanetStatus status;
                status.success = false;
                status.stage = EpanetStage::AddTankVolumeCurve;
                status.entity.type = EpanetEntityType::Curve;
                status.entity.id = curve.id;
                status.message = "Tank volume curve levels must increase";
                return status;
            }
            
            if (point.volume_m3 <= previous_point.volume_m3)
            {
                EpanetStatus status;
                status.success = false;
                status.stage = EpanetStage::AddTankVolumeCurve;
                status.entity.type = EpanetEntityType::Curve;
                status.entity.id = curve.id;
                status.message = "Tank volume curve volumes must increase";
                return status;
            }
        }
        
        levels_m.append(point.level_m);
        volumes_m3.append(point.volume_m3);
    }
    
    error = EN_setcurve(
        this->epanet_project,
        curve_index,
        levels_m.data(),
        volumes_m3.data(),
        levels_m.length()
        );
    
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTankVolumeCurve;
        status.operation = EpanetOperation::EN_setcurve;
        status.entity.type = EpanetEntityType::Curve;
        status.entity.id = curve.id;
        status.entity.index = curve_index;
        status.message = "Failed to set tank volume curve data";
        status.message_epanet = getEpanetErrorMessage(error);
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}

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
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.entity.type = EpanetEntityType::Junction;
        status.entity.id = junction.id;
        status.message = "Failed to add Junction";
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.entity.type = EpanetEntityType::Junction;
        status.entity.id = junction.id;
        status.message = "Failed to add Junction Data";
        status.message_epanet = getEpanetErrorMessage(error);
        status.details << "Node: " + junction.id;
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}

EpanetStatus EpanetWrapper::addTank(const Tank &tank)
{
    QByteArray tank_id = tank.id.toUtf8();
    
    int tank_index = 0;
    
    int error = EN_addnode(
        this->epanet_project,
        tank_id.data(),
        EN_TANK,
        &tank_index
        );
    
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTank;
        status.operation = EpanetOperation::EN_addnode;
        status.entity.type = EpanetEntityType::Tank;
        status.entity.id = tank.id;
        status.message = "Failed to add tank";
        status.message_epanet = getEpanetErrorMessage(error);
        return status;
    }
    
    EpanetResolvers resolvers;
    
    const double bottom_elevation_m =
        resolvers.resolveTankBottomElevation(tank);
    
    const double diameter_m =
        resolvers.resolveTankDiameter(tank);
    
    QByteArray volume_curve_id;
    
    if (tank.geometry_input_type == TankGeometryInputType::VolumeCurve)
    {
        if (tank.volume_curve_id.isEmpty())
        {
            EpanetStatus status;
            status.success = false;
            status.stage = EpanetStage::AddTank;
            status.entity.type = EpanetEntityType::Tank;
            status.entity.id = tank.id;
            status.entity.index = tank_index;
            status.message = "Volume-curve tank has no volume curve ID";
            return status;
        }
        
        volume_curve_id = tank.volume_curve_id.toUtf8();
    }
    
    error = EN_settankdata(
        this->epanet_project,
        tank_index,
        bottom_elevation_m,
        tank.initial_level_m,
        tank.minimum_level_m,
        tank.maximum_level_m,
        diameter_m,
        tank.minimum_volume_m3,
        volume_curve_id.data()
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTank;
        status.operation =
            EpanetOperation::EN_settankdata;
        status.entity.type = EpanetEntityType::Tank;
        status.entity.id = tank.id;
        status.entity.index = tank_index;
        status.message = "Failed to set tank data";
        status.message_epanet = getEpanetErrorMessage(error);
        status.details
            << "Bottom elevation: "
                   + QString::number(bottom_elevation_m)
            << "Initial level: "
                   + QString::number(tank.initial_level_m)
            << "Minimum level: "
                   + QString::number(tank.minimum_level_m)
            << "Maximum level: "
                   + QString::number(tank.maximum_level_m)
            << "Diameter: "
                   + QString::number(diameter_m)
            << "Minimum volume: "
                   + QString::number(tank.minimum_volume_m3);
        
        if (!tank.volume_curve_id.isEmpty())
        {
            status.details
                << "Volume curve: "
                       + tank.volume_curve_id;
        }
        
        return status;
    }
    
    error = EN_setnodevalue(
        this->epanet_project,
        tank_index,
        EN_CANOVERFLOW,
        tank.can_overflow ? 1.0 : 0.0
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::AddTank;
        status.operation = EpanetOperation::EN_setnodevalue;
        status.entity.type = EpanetEntityType::Tank;
        status.entity.id = tank.id;
        status.entity.index = tank_index;
        status.message = "Failed to set tank overflow option";
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.message_epanet = getEpanetErrorMessage(error);
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
        status.message_epanet = getEpanetErrorMessage(error);
        status.details << "From node: " + pipe.node_id_from;
        status.details << "To node: " + pipe.node_id_to;
        return status;
    }
    
    EpanetStatus status;
    status.success = true;
    return status;
}
