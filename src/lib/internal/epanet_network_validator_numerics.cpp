#include "epanet_network_validator_parts.h"
#include "epanet_network_validator_support.h"
#include "epanet_status_helpers.h"

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cmath>
#include <functional>
#include <optional>

namespace EpanetNetworkValidatorParts
{
void collectReportThresholdFailures(
    QList<HydraulicSimulationStatus> &failures,
    const std::optional<double> &threshold,
    const QString &field_name,
    const NetworkHydraulic &network)
{
    if (!threshold.has_value() || std::isfinite(threshold.value()))
        return;

    failures.append(EpanetNetworkValidatorSupport::invalidNumeric(
        HydraulicSimulationStatusEntityType::Report,
        network.id,
        network.uuid,
        field_name,
        QStringLiteral("must be finite when configured")));
}

QList<HydraulicSimulationStatus> validateNumerics(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    HydraulicSimulationStatus status;


    const WaterQualityReactionOptions &reactions = network.options_reaction;
    status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(reactions.global_pipe_bulk_reaction.order, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_bulk_reaction.order"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(reactions.global_tank_bulk_reaction.order, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_tank_bulk_reaction.order"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = EpanetNetworkValidatorSupport::validateFinite(reactions.global_pipe_bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_bulk_reaction.coefficient"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = EpanetNetworkValidatorSupport::validateFinite(reactions.global_tank_bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_tank_bulk_reaction.coefficient"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = EpanetNetworkValidatorSupport::validateFinite(reactions.global_pipe_wall_reaction.coefficient, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_wall_reaction.coefficient"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    if (!std::isfinite(reactions.global_pipe_wall_reaction.order) || (reactions.global_pipe_wall_reaction.order != 0.0 && reactions.global_pipe_wall_reaction.order != 1.0))
    {
        failures.append(EpanetNetworkValidatorSupport::invalidNumeric(HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.global_pipe_wall_reaction.order"), QStringLiteral("must be either 0 or 1 for EPANET")));
    }
    status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(reactions.limiting_concentration_mg_per_l, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.limiting_concentration_mg_per_l"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = EpanetNetworkValidatorSupport::validateFinite(reactions.roughness_reaction_factor, HydraulicSimulationStatusEntityType::QualitySolver, network.id, network.uuid, QStringLiteral("options_reaction.roughness_reaction_factor"));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

    const std::function<void(const HydraulicNodeQualitySource &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> validate_quality_source = [&failures](const HydraulicNodeQualitySource &source, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        HydraulicSimulationStatus source_status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(source.chemical_concentration_mg_per_l, entity_type, id, uuid, QStringLiteral("quality_source.chemical_concentration_mg_per_l"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, source_status);
        source_status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(source.chemical_mass_flow_mg_per_min, entity_type, id, uuid, QStringLiteral("quality_source.chemical_mass_flow_mg_per_min"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, source_status);
    };

    const HydraulicSolverOptions &hydraulic = network.options_hydraulic;
    const QList<QPair<QString, double>> hydraulic_values = {
        {QStringLiteral("minimum_pressure_head_m"), hydraulic.minimum_pressure_head_m},
        {QStringLiteral("required_pressure_head_m"), hydraulic.required_pressure_head_m},
        {QStringLiteral("pressure_exponent"), hydraulic.pressure_exponent},
        {QStringLiteral("accuracy"), hydraulic.accuracy},
        {QStringLiteral("damping_limit"), hydraulic.damping_limit},
        {QStringLiteral("maximum_head_error_m"), hydraulic.maximum_head_error_m},
        {QStringLiteral("maximum_flow_change_m3_per_h"), hydraulic.maximum_flow_change_m3_per_h},
        {QStringLiteral("demand_multiplier"), hydraulic.demand_multiplier},
        {QStringLiteral("specific_gravity"), hydraulic.specific_gravity},
        {QStringLiteral("relative_viscosity"), hydraulic.relative_viscosity}
    };
    for (const QPair<QString, double> &field : hydraulic_values)
    {
        status = EpanetNetworkValidatorSupport::validateFinite(field.second, HydraulicSimulationStatusEntityType::HydraulicSolver, network.id, network.uuid, field.first);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    const PumpEnergyOptions &energy = network.options_energy;
    const QList<QPair<QString, double>> energy_values = {
        {QStringLiteral("global_pump_efficiency_percent"), energy.global_pump_efficiency_percent},
        {QStringLiteral("global_energy_price_per_kw_h"), energy.global_energy_price_per_kw_h},
        {QStringLiteral("demand_charge_per_kw"), energy.demand_charge_per_kw}
    };
    for (const QPair<QString, double> &field : energy_values)
    {
        status = EpanetNetworkValidatorSupport::validateFinite(field.second, HydraulicSimulationStatusEntityType::HydraulicSolver, network.id, network.uuid, field.first);
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    struct NamedReportThreshold
    {
        const char *name;
        const std::optional<double> *threshold;
    };

    const std::array<NamedReportThreshold, 20> report_thresholds = {{
        {"fields_node.elevation.below_m", &network.options_report.fields_node.elevation.below_m},
        {"fields_node.elevation.above_m", &network.options_report.fields_node.elevation.above_m},
        {"fields_node.demand.below_m3_per_h", &network.options_report.fields_node.demand.below_m3_per_h},
        {"fields_node.demand.above_m3_per_h", &network.options_report.fields_node.demand.above_m3_per_h},
        {"fields_node.head.below_m", &network.options_report.fields_node.head.below_m},
        {"fields_node.head.above_m", &network.options_report.fields_node.head.above_m},
        {"fields_node.pressure.below_m", &network.options_report.fields_node.pressure.below_m},
        {"fields_node.pressure.above_m", &network.options_report.fields_node.pressure.above_m},
        {"fields_link.length.below_m", &network.options_report.fields_link.length.below_m},
        {"fields_link.length.above_m", &network.options_report.fields_link.length.above_m},
        {"fields_link.diameter.below_mm", &network.options_report.fields_link.diameter.below_mm},
        {"fields_link.diameter.above_mm", &network.options_report.fields_link.diameter.above_mm},
        {"fields_link.flow.below_m3_per_h", &network.options_report.fields_link.flow.below_m3_per_h},
        {"fields_link.flow.above_m3_per_h", &network.options_report.fields_link.flow.above_m3_per_h},
        {"fields_link.velocity.below_m_per_s", &network.options_report.fields_link.velocity.below_m_per_s},
        {"fields_link.velocity.above_m_per_s", &network.options_report.fields_link.velocity.above_m_per_s},
        {"fields_link.headloss.below_m_per_km", &network.options_report.fields_link.headloss.below_m_per_km},
        {"fields_link.headloss.above_m_per_km", &network.options_report.fields_link.headloss.above_m_per_km},
        {"fields_link.friction.below_friction_factor", &network.options_report.fields_link.friction.below_friction_factor},
        {"fields_link.friction.above_friction_factor", &network.options_report.fields_link.friction.above_friction_factor}
    }};

    for (const NamedReportThreshold &report_threshold : report_thresholds)
    {
        collectReportThresholdFailures(
            failures,
            *report_threshold.threshold,
            QString::fromLatin1(report_threshold.name),
            network);
    }

    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        for (int index = 0; index < pattern.multipliers.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(pattern.multipliers.at(index), HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("multipliers[%1]").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).water_level_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].water_level_m").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).volume_m3, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].volume_m3").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).head_gain_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_gain_m").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).efficiency_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].efficiency_percent").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).flow_m3_per_h, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].flow_m3_per_h").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).head_loss_m, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].head_loss_m").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).position_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].position_percent").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).relative_flow_percent, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].relative_flow_percent").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicCurveGeneric &curve : network.curves_generic)
    {
        for (int index = 0; index < curve.points.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).x, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].x").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(curve.points.at(index).y, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("points[%1].y").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicMapLabel &label : network.map_labels)
    {
        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(label.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map_labels.coordinate_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(label.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("map_labels.coordinate_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    if (network.map_backdrop.enabled)
    {
        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(network.map_backdrop.lower_left_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.lower_left_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(network.map_backdrop.lower_left_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.lower_left_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(network.map_backdrop.upper_right_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.upper_right_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(network.map_backdrop.upper_right_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.upper_right_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinite(network.map_backdrop.offset_longitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.offset_longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinite(network.map_backdrop.offset_latitude_deg, HydraulicSimulationStatusEntityType::Network, network.id, network.uuid, QStringLiteral("map_backdrop.offset_latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (!junction.metadata.enabled)
            continue;
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(junction.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(junction.initial_water_age_h, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("initial_water_age_h"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        validate_quality_source(junction.quality_source, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid);
        if (junction.elevation_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(junction.terrain_elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("terrain_elevation_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(junction.elevation_offset_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_offset_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        else
        {
            status = EpanetNetworkValidatorSupport::validateFinite(junction.elevation_m, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("elevation_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(junction.emitter.coefficient, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("emitter.coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinitePositive(junction.emitter.pressure_exponent, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("emitter.pressure_exponent"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(junction.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(junction.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        for (int index = 0; index < junction.demands.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(junction.demands.at(index).base_demand_m3_per_h, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("demands[%1].base_demand_m3_per_h").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    std::optional<double> active_emitter_pressure_exponent;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (!junction.metadata.enabled || junction.emitter.coefficient <= 0.0 || !std::isfinite(junction.emitter.pressure_exponent))
            continue;

        if (!active_emitter_pressure_exponent.has_value())
        {
            active_emitter_pressure_exponent = junction.emitter.pressure_exponent;
            continue;
        }

        if (std::abs(active_emitter_pressure_exponent.value() - junction.emitter.pressure_exponent) > 1.0e-12)
        {
            failures.append(EpanetNetworkValidatorSupport::validationStatus(
                HydraulicSimulationStatusOperation::ConfigureHydraulics,
                HydraulicSimulationStatusEntityType::Junction,
                junction.id,
                junction.uuid,
                QStringLiteral("Enabled junction emitters must use one common pressure exponent because EPANET exposes a network-wide emitter exponent"),
                {QStringLiteral("emitter.pressure_exponent: %1; expected %2")
                    .arg(junction.emitter.pressure_exponent, 0, 'g', 17)
                    .arg(active_emitter_pressure_exponent.value(), 0, 'g', 17)}));
        }
    }

    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (!reservoir.metadata.enabled)
            continue;
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(reservoir.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(reservoir.initial_water_age_h, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("initial_water_age_h"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        validate_quality_source(reservoir.quality_source, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid);
        if (reservoir.head_input_type == HydraulicNodeElevationInputType::TerrainElevationAndOffset)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(reservoir.terrain_elevation_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("terrain_elevation_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(reservoir.hydraulic_head_offset_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("hydraulic_head_offset_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        else
        {
            status = EpanetNetworkValidatorSupport::validateFinite(reservoir.hydraulic_head_m, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("hydraulic_head_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(reservoir.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(reservoir.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (!tank.metadata.enabled)
            continue;
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(tank.initial_chemical_concentration_mg_per_l, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("initial_chemical_concentration_mg_per_l"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(tank.initial_water_age_h, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("initial_water_age_h"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        validate_quality_source(tank.quality_source, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid);
        if (!std::isfinite(tank.mixing_fraction) || tank.mixing_fraction < 0.0 || tank.mixing_fraction > 1.0)
            failures.append(EpanetNetworkValidatorSupport::invalidNumeric(HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("mixing_fraction"), QStringLiteral("must be finite and between 0 and 1")));
        status = EpanetNetworkValidatorSupport::validateFinite(tank.bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bulk_reaction.coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(tank.bulk_reaction.order, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bulk_reaction.order"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        if (tank.override_bulk_reaction && std::isfinite(tank.bulk_reaction.order) && std::abs(tank.bulk_reaction.order - reactions.global_tank_bulk_reaction.order) > 1.0e-12)
            failures.append(EpanetNetworkValidatorSupport::validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Tank bulk reaction order must match the network-wide EPANET tank reaction order")));

        if (tank.elevation_input_type == HydraulicNodeTankElevationInputType::TerrainElevationAndOffset)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(tank.terrain_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("terrain_elevation_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(tank.bottom_offset_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_offset_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        else
        {
            status = EpanetNetworkValidatorSupport::validateFinite(tank.bottom_elevation_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("bottom_elevation_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }

        status = EpanetNetworkValidatorSupport::validateFinite(tank.water_level_initial_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_initial_m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinite(tank.water_level_minimum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_minimum_m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinite(tank.water_level_maximum_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("water_level_maximum_m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(tank.minimum_volume_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("minimum_volume_m3"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::Cylindrical)
            status = EpanetNetworkValidatorSupport::validateFinitePositive(tank.diameter_m, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("diameter_m"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::UniformArea)
            status = EpanetNetworkValidatorSupport::validateFinitePositive(tank.cross_section_area_m2, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("cross_section_area_m2"));
        else if (tank.geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel)
            status = EpanetNetworkValidatorSupport::validateFinitePositive(tank.volume_at_maximum_level_m3, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("volume_at_maximum_level_m3"));
        else
            status = makeEpanetSuccess();
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        status = EpanetNetworkValidatorSupport::validateLongitudeDeg(tank.coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.longitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateLatitudeDeg(tank.coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("coordinate_wgs84.latitude_deg"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    }

    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (!pipe.metadata.enabled)
            continue;
        status = EpanetNetworkValidatorSupport::validateFinite(pipe.bulk_reaction.coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("bulk_reaction.coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(pipe.bulk_reaction.order, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("bulk_reaction.order"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinite(pipe.wall_reaction.coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("wall_reaction.coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        if (!std::isfinite(pipe.wall_reaction.order) || (pipe.wall_reaction.order != 0.0 && pipe.wall_reaction.order != 1.0))
            failures.append(EpanetNetworkValidatorSupport::invalidNumeric(HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("wall_reaction.order"), QStringLiteral("must be either 0 or 1 for EPANET")));
        if (pipe.override_reactions && std::isfinite(pipe.bulk_reaction.order) && std::abs(pipe.bulk_reaction.order - reactions.global_pipe_bulk_reaction.order) > 1.0e-12)
            failures.append(EpanetNetworkValidatorSupport::validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe bulk reaction order must match the network-wide EPANET bulk reaction order")));
        if (pipe.override_reactions && std::isfinite(pipe.wall_reaction.order) && std::abs(pipe.wall_reaction.order - reactions.global_pipe_wall_reaction.order) > 1.0e-12)
            failures.append(EpanetNetworkValidatorSupport::validationStatus(HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe wall reaction order must match the network-wide EPANET wall reaction order")));
        const double length_m = pipe.length_measured_m.value_or(pipe.length_calculated_m);
        status = EpanetNetworkValidatorSupport::validateFinitePositive(length_m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("length_m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFinitePositive(pipe.diameter_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("diameter_mm"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::HazenWilliams)
            status = EpanetNetworkValidatorSupport::validateFinite(pipe.roughness_hazen_williams, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_hazen_williams"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach)
            status = EpanetNetworkValidatorSupport::validateFinite(pipe.roughness_darcy_weisbach_mm, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_darcy_weisbach_mm"));
        else if (network.options_hydraulic.headloss_formula == HydraulicHeadlossFormula::ChezyManning)
            status = EpanetNetworkValidatorSupport::validateFinite(pipe.roughness_chezy_manning, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("roughness_chezy_manning"));
        else
            status = makeEpanetSuccess();
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(pipe.minor_loss_coefficient, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("minor_loss_coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(pipe.leak_area_mm2_per_100m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_area_mm2_per_100m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(pipe.leak_area_expansion_per_pressure_head_mm2_per_m, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("leak_area_expansion_per_pressure_head_mm2_per_m"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        for (int index = 0; index < pipe.vertices.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateLongitudeDeg(pipe.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateLatitudeDeg(pipe.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (!pump.metadata.enabled)
            continue;
        if (pump.definition_type == HydraulicLinkPumpDefinitionType::ConstantPower)
        {
            status = EpanetNetworkValidatorSupport::validateFinitePositive(pump.constant_power_kw, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_power_kw"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(pump.initial_speed_ratio, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("initial_speed_ratio"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        if (pump.efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Constant)
        {
            status = EpanetNetworkValidatorSupport::validateFinitePositive(pump.constant_efficiency_percent, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("constant_efficiency_percent"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(pump.energy_price_per_kw_h, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("energy_price_per_kw_h"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
        for (int index = 0; index < pump.vertices.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateLongitudeDeg(pump.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateLatitudeDeg(pump.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (!valve.metadata.enabled)
            continue;
        status = EpanetNetworkValidatorSupport::validateFinitePositive(valve.diameter_mm, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("diameter_mm"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        status = EpanetNetworkValidatorSupport::validateFiniteNonNegative(valve.minor_loss_coefficient, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("minor_loss_coefficient"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        switch (valve.type)
        {
        case HydraulicLinkValveType::PRV:
        case HydraulicLinkValveType::PSV:
        case HydraulicLinkValveType::PBV:
            status = EpanetNetworkValidatorSupport::validateFinite(valve.setting_pressure_head_m, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_pressure_head_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::FCV:
            status = EpanetNetworkValidatorSupport::validateFinite(valve.setting_flow_m3_per_h, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_flow_m3_per_h"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::TCV:
            status = EpanetNetworkValidatorSupport::validateFinite(valve.setting_loss_coefficient, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_loss_coefficient"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::PCV:
            status = EpanetNetworkValidatorSupport::validateFinite(valve.setting_position_percent, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("setting_position_percent"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            break;
        case HydraulicLinkValveType::GPV:
            break;
        }
        for (int index = 0; index < valve.vertices.size(); index++)
        {
            status = EpanetNetworkValidatorSupport::validateLongitudeDeg(valve.vertices.at(index).coordinate_wgs84.longitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].longitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateLatitudeDeg(valve.vertices.at(index).coordinate_wgs84.latitude_deg, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("vertices[%1].latitude_deg").arg(index));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    const std::function<void(const HydraulicControlLinkSetting &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &, const QString &)> validate_control_setting = [&failures](const HydraulicControlLinkSetting &setting, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid, const QString &prefix)
    {
        const QList<QPair<QString, std::optional<double>>> values = {
            {QStringLiteral("pump_speed_ratio"), setting.pump_speed_ratio},
            {QStringLiteral("valve_pressure_head_m"), setting.valve_pressure_head_m},
            {QStringLiteral("valve_flow_m3_per_h"), setting.valve_flow_m3_per_h},
            {QStringLiteral("valve_loss_coefficient"), setting.valve_loss_coefficient},
            {QStringLiteral("valve_position_percent"), setting.valve_position_percent}
        };
        for (const QPair<QString, std::optional<double>> &entry : values)
        {
            if (!entry.second.has_value())
                continue;
            HydraulicSimulationStatus setting_status = EpanetNetworkValidatorSupport::validateFinite(entry.second.value(), entity_type, id, uuid, prefix + entry.first);
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, setting_status);
        }
    };

    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        if (control.action == HydraulicControlActionType::Setting)
            validate_control_setting(control.setting, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("setting."));
        if (control.type == HydraulicControlSimpleType::LowLevel || control.type == HydraulicControlSimpleType::HighLevel)
        {
            status = EpanetNetworkValidatorSupport::validateFinite(control.trigger_water_level_m, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger_water_level_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            status = EpanetNetworkValidatorSupport::validateFinite(control.trigger_pressure_head_m, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("trigger_pressure_head_m"));
            EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
        }
    }

    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        status = EpanetNetworkValidatorSupport::validateFinite(rule.priority, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("priority"));
        EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

        for (int index = 0; index < rule.premises.size(); index++)
        {
            const HydraulicControlRulePremise &premise = rule.premises.at(index);
            const QList<QPair<QString, std::optional<double>>> values = {
                {QStringLiteral("demand_m3_per_h"), premise.demand_m3_per_h},
                {QStringLiteral("hydraulic_head_m"), premise.hydraulic_head_m},
                {QStringLiteral("water_level_m"), premise.water_level_m},
                {QStringLiteral("pressure_head_m"), premise.pressure_head_m},
                {QStringLiteral("flow_m3_per_h"), premise.flow_m3_per_h},
                {QStringLiteral("power_kw"), premise.power_kw}
            };
            for (const QPair<QString, std::optional<double>> &entry : values)
            {
                if (!entry.second.has_value())
                    continue;
                status = EpanetNetworkValidatorSupport::validateFinite(entry.second.value(), HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premises[%1].").arg(index) + entry.first);
                EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
            }
            validate_control_setting(premise.link_setting, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("premises[%1].link_setting.").arg(index));
        }

        const QList<QList<HydraulicControlRuleAction>> action_groups = {rule.actions_then, rule.actions_else};
        for (const QList<HydraulicControlRuleAction> &actions : action_groups)
        {
            for (const HydraulicControlRuleAction &action : actions)
                validate_control_setting(action.setting, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("action.setting."));
        }
    }

    return failures;
}
}
