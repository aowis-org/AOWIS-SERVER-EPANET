#include "epanet_network_builder.h"

#include "epanet_index_registry.h"
#include "epanet_network_builder_support.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_resolvers.h>

#include <QByteArray>
#include <QList>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkPipe(const HydraulicLinkPipe &pipe)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(pipe.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Could not resolve pipe start-node UUID"));

    const QString node_id_to_string = this->node_ids_by_uuid.value(pipe.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Could not resolve pipe end-node UUID"));

    const QByteArray pipe_id = pipe.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    const int pipe_type = pipe.initial_status == HydraulicLinkPipeInitialStatus::CheckValve ? EN_CVPIPE : EN_PIPE;
    int pipe_index = 0;

    int error = EN_addlink(this->project.handle(), pipe_id.constData(), pipe_type, node_id_from.constData(), node_id_to.constData(), &pipe_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to add pipe"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::setLinkVertices(this->project, pipe_index, pipe.vertices, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("pipe"));
    if (!status.success)
        return status;

    const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
    // Static network construction is intentionally independent of the requested
    // headloss formula. The actual formula-specific roughness is applied by
    // configureEpanetHydraulicRun() before hydraulics or INP export.
    constexpr double construction_roughness = 1.0;
    error = EN_setpipedata(this->project.handle(), pipe_index, length_m, pipe.diameter_mm, construction_roughness, pipe.minor_loss_coefficient);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_setpipedata"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!std::isfinite(pipe.leak_area_mm2_per_100m) || pipe.leak_area_mm2_per_100m < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe fixed leakage area must be finite and non-negative"));
    error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_LEAK_AREA, pipe.leak_area_mm2_per_100m);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LEAK_AREA)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe fixed leakage area"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!std::isfinite(pipe.leak_area_expansion_per_pressure_head_mm2_per_m) || pipe.leak_area_expansion_per_pressure_head_mm2_per_m < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe leakage expansion coefficient must be finite and non-negative"));
    error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_LEAK_EXPAN, pipe.leak_area_expansion_per_pressure_head_mm2_per_m);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LEAK_EXPAN)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe leakage expansion coefficient"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (pipe.initial_status != HydraulicLinkPipeInitialStatus::CheckValve)
    {
        const double initial_status = pipe.initial_status == HydraulicLinkPipeInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), pipe_index, EN_INITSTATUS, initial_status);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPipe, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to set pipe status"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_pipes.insert(pipe.uuid, pipe_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkPump(const HydraulicLinkPump &pump)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(pump.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump start-node UUID"));

    const QString node_id_to_string = this->node_ids_by_uuid.value(pump.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump end-node UUID"));

    const QByteArray pump_id = pump.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    int pump_index = 0;
    int error = EN_addlink(this->project.handle(), pump_id.constData(), EN_PUMP, node_id_from.constData(), node_id_to.constData(), &pump_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to add pump"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::setLinkVertices(this->project, pump_index, pump.vertices, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("pump"));
    if (!status.success)
        return status;

    if (pump.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower)
    {
        if (pump.constant_power_kw <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Constant-power pump requires positive power"));

        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_POWER, pump.constant_power_kw);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_POWER)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump constant power"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else
    {
        if (pump.head_curve_uuid.isNull())
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Curve-based pump has no head curve UUID"));

        const int point_count = this->pump_head_curve_point_counts_by_uuid.value(pump.head_curve_uuid, 0);
        if (point_count == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump head curve UUID"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::OnePointCurve && point_count != 1)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("One-point pump definition requires exactly one head-curve point"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::ThreePointCurve && point_count != 3)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Three-point pump definition requires exactly three head-curve points"));
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::MultiPointCurve && (point_count == 1 || point_count == 3))
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Multi-point pump definition requires two or at least four head-curve points"));

        const int head_curve_index = this->indices.curves_pump_head.value(pump.head_curve_uuid, 0);
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_HCURVE, static_cast<double>(head_curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_HCURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump head curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.initial_speed_ratio < 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump initial speed cannot be negative"));
    error = EN_setlinkvalue(this->project.handle(), pump_index, EN_INITSETTING, pump.initial_speed_ratio);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSETTING)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump initial speed"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const double initial_status = pump.initial_status == HydraulicLinkPumpInitialStatus::On ? EN_OPEN : EN_CLOSED;
    error = EN_setlinkvalue(this->project.handle(), pump_index, EN_INITSTATUS, initial_status);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSTATUS)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump initial status"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (!pump.speed_pattern_uuid.isNull())
    {
        const int speed_pattern_index = this->indices.patterns_time.value(pump.speed_pattern_uuid, 0);
        if (speed_pattern_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump speed pattern UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_LINKPATTERN, static_cast<double>(speed_pattern_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_LINKPATTERN)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump speed pattern"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Constant)
    {
        if (pump.constant_efficiency_percent <= 0.0 || pump.constant_efficiency_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump constant efficiency must be in (0, 100] percent"));

        const QString curve_id = QStringLiteral("__aowis_eff_") + pump.uuid.toString(QUuid::Id128).left(19);
        QList<double> flows = {0.0};
        QList<double> efficiencies = {pump.constant_efficiency_percent};
        QHash<QUuid, int> synthetic_indices;
        status = EpanetNetworkBuilderSupport::addCurveData(this->project, curve_id, pump.uuid, QString(), flows, efficiencies, EN_EFFIC_CURVE, synthetic_indices, QStringLiteral("constant pump efficiency curve"));
        if (!status.success)
            return status;
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECURVE, static_cast<double>(synthetic_indices.value(pump.uuid)));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump constant efficiency"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve)
    {
        const int efficiency_curve_index = this->indices.curves_pump_efficiency.value(pump.efficiency_curve_uuid, 0);
        if (efficiency_curve_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump efficiency curve UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECURVE, static_cast<double>(efficiency_curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECURVE)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump efficiency curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
    {
        if (pump.energy_price_per_kw_h < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump energy price cannot be negative"));
        if (pump.energy_price_per_kw_h == 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("EPANET cannot represent a pump-specific zero energy price; zero selects the global price"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_ECOST, pump.energy_price_per_kw_h);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_ECOST)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump energy price"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
    {
        const int price_pattern_index = this->indices.patterns_time.value(pump.price_pattern_uuid, 0);
        if (price_pattern_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Could not resolve pump energy-price pattern UUID"));
        error = EN_setlinkvalue(this->project.handle(), pump_index, EN_PUMP_EPAT, static_cast<double>(price_pattern_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPump, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PUMP_EPAT)"), HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to set pump energy-price pattern"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_pumps.insert(pump.uuid, pump_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addLinkValve(const HydraulicLinkValve &valve)
{
    const QString node_id_from_string = this->node_ids_by_uuid.value(valve.node_uuid_from);
    if (node_id_from_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve valve start-node UUID"));
    const QString node_id_to_string = this->node_ids_by_uuid.value(valve.node_uuid_to);
    if (node_id_to_string.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve valve end-node UUID"));

    int backend_type = 0;
    if (!EpanetNetworkBuilderSupport::resolveValveType(valve.type, backend_type))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::AddLink, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Unsupported valve type"));

    const QByteArray valve_id = valve.id.toUtf8();
    const QByteArray node_id_from = node_id_from_string.toUtf8();
    const QByteArray node_id_to = node_id_to_string.toUtf8();
    int valve_index = 0;
    int error = EN_addlink(this->project.handle(), valve_id.constData(), backend_type, node_id_from.constData(), node_id_to.constData(), &valve_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::AddLink, QStringLiteral("EN_addlink"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to add valve"));
        if (!epanet_status.success)
            return epanet_status;
    }

    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::setLinkVertices(this->project, valve_index, valve.vertices, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("valve"));
    if (!status.success)
        return status;

    error = EN_setlinkvalue(this->project.handle(), valve_index, EN_DIAMETER, valve.diameter_mm);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityGeometry, QStringLiteral("EN_setlinkvalue(EN_DIAMETER)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve diameter"));
        if (!epanet_status.success)
            return epanet_status;
    }
    error = EN_setlinkvalue(this->project.handle(), valve_index, EN_MINORLOSS, valve.minor_loss_coefficient);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_MINORLOSS)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve minor-loss coefficient"));
        if (!epanet_status.success)
            return epanet_status;
    }

    if (valve.type == HydraulicLinkValveType::GPV)
    {
        const int curve_index = this->indices.curves_valve_headloss.value(valve.head_loss_curve_uuid, 0);
        if (curve_index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve GPV head-loss curve UUID"));
        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_GPV_CURVE, static_cast<double>(curve_index));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_GPV_CURVE)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set GPV head-loss curve"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }
    else
    {
        double initial_setting = 0.0;
        switch (valve.type)
        {
        case HydraulicLinkValveType::PRV:
        case HydraulicLinkValveType::PSV:
        case HydraulicLinkValveType::PBV:
            initial_setting = valve.setting_pressure_head_m;
            break;
        case HydraulicLinkValveType::FCV:
            initial_setting = valve.setting_flow_m3_per_h;
            break;
        case HydraulicLinkValveType::TCV:
            initial_setting = valve.setting_loss_coefficient;
            break;
        case HydraulicLinkValveType::PCV:
            initial_setting = valve.setting_position_percent;
            if (initial_setting < 0.0 || initial_setting > 100.0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("PCV position must be in [0, 100] percent"));
            break;
        case HydraulicLinkValveType::GPV:
            break;
        }

        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_INITSETTING, initial_setting);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSETTING)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve setting"));
            if (!epanet_status.success)
                return epanet_status;
        }

        if (valve.type == HydraulicLinkValveType::PCV && !valve.characteristic_curve_uuid.isNull())
        {
            const int curve_index = this->indices.curves_valve_characteristic.value(valve.characteristic_curve_uuid, 0);
            if (curve_index == 0)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Could not resolve PCV characteristic curve UUID"));
            error = EN_setlinkvalue(this->project.handle(), valve_index, EN_PCV_CURVE, static_cast<double>(curve_index));
            if (error != 0)
            {
                const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_PCV_CURVE)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set PCV characteristic curve"));
                if (!epanet_status.success)
                    return epanet_status;
            }
        }
    }

    if (valve.initial_status != HydraulicLinkValveInitialStatus::Active)
    {
        const double initial_status = valve.initial_status == HydraulicLinkValveInitialStatus::Open ? EN_OPEN : EN_CLOSED;
        error = EN_setlinkvalue(this->project.handle(), valve_index, EN_INITSTATUS, initial_status);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddValve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setlinkvalue(EN_INITSTATUS)"), HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to set valve initial status"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    this->indices.links_valves.insert(valve.uuid, valve_index);
    return makeEpanetSuccess();
}
