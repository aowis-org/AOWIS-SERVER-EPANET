#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/inp_import_scenarios.h"
#include "conformance/inp_roundtrip_proof.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/native_quality_reference_runner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr NumericTolerance numeric_tolerance{1.0e-10, 1.0e-10};
constexpr double epanet_cmh_per_cfs = 101.94;
constexpr double epanet_gpm_per_cfs = 448.831;
constexpr double epanet_gpm_to_cmh = epanet_cmh_per_cfs / epanet_gpm_per_cfs;

constexpr double wgs84_equatorial_radius_m = 6378137.0;

double greatCircleDistanceM(const CoordinateWGS84 &first, const CoordinateWGS84 &second)
{
    const double first_latitude = first.latitude_deg * 3.14159265358979323846 / 180.0;
    const double second_latitude = second.latitude_deg * 3.14159265358979323846 / 180.0;
    const double latitude_delta = second_latitude - first_latitude;
    const double longitude_delta =
        (second.longitude_deg - first.longitude_deg) * 3.14159265358979323846 / 180.0;
    const double sin_latitude = std::sin(latitude_delta / 2.0);
    const double sin_longitude = std::sin(longitude_delta / 2.0);
    const double a = sin_latitude * sin_latitude
        + std::cos(first_latitude) * std::cos(second_latitude)
            * sin_longitude * sin_longitude;
    return 2.0 * wgs84_equatorial_radius_m
        * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

bool hasDiagnosticContaining(const EpanetResultImport &result, const QString &text)
{
    for (const HydraulicSimulationDiagnostic &diagnostic : result.diagnostics)
    {
        if (diagnostic.message.contains(text, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

ComparisonContext comparison(std::string field)
{
    ComparisonContext value;
    value.field = std::move(field);
    return value;
}

const HydraulicNodeJunction *junctionById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const HydraulicNodeReservoir *reservoirById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

const HydraulicNodeTank *tankById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const HydraulicLinkPipe *pipeById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const HydraulicLinkPump *pumpById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.id == id)
            return &pump;
    }
    return nullptr;
}

const HydraulicLinkValve *valveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (valve.id == id)
            return &valve;
    }
    return nullptr;
}

const HydraulicPatternTime *patternById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        if (pattern.id == id)
            return &pattern;
    }
    return nullptr;
}

const HydraulicCurvePumpHead *pumpHeadCurveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

const HydraulicCurvePumpEfficiency *pumpEfficiencyCurveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

const HydraulicCurveGeneric *genericCurveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurveGeneric &curve : network.curves_generic)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

const HydraulicCurveTankVolume *tankVolumeCurveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

const HydraulicCurveValveHeadloss *valveHeadlossCurveById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

const HydraulicCurveValveCharacteristic *valveCharacteristicCurveById(
    const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
    {
        if (curve.id == id)
            return &curve;
    }
    return nullptr;
}

QString nodeIdForUuid(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return junction.id;
    }
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return reservoir.id;
    }
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return tank.id;
    }
    return QString();
}


template<typename ResultType>
double importedQualityValue(const ResultType &result, WaterQualityAnalysisType analysis)
{
    switch (analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        return result.chemical_concentration_mg_per_l;
    case WaterQualityAnalysisType::WaterAge:
        return result.water_age_h;
    case WaterQualityAnalysisType::SourceTrace:
        return result.source_trace_percent;
    case WaterQualityAnalysisType::None:
        return 0.0;
    }
    return 0.0;
}

ComparisonContext qualityComparison(
    std::int64_t time_s,
    const QString &entity_type,
    const QString &entity_id)
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = entity_type.toStdString();
    value.entity_id = entity_id.toStdString();
    value.field = "quality";
    return value;
}

template<typename ResultType>
void compareImportedQualityNodes(
    TestContext &context,
    const QList<ResultType> &actual,
    const AowisEpanetTests::NativeQualityReferenceStep &expected,
    WaterQualityAnalysisType analysis,
    const QString &entity_type,
    NumericTolerance quality_tolerance)
{
    for (const ResultType &result : actual)
    {
        context.expect(
            expected.node_quality.contains(result.id),
            "native quality reference must contain every imported node");
        if (!expected.node_quality.contains(result.id))
            continue;
        context.expectNear(
            importedQualityValue(result, analysis),
            expected.node_quality.value(result.id),
            quality_tolerance,
            qualityComparison(expected.time_s, entity_type, result.id));

        context.expect(
            expected.node_source_mass_mg_per_min.contains(result.id),
            "native quality reference must contain source-mass output for every imported node");
        if (expected.node_source_mass_mg_per_min.contains(result.id))
        {
            ComparisonContext source_mass = qualityComparison(expected.time_s, entity_type, result.id);
            source_mass.field = "source_mass_flow_mg_per_min";
            context.expectNear(
                result.source_mass_flow_mg_per_min,
                expected.node_source_mass_mg_per_min.value(result.id),
                quality_tolerance,
                source_mass);
        }
    }
}

template<typename ResultType>
void compareImportedQualityLinks(
    TestContext &context,
    const QList<ResultType> &actual,
    const AowisEpanetTests::NativeQualityReferenceStep &expected,
    WaterQualityAnalysisType analysis,
    const QString &entity_type,
    NumericTolerance quality_tolerance)
{
    for (const ResultType &result : actual)
    {
        context.expect(
            expected.link_quality.contains(result.id),
            "native quality reference must contain every imported link");
        if (!expected.link_quality.contains(result.id))
            continue;
        context.expectNear(
            importedQualityValue(result, analysis),
            expected.link_quality.value(result.id),
            quality_tolerance,
            qualityComparison(expected.time_s, entity_type, result.id));
    }
}

void compareImportedQualityTimeline(
    TestContext &context,
    const AowisEpanetTests::NativeQualityReferenceTimeline &native,
    const EpanetResultRun &run,
    WaterQualityAnalysisType analysis,
    NumericTolerance quality_tolerance = NumericTolerance{2.0e-6, 2.0e-6})
{
    context.expect(native.success, "native quality reference must solve successfully");
    context.expect(run.status.success, "imported quality request must solve successfully");
    context.expectEqual(
        static_cast<std::int64_t>(run.quality_results.size()),
        std::int64_t{1},
        comparison("quality_results.size"));
    if (!native.success || !run.status.success || run.quality_results.size() != 1)
        return;

    context.expectNear(
        run.quality_results.constFirst().options.relative_diffusivity,
        native.relative_diffusivity,
        numeric_tolerance,
        comparison("quality.relative_diffusivity"));

    const WaterQualitySimulationResultTimeline &actual =
        run.quality_results.constFirst().result_timeline;
    context.expect(
        actual.validity == WaterQualitySimulationResultValidity::Valid,
        "imported quality request must produce a valid quality timeline");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(native.results.size()),
        comparison("quality_timeline.size"));

    const int step_count = qMin(actual.results.size(), native.results.size());
    for (int index = 0; index < step_count; index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(index);
        const AowisEpanetTests::NativeQualityReferenceStep &expected_step = native.results.at(index);
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            expected_step.time_s,
            comparison("quality_time_s"));
        compareImportedQualityNodes(
            context, actual_step.nodes_junctions, expected_step, analysis, QStringLiteral("Junction"), quality_tolerance);
        compareImportedQualityNodes(
            context, actual_step.nodes_reservoirs, expected_step, analysis, QStringLiteral("Reservoir"), quality_tolerance);
        compareImportedQualityNodes(
            context, actual_step.nodes_tanks, expected_step, analysis, QStringLiteral("Tank"), quality_tolerance);
        compareImportedQualityLinks(
            context, actual_step.links_pipes, expected_step, analysis, QStringLiteral("Pipe"), quality_tolerance);
        compareImportedQualityLinks(
            context, actual_step.links_pumps, expected_step, analysis, QStringLiteral("Pump"), quality_tolerance);
        compareImportedQualityLinks(
            context, actual_step.links_valves, expected_step, analysis, QStringLiteral("Valve"), quality_tolerance);
    }
}

void expectNoDeferredQualityImportDiagnostics(
    TestContext &context,
    const EpanetResultImport &result)
{
    for (const HydraulicSimulationDiagnostic &diagnostic : result.diagnostics)
    {
        const bool quality_diagnostic =
            diagnostic.entity.type == HydraulicSimulationStatusEntityType::QualitySolver
            || diagnostic.message.contains(QStringLiteral("quality"), Qt::CaseInsensitive)
            || diagnostic.message.contains(QStringLiteral("reaction"), Qt::CaseInsensitive)
            || diagnostic.message.contains(QStringLiteral("mixing"), Qt::CaseInsensitive);
        context.expect(
            !quality_diagnostic,
            "supported EPANET quality import must not emit a deferred quality diagnostic");
    }
}

QString linkIdForUuid(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.uuid == uuid)
            return pipe.id;
    }
    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.uuid == uuid)
            return pump.id;
    }
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        if (valve.uuid == uuid)
            return valve.id;
    }
    return QString();
}

const HydraulicControlSimple *simpleControlById(
    const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicControlSimple &control : network.controls_simple)
    {
        if (control.id == id)
            return &control;
    }
    return nullptr;
}

const HydraulicControlRule *ruleById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicControlRule &rule : network.controls_rules)
    {
        if (rule.id == id)
            return &rule;
    }
    return nullptr;
}

void mapNativeControlIds(
    const NetworkHydraulic &network,
    AowisEpanetTests::NativeReferenceConfiguration &configuration)
{
    for (int index = 0; index < network.controls_simple.size(); index++)
        configuration.control_ids_by_index.insert(index + 1, network.controls_simple.at(index).id);
}

void scenarioImportNet1ProjectGlobals(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));

    context.expect(result.status.success, "Net1 INP import must open successfully");
    context.expect(result.complete, "Net1 import must be complete across the supported EPANET 2.3 INP surface");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(network.id.toStdString(), std::string("Net1"), comparison("network.id"));
    context.expect(!network.uuid.isNull(), "imported network must receive a stable in-memory UUID");
    context.expectEqual(network.title_line_1.toStdString(), std::string(" EPANET Example Network 1"), comparison("title_line_1"));
    context.expectEqual(network.title_line_2.toStdString(), std::string("A simple example of modeling chlorine decay. Both bulk and"), comparison("title_line_2"));
    context.expectEqual(network.title_line_3.toStdString(), std::string("wall reactions are included. "), comparison("title_line_3"));

    context.expectEqual(static_cast<std::int64_t>(network.duration_s), std::int64_t{86400}, comparison("duration_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_hydraulic_s), std::int64_t{3600}, comparison("timestep_hydraulic_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_quality_s), std::int64_t{300}, comparison("timestep_quality_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_pattern_s), std::int64_t{7200}, comparison("timestep_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_report_s), std::int64_t{3600}, comparison("timestep_report_s"));
    context.expectEqual(
        static_cast<std::int64_t>(network.report_statistic),
        static_cast<std::int64_t>(HydraulicSimulationReportStatistic::Series),
        comparison("report_statistic"));

    context.expectEqual(
        static_cast<std::int64_t>(network.options_hydraulic.headloss_formula),
        static_cast<std::int64_t>(HydraulicHeadlossFormula::HazenWilliams),
        comparison("headloss_formula"));
    context.expectEqual(static_cast<std::int64_t>(network.options_hydraulic.maximum_trials), std::int64_t{40}, comparison("maximum_trials"));
    context.expectNear(network.options_hydraulic.accuracy, 0.001, numeric_tolerance, comparison("accuracy"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_hydraulic.unbalanced_action),
        static_cast<std::int64_t>(HydraulicUnbalancedAction::Continue),
        comparison("unbalanced_action"));
    context.expectEqual(static_cast<std::int64_t>(network.options_hydraulic.unbalanced_extra_trials), std::int64_t{10}, comparison("unbalanced_extra_trials"));
    context.expectNear(network.options_energy.global_pump_efficiency_percent, 75.0, numeric_tolerance, comparison("global_pump_efficiency_percent"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_report.status),
        static_cast<std::int64_t>(HydraulicSimulationReportStatus::Normal),
        comparison("report_status"));

    context.expectEqual(static_cast<std::int64_t>(network.nodes_junctions.size()), std::int64_t{9}, comparison("junction_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_reservoirs.size()), std::int64_t{1}, comparison("reservoir_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_tanks.size()), std::int64_t{1}, comparison("tank_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pipes.size()), std::int64_t{12}, comparison("pipe_count"));
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"),
        "Net1 CHEMICAL configuration must be imported as one quality child");
}

void scenarioImportCanonicalGlobalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GLOBAL_OPTIONS_US_INP));

    context.expect(result.status.success, "custom global-options INP import must open successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expect(result.complete, "global-options fixture must import completely once report directives are reconstructed");
    context.expect(result.request.quality_runs.isEmpty(), "QUALITY NONE must import as a hydraulics-only request");
    const NetworkHydraulic &network = result.request.network;

    context.expectEqual(network.title_line_1.toStdString(), std::string("Import Global Options Fixture"), comparison("title_line_1"));
    context.expectEqual(static_cast<std::int64_t>(network.duration_s), std::int64_t{45000}, comparison("duration_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_hydraulic_s), std::int64_t{1800}, comparison("timestep_hydraulic_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_quality_s), std::int64_t{600}, comparison("timestep_quality_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_pattern_s), std::int64_t{5400}, comparison("timestep_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_pattern_s), std::int64_t{900}, comparison("start_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_report_s), std::int64_t{2700}, comparison("timestep_report_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_report_s), std::int64_t{1800}, comparison("start_report_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_rule_s), std::int64_t{300}, comparison("timestep_rule_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_time_of_day_s), std::int64_t{54900}, comparison("start_time_of_day_s"));
    context.expectEqual(
        static_cast<std::int64_t>(network.report_statistic),
        static_cast<std::int64_t>(HydraulicSimulationReportStatistic::Average),
        comparison("report_statistic"));

    const HydraulicSolverOptions &hydraulic = network.options_hydraulic;
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.headloss_formula),
        static_cast<std::int64_t>(HydraulicHeadlossFormula::DarcyWeisbach),
        comparison("headloss_formula"));
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.demand_model),
        static_cast<std::int64_t>(HydraulicDemandModel::PressureDriven),
        comparison("demand_model"));
    context.expectNear(hydraulic.specific_gravity, 1.2, numeric_tolerance, comparison("specific_gravity"));

    const double expected_minimum_pressure_head_m = 20.0 / (0.4333 * 1.2) * 0.3048;
    const double expected_required_pressure_head_m = 50.0 / (0.4333 * 1.2) * 0.3048;
    context.expectNear(hydraulic.minimum_pressure_head_m, expected_minimum_pressure_head_m, numeric_tolerance, comparison("minimum_pressure_head_m"));
    context.expectNear(hydraulic.required_pressure_head_m, expected_required_pressure_head_m, numeric_tolerance, comparison("required_pressure_head_m"));
    context.expectNear(hydraulic.pressure_exponent, 0.7, numeric_tolerance, comparison("pressure_exponent"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.maximum_trials), std::int64_t{123}, comparison("maximum_trials"));
    context.expectNear(hydraulic.accuracy, 0.0005, numeric_tolerance, comparison("accuracy"));
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.unbalanced_action),
        static_cast<std::int64_t>(HydraulicUnbalancedAction::Continue),
        comparison("unbalanced_action"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.unbalanced_extra_trials), std::int64_t{7}, comparison("unbalanced_extra_trials"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.check_frequency), std::int64_t{4}, comparison("check_frequency"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.maximum_check), std::int64_t{8}, comparison("maximum_check"));
    context.expectNear(hydraulic.damping_limit, 0.01, numeric_tolerance, comparison("damping_limit"));
    context.expectNear(hydraulic.maximum_head_error_m, 2.0 * 0.3048, numeric_tolerance, comparison("maximum_head_error_m"));
    context.expectNear(hydraulic.maximum_flow_change_m3_per_h, 10.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("maximum_flow_change_m3_per_h"));
    context.expectNear(hydraulic.demand_multiplier, 1.25, numeric_tolerance, comparison("demand_multiplier"));
    context.expect(hydraulic.emitters_can_backflow, "emitter backflow flag must import from EPANET options");
    context.expectNear(hydraulic.relative_viscosity, 1.3, numeric_tolerance, comparison("relative_viscosity"));

    context.expectNear(network.options_energy.global_pump_efficiency_percent, 82.0, numeric_tolerance, comparison("global_pump_efficiency_percent"));
    context.expectNear(network.options_energy.global_energy_price_per_kw_h, 0.15, numeric_tolerance, comparison("global_energy_price_per_kw_h"));
    context.expectNear(network.options_energy.demand_charge_per_kw, 5.0, numeric_tolerance, comparison("demand_charge_per_kw"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_report.status),
        static_cast<std::int64_t>(HydraulicSimulationReportStatus::Full),
        comparison("report_status"));
}

void scenarioImportCoreTopologyNet1(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));
    context.expect(result.status.success, "Net1 core topology import must succeed");
    context.expect(result.complete, "Net1 core import must no longer depend on deferred metadata/report layers");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeJunction *junction_11 = junctionById(network, QStringLiteral("11"));
    context.expect(junction_11 != nullptr, "Net1 junction 11 must be imported");
    if (junction_11 != nullptr)
    {
        context.expectNear(junction_11->elevation_m, 710.0 * 0.3048, numeric_tolerance, comparison("junction_11.elevation_m"));
        context.expectEqual(static_cast<std::int64_t>(junction_11->demands.size()), std::int64_t{1}, comparison("junction_11.demands.size"));
        if (!junction_11->demands.isEmpty())
        {
            context.expectNear(
                junction_11->demands.first().base_demand_m3_per_h,
                150.0 * epanet_gpm_to_cmh,
                numeric_tolerance,
                comparison("junction_11.base_demand_m3_per_h"));
        }
    }

    const HydraulicNodeReservoir *reservoir_9 = reservoirById(network, QStringLiteral("9"));
    context.expect(reservoir_9 != nullptr, "Net1 reservoir 9 must be imported");
    if (reservoir_9 != nullptr)
        context.expectNear(reservoir_9->hydraulic_head_m, 800.0 * 0.3048, numeric_tolerance, comparison("reservoir_9.hydraulic_head_m"));

    const HydraulicNodeTank *tank_2 = tankById(network, QStringLiteral("2"));
    context.expect(tank_2 != nullptr, "Net1 tank 2 must be imported");
    if (tank_2 != nullptr)
    {
        context.expectNear(tank_2->bottom_elevation_m, 850.0 * 0.3048, numeric_tolerance, comparison("tank_2.bottom_elevation_m"));
        context.expectNear(tank_2->water_level_initial_m, 120.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_initial_m"));
        context.expectNear(tank_2->water_level_minimum_m, 100.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_minimum_m"));
        context.expectNear(tank_2->water_level_maximum_m, 150.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_maximum_m"));
        context.expectNear(tank_2->diameter_m, 50.5 * 0.3048, numeric_tolerance, comparison("tank_2.diameter_m"));
        const double expected_minimum_volume_ft3 =
            std::acos(-1.0) * 50.5 * 50.5 / 4.0 * 100.0;
        context.expectNear(
            tank_2->minimum_volume_m3,
            expected_minimum_volume_ft3 * 0.028316846592,
            numeric_tolerance,
            comparison("tank_2.minimum_volume_m3"));
    }

    const HydraulicLinkPipe *pipe_10 = pipeById(network, QStringLiteral("10"));
    context.expect(pipe_10 != nullptr, "Net1 pipe 10 must be imported");
    if (pipe_10 != nullptr)
    {
        context.expectEqual(nodeIdForUuid(network, pipe_10->node_uuid_from).toStdString(), std::string("10"), comparison("pipe_10.node_from"));
        context.expectEqual(nodeIdForUuid(network, pipe_10->node_uuid_to).toStdString(), std::string("11"), comparison("pipe_10.node_to"));
        context.expect(pipe_10->length_measured_m.has_value(), "imported explicit pipe length must be retained as measured length");
        if (pipe_10->length_measured_m.has_value())
            context.expectNear(pipe_10->length_measured_m.value(), 10530.0 * 0.3048, numeric_tolerance, comparison("pipe_10.length_measured_m"));
        context.expectNear(pipe_10->diameter_mm, 18.0 * 25.4, numeric_tolerance, comparison("pipe_10.diameter_mm"));
        context.expectNear(pipe_10->roughness_hazen_williams, 100.0, numeric_tolerance, comparison("pipe_10.roughness_hazen_williams"));
        context.expectEqual(
            static_cast<std::int64_t>(pipe_10->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::Open),
            comparison("pipe_10.initial_status"));
    }
}

void scenarioImportCoreTopologyCanonicalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CORE_TOPOLOGY_US_INP));
    context.expect(result.status.success, "US customary core-topology fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(static_cast<std::int64_t>(network.nodes_junctions.size()), std::int64_t{2}, comparison("junction_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_reservoirs.size()), std::int64_t{1}, comparison("reservoir_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_tanks.size()), std::int64_t{1}, comparison("tank_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pipes.size()), std::int64_t{3}, comparison("pipe_count"));

    const HydraulicNodeJunction *junction_1 = junctionById(network, QStringLiteral("J1"));
    context.expect(junction_1 != nullptr, "J1 must be imported");
    if (junction_1 != nullptr)
    {
        context.expectNear(junction_1->elevation_m, 100.0 * 0.3048, numeric_tolerance, comparison("J1.elevation_m"));
        context.expectEqual(static_cast<std::int64_t>(junction_1->demands.size()), std::int64_t{2}, comparison("J1.demands.size"));
        if (junction_1->demands.size() == 2)
        {
            context.expectNear(junction_1->demands.at(0).base_demand_m3_per_h, 10.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("J1.demands[0]"));
            context.expectNear(junction_1->demands.at(1).base_demand_m3_per_h, 5.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("J1.demands[1]"));
        }
    }

    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    context.expect(junction_2 != nullptr, "J2 must be imported");
    if (junction_2 != nullptr)
    {
        const double pressure_head_m_per_psi = 0.3048 / 0.4333;
        const double expected_emitter = 2.5 * epanet_gpm_to_cmh / std::pow(pressure_head_m_per_psi, 0.6);
        context.expectNear(junction_2->emitter.pressure_exponent, 0.6, numeric_tolerance, comparison("J2.emitter.pressure_exponent"));
        context.expectNear(junction_2->emitter.coefficient, expected_emitter, numeric_tolerance, comparison("J2.emitter.coefficient"));
    }

    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    context.expect(reservoir != nullptr, "R1 must be imported");
    if (reservoir != nullptr)
        context.expectNear(reservoir->hydraulic_head_m, 250.0 * 0.3048, numeric_tolerance, comparison("R1.hydraulic_head_m"));

    const HydraulicNodeTank *tank = tankById(network, QStringLiteral("T1"));
    context.expect(tank != nullptr, "T1 must be imported");
    if (tank != nullptr)
    {
        context.expectNear(tank->bottom_elevation_m, 150.0 * 0.3048, numeric_tolerance, comparison("T1.bottom_elevation_m"));
        context.expectNear(tank->water_level_initial_m, 10.0 * 0.3048, numeric_tolerance, comparison("T1.water_level_initial_m"));
        context.expectNear(tank->water_level_maximum_m, 20.0 * 0.3048, numeric_tolerance, comparison("T1.water_level_maximum_m"));
        context.expectNear(tank->diameter_m, 30.0 * 0.3048, numeric_tolerance, comparison("T1.diameter_m"));
        context.expectNear(tank->minimum_volume_m3, 100.0 * 0.028316846592, numeric_tolerance, comparison("T1.minimum_volume_m3"));
    }

    const HydraulicLinkPipe *pipe_1 = pipeById(network, QStringLiteral("P1"));
    context.expect(pipe_1 != nullptr, "P1 must be imported");
    if (pipe_1 != nullptr)
    {
        context.expectEqual(nodeIdForUuid(network, pipe_1->node_uuid_from).toStdString(), std::string("R1"), comparison("P1.node_from"));
        context.expectEqual(nodeIdForUuid(network, pipe_1->node_uuid_to).toStdString(), std::string("J1"), comparison("P1.node_to"));
        context.expectNear(pipe_1->length_measured_m.value_or(0.0), 1000.0 * 0.3048, numeric_tolerance, comparison("P1.length_measured_m"));
        context.expectNear(pipe_1->diameter_mm, 12.0 * 25.4, numeric_tolerance, comparison("P1.diameter_mm"));
        context.expectNear(pipe_1->roughness_darcy_weisbach_mm, 0.5 * 0.3048, numeric_tolerance, comparison("P1.roughness_darcy_weisbach_mm"));
        context.expectNear(pipe_1->minor_loss_coefficient, 0.2, numeric_tolerance, comparison("P1.minor_loss_coefficient"));
        context.expectNear(pipe_1->leak_area_mm2_per_100m, 2.0 / 0.3048, numeric_tolerance, comparison("P1.leak_area_mm2_per_100m"));
        context.expectNear(pipe_1->leak_area_expansion_per_pressure_head_mm2_per_m, 0.1 / 0.3048, numeric_tolerance, comparison("P1.leak_expansion"));
    }

    const HydraulicLinkPipe *pipe_2 = pipeById(network, QStringLiteral("P2"));
    context.expect(pipe_2 != nullptr, "P2 must be imported");
    if (pipe_2 != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(pipe_2->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::Closed),
            comparison("P2.initial_status"));
    }

    const HydraulicLinkPipe *pipe_3 = pipeById(network, QStringLiteral("P3"));
    context.expect(pipe_3 != nullptr, "P3 must be imported");
    if (pipe_3 != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(pipe_3->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::CheckValve),
            comparison("P3.initial_status"));
    }

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CORE_TOPOLOGY_US_INP);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    context.expect(native_timeline.success, "native topology fixture must solve successfully");

    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(wrapper_run.status.success, "imported topology fixture must solve successfully through the wrapper");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
}

void scenarioImportHydraulicAssetsNet1(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));
    context.expect(result.status.success, "Net1 hydraulic asset import must succeed");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(static_cast<std::int64_t>(network.patterns_time.size()), std::int64_t{1}, comparison("pattern_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_pump_head.size()), std::int64_t{1}, comparison("pump_head_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pumps.size()), std::int64_t{1}, comparison("pump_count"));

    const HydraulicPatternTime *pattern = patternById(network, QStringLiteral("1"));
    context.expect(pattern != nullptr, "Net1 demand pattern 1 must be imported");
    if (pattern != nullptr)
        context.expectEqual(static_cast<std::int64_t>(pattern->multipliers.size()), std::int64_t{12}, comparison("pattern_1.length"));

    const HydraulicCurvePumpHead *curve = pumpHeadCurveById(network, QStringLiteral("1"));
    context.expect(curve != nullptr, "Net1 pump head curve 1 must be imported");
    if (curve != nullptr)
    {
        context.expectEqual(static_cast<std::int64_t>(curve->points.size()), std::int64_t{1}, comparison("curve_1.points"));
        if (!curve->points.isEmpty())
        {
            context.expectNear(curve->points.first().flow_m3_per_h, 1500.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("curve_1.flow"));
            context.expectNear(curve->points.first().head_gain_m, 250.0 * 0.3048, numeric_tolerance, comparison("curve_1.head"));
        }
    }

    const HydraulicLinkPump *pump = pumpById(network, QStringLiteral("9"));
    context.expect(pump != nullptr, "Net1 pump 9 must be imported");
    if (pump != nullptr && curve != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(pump->definition_type),
            static_cast<std::int64_t>(HydraulicLinkPumpDefinitionType::OnePointCurve),
            comparison("pump_9.definition_type"));
        context.expect(pump->head_curve_uuid == curve->uuid, "Net1 pump 9 must reference imported head curve 1");
    }

}

