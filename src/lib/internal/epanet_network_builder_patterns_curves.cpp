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

HydraulicSimulationStatus EpanetNetworkBuilder::addPatternTime(const HydraulicPatternTime &pattern)
{
    if (pattern.id.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Pattern, QString(), pattern.uuid, QStringLiteral("Time pattern has no ID"));

    if (pattern.multipliers.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Time pattern requires at least one factor"));

    const QByteArray pattern_id = pattern.id.toUtf8();
    int error = EN_addpattern(this->project.handle(), pattern_id.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_addpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to add time pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int pattern_index = 0;
    error = EN_getpatternindex(this->project.handle(), pattern_id.constData(), &pattern_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to get time pattern index"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QList<double> multipliers = pattern.multipliers;
    error = EN_setpattern(this->project.handle(), pattern_index, multipliers.data(), multipliers.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("Failed to set time pattern multipliers"));
        if (!status.success)
        {
            status.entity.index = pattern_index;
            return status;
        }
    }

    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::setObjectComment(this->project, EN_TIMEPAT, pattern_index, pattern.comment, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("time pattern"));
    if (!status.success)
        return status;

    this->indices.patterns_time.insert(pattern.uuid, pattern_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureConstantDemandPattern(const NetworkHydraulic &request)
{
    bool needs_constant_pattern = false;
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        for (const HydraulicNodeJunctionDemand &demand : junction.demands)
        {
            if (demand.pattern_mode == HydraulicTimePatternMode::Constant)
            {
                needs_constant_pattern = true;
                break;
            }
        }
        if (needs_constant_pattern)
            break;
    }

    if (!needs_constant_pattern)
        return makeEpanetSuccess();

    QString pattern_id = QStringLiteral("__AOWIS_CONSTANT");
    int suffix = 1;
    bool id_in_use = true;
    while (id_in_use)
    {
        id_in_use = false;
        for (const HydraulicPatternTime &pattern : request.patterns_time)
        {
            if (pattern.id == pattern_id)
            {
                id_in_use = true;
                pattern_id = QStringLiteral("__AOWIS_CONSTANT_%1").arg(suffix++);
                break;
            }
        }
    }

    const QByteArray pattern_id_utf8 = pattern_id.toUtf8();
    int error = EN_addpattern(this->project.handle(), pattern_id_utf8.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_addpattern"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to add internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int pattern_index = 0;
    error = EN_getpatternindex(this->project.handle(), pattern_id_utf8.constData(), &pattern_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to resolve internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setpatternvalue(this->project.handle(), pattern_index, 1, 1.0);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::AddPattern, QStringLiteral("EN_setpatternvalue"), HydraulicSimulationStatusEntityType::Pattern, pattern_id, QUuid(), QStringLiteral("Failed to set internal constant-demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    this->constant_demand_pattern_id = pattern_id;
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureDefaultDemandPattern(const NetworkHydraulic &request)
{
    const QUuid pattern_uuid = request.options_hydraulic.default_demand_pattern_uuid;
    if (pattern_uuid.isNull())
        return makeEpanetSuccess();

    int pattern_index = 0;
    if (!EpanetNetworkBuilderSupport::resolveBackendIndex(this->indices.patterns_time, pattern_uuid, pattern_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Could not resolve default demand pattern UUID"));

    const int error = EN_setoption(this->project.handle(), EN_DEMANDPATTERN, static_cast<double>(pattern_index));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_DEMANDPATTERN)"), HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Failed to configure the default demand pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::configureGlobalEnergyPattern(const NetworkHydraulic &request)
{
    const QUuid pattern_uuid = request.options_energy.global_energy_price_pattern_uuid;
    if (pattern_uuid.isNull())
        return makeEpanetSuccess();

    int pattern_index = 0;
    if (!EpanetNetworkBuilderSupport::resolveBackendIndex(this->indices.patterns_time, pattern_uuid, pattern_index))
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Could not resolve global energy-price pattern UUID"));

    const int error = EN_setoption(this->project.handle(), EN_GLOBALPATTERN, static_cast<double>(pattern_index));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddPattern, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_GLOBALPATTERN)"), HydraulicSimulationStatusEntityType::Pattern, this->pattern_ids_by_uuid.value(pattern_uuid), pattern_uuid, QStringLiteral("Failed to configure the global energy-price pattern"));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveTankVolume(const HydraulicCurveTankVolume &curve)
{
    if (curve.id.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, QString(), curve.uuid, QStringLiteral("Tank volume curve has no ID"));

    if (curve.points.length() < 2)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve requires at least two points"));

    const QByteArray curve_id = curve.id.toUtf8();
    int error = EN_addcurve(this->project.handle(), curve_id.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_addcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to add tank volume curve"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int curve_index = 0;
    error = EN_getcurveindex(this->project.handle(), curve_id.constData(), &curve_index);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::ResolveEntity, QStringLiteral("EN_getcurveindex"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to get tank volume curve index"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QList<double> levels_m;
    QList<double> volumes_m3;
    levels_m.reserve(curve.points.length());
    volumes_m3.reserve(curve.points.length());

    for (int index = 0; index < curve.points.length(); index++)
    {
        const HydraulicCurveTankVolumePoint &point = curve.points.at(index);
        if (index > 0)
        {
            const HydraulicCurveTankVolumePoint &previous_point = curve.points.at(index - 1);
            if (point.water_level_m <= previous_point.water_level_m)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve levels must increase"));
            if (point.volume_m3 <= previous_point.volume_m3)
                return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Tank volume curve volumes must increase"));
        }

        levels_m.append(point.water_level_m);
        volumes_m3.append(point.volume_m3);
    }

    error = EN_setcurve(this->project.handle(), curve_index, levels_m.data(), volumes_m3.data(), levels_m.length());
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::AddCurve, QStringLiteral("EN_setcurve"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to set tank volume curve data"));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    error = EN_setcurvetype(this->project.handle(), curve_index, EN_VOLUME_CURVE);
    if (error != 0)
    {
        HydraulicSimulationStatus status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_setcurvetype"), HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Failed to set tank volume curve type"));
        if (!status.success)
        {
            status.entity.index = curve_index;
            return status;
        }
    }

    HydraulicSimulationStatus status = EpanetNetworkBuilderSupport::setObjectComment(this->project, EN_CURVE, curve_index, curve.comment, HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("tank volume curve"));
    if (!status.success)
        return status;

    this->indices.curves_tank_volume.insert(curve.uuid, curve_index);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurvePumpHead(const HydraulicCurvePumpHead &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve requires at least one point"));

    QList<double> flows;
    QList<double> heads;
    flows.reserve(curve.points.size());
    heads.reserve(curve.points.size());
    if (curve.points.size() == 3 && curve.points.first().flow_m3_per_h != 0.0)
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Three-point pump curve must start at zero flow"));
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurvePumpHeadPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.head_gain_m <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve requires non-negative flows and positive heads"));
        if (curve.points.size() == 1 && point.flow_m3_per_h <= 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("One-point pump curve requires positive design flow"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump head curve flows must increase"));
        if (index > 0 && point.head_gain_m >= curve.points.at(index - 1).head_gain_m)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump curve heads must decrease"));
        flows.append(point.flow_m3_per_h);
        heads.append(point.head_gain_m);
    }

    return EpanetNetworkBuilderSupport::addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, heads, EN_PUMP_CURVE, this->indices.curves_pump_head, QStringLiteral("pump head curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurvePumpEfficiency(const HydraulicCurvePumpEfficiency &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve requires at least one point"));

    QList<double> flows;
    QList<double> efficiencies;
    flows.reserve(curve.points.size());
    efficiencies.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurvePumpEfficiencyPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.efficiency_percent <= 0.0 || point.efficiency_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve requires non-negative flows and efficiencies in (0, 100] percent"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Pump efficiency curve flows must increase"));
        flows.append(point.flow_m3_per_h);
        efficiencies.append(point.efficiency_percent);
    }

    return EpanetNetworkBuilderSupport::addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, efficiencies, EN_EFFIC_CURVE, this->indices.curves_pump_efficiency, QStringLiteral("pump efficiency curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveValveHeadloss(const HydraulicCurveValveHeadloss &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve requires at least one point"));

    QList<double> flows;
    QList<double> head_losses;
    flows.reserve(curve.points.size());
    head_losses.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveValveHeadlossPoint &point = curve.points.at(index);
        if (point.flow_m3_per_h < 0.0 || point.head_loss_m < 0.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve values cannot be negative"));
        if (index > 0 && point.flow_m3_per_h <= curve.points.at(index - 1).flow_m3_per_h)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve head-loss curve flows must increase"));
        flows.append(point.flow_m3_per_h);
        head_losses.append(point.head_loss_m);
    }

    return EpanetNetworkBuilderSupport::addCurveData(this->project, curve.id, curve.uuid, curve.comment, flows, head_losses, EN_HLOSS_CURVE, this->indices.curves_valve_headloss, QStringLiteral("valve head-loss curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveValveCharacteristic(const HydraulicCurveValveCharacteristic &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve requires at least one point"));

    QList<double> positions;
    QList<double> relative_flows;
    positions.reserve(curve.points.size());
    relative_flows.reserve(curve.points.size());
    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveValveCharacteristicPoint &point = curve.points.at(index);
        if (point.position_percent < 0.0 || point.position_percent > 100.0 || point.relative_flow_percent < 0.0 || point.relative_flow_percent > 100.0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve values must be in [0, 100] percent"));
        if (index > 0 && point.position_percent <= curve.points.at(index - 1).position_percent)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Valve characteristic curve positions must increase"));
        positions.append(point.position_percent);
        relative_flows.append(point.relative_flow_percent);
    }

    return EpanetNetworkBuilderSupport::addCurveData(this->project, curve.id, curve.uuid, curve.comment, positions, relative_flows, EN_VALVE_CURVE, this->indices.curves_valve_characteristic, QStringLiteral("valve characteristic curve"));
}

HydraulicSimulationStatus EpanetNetworkBuilder::addCurveGeneric(const HydraulicCurveGeneric &curve)
{
    if (curve.points.isEmpty())
        return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Generic curve requires at least one point"));

    QList<double> x_values;
    QList<double> y_values;
    x_values.reserve(curve.points.size());
    y_values.reserve(curve.points.size());

    for (int index = 0; index < curve.points.size(); index++)
    {
        const HydraulicCurveGenericPoint &point = curve.points.at(index);
        if (index > 0 && point.x <= curve.points.at(index - 1).x)
            return makeEpanetStatus(HydraulicSimulationStatusStage::AddCurve, HydraulicSimulationStatusOperation::None, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("Generic curve x values must increase"));
        x_values.append(point.x);
        y_values.append(point.y);
    }

    return EpanetNetworkBuilderSupport::addCurveData(this->project, curve.id, curve.uuid, curve.comment, x_values, y_values, EN_GENERIC_CURVE, this->indices.curves_generic, QStringLiteral("generic curve"));
}