void scenarioImportPatternsCurvesPumpsCanonicalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_PUMPS_US_INP));
    context.expect(result.status.success, "pump/pattern/curve fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(static_cast<std::int64_t>(network.patterns_time.size()), std::int64_t{5}, comparison("pattern_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_tank_volume.size()), std::int64_t{1}, comparison("tank_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_pump_head.size()), std::int64_t{1}, comparison("pump_head_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_pump_efficiency.size()), std::int64_t{1}, comparison("efficiency_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_generic.size()), std::int64_t{1}, comparison("generic_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pumps.size()), std::int64_t{2}, comparison("pump_count"));

    const HydraulicPatternTime *demand_pattern = patternById(network, QStringLiteral("DMD"));
    const HydraulicPatternTime *reservoir_pattern = patternById(network, QStringLiteral("RPAT"));
    const HydraulicPatternTime *speed_pattern = patternById(network, QStringLiteral("SPD"));
    const HydraulicPatternTime *global_price_pattern = patternById(network, QStringLiteral("PRICEG"));
    const HydraulicPatternTime *pump_price_pattern = patternById(network, QStringLiteral("PRICEP"));
    context.expect(demand_pattern != nullptr, "DMD pattern must be imported");
    context.expect(reservoir_pattern != nullptr, "RPAT pattern must be imported");
    context.expect(speed_pattern != nullptr, "SPD pattern must be imported");
    context.expect(global_price_pattern != nullptr, "PRICEG pattern must be imported");
    context.expect(pump_price_pattern != nullptr, "PRICEP pattern must be imported");

    if (demand_pattern != nullptr)
        context.expect(network.options_hydraulic.default_demand_pattern_uuid == demand_pattern->uuid, "default demand pattern must resolve to DMD UUID");
    if (global_price_pattern != nullptr)
        context.expect(network.options_energy.global_energy_price_pattern_uuid == global_price_pattern->uuid, "global energy-price pattern must resolve to PRICEG UUID");

    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    context.expect(junction != nullptr, "J1 must be imported");
    if (junction != nullptr && !junction->demands.isEmpty() && demand_pattern != nullptr)
    {
        context.expect(
            junction->demands.first().pattern_mode == HydraulicTimePatternMode::TimePattern,
            "J1 demand must retain time-pattern mode");
        context.expect(junction->demands.first().pattern_uuid == demand_pattern->uuid, "J1 demand must resolve DMD pattern UUID");
    }

    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    context.expect(junction_2 != nullptr, "J2 must be imported");
    if (junction_2 != nullptr && !junction_2->demands.isEmpty() && demand_pattern != nullptr)
    {
        context.expect(
            junction_2->demands.first().pattern_mode == HydraulicTimePatternMode::TimePattern,
            "J2 demand must make EPANET default-pattern semantics explicit");
        context.expect(junction_2->demands.first().pattern_uuid == demand_pattern->uuid, "J2 demand must resolve the project default DMD pattern UUID");
    }

    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    context.expect(reservoir != nullptr, "R1 must be imported");
    if (reservoir != nullptr && reservoir_pattern != nullptr)
        context.expect(reservoir->head_pattern_uuid == reservoir_pattern->uuid, "reservoir head pattern must resolve RPAT UUID");

    const HydraulicCurveTankVolume *tank_curve = tankVolumeCurveById(network, QStringLiteral("TVOL"));
    context.expect(tank_curve != nullptr, "TVOL must be imported as a tank-volume curve");
    if (tank_curve != nullptr && tank_curve->points.size() == 3)
    {
        context.expectNear(tank_curve->points.at(1).water_level_m, 10.0 * 0.3048, numeric_tolerance, comparison("TVOL.level"));
        context.expectNear(tank_curve->points.at(1).volume_m3, 5000.0 * 0.028316846592, numeric_tolerance, comparison("TVOL.volume"));
    }
    const HydraulicNodeTank *tank = tankById(network, QStringLiteral("T1"));
    context.expect(tank != nullptr, "T1 must be imported");
    if (tank != nullptr && tank_curve != nullptr)
    {
        context.expect(tank->geometry_input_type == HydraulicNodeTankGeometryInputType::VolumeCurve, "T1 must use volume-curve geometry");
        context.expect(tank->volume_curve_uuid == tank_curve->uuid, "T1 must reference TVOL UUID");
    }

    const HydraulicCurvePumpHead *head_curve = pumpHeadCurveById(network, QStringLiteral("PHEAD"));
    const HydraulicCurvePumpEfficiency *efficiency_curve = pumpEfficiencyCurveById(network, QStringLiteral("EFF"));
    const HydraulicCurveGeneric *generic_curve = genericCurveById(network, QStringLiteral("GEN"));
    context.expect(head_curve != nullptr, "PHEAD must be imported as pump-head curve");
    context.expect(efficiency_curve != nullptr, "EFF must be imported as pump-efficiency curve");
    context.expect(generic_curve != nullptr, "GEN must be imported as a generic curve");
    if (head_curve != nullptr && head_curve->points.size() == 3)
    {
        context.expectNear(head_curve->points.at(1).flow_m3_per_h, 100.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("PHEAD.flow"));
        context.expectNear(head_curve->points.at(1).head_gain_m, 120.0 * 0.3048, numeric_tolerance, comparison("PHEAD.head"));
    }
    if (efficiency_curve != nullptr && efficiency_curve->points.size() == 3)
    {
        context.expectNear(efficiency_curve->points.at(1).flow_m3_per_h, 100.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("EFF.flow"));
        context.expectNear(efficiency_curve->points.at(1).efficiency_percent, 82.0, numeric_tolerance, comparison("EFF.efficiency"));
    }
    if (generic_curve != nullptr && generic_curve->points.size() == 2)
    {
        context.expectNear(generic_curve->points.at(1).x, 1.0, numeric_tolerance, comparison("GEN.x"));
        context.expectNear(generic_curve->points.at(1).y, 20.0, numeric_tolerance, comparison("GEN.y"));
    }

    const HydraulicLinkPump *pump_1 = pumpById(network, QStringLiteral("P1"));
    context.expect(pump_1 != nullptr, "P1 must be imported");
    if (pump_1 != nullptr)
    {
        context.expect(pump_1->definition_type == HydraulicLinkPumpDefinitionType::ThreePointCurve, "P1 must retain three-point curve definition");
        if (head_curve != nullptr)
            context.expect(pump_1->head_curve_uuid == head_curve->uuid, "P1 must reference PHEAD UUID");
        context.expectNear(pump_1->initial_speed_ratio, 0.8, numeric_tolerance, comparison("P1.speed"));
        if (speed_pattern != nullptr)
            context.expect(pump_1->speed_pattern_uuid == speed_pattern->uuid, "P1 must reference SPD UUID");
        context.expect(pump_1->efficiency_input_type == HydraulicLinkPumpEfficiencyInputType::Curve, "P1 must retain efficiency-curve input");
        if (efficiency_curve != nullptr)
            context.expect(pump_1->efficiency_curve_uuid == efficiency_curve->uuid, "P1 must reference EFF UUID");
        context.expect(pump_1->energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern, "P1 must retain patterned pump-specific energy price");
        context.expectNear(pump_1->energy_price_per_kw_h, 0.25, numeric_tolerance, comparison("P1.energy_price"));
        if (pump_price_pattern != nullptr)
            context.expect(pump_1->price_pattern_uuid == pump_price_pattern->uuid, "P1 must reference PRICEP UUID");
    }

    const HydraulicLinkPump *pump_2 = pumpById(network, QStringLiteral("P2"));
    context.expect(pump_2 != nullptr, "P2 must be imported");
    if (pump_2 != nullptr)
    {
        context.expect(pump_2->definition_type == HydraulicLinkPumpDefinitionType::ConstantPower, "P2 must retain constant-power definition");
        context.expectNear(pump_2->constant_power_kw, 25.0 * 0.7457, numeric_tolerance, comparison("P2.power"));
        context.expectNear(pump_2->initial_speed_ratio, 1.1, numeric_tolerance, comparison("P2.speed"));
        context.expect(pump_2->energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Constant, "P2 must retain constant pump-specific energy price");
        context.expectNear(pump_2->energy_price_per_kw_h, 0.30, numeric_tolerance, comparison("P2.energy_price"));
    }

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_PUMPS_US_INP);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(native_timeline.success, "native pump fixture must solve successfully");
    context.expect(wrapper_run.status.success, "imported pump fixture must solve successfully");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
}

void scenarioImportValvesCanonicalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_VALVES_US_INP));
    context.expect(result.status.success, "all-valve fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(static_cast<std::int64_t>(network.links_valves.size()), std::int64_t{7}, comparison("valve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_valve_headloss.size()), std::int64_t{1}, comparison("headloss_curve_count"));
    context.expectEqual(static_cast<std::int64_t>(network.curves_valve_characteristic.size()), std::int64_t{1}, comparison("characteristic_curve_count"));

    const double metres_per_psi = 0.3048 / 0.4333;
    struct ValveExpectation
    {
        const char *id;
        HydraulicLinkValveType type;
        double setting;
    };
    const std::array<ValveExpectation, 5> scalar_valves = {{
        {"VPRV", HydraulicLinkValveType::PRV, 50.0 * metres_per_psi},
        {"VPSV", HydraulicLinkValveType::PSV, 30.0 * metres_per_psi},
        {"VPBV", HydraulicLinkValveType::PBV, 20.0 * metres_per_psi},
        {"VFCV", HydraulicLinkValveType::FCV, 100.0 * epanet_gpm_to_cmh},
        {"VTCV", HydraulicLinkValveType::TCV, 2.5}
    }};

    for (const ValveExpectation &expected : scalar_valves)
    {
        const HydraulicLinkValve *valve = valveById(network, QString::fromLatin1(expected.id));
        context.expect(valve != nullptr, std::string(expected.id) + " must be imported");
        if (valve == nullptr)
            continue;
        context.expect(valve->type == expected.type, std::string(expected.id) + " must retain valve type");
        context.expectNear(valve->diameter_mm, 12.0 * 25.4, numeric_tolerance, comparison(std::string(expected.id) + ".diameter"));
        if (expected.type == HydraulicLinkValveType::PRV
            || expected.type == HydraulicLinkValveType::PSV
            || expected.type == HydraulicLinkValveType::PBV)
        {
            context.expectNear(valve->setting_pressure_head_m, expected.setting, numeric_tolerance, comparison(std::string(expected.id) + ".pressure_setting"));
        }
        else if (expected.type == HydraulicLinkValveType::FCV)
        {
            context.expectNear(valve->setting_flow_m3_per_h, expected.setting, numeric_tolerance, comparison(std::string(expected.id) + ".flow_setting"));
        }
        else
        {
            context.expectNear(valve->setting_loss_coefficient, expected.setting, numeric_tolerance, comparison(std::string(expected.id) + ".loss_setting"));
        }
    }

    const HydraulicCurveValveHeadloss *headloss_curve = valveHeadlossCurveById(network, QStringLiteral("HLOSS"));
    const HydraulicLinkValve *gpv = valveById(network, QStringLiteral("VGPV"));
    context.expect(headloss_curve != nullptr, "HLOSS must be imported as valve head-loss curve");
    context.expect(gpv != nullptr, "VGPV must be imported");
    if (gpv != nullptr && headloss_curve != nullptr)
    {
        context.expect(gpv->type == HydraulicLinkValveType::GPV, "VGPV must retain GPV type");
        context.expect(gpv->head_loss_curve_uuid == headloss_curve->uuid, "VGPV must reference HLOSS UUID");
        context.expect(gpv->initial_status == HydraulicLinkValveInitialStatus::Open, "VGPV explicit Open status must be retained");
    }
    if (headloss_curve != nullptr && headloss_curve->points.size() == 3)
    {
        context.expectNear(headloss_curve->points.at(1).flow_m3_per_h, 100.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("HLOSS.flow"));
        context.expectNear(headloss_curve->points.at(1).head_loss_m, 10.0 * 0.3048, numeric_tolerance, comparison("HLOSS.head"));
    }

    const HydraulicCurveValveCharacteristic *characteristic_curve =
        valveCharacteristicCurveById(network, QStringLiteral("PCVC"));
    const HydraulicLinkValve *pcv = valveById(network, QStringLiteral("VPCV"));
    context.expect(characteristic_curve != nullptr, "PCVC must be imported as valve-characteristic curve");
    context.expect(pcv != nullptr, "VPCV must be imported");
    if (pcv != nullptr && characteristic_curve != nullptr)
    {
        context.expect(pcv->type == HydraulicLinkValveType::PCV, "VPCV must retain PCV type");
        context.expectNear(pcv->setting_position_percent, 35.0, numeric_tolerance, comparison("VPCV.position"));
        context.expect(pcv->characteristic_curve_uuid == characteristic_curve->uuid, "VPCV must reference PCVC UUID");
        context.expect(pcv->initial_status == HydraulicLinkValveInitialStatus::Active, "VPCV default Active status must be retained");
    }
    if (characteristic_curve != nullptr && characteristic_curve->points.size() == 3)
    {
        context.expectNear(characteristic_curve->points.at(1).position_percent, 50.0, numeric_tolerance, comparison("PCVC.position"));
        context.expectNear(characteristic_curve->points.at(1).relative_flow_percent, 35.0, numeric_tolerance, comparison("PCVC.relative_flow"));
    }

    const HydraulicLinkValve *tcv = valveById(network, QStringLiteral("VTCV"));
    if (tcv != nullptr)
        context.expect(tcv->initial_status == HydraulicLinkValveInitialStatus::Closed, "VTCV explicit Closed status must be retained");

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_VALVES_US_INP);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(native_timeline.success, "native valve fixture must solve successfully");
    context.expect(wrapper_run.status.success, "imported valve fixture must solve successfully");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
}

void scenarioImportControlsNet1Equivalence(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));
    context.expect(result.status.success, "Net1 controls must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(
        static_cast<std::int64_t>(network.controls_simple.size()),
        std::int64_t{2}, comparison("simple_control_count"));
    context.expect(network.controls_rules.isEmpty(), "Net1 contains no rule-based controls");

    const HydraulicControlSimple *open_control = simpleControlById(
        network, QStringLiteral("CONTROL_1"));
    const HydraulicControlSimple *close_control = simpleControlById(
        network, QStringLiteral("CONTROL_2"));
    context.expect(open_control != nullptr, "Net1 control 1 must be imported");
    context.expect(close_control != nullptr, "Net1 control 2 must be imported");

    if (open_control != nullptr)
    {
        context.expect(
            open_control->type == HydraulicControlSimpleType::LowLevel,
            "Net1 control 1 must retain its low-level trigger");
        context.expect(
            open_control->action == HydraulicControlActionType::Open,
            "Net1 control 1 must retain its OPEN action");
        context.expectEqual(
            linkIdForUuid(network, open_control->link_uuid).toStdString(),
            std::string("9"), comparison("control_1.link"));
        context.expectEqual(
            nodeIdForUuid(network, open_control->trigger_node_uuid).toStdString(),
            std::string("2"), comparison("control_1.trigger_node"));
        context.expectNear(
            open_control->trigger_water_level_m,
            110.0 * 0.3048,
            numeric_tolerance,
            comparison("control_1.trigger_water_level_m"));
        context.expect(open_control->enabled, "Net1 control 1 must remain enabled");
    }

    if (close_control != nullptr)
    {
        context.expect(
            close_control->type == HydraulicControlSimpleType::HighLevel,
            "Net1 control 2 must retain its high-level trigger");
        context.expect(
            close_control->action == HydraulicControlActionType::Close,
            "Net1 control 2 must retain its CLOSED action");
        context.expectNear(
            close_control->trigger_water_level_m,
            140.0 * 0.3048,
            numeric_tolerance,
            comparison("control_2.trigger_water_level_m"));
        context.expect(close_control->enabled, "Net1 control 2 must remain enabled");
    }

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_NET1_INP);
    mapNativeControlIds(network, native_configuration);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(native_timeline.success, "native Net1 with controls must solve successfully");
    context.expect(wrapper_run.status.success, "imported Net1 with controls must solve successfully");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
}

void scenarioImportStructuredRulesCanonicalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CONTROLS_RULES_US_INP));
    context.expect(result.status.success, "controls/rules fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(
        static_cast<std::int64_t>(network.controls_simple.size()),
        std::int64_t{10}, comparison("simple_control_count"));
    context.expectEqual(
        static_cast<std::int64_t>(network.controls_rules.size()),
        std::int64_t{3}, comparison("rule_count"));

    const HydraulicControlSimple *timer = simpleControlById(
        network, QStringLiteral("CONTROL_3"));
    const HydraulicControlSimple *time_of_day = simpleControlById(
        network, QStringLiteral("CONTROL_4"));
    const HydraulicControlSimple *disabled = simpleControlById(
        network, QStringLiteral("CONTROL_5"));
    const HydraulicControlSimple *junction_pressure = simpleControlById(
        network, QStringLiteral("CONTROL_6"));
    const HydraulicControlSimple *reservoir_level = simpleControlById(
        network, QStringLiteral("CONTROL_7"));
    const HydraulicControlSimple *numeric_unit_speed = simpleControlById(
        network, QStringLiteral("CONTROL_8"));
    const HydraulicControlSimple *gpv_open = simpleControlById(
        network, QStringLiteral("CONTROL_9"));
    const HydraulicControlSimple *gpv_close = simpleControlById(
        network, QStringLiteral("CONTROL_10"));
    context.expect(timer != nullptr, "timer control must be imported");
    context.expect(time_of_day != nullptr, "time-of-day control must be imported");
    context.expect(disabled != nullptr, "disabled control must be imported");
    context.expect(junction_pressure != nullptr, "junction-pressure control must be imported");
    context.expect(reservoir_level != nullptr, "reservoir-triggered control must be imported");
    context.expect(numeric_unit_speed != nullptr, "numeric pump speed 1.0 control must be imported");
    context.expect(gpv_open != nullptr, "GPV OPEN control must be imported");
    context.expect(gpv_close != nullptr, "GPV CLOSED control must be imported");
    if (timer != nullptr)
    {
        context.expect(timer->type == HydraulicControlSimpleType::Timer, "control 3 must be a timer");
        context.expect(timer->action == HydraulicControlActionType::Setting, "control 3 must set pump speed");
        context.expectEqual(
            static_cast<std::int64_t>(timer->trigger_elapsed_time_s),
            std::int64_t{7200}, comparison("control_3.elapsed_time_s"));
        context.expect(timer->setting.pump_speed_ratio.has_value(), "control 3 must contain pump speed setting");
        if (timer->setting.pump_speed_ratio.has_value())
            context.expectNear(timer->setting.pump_speed_ratio.value(), 0.8, numeric_tolerance, comparison("control_3.speed"));
    }
    if (time_of_day != nullptr)
    {
        context.expect(time_of_day->type == HydraulicControlSimpleType::TimeOfDay, "control 4 must be time-of-day");
        context.expectEqual(
            static_cast<std::int64_t>(time_of_day->trigger_time_of_day_s),
            std::int64_t{10800}, comparison("control_4.clock_time_s"));
        context.expect(time_of_day->setting.valve_pressure_head_m.has_value(), "control 4 must contain a PRV pressure setting");
        if (time_of_day->setting.valve_pressure_head_m.has_value())
        {
            context.expectNear(
                time_of_day->setting.valve_pressure_head_m.value(),
                31.654742672513269,
                numeric_tolerance,
                comparison("control_4.pressure_head_m"));
        }
    }
    if (disabled != nullptr)
    {
        context.expect(!disabled->enabled, "control 5 must retain disabled state");
        context.expect(disabled->action == HydraulicControlActionType::Close, "control 5 must retain CLOSED action");
    }
    if (junction_pressure != nullptr)
    {
        context.expect(!junction_pressure->enabled, "control 6 must retain disabled state");
        context.expect(junction_pressure->type == HydraulicControlSimpleType::LowLevel, "control 6 must be a low-pressure trigger");
        context.expectEqual(
            nodeIdForUuid(network, junction_pressure->trigger_node_uuid).toStdString(),
            std::string("J2"),
            comparison("control_6.trigger_node"));
        context.expectNear(
            junction_pressure->trigger_pressure_head_m,
            17.585968151396262,
            numeric_tolerance,
            comparison("control_6.trigger_pressure_head_m"));
        context.expect(junction_pressure->setting.valve_pressure_head_m.has_value(), "control 6 must retain the PRV setting");
        if (junction_pressure->setting.valve_pressure_head_m.has_value())
        {
            context.expectNear(
                junction_pressure->setting.valve_pressure_head_m.value(),
                31.654742672513269,
                numeric_tolerance,
                comparison("control_6.setting_pressure_head_m"));
        }
    }

    if (reservoir_level != nullptr)
    {
        context.expect(!reservoir_level->enabled, "control 7 must retain disabled state");
        context.expect(reservoir_level->type == HydraulicControlSimpleType::LowLevel, "control 7 must retain reservoir low-level trigger");
        context.expect(reservoir_level->action == HydraulicControlActionType::Open, "control 7 must retain OPEN action");
        context.expectEqual(
            nodeIdForUuid(network, reservoir_level->trigger_node_uuid).toStdString(),
            std::string("R1"),
            comparison("control_7.trigger_node"));
        context.expectNear(
            reservoir_level->trigger_water_level_m,
            5.0 * 0.3048,
            numeric_tolerance,
            comparison("control_7.trigger_water_level_m"));
    }
    if (numeric_unit_speed != nullptr)
    {
        context.expect(!numeric_unit_speed->enabled, "control 8 must retain disabled state");
        context.expect(numeric_unit_speed->action == HydraulicControlActionType::Setting, "numeric pump speed 1.0 must remain a numeric setting, not collapse to OPEN");
        context.expect(numeric_unit_speed->setting.pump_speed_ratio.has_value(), "control 8 must retain pump speed");
        if (numeric_unit_speed->setting.pump_speed_ratio.has_value())
            context.expectNear(numeric_unit_speed->setting.pump_speed_ratio.value(), 1.0, numeric_tolerance, comparison("control_8.speed"));
    }
    if (gpv_open != nullptr)
    {
        context.expect(gpv_open->action == HydraulicControlActionType::Open, "control 9 must recover GPV OPEN from the source statement");
        context.expectEqual(linkIdForUuid(network, gpv_open->link_uuid).toStdString(), std::string("VGPV"), comparison("control_9.link"));
    }
    if (gpv_close != nullptr)
    {
        context.expect(gpv_close->action == HydraulicControlActionType::Close, "control 10 must recover GPV CLOSED from the source statement");
        context.expectEqual(linkIdForUuid(network, gpv_close->link_uuid).toStdString(), std::string("VGPV"), comparison("control_10.link"));
    }

    const HydraulicControlRule *level_rule = ruleById(network, QStringLiteral("R_LEVEL"));
    const HydraulicControlRule *flow_rule = ruleById(network, QStringLiteral("R_FLOW"));
    const HydraulicControlRule *system_rule = ruleById(network, QStringLiteral("R_SYSTEM"));
    context.expect(level_rule != nullptr, "R_LEVEL must be imported");
    context.expect(flow_rule != nullptr, "R_FLOW must be imported");
    context.expect(system_rule != nullptr, "R_SYSTEM must be imported");

    if (level_rule != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(level_rule->premises.size()),
            std::int64_t{2}, comparison("R_LEVEL.premise_count"));
        context.expectEqual(
            static_cast<std::int64_t>(level_rule->actions_then.size()),
            std::int64_t{1}, comparison("R_LEVEL.then_count"));
        context.expectEqual(
            static_cast<std::int64_t>(level_rule->actions_else.size()),
            std::int64_t{1}, comparison("R_LEVEL.else_count"));
        context.expectNear(level_rule->priority, 3.5, numeric_tolerance, comparison("R_LEVEL.priority"));
        if (level_rule->premises.size() == 2)
        {
            const HydraulicControlRulePremise &level = level_rule->premises.at(0);
            const HydraulicControlRulePremise &time = level_rule->premises.at(1);
            context.expect(level.logical_operator == HydraulicControlRuleLogicalOperator::If, "first R_LEVEL premise must use IF");
            context.expect(level.object == HydraulicControlRuleObject::Node, "R_LEVEL level premise must target a node");
            context.expect(level.variable == HydraulicControlRuleVariable::Level, "R_LEVEL first premise must use LEVEL");
            context.expect(level.comparison == HydraulicControlRuleOperator::Less, "EPANET BELOW must canonicalize to less-than semantics");
            context.expectEqual(nodeIdForUuid(network, level.object_uuid).toStdString(), std::string("T1"), comparison("R_LEVEL.node"));
            context.expect(level.water_level_m.has_value(), "R_LEVEL must contain canonical water level");
            if (level.water_level_m.has_value())
                context.expectNear(level.water_level_m.value(), 8.0 * 0.3048, numeric_tolerance, comparison("R_LEVEL.level_m"));

            context.expect(time.logical_operator == HydraulicControlRuleLogicalOperator::And, "second R_LEVEL premise must use AND");
            context.expect(time.object == HydraulicControlRuleObject::System, "R_LEVEL time premise must target SYSTEM");
            context.expect(time.variable == HydraulicControlRuleVariable::Time, "R_LEVEL second premise must use TIME");
            context.expect(time.elapsed_time_s.has_value(), "R_LEVEL time premise must contain seconds");
            if (time.elapsed_time_s.has_value())
                context.expectEqual(static_cast<std::int64_t>(time.elapsed_time_s.value()), std::int64_t{3600}, comparison("R_LEVEL.time_s"));
        }
        if (!level_rule->actions_then.isEmpty())
        {
            const HydraulicControlRuleAction &action = level_rule->actions_then.first();
            context.expect(action.setting.pump_speed_ratio.has_value(), "R_LEVEL THEN must set pump speed");
            if (action.setting.pump_speed_ratio.has_value())
                context.expectNear(action.setting.pump_speed_ratio.value(), 0.9, numeric_tolerance, comparison("R_LEVEL.then_speed"));
        }
        if (!level_rule->actions_else.isEmpty())
        {
            const HydraulicControlRuleAction &action = level_rule->actions_else.first();
            context.expect(action.status.has_value(), "R_LEVEL ELSE must retain status action");
            if (action.status.has_value())
                context.expect(action.status.value() == HydraulicControlRuleStatus::Open, "R_LEVEL ELSE must OPEN the pump");
        }
    }

    if (flow_rule != nullptr && flow_rule->premises.size() == 2)
    {
        const HydraulicControlRulePremise &flow = flow_rule->premises.at(0);
        const HydraulicControlRulePremise &status = flow_rule->premises.at(1);
        context.expect(flow.variable == HydraulicControlRuleVariable::Flow, "R_FLOW first premise must use FLOW");
        context.expect(flow.flow_m3_per_h.has_value(), "R_FLOW must contain canonical flow threshold");
        if (flow.flow_m3_per_h.has_value())
        {
            context.expectNear(
                flow.flow_m3_per_h.value(),
                2.271233493230191,
                numeric_tolerance,
                comparison("R_FLOW.flow_m3_per_h"));
        }
        context.expectEqual(linkIdForUuid(network, flow.object_uuid).toStdString(), std::string("L1"), comparison("R_FLOW.link"));
        context.expect(status.logical_operator == HydraulicControlRuleLogicalOperator::Or, "R_FLOW second premise must use OR");
        context.expect(status.variable == HydraulicControlRuleVariable::Status, "R_FLOW second premise must use STATUS");
        context.expect(status.comparison == HydraulicControlRuleOperator::Equal, "EPANET IS must canonicalize to equality semantics");
        context.expect(status.status.has_value(), "R_FLOW status premise must retain ACTIVE value");
        if (status.status.has_value())
            context.expect(status.status.value() == HydraulicControlRuleStatus::Active, "R_FLOW status premise must compare ACTIVE");

        context.expectEqual(
            static_cast<std::int64_t>(flow_rule->actions_then.size()),
            std::int64_t{1}, comparison("R_FLOW.then_count"));
        context.expectEqual(
            static_cast<std::int64_t>(flow_rule->actions_else.size()),
            std::int64_t{1}, comparison("R_FLOW.else_count"));
        if (!flow_rule->actions_then.isEmpty())
        {
            const HydraulicControlRuleAction &action = flow_rule->actions_then.first();
            context.expect(action.setting.valve_pressure_head_m.has_value(), "R_FLOW THEN must contain a PRV pressure setting");
            if (action.setting.valve_pressure_head_m.has_value())
            {
                context.expectNear(
                    action.setting.valve_pressure_head_m.value(),
                    28.137549042234017,
                    numeric_tolerance,
                    comparison("R_FLOW.then_pressure_head_m"));
            }
        }
        if (!flow_rule->actions_else.isEmpty())
        {
            const HydraulicControlRuleAction &action = flow_rule->actions_else.first();
            context.expect(action.status.has_value(), "R_FLOW ELSE must retain status action");
            if (action.status.has_value())
                context.expect(action.status.value() == HydraulicControlRuleStatus::Open, "R_FLOW ELSE must OPEN the valve");
        }
    }

    if (system_rule != nullptr)
    {
        context.expect(!system_rule->enabled, "R_SYSTEM must retain disabled state");
        context.expectNear(system_rule->priority, 1.0, numeric_tolerance, comparison("R_SYSTEM.priority"));
        context.expectEqual(
            static_cast<std::int64_t>(system_rule->premises.size()),
            std::int64_t{2}, comparison("R_SYSTEM.premise_count"));
        if (system_rule->premises.size() == 2)
        {
            const HydraulicControlRulePremise &demand = system_rule->premises.at(0);
            const HydraulicControlRulePremise &pressure = system_rule->premises.at(1);
            context.expect(demand.object == HydraulicControlRuleObject::System, "R_SYSTEM first premise must target SYSTEM");
            context.expect(demand.variable == HydraulicControlRuleVariable::Demand, "R_SYSTEM first premise must use DEMAND");
            context.expect(demand.demand_m3_per_h.has_value(), "R_SYSTEM demand premise must contain canonical flow");
            if (demand.demand_m3_per_h.has_value())
            {
                context.expectNear(
                    demand.demand_m3_per_h.value(),
                    1.1356167466150955,
                    numeric_tolerance,
                    comparison("R_SYSTEM.demand_m3_per_h"));
            }
            context.expect(pressure.logical_operator == HydraulicControlRuleLogicalOperator::And, "R_SYSTEM second premise must use AND");
            context.expect(pressure.object == HydraulicControlRuleObject::Node, "R_SYSTEM pressure premise must target a node");
            context.expect(pressure.variable == HydraulicControlRuleVariable::Pressure, "R_SYSTEM second premise must use PRESSURE");
            context.expectEqual(nodeIdForUuid(network, pressure.object_uuid).toStdString(), std::string("J1"), comparison("R_SYSTEM.node"));
            context.expect(pressure.pressure_head_m.has_value(), "R_SYSTEM pressure premise must contain canonical pressure head");
            if (pressure.pressure_head_m.has_value())
            {
                context.expectNear(
                    pressure.pressure_head_m.value(),
                    14.068774521117009,
                    numeric_tolerance,
                    comparison("R_SYSTEM.pressure_head_m"));
            }
        }
    }

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CONTROLS_RULES_US_INP);
    mapNativeControlIds(network, native_configuration);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(native_timeline.success, "native controls/rules fixture must solve successfully");
    context.expect(wrapper_run.status.success, "imported controls/rules fixture must solve successfully");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
}


void scenarioImportQualityChemicalCanonicalUnits(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_CHEMICAL_UG_L_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "chemical quality fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const WaterQualitySolverOptions &quality = result.request.quality_runs.constFirst();
    context.expect(
        quality.analysis == WaterQualityAnalysisType::Chemical,
        "CHEMICAL quality mode must map to the chemical analysis type");
    context.expectEqual(
        quality.chemical_name.toStdString(),
        std::string("Chlorine"),
        comparison("chemical_name"));
    context.expectNear(
        quality.chemical_tolerance_mg_per_l,
        0.005,
        numeric_tolerance,
        comparison("chemical_tolerance_mg_per_l"));
    context.expectNear(
        quality.relative_diffusivity,
        1.2,
        numeric_tolerance,
        comparison("relative_diffusivity"));
    context.expectEqual(
        static_cast<std::int64_t>(network.timestep_quality_s),
        std::int64_t{300},
        comparison("timestep_quality_s"));

    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction_1 = junctionById(network, QStringLiteral("J1"));
    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    const HydraulicNodeTank *tank = tankById(network, QStringLiteral("T1"));
    context.expect(
        reservoir != nullptr && junction_1 != nullptr && junction_2 != nullptr && tank != nullptr,
        "chemical quality fixture nodes must be imported");
    if (reservoir != nullptr)
        context.expectNear(reservoir->initial_chemical_concentration_mg_per_l, 2.0, numeric_tolerance, comparison("R1.initial_chemical_concentration_mg_per_l"));
    if (junction_1 != nullptr)
        context.expectNear(junction_1->initial_chemical_concentration_mg_per_l, 1.0, numeric_tolerance, comparison("J1.initial_chemical_concentration_mg_per_l"));
    if (junction_2 != nullptr)
        context.expectNear(junction_2->initial_chemical_concentration_mg_per_l, 0.5, numeric_tolerance, comparison("J2.initial_chemical_concentration_mg_per_l"));
    if (tank != nullptr)
        context.expectNear(tank->initial_chemical_concentration_mg_per_l, 0.25, numeric_tolerance, comparison("T1.initial_chemical_concentration_mg_per_l"));

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(context, native, run, WaterQualityAnalysisType::Chemical);
}

void scenarioImportQualitySourcesCanonicalUnits(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_SOURCES_UG_L_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "quality-source fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const HydraulicPatternTime *pattern = patternById(network, QStringLiteral("SRC"));
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *mass = junctionById(network, QStringLiteral("J1"));
    const HydraulicNodeJunction *flow_paced = junctionById(network, QStringLiteral("J2"));
    const HydraulicNodeTank *setpoint = tankById(network, QStringLiteral("T1"));
    context.expect(
        pattern != nullptr && reservoir != nullptr && mass != nullptr
            && flow_paced != nullptr && setpoint != nullptr,
        "quality-source fixture entities and source pattern must be imported");
    if (pattern == nullptr || reservoir == nullptr || mass == nullptr
        || flow_paced == nullptr || setpoint == nullptr)
    {
        return;
    }

    context.expect(
        reservoir->quality_source.type == HydraulicNodeQualitySourceType::Concentration,
        "R1 must import as a concentration source");
    context.expectNear(
        reservoir->quality_source.chemical_concentration_mg_per_l,
        1.5,
        numeric_tolerance,
        comparison("R1.quality_source.chemical_concentration_mg_per_l"));
    context.expectEqual(
        reservoir->quality_source.pattern_uuid.toString().toStdString(),
        pattern->uuid.toString().toStdString(),
        comparison("R1.quality_source.pattern_uuid"));

    context.expect(
        mass->quality_source.type == HydraulicNodeQualitySourceType::MassBooster,
        "J1 must import as a mass-booster source");
    context.expectNear(
        mass->quality_source.chemical_mass_flow_mg_per_min,
        12.0,
        numeric_tolerance,
        comparison("J1.quality_source.chemical_mass_flow_mg_per_min"));
    context.expectEqual(
        mass->quality_source.pattern_uuid.toString().toStdString(),
        pattern->uuid.toString().toStdString(),
        comparison("J1.quality_source.pattern_uuid"));

    context.expect(
        flow_paced->quality_source.type == HydraulicNodeQualitySourceType::FlowPacedBooster,
        "J2 must import as a flow-paced source");
    context.expectNear(
        flow_paced->quality_source.chemical_concentration_mg_per_l,
        0.35,
        numeric_tolerance,
        comparison("J2.quality_source.chemical_concentration_mg_per_l"));
    context.expect(
        flow_paced->quality_source.pattern_uuid.isNull(),
        "J2 source must retain the absence of a pattern");

    context.expect(
        setpoint->quality_source.type == HydraulicNodeQualitySourceType::SetpointBooster,
        "T1 must import as a setpoint source");
    context.expectNear(
        setpoint->quality_source.chemical_concentration_mg_per_l,
        0.8,
        numeric_tolerance,
        comparison("T1.quality_source.chemical_concentration_mg_per_l"));
    context.expect(
        setpoint->quality_source.pattern_uuid.isNull(),
        "T1 source must retain the absence of a pattern");

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(context, native, run, WaterQualityAnalysisType::Chemical);
}

void scenarioImportQualityMixingReactionsCanonicalUnits(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_MIXING_REACTIONS_UG_L_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "quality mixing/reaction fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const WaterQualityReactionOptions &reactions = network.options_reaction;

    context.expectNear(
        reactions.global_pipe_bulk_reaction.order,
        2.0,
        numeric_tolerance,
        comparison("global_pipe_bulk_reaction.order"));
    context.expectNear(
        reactions.global_pipe_bulk_reaction.coefficient,
        -0.0001,
        numeric_tolerance,
        comparison("global_pipe_bulk_reaction.coefficient"));
    context.expectNear(
        reactions.global_pipe_wall_reaction.order,
        0.0,
        numeric_tolerance,
        comparison("global_pipe_wall_reaction.order"));
    context.expectNear(
        reactions.global_pipe_wall_reaction.coefficient,
        -0.002,
        numeric_tolerance,
        comparison("global_pipe_wall_reaction.coefficient"));
    context.expectNear(
        reactions.global_tank_bulk_reaction.order,
        -1.0,
        numeric_tolerance,
        comparison("global_tank_bulk_reaction.order"));
    context.expectNear(
        reactions.global_tank_bulk_reaction.coefficient,
        -1.0e-10,
        numeric_tolerance,
        comparison("global_tank_bulk_reaction.coefficient"));
    context.expectNear(
        reactions.limiting_concentration_mg_per_l,
        5.0,
        numeric_tolerance,
        comparison("limiting_concentration_mg_per_l"));
    context.expectNear(
        reactions.roughness_reaction_factor,
        -0.01,
        numeric_tolerance,
        comparison("roughness_reaction_factor"));

    struct MixingExpectation
    {
        const char *id;
        HydraulicNodeTankMixingModel model;
        double fraction;
    };
    const std::array<MixingExpectation, 4> mixing_expectations = {{
        {"T1", HydraulicNodeTankMixingModel::CompleteMix, 1.0},
        {"T2", HydraulicNodeTankMixingModel::TwoCompartment, 0.60},
        {"T3", HydraulicNodeTankMixingModel::FirstInFirstOut, 1.0},
        {"T4", HydraulicNodeTankMixingModel::LastInFirstOut, 1.0}
    }};
    for (const MixingExpectation &expected : mixing_expectations)
    {
        const HydraulicNodeTank *tank = tankById(network, QString::fromLatin1(expected.id));
        context.expect(tank != nullptr, "every quality fixture tank must be imported");
        if (tank == nullptr)
            continue;
        context.expect(
            tank->mixing_model == expected.model,
            "tank mixing model must match the source INP");
        context.expectNear(
            tank->mixing_fraction,
            expected.fraction,
            numeric_tolerance,
            comparison(std::string(expected.id) + ".mixing_fraction"));
    }

    const HydraulicLinkPipe *bulk_override = pipeById(network, QStringLiteral("P1"));
    const HydraulicLinkPipe *wall_override = pipeById(network, QStringLiteral("P2"));
    const HydraulicLinkPipe *roughness_derived = pipeById(network, QStringLiteral("P3"));
    context.expect(
        bulk_override != nullptr && wall_override != nullptr && roughness_derived != nullptr,
        "reaction fixture pipes must be imported");
    if (bulk_override != nullptr)
    {
        context.expect(bulk_override->override_bulk_reaction, "P1 must retain its explicit bulk override");
        context.expect(!bulk_override->override_wall_reaction, "P1 must not gain a wall override");
        context.expectNear(
            bulk_override->bulk_reaction.coefficient,
            -0.0002,
            numeric_tolerance,
            comparison("P1.bulk_reaction.coefficient"));
        context.expectNear(
            bulk_override->bulk_reaction.order,
            2.0,
            numeric_tolerance,
            comparison("P1.bulk_reaction.order"));
    }
    if (wall_override != nullptr)
    {
        context.expect(!wall_override->override_bulk_reaction, "P2 must not gain a bulk override");
        context.expect(wall_override->override_wall_reaction, "P2 must retain its explicit wall override");
        context.expectNear(
            wall_override->wall_reaction.coefficient,
            -0.004,
            numeric_tolerance,
            comparison("P2.wall_reaction.coefficient"));
        context.expectNear(
            wall_override->wall_reaction.order,
            0.0,
            numeric_tolerance,
            comparison("P2.wall_reaction.order"));
    }
    if (roughness_derived != nullptr)
    {
        context.expect(!roughness_derived->override_wall_reaction, "P3 must remain roughness-derived");
        context.expectNear(
            roughness_derived->wall_reaction.coefficient,
            -0.0001,
            numeric_tolerance,
            comparison("P3.wall_reaction.coefficient"));
    }

    const HydraulicNodeTank *global_tank = tankById(network, QStringLiteral("T1"));
    const HydraulicNodeTank *override_tank_2 = tankById(network, QStringLiteral("T2"));
    const HydraulicNodeTank *override_tank_3 = tankById(network, QStringLiteral("T3"));
    const HydraulicNodeTank *global_tank_4 = tankById(network, QStringLiteral("T4"));
    context.expect(
        global_tank != nullptr && override_tank_2 != nullptr
            && override_tank_3 != nullptr && global_tank_4 != nullptr,
        "reaction fixture tanks must be imported");
    if (global_tank != nullptr)
    {
        context.expect(!global_tank->override_bulk_reaction, "T1 must use the global tank reaction");
        context.expectNear(
            global_tank->bulk_reaction.coefficient,
            -1.0e-10,
            numeric_tolerance,
            comparison("T1.bulk_reaction.coefficient"));
        context.expectNear(
            global_tank->bulk_reaction.order,
            -1.0,
            numeric_tolerance,
            comparison("T1.bulk_reaction.order"));
    }
    for (const HydraulicNodeTank *tank : {override_tank_2, override_tank_3})
    {
        if (tank == nullptr)
            continue;
        context.expect(tank->override_bulk_reaction, "T2-T3 range must import as tank bulk overrides");
        context.expectNear(
            tank->bulk_reaction.coefficient,
            -0.05,
            numeric_tolerance,
            comparison(tank->id.toStdString() + ".bulk_reaction.coefficient"));
        context.expectNear(
            tank->bulk_reaction.order,
            -1.0,
            numeric_tolerance,
            comparison(tank->id.toStdString() + ".bulk_reaction.order"));
    }
    if (global_tank_4 != nullptr)
        context.expect(!global_tank_4->override_bulk_reaction, "T4 must use the global tank reaction");

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(
        context, native, run, WaterQualityAnalysisType::Chemical, NumericTolerance{5.0e-6, 5.0e-6});

    const QString us_wall_input = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_WALL_REACTION_US_INP);
    const EpanetResultImport us_wall_result = EpanetRunner().importInp(us_wall_input);
    context.expect(us_wall_result.status.success, "US zero-order wall-reaction fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, us_wall_result);
    context.expectEqual(
        static_cast<std::int64_t>(us_wall_result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("us_wall.quality_runs.size"));
    if (us_wall_result.request.quality_runs.size() == 1)
    {
        const NetworkHydraulic &us_wall_network = us_wall_result.request.network;
        context.expectNear(
            us_wall_network.options_reaction.global_pipe_wall_reaction.order,
            0.0,
            numeric_tolerance,
            comparison("us_wall.global_pipe_wall_reaction.order"));
        context.expectNear(
            us_wall_network.options_reaction.global_pipe_wall_reaction.coefficient,
            -0.5381955208354862,
            numeric_tolerance,
            comparison("us_wall.global_pipe_wall_reaction.coefficient"));

        const AowisEpanetTests::NativeQualityReferenceTimeline us_wall_native =
            AowisEpanetTests::runNativeQualityReference(us_wall_input, us_wall_network);
        const EpanetResultRun us_wall_run = EpanetRunner().run(us_wall_result.request);
        compareImportedQualityTimeline(
            context, us_wall_native, us_wall_run, WaterQualityAnalysisType::Chemical);
    }
}

void scenarioImportQualityWaterAge(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_AGE_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "water-age quality fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const WaterQualitySolverOptions &quality = result.request.quality_runs.constFirst();
    context.expect(
        quality.analysis == WaterQualityAnalysisType::WaterAge,
        "AGE quality mode must map to the water-age analysis type");
    context.expectNear(
        quality.water_age_tolerance_h,
        0.025,
        numeric_tolerance,
        comparison("water_age_tolerance_h"));
    context.expectEqual(
        static_cast<std::int64_t>(network.timestep_quality_s),
        std::int64_t{300},
        comparison("timestep_quality_s"));

    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction_1 = junctionById(network, QStringLiteral("J1"));
    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    const HydraulicNodeTank *tank = tankById(network, QStringLiteral("T1"));
    context.expect(
        reservoir != nullptr && junction_1 != nullptr && junction_2 != nullptr && tank != nullptr,
        "water-age quality fixture nodes must be imported");
    if (reservoir != nullptr)
        context.expectNear(reservoir->initial_water_age_h, 1.0, numeric_tolerance, comparison("R1.initial_water_age_h"));
    if (junction_1 != nullptr)
        context.expectNear(junction_1->initial_water_age_h, 2.0, numeric_tolerance, comparison("J1.initial_water_age_h"));
    if (junction_2 != nullptr)
        context.expectNear(junction_2->initial_water_age_h, 3.0, numeric_tolerance, comparison("J2.initial_water_age_h"));
    if (tank != nullptr)
        context.expectNear(tank->initial_water_age_h, 4.0, numeric_tolerance, comparison("T1.initial_water_age_h"));

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(context, native, run, WaterQualityAnalysisType::WaterAge);
}

void scenarioImportQualitySourceTrace(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_QUALITY_TRACE_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "source-trace quality fixture must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const WaterQualitySolverOptions &quality = result.request.quality_runs.constFirst();
    context.expect(
        quality.analysis == WaterQualityAnalysisType::SourceTrace,
        "TRACE quality mode must map to the source-trace analysis type");
    context.expectEqual(
        nodeIdForUuid(network, quality.trace_node_uuid).toStdString(),
        std::string("J1"),
        comparison("trace_node"));
    context.expectNear(
        quality.source_trace_tolerance_percent,
        0.05,
        numeric_tolerance,
        comparison("source_trace_tolerance_percent"));
    context.expectNear(
        quality.relative_diffusivity,
        0.05,
        numeric_tolerance,
        comparison("relative_diffusivity"));
    context.expectEqual(
        static_cast<std::int64_t>(network.timestep_quality_s),
        std::int64_t{300},
        comparison("timestep_quality_s"));

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(context, native, run, WaterQualityAnalysisType::SourceTrace);
    AowisEpanetTests::proveInpRoundTrip(context, input_file);
}

void scenarioImportQualityNet1Equivalence(TestContext &context)
{
    const QString input_file = QStringLiteral(AOWIS_EPANET_TEST_NET1_INP);
    const EpanetResultImport result = EpanetRunner().importInp(input_file);
    context.expect(result.status.success, "upstream Net1 quality proof must import successfully");
    expectNoDeferredQualityImportDiagnostics(context, result);
    context.expectEqual(
        static_cast<std::int64_t>(result.request.quality_runs.size()),
        std::int64_t{1},
        comparison("quality_runs.size"));
    if (result.request.quality_runs.size() != 1)
        return;

    const NetworkHydraulic &network = result.request.network;
    const WaterQualitySolverOptions &quality = result.request.quality_runs.constFirst();
    context.expect(
        quality.analysis == WaterQualityAnalysisType::Chemical,
        "upstream Net1 must retain its CHEMICAL water-quality analysis");

    const WaterQualityReactionOptions &reactions = network.options_reaction;
    context.expectNear(
        reactions.global_pipe_wall_reaction.order,
        1.0,
        numeric_tolerance,
        comparison("global_pipe_wall_reaction.order"));
    context.expectNear(
        reactions.global_pipe_wall_reaction.coefficient,
        -0.3048,
        numeric_tolerance,
        comparison("global_pipe_wall_reaction.coefficient"));

    const AowisEpanetTests::NativeQualityReferenceTimeline native =
        AowisEpanetTests::runNativeQualityReference(input_file, network);
    const EpanetResultRun run = EpanetRunner().run(result.request);
    compareImportedQualityTimeline(context, native, run, WaterQualityAnalysisType::Chemical);
}

void scenarioImportGeometryDegrees(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_DEGREES_INP));
    context.expect(result.status.success, "degree geometry fixture must import successfully");
    context.expect(result.source_geometry.units == EpanetImportMapUnits::Degrees,
        "degree geometry source units must be retained");
    context.expect(!result.source_geometry.epsg_code_declared,
        "degree fixture must remain explicitly CRS-unspecified");
    context.expect(result.source_geometry.georeferenced,
        "valid degree geometry must be usable on the geographic map");
    context.expect(!result.source_geometry.transformed_to_wgs84,
        "CRS-unspecified degree geometry is interpreted directly rather than projected");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    const HydraulicLinkPipe *pipe = pipeById(network, QStringLiteral("P1"));
    context.expect(reservoir != nullptr && junction != nullptr && pipe != nullptr,
        "degree geometry fixture entities must be imported");
    if (reservoir == nullptr || junction == nullptr || pipe == nullptr)
        return;

    context.expectNear(reservoir->coordinate_wgs84.longitude_deg, 7.0, numeric_tolerance,
        comparison("R1.longitude_deg"));
    context.expectNear(reservoir->coordinate_wgs84.latitude_deg, 50.0, numeric_tolerance,
        comparison("R1.latitude_deg"));
    context.expectNear(junction->coordinate_wgs84.longitude_deg, 7.01, numeric_tolerance,
        comparison("J1.longitude_deg"));
    context.expectNear(junction->coordinate_wgs84.latitude_deg, 50.0, numeric_tolerance,
        comparison("J1.latitude_deg"));
    context.expectEqual(static_cast<std::int64_t>(pipe->vertices.size()), std::int64_t{1},
        comparison("P1.vertices.size"));
    if (!pipe->vertices.isEmpty())
    {
        context.expectNear(pipe->vertices.first().coordinate_wgs84.longitude_deg, 7.005,
            numeric_tolerance, comparison("P1.vertex.longitude_deg"));
        context.expectNear(pipe->vertices.first().coordinate_wgs84.latitude_deg, 50.001,
            numeric_tolerance, comparison("P1.vertex.latitude_deg"));
    }

    context.expectEqual(static_cast<std::int64_t>(network.map_labels.size()), std::int64_t{1},
        comparison("map_labels.size"));
    if (!network.map_labels.isEmpty())
    {
        const HydraulicMapLabel &label = network.map_labels.first();
        context.expectEqual(label.text.toStdString(), std::string("Pump Area"), comparison("label.text"));
        context.expect(label.anchor_node_uuid == junction->uuid,
            "map label anchor must resolve to the imported junction UUID");
    }
    context.expect(network.map_backdrop.enabled, "degree backdrop must be imported");
    context.expectEqual(network.map_backdrop.file.toStdString(), std::string("map.png"),
        comparison("map_backdrop.file"));
    context.expectNear(network.map_backdrop.offset_longitude_deg, 0.001, numeric_tolerance,
        comparison("map_backdrop.offset_longitude_deg"));
    context.expectNear(network.map_backdrop.offset_latitude_deg, 0.002, numeric_tolerance,
        comparison("map_backdrop.offset_latitude_deg"));
    context.expect(hasDiagnosticContaining(result, QStringLiteral("interpreted as WGS84")),
        "degree import must document its WGS84 interpretation");
}

void scenarioImportGeometryEpsg3089(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_EPSG3089_INP));
    context.expect(result.status.success, "EPSG:3089 geometry fixture must import successfully");
    context.expect(result.complete, "supported EPSG:3089 geometry must import completely");

    const EpanetImportSourceGeometry &source = result.source_geometry;
    context.expect(source.units == EpanetImportMapUnits::Feet,
        "EPSG:3089 source map units must remain recorded as feet");
    context.expect(source.epsg_code_declared, "EPSG:3089 source must retain its explicit CRS marker");
    context.expectEqual(static_cast<std::int64_t>(source.epsg_code), std::int64_t{3089},
        comparison("source_geometry.epsg_code"));
    context.expect(source.georeferenced, "supported EPSG geometry must be marked georeferenced");
    context.expect(source.transformed_to_wgs84, "projected EPSG geometry must be marked as transformed to WGS84");
    context.expect(source.coordinate_reference_name.contains(QStringLiteral("Kentucky")),
        "EPSG:3089 source CRS name must be retained");
    context.expect(source.node_coordinates.contains(QStringLiteral("R1")),
        "original EPSG:3089 reservoir coordinate must be retained");
    if (source.node_coordinates.contains(QStringLiteral("R1")))
    {
        const EpanetImportSourcePoint point = source.node_coordinates.value(QStringLiteral("R1"));
        context.expectNear(point.x, 4971350.0, numeric_tolerance, comparison("source.R1.x"));
        context.expectNear(point.y, 3905604.0, numeric_tolerance, comparison("source.R1.y"));
    }
    context.expect(source.link_vertices.contains(QStringLiteral("P1")),
        "original EPSG:3089 link vertices must be retained");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    const HydraulicLinkPipe *pipe = pipeById(network, QStringLiteral("P1"));
    context.expect(reservoir != nullptr && junction != nullptr && pipe != nullptr,
        "EPSG:3089 geometry fixture entities must be imported");
    if (reservoir == nullptr || junction == nullptr || pipe == nullptr)
        return;

    context.expectNear(reservoir->coordinate_wgs84.longitude_deg, -85.576008643001,
        numeric_tolerance, comparison("R1.longitude_deg"));
    context.expectNear(reservoir->coordinate_wgs84.latitude_deg, 38.049062826866,
        numeric_tolerance, comparison("R1.latitude_deg"));
    context.expectNear(junction->coordinate_wgs84.longitude_deg, -85.6445120695213,
        numeric_tolerance, comparison("J1.longitude_deg"));
    context.expectNear(junction->coordinate_wgs84.latitude_deg, 38.0193468587353,
        numeric_tolerance, comparison("J1.latitude_deg"));
    context.expectEqual(static_cast<std::int64_t>(pipe->vertices.size()), std::int64_t{1},
        comparison("P1.vertices.size"));
    if (!pipe->vertices.isEmpty())
    {
        context.expectNear(pipe->vertices.first().coordinate_wgs84.longitude_deg, -85.6102672510135,
            numeric_tolerance, comparison("P1.vertex.longitude_deg"));
        context.expectNear(pipe->vertices.first().coordinate_wgs84.latitude_deg, 38.0342098384877,
            numeric_tolerance, comparison("P1.vertex.latitude_deg"));
    }

    context.expectEqual(static_cast<std::int64_t>(network.map_labels.size()), std::int64_t{1},
        comparison("map_labels.size"));
    if (!network.map_labels.isEmpty())
    {
        const HydraulicMapLabel &label = network.map_labels.first();
        context.expectNear(label.coordinate_wgs84.longitude_deg, -85.5108617949970,
            numeric_tolerance, comparison("label.longitude_deg"));
        context.expectNear(label.coordinate_wgs84.latitude_deg, 38.0565065341553,
            numeric_tolerance, comparison("label.latitude_deg"));
    }
    context.expect(network.map_backdrop.enabled, "EPSG:3089 backdrop must be imported");
    context.expectNear(network.map_backdrop.lower_left_wgs84.longitude_deg, -85.7173919677734,
        numeric_tolerance, comparison("backdrop.lower_left.longitude_deg"));
    context.expectNear(network.map_backdrop.lower_left_wgs84.latitude_deg, 37.9274092226777,
        numeric_tolerance, comparison("backdrop.lower_left.latitude_deg"));
    context.expectNear(network.map_backdrop.upper_right_wgs84.longitude_deg, -85.4585266113278,
        numeric_tolerance, comparison("backdrop.upper_right.longitude_deg"));
    context.expectNear(network.map_backdrop.upper_right_wgs84.latitude_deg, 38.1561569699247,
        numeric_tolerance, comparison("backdrop.upper_right.latitude_deg"));
    context.expect(hasDiagnosticContaining(result, QStringLiteral("explicit EPSG")),
        "EPSG:3089 import must document its CRS transformation");
}

void scenarioImportGeometryUnknownEpsg(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_UNKNOWN_EPSG_INP));
    context.expect(result.status.success, "unsupported-EPSG geometry fixture must remain importable");
    context.expect(!result.complete, "unsupported EPSG geometry must mark the import incomplete");
    context.expect(result.source_geometry.epsg_code_declared,
        "unsupported EPSG code must still be retained");
    context.expectEqual(static_cast<std::int64_t>(result.source_geometry.epsg_code),
        std::int64_t{999999}, comparison("source_geometry.epsg_code"));
    context.expect(!result.source_geometry.georeferenced,
        "unsupported EPSG geometry must not be claimed as georeferenced");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    context.expect(reservoir != nullptr && junction != nullptr,
        "unsupported-EPSG fixture nodes must be imported");
    if (reservoir == nullptr || junction == nullptr)
        return;

    context.expectNear(
        greatCircleDistanceM(reservoir->coordinate_wgs84, junction->coordinate_wgs84),
        1000.0,
        NumericTolerance{0.05, 0.0},
        comparison("null-island-distance"));
    context.expect(std::abs((reservoir->coordinate_wgs84.longitude_deg + junction->coordinate_wgs84.longitude_deg) / 2.0) < 1.0e-8,
        "unsupported EPSG fallback must remain centered on Null Island longitude");
    context.expect(std::abs((reservoir->coordinate_wgs84.latitude_deg + junction->coordinate_wgs84.latitude_deg) / 2.0) < 1.0e-8,
        "unsupported EPSG fallback must remain centered on Null Island latitude");
    context.expect(hasDiagnosticContaining(result, QStringLiteral("does not support")),
        "unsupported EPSG import must explain that no projection was guessed");
}

void scenarioImportGeometryMeters(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_METERS_INP));
    context.expect(result.status.success, "metric geometry fixture must import successfully");
    context.expect(result.source_geometry.units == EpanetImportMapUnits::Meters,
        "metric source units must be retained");
    context.expect(!result.source_geometry.georeferenced,
        "metric geometry without a CRS must remain explicitly unreferenced");
    context.expect(result.source_geometry.node_coordinates.contains(QStringLiteral("R1")),
        "original metric coordinates must be retained alongside Null Island geometry");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    const HydraulicLinkPipe *pipe = pipeById(network, QStringLiteral("P1"));
    context.expect(reservoir != nullptr && junction != nullptr && pipe != nullptr,
        "metric geometry fixture entities must be imported");
    if (reservoir == nullptr || junction == nullptr || pipe == nullptr)
        return;

    context.expectNear(
        greatCircleDistanceM(reservoir->coordinate_wgs84, junction->coordinate_wgs84),
        1000.0, NumericTolerance{0.2, 0.0}, comparison("metric_node_distance_m"));
    context.expectNear(
        (reservoir->coordinate_wgs84.longitude_deg + junction->coordinate_wgs84.longitude_deg) / 2.0,
        0.0, NumericTolerance{1.0e-8, 0.0}, comparison("metric_center_longitude_deg"));
    context.expectEqual(static_cast<std::int64_t>(pipe->vertices.size()), std::int64_t{1},
        comparison("metric_vertices.size"));
    context.expect(hasDiagnosticContaining(result, QStringLiteral("centered at WGS84 0")),
        "metric geometry import must document synthetic WGS84 placement");
}

void scenarioImportGeometryFeet(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_FEET_INP));
    context.expect(result.status.success, "feet geometry fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    context.expect(reservoir != nullptr && junction != nullptr,
        "feet geometry fixture nodes must be imported");
    if (reservoir == nullptr || junction == nullptr)
        return;

    context.expectNear(
        greatCircleDistanceM(reservoir->coordinate_wgs84, junction->coordinate_wgs84),
        304.8, NumericTolerance{0.1, 0.0}, comparison("feet_node_distance_m"));
    context.expect(hasDiagnosticContaining(result, QStringLiteral("feet was converted to metres")),
        "feet geometry import must document unit conversion");
}

void scenarioImportGeometryArbitrary(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_ARBITRARY_INP));
    context.expect(result.status.success, "arbitrary geometry fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    context.expect(reservoir != nullptr && junction != nullptr,
        "arbitrary geometry fixture nodes must be imported");
    if (reservoir == nullptr || junction == nullptr)
        return;

    context.expectNear(
        greatCircleDistanceM(reservoir->coordinate_wgs84, junction->coordinate_wgs84),
        1000.0, NumericTolerance{0.2, 0.0}, comparison("arbitrary_node_distance_m"));
    context.expect(hasDiagnosticContaining(result, QStringLiteral("one map unit as one metre")),
        "arbitrary geometry import must document its synthetic scale convention");
}

void scenarioImportGeometryMissingCoordinates(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GEOMETRY_MISSING_INP));
    context.expect(result.status.success, "coordinate-free geometry fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicNodeJunction *junction_1 = junctionById(network, QStringLiteral("J1"));
    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    context.expect(reservoir != nullptr && junction_1 != nullptr && junction_2 != nullptr,
        "coordinate-free fixture nodes must be imported");
    if (reservoir == nullptr || junction_1 == nullptr || junction_2 == nullptr)
        return;

    const bool all_same =
        reservoir->coordinate_wgs84.longitude_deg == junction_1->coordinate_wgs84.longitude_deg
        && reservoir->coordinate_wgs84.latitude_deg == junction_1->coordinate_wgs84.latitude_deg
        && reservoir->coordinate_wgs84.longitude_deg == junction_2->coordinate_wgs84.longitude_deg
        && reservoir->coordinate_wgs84.latitude_deg == junction_2->coordinate_wgs84.latitude_deg;
    context.expect(!all_same, "coordinate-free nodes must receive distinct generated map positions");
    context.expect(std::abs(reservoir->coordinate_wgs84.longitude_deg) < 0.01
            && std::abs(reservoir->coordinate_wgs84.latitude_deg) < 0.01,
        "generated geometry must remain near the synthetic 0°,0° origin");
    context.expect(hasDiagnosticContaining(result, QStringLiteral("deterministic schematic WGS84 layout")),
        "coordinate-free import must document generated geometry");
}

void scenarioImportMetadataReportFidelity(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_METADATA_REPORT_US_INP));

    context.expect(result.status.success, "metadata/report fixture must import successfully");
    context.expect(result.complete, "fully representable metadata/report fixture must import completely");
    const NetworkHydraulic &network = result.request.network;

    const HydraulicNodeJunction *junction = junctionById(network, QStringLiteral("J1"));
    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    const HydraulicLinkPipe *pipe = pipeById(network, QStringLiteral("P1"));
    context.expect(junction != nullptr, "metadata fixture junction must be imported");
    context.expect(reservoir != nullptr, "metadata fixture reservoir must be imported");
    context.expect(pipe != nullptr, "metadata fixture pipe must be imported");
    if (junction != nullptr)
    {
        context.expectEqual(
            junction->metadata.comment.toStdString(), std::string("Junction comment"),
            comparison("J1.comment"));
        context.expectEqual(
            junction->metadata.tag.toStdString(), std::string("JunctionTag"),
            comparison("J1.tag"));
    }
    if (reservoir != nullptr)
    {
        context.expectEqual(
            reservoir->metadata.comment.toStdString(), std::string("Reservoir comment"),
            comparison("R1.comment"));
        context.expectEqual(
            reservoir->metadata.tag.toStdString(), std::string("ReservoirTag"),
            comparison("R1.tag"));
    }
    if (pipe != nullptr)
    {
        context.expectEqual(
            pipe->metadata.comment.toStdString(), std::string("Pipe comment"),
            comparison("P1.comment"));
        context.expectEqual(
            pipe->metadata.tag.toStdString(), std::string("PipeTag"),
            comparison("P1.tag"));
    }

    const HydraulicSimulationReportOptions &report = network.options_report;
    context.expectEqual(
        static_cast<std::int64_t>(report.page_size), std::int64_t{77},
        comparison("report.page_size"));
    context.expectEqual(
        static_cast<std::int64_t>(report.status),
        static_cast<std::int64_t>(HydraulicSimulationReportStatus::Full),
        comparison("report.status"));
    context.expect(!report.summary, "report SUMMARY NO must be imported");
    context.expect(!report.messages, "report MESSAGES NO must be imported");
    context.expect(report.energy, "report ENERGY YES must be imported");

    context.expect(
        report.selection_nodes.mode == HydraulicSimulationReportSelectionMode::Selected,
        "selected report nodes must retain Selected mode");
    context.expectEqual(
        static_cast<std::int64_t>(report.selection_nodes.uuids.size()), std::int64_t{2},
        comparison("report.selection_nodes.size"));
    if (report.selection_nodes.uuids.size() == 2)
    {
        const QString first = nodeIdForUuid(network, report.selection_nodes.uuids.at(0));
        const QString second = nodeIdForUuid(network, report.selection_nodes.uuids.at(1));
        context.expect(
            (first == QStringLiteral("J1") && second == QStringLiteral("R1"))
                || (first == QStringLiteral("R1") && second == QStringLiteral("J1")),
            "selected report node UUIDs must resolve to J1 and R1");
    }
    context.expect(
        report.selection_links.mode == HydraulicSimulationReportSelectionMode::Selected,
        "selected report links must retain Selected mode");
    context.expectEqual(
        static_cast<std::int64_t>(report.selection_links.uuids.size()), std::int64_t{1},
        comparison("report.selection_links.size"));
    if (!report.selection_links.uuids.isEmpty())
    {
        context.expectEqual(
            linkIdForUuid(network, report.selection_links.uuids.first()).toStdString(),
            std::string("P1"), comparison("report.selection_links.first"));
    }

    context.expect(report.fields_node.elevation.enabled, "ELEVATION YES must be imported");
    context.expectEqual(
        static_cast<std::int64_t>(report.fields_node.elevation.precision.value_or(-1)),
        std::int64_t{3}, comparison("report.elevation.precision"));
    context.expectNear(
        report.fields_node.elevation.below_m.value_or(-1.0), 90.0 * 0.3048,
        numeric_tolerance, comparison("report.elevation.below_m"));
    context.expectNear(
        report.fields_node.elevation.above_m.value_or(-1.0), 150.0 * 0.3048,
        numeric_tolerance, comparison("report.elevation.above_m"));

    context.expectEqual(
        static_cast<std::int64_t>(report.fields_node.demand.precision.value_or(-1)),
        std::int64_t{4}, comparison("report.demand.precision"));
    context.expectNear(
        report.fields_node.demand.below_m3_per_h.value_or(-1.0),
        10.0 * epanet_gpm_to_cmh, numeric_tolerance,
        comparison("report.demand.below_m3_per_h"));
    context.expectNear(
        report.fields_node.demand.above_m3_per_h.value_or(-1.0),
        50.0 * epanet_gpm_to_cmh, numeric_tolerance,
        comparison("report.demand.above_m3_per_h"));

    context.expectEqual(
        static_cast<std::int64_t>(report.fields_node.head.precision.value_or(-1)),
        std::int64_t{5}, comparison("report.head.precision"));
    context.expectNear(
        report.fields_node.head.below_m.value_or(-1.0), 200.0 * 0.3048,
        numeric_tolerance, comparison("report.head.below_m"));

    context.expectEqual(
        static_cast<std::int64_t>(report.fields_node.pressure.precision.value_or(-1)),
        std::int64_t{6}, comparison("report.pressure.precision"));
    const double pressure_to_head_m = 0.3048 / (0.4333 * 1.2);
    context.expectNear(
        report.fields_node.pressure.below_m.value_or(-1.0), 20.0 * pressure_to_head_m,
        numeric_tolerance, comparison("report.pressure.below_m"));
    context.expect(!report.fields_node.quality.enabled, "QUALITY NO must be imported");

    context.expect(report.fields_link.length.enabled, "LENGTH YES must be imported");
    context.expectNear(
        report.fields_link.length.below_m.value_or(-1.0), 500.0 * 0.3048,
        numeric_tolerance, comparison("report.length.below_m"));
    context.expect(report.fields_link.diameter.enabled, "DIAMETER YES must be imported");
    context.expectNear(
        report.fields_link.diameter.below_mm.value_or(-1.0), 254.0,
        numeric_tolerance, comparison("report.diameter.below_mm"));
    context.expectNear(
        report.fields_link.flow.below_m3_per_h.value_or(-1.0),
        100.0 * epanet_gpm_to_cmh, numeric_tolerance,
        comparison("report.flow.below_m3_per_h"));
    context.expectNear(
        report.fields_link.velocity.below_m_per_s.value_or(-1.0), 2.0 * 0.3048,
        numeric_tolerance, comparison("report.velocity.below_m_per_s"));
    context.expectNear(
        report.fields_link.headloss.below_m_per_km.value_or(-1.0), 5.0,
        numeric_tolerance, comparison("report.headloss.below_m_per_km"));
    context.expect(report.fields_link.position.enabled, "STATE YES must map to the AOWIS link-position report field");
    context.expectEqual(
        static_cast<std::int64_t>(report.fields_link.position.precision.value_or(-1)),
        std::int64_t{0}, comparison("report.state.precision"));
    context.expect(report.fields_link.setting.enabled, "SETTING YES must be imported");
    context.expectEqual(
        static_cast<std::int64_t>(report.fields_link.setting.precision.value_or(-1)),
        std::int64_t{2}, comparison("report.setting.precision"));
    context.expect(report.fields_link.reaction.enabled, "REACTION YES must be imported");
    context.expectEqual(
        static_cast<std::int64_t>(report.fields_link.reaction.precision.value_or(-1)),
        std::int64_t{3}, comparison("report.reaction.precision"));
    context.expect(report.fields_link.friction.enabled, "F-FACTOR YES must be imported");
    context.expectEqual(
        static_cast<std::int64_t>(report.fields_link.friction.precision.value_or(-1)),
        std::int64_t{4}, comparison("report.friction.precision"));
    context.expectNear(
        report.fields_link.friction.below_friction_factor.value_or(-1.0), 0.01,
        numeric_tolerance, comparison("report.friction.below"));

    context.expect(
        report.backend_commands.contains(QStringLiteral("STATE BELOW 1")),
        "STATE threshold must be retained through backend report commands");
    context.expect(
        report.backend_commands.contains(QStringLiteral("REACTION BELOW 0.02")),
        "REACTION threshold must be retained through backend report commands");
    bool file_command_found = false;
    for (const QString &command : report.backend_commands)
    {
        if (command.startsWith(QStringLiteral("File"), Qt::CaseInsensitive)
            && command.contains(QStringLiteral("report-output.rpt")))
        {
            file_command_found = true;
        }
    }
    context.expect(file_command_found, "REPORT FILE destination must be retained as a backend command");

    const EpanetResultInp exported = EpanetRunner().retrieveInp(result.request);
    context.expect(exported.status.success, "imported metadata/report configuration must export successfully");
    if (exported.status.success)
    {
        context.expect(
            exported.inp_text.contains(QStringLiteral("Junction comment")),
            "imported junction comment must survive generated INP export");
        context.expect(
            exported.inp_text.contains(QStringLiteral("JunctionTag")),
            "imported junction tag must survive generated INP export");
        context.expect(
            exported.inp_text.contains(QStringLiteral("STATE BELOW 1"), Qt::CaseInsensitive),
            "generic STATE report threshold must survive generated INP export");
        context.expect(
            exported.inp_text.contains(QStringLiteral("REACTION BELOW 0.02"), Qt::CaseInsensitive),
            "generic REACTION report threshold must survive generated INP export");
        context.expect(
            exported.inp_text.contains(QStringLiteral("report-output.rpt"), Qt::CaseInsensitive),
            "REPORT FILE destination must survive generated INP export");
    }
}

void scenarioImportReportSettingThresholdCanonical(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_REPORT_SETTING_THRESHOLD_CANONICAL_INP));

    context.expect(result.status.success, "canonical SETTING-threshold fixture must import successfully");
    context.expect(result.complete, "canonical SETTING report threshold must import completely");
    context.expect(
        result.request.network.options_report.backend_commands.contains(
            QStringLiteral("SETTING BELOW 5")),
        "canonical SETTING threshold must be retained exactly as a backend report command");

    const EpanetResultInp exported = EpanetRunner().retrieveInp(result.request);
    context.expect(exported.status.success, "canonical SETTING threshold must export successfully");
    if (exported.status.success)
    {
        context.expect(
            exported.inp_text.contains(QStringLiteral("SETTING BELOW 5"), Qt::CaseInsensitive),
            "canonical SETTING threshold must survive generated INP export");
    }
}

void scenarioImportReportSettingThresholdDiagnostic(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_REPORT_SETTING_THRESHOLD_US_INP));

    context.expect(result.status.success, "SETTING-threshold fixture must still import successfully");
    context.expect(!result.complete, "non-canonical SETTING report threshold must mark import incomplete");
    context.expect(
        hasDiagnosticContaining(result, QStringLiteral("SETTING report threshold")),
        "lossy SETTING threshold must produce an explicit import diagnostic");

    bool setting_threshold_preserved = false;
    for (const QString &command : result.request.network.options_report.backend_commands)
    {
        if (command.startsWith(QStringLiteral("SETTING BELOW"), Qt::CaseInsensitive))
            setting_threshold_preserved = true;
    }
    context.expect(
        !setting_threshold_preserved,
        "non-canonical SETTING threshold must not be silently replayed with incorrect units");
}

void scenarioImportOpenErrorDiagnostic(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral("/this/path/does/not/exist/aowis-import-missing.inp"));

    context.expect(!result.status.success, "missing INP must fail import");
    context.expect(!result.complete, "failed import cannot be complete");
    context.expect(result.status.stage == HydraulicSimulationStatusStage::OpenInput, "missing INP must identify the input-open stage");
    context.expect(result.status.operation == HydraulicSimulationStatusOperation::OpenInput, "missing INP must identify the input-open operation");
    context.expectEqual(result.status.backend_operation.toStdString(), std::string("EN_open"), comparison("backend_operation"));
    context.expect(result.status.backend_error_code != 0, "missing INP must retain the native EPANET error code");
    context.expect(!result.diagnostics.isEmpty(), "missing INP must retain a structured import diagnostic");
}
}

namespace AowisEpanetTests
{
void registerInpImportScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-import-project-globals-net1",
        "Opens the upstream Net1 INP and reconstructs project titles, timing, hydraulic globals, energy globals, and report status.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportNet1ProjectGlobals});
    registry.add(ScenarioDefinition{
        "conformance-import-project-globals-canonical-units",
        "Imports non-canonical EPANET source units and verifies canonical AOWIS pressure-head, head-error, and flow-change values.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCanonicalGlobalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-core-topology-net1",
        "Reconstructs Net1 junctions, reservoir, tank, and pipes with endpoint references and canonical US-unit conversion.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCoreTopologyNet1});
    registry.add(ScenarioDefinition{
        "conformance-import-core-topology-canonical-units",
        "Imports demands, emitter data, tank geometry, pipe status, Darcy-Weisbach roughness, and leakage from US customary source units into canonical AOWIS fields.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCoreTopologyCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-hydraulic-assets-net1",
        "Imports Net1 patterns, pump head curve, and pump references with UUID-resolved reference integrity.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportHydraulicAssetsNet1});
    registry.add(ScenarioDefinition{
        "conformance-import-patterns-curves-pumps-canonical-units",
        "Imports time patterns, typed curves, tank-volume references, curve/constant-power pumps, efficiency, speed, and energy inputs from US units into canonical AOWIS fields.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportPatternsCurvesPumpsCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-valves-canonical-units",
        "Imports all seven EPANET valve families, canonical settings, statuses, and GPV/PCV curve references.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportValvesCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-controls-net1-equivalence",
        "Imports both Net1 simple controls and restores full native-vs-imported Net1 hydraulic equivalence with stable control-event identity.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportControlsNet1Equivalence});
    registry.add(ScenarioDefinition{
        "conformance-import-structured-rules-canonical-units",
        "Imports low/high/timer/time-of-day controls with junction/tank/reservoir triggers, exact GPV and pump action intent, plus structured IF/AND/OR rules, THEN/ELSE actions, priorities, enabled state, and canonical rule/control quantities.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportStructuredRulesCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-chemical-canonical-units",
        "Imports CHEMICAL quality configuration and initial node quality, converting documented ug/L source values to canonical AOWIS mg/L, and proves native quality equivalence.",
        {"conformance", "import", "quality"},
        &scenarioImportQualityChemicalCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-sources-canonical-units",
        "Imports all four EPANET chemical source types, canonical source strengths, and UUID-resolved source patterns, then proves native quality and source-mass equivalence.",
        {"conformance", "import", "quality"},
        &scenarioImportQualitySourcesCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-mixing-reactions-canonical-units",
        "Imports every tank mixing model plus global and entity reaction configuration, including independent pipe overrides, roughness correlation, limiting concentration, negative-order Michaelis-Menten tank kinetics, and ug/L-to-mg/L coefficient scaling, then proves native quality equivalence.",
        {"conformance", "import", "quality"},
        &scenarioImportQualityMixingReactionsCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-net1-equivalence",
        "Imports the upstream Net1 CHEMICAL model with no deferred quality diagnostics and proves its full native-vs-imported quality timeline.",
        {"conformance", "import", "quality", "proof"},
        &scenarioImportQualityNet1Equivalence});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-water-age",
        "Imports AGE configuration, tolerance, quality timestep, and initial node water age, then proves native quality equivalence.",
        {"conformance", "import", "quality"},
        &scenarioImportQualityWaterAge});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-source-trace",
        "Imports TRACE configuration, UUID-resolved trace source node, tolerance, and quality timestep, then proves native quality equivalence.",
        {"conformance", "import", "quality"},
        &scenarioImportQualitySourceTrace});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-degrees-wgs84",
        "Imports EPANET degree coordinates, link vertices, labels, and backdrop geometry directly as WGS84.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryDegrees});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-epsg3089-wgs84",
        "Imports explicit EPSG:3089 Kentucky projected coordinates, vertices, labels, and backdrop geometry into their real WGS84 location while retaining original source geometry.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryEpsg3089});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-unknown-epsg-null-island",
        "Retains an unsupported explicit EPSG code, refuses to guess its projection, marks import incomplete, and uses Null Island fallback geometry.",
        {"conformance", "import", "coordinate", "negative"},
        &scenarioImportGeometryUnknownEpsg});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-meters-null-island",
        "Imports metric EPANET map coordinates through GeographicLib and centers the preserved metric layout at WGS84 0°,0°.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryMeters});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-feet-null-island",
        "Converts EPANET map feet to metres and centers the preserved layout at WGS84 0°,0°.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryFeet});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-arbitrary-null-island",
        "Applies the documented one-map-unit-equals-one-metre convention to arbitrary EPANET map coordinates and centers them at WGS84 0°,0°.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryArbitrary});
    registry.add(ScenarioDefinition{
        "conformance-import-geometry-missing-layout",
        "Generates deterministic WGS84 schematic positions near 0°,0° for EPANET nodes without source coordinates.",
        {"conformance", "import", "coordinate"},
        &scenarioImportGeometryMissingCoordinates});
    registry.add(ScenarioDefinition{
        "conformance-import-metadata-report-fidelity",
        "Imports node/link comments and tags plus the complete representable EPANET report surface, canonicalizes report thresholds, and proves generated-INP persistence.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportMetadataReportFidelity});
    registry.add(ScenarioDefinition{
        "conformance-import-report-setting-threshold-canonical",
        "Preserves a SETTING report threshold exactly when the source project already uses AOWIS canonical CMH/metre setting units.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportReportSettingThresholdCanonical});
    registry.add(ScenarioDefinition{
        "conformance-import-report-setting-threshold-diagnostic",
        "Diagnoses non-canonical SETTING report thresholds whose mixed link-setting units cannot be converted losslessly after project normalization.",
        {"conformance", "import", "negative"},
        &scenarioImportReportSettingThresholdDiagnostic});
    registry.add(ScenarioDefinition{
        "conformance-import-open-error-diagnostic",
        "Rejects an unavailable INP path with native EPANET error details and a structured import diagnostic.",
        {"conformance", "import", "negative"},
        &scenarioImportOpenErrorDiagnostic});
}
}
