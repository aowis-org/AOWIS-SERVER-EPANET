#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/upstream_step3_scenarios.h"

#include <QUuid>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#ifndef AOWIS_EPANET_TEST_NET1_INP
#error "AOWIS_EPANET_TEST_NET1_INP must identify the vendored Net1 fixture"
#endif

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NativeHydraulicResult;
using AowisEpanetTests::NativeHydraulicTimeline;
using AowisEpanetTests::NativeJunctionResult;
using AowisEpanetTests::NativePipeResult;
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeReferenceVariant;
using AowisEpanetTests::NativeTankResult;
using AowisEpanetTests::NativeValveResult;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr double kMetresPerFoot = 0.3048;
constexpr double kCubicMetresPerHourPerCubicFootPerSecond = 101.94;
constexpr double kGallonsPerMinutePerCubicFootPerSecond = 448.831;
constexpr double kPsiPerFoot = 0.4333;

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

double feetToMetres(double feet)
{
    return feet * kMetresPerFoot;
}

double metresToFeet(double metres)
{
    return metres / kMetresPerFoot;
}

double gallonsPerMinuteToCubicMetresPerHour(double gallons_per_minute)
{
    return gallons_per_minute / kGallonsPerMinutePerCubicFootPerSecond
        * kCubicMetresPerHourPerCubicFootPerSecond;
}


double psiToMetresOfHead(double psi)
{
    return psi / kPsiPerFoot * kMetresPerFoot;
}

NativeReferenceConfiguration nativeConfiguration(NativeReferenceVariant variant, const QHash<int, QString> &control_ids = {})
{
    NativeReferenceConfiguration configuration;
    configuration.input_file = QString::fromUtf8(AOWIS_EPANET_TEST_NET1_INP);
    configuration.control_ids_by_index = control_ids;
    configuration.variant = variant;
    return configuration;
}

const NativeJunctionResult *findJunction(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeJunctionResult &junction : result.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const NativeTankResult *findTank(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeTankResult &tank : result.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const NativePipeResult *findPipe(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativePipeResult &pipe : result.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const NativeValveResult *findValve(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeValveResult &valve : result.links_valves)
    {
        if (valve.id == id)
            return &valve;
    }
    return nullptr;
}

const NativeHydraulicResult *findResult(const NativeHydraulicTimeline &timeline, std::int64_t time_s)
{
    for (const NativeHydraulicResult &result : timeline.results)
    {
        if (result.time_elapsed_s == time_s)
            return &result;
    }
    return nullptr;
}

void configureStressDemand(NetworkHydraulic &network, bool pressure_driven)
{
    network.duration_s = 0;
    network.options_hydraulic.demand_multiplier = 10.0;
    network.options_hydraulic.demand_model = pressure_driven
        ? HydraulicDemandModel::PressureDriven
        : HydraulicDemandModel::DemandDriven;
    network.options_hydraulic.minimum_pressure_head_m = psiToMetresOfHead(20.0);
    network.options_hydraulic.required_pressure_head_m = psiToMetresOfHead(100.0);
    network.options_hydraulic.pressure_exponent = 0.5;
}


void testDdaPda(TestContext &context)
{
    Net1Fixture dda_fixture = AowisEpanetTests::makeNet1Fixture();
    configureStressDemand(dda_fixture.network, false);

    NativeReferenceConfiguration dda_configuration = nativeConfiguration(
        NativeReferenceVariant::DdaStress,
        dda_fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline dda_native = AowisEpanetTests::runNativeEpanetReference(dda_configuration);
    context.expect(dda_native.success, dda_native.error.toStdString());
    if (dda_native.success && !dda_native.results.isEmpty())
    {
        const NativeHydraulicResult &result = dda_native.results.first();
        context.expectEqual(result.statistics.deficient_nodes, std::int64_t{4},
            comparison("upstream_golden.statistics.deficient_nodes", result.time_elapsed_s),
            "upstream DDA stress case must retain four deficient demand nodes");
        context.expect(dda_native.warning_codes.contains(6),
            "upstream DDA stress case must retain EPANET warning 6 for negative pressures");

        const EpanetResultRun wrapper_run = EpanetRunner().run(dda_fixture.network);
        AowisEpanetTests::compareHydraulicTimelines(dda_native, wrapper_run, dda_fixture.network, context);
    }

    Net1Fixture pda_fixture = AowisEpanetTests::makeNet1Fixture();
    configureStressDemand(pda_fixture.network, true);

    NativeReferenceConfiguration pda_configuration = nativeConfiguration(
        NativeReferenceVariant::PdaStress,
        pda_fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline pda_native = AowisEpanetTests::runNativeEpanetReference(pda_configuration);
    context.expect(pda_native.success, pda_native.error.toStdString());
    if (!pda_native.success || pda_native.results.isEmpty())
        return;

    const NativeHydraulicResult &result = pda_native.results.first();
    context.expectEqual(result.statistics.deficient_nodes, std::int64_t{6},
        comparison("upstream_golden.statistics.deficient_nodes", result.time_elapsed_s),
        "upstream PDA case must retain six deficient demand nodes");
    context.expectNear(result.statistics.demand_reduction_percent, 32.66,
        NumericTolerance{0.01, 0.0},
        comparison("upstream_golden.statistics.demand_reduction_percent", result.time_elapsed_s));

    const NativeJunctionResult *junction_12 = findJunction(result, QStringLiteral("12"));
    const NativeJunctionResult *junction_21 = findJunction(result, QStringLiteral("21"));
    context.expect(junction_12 != nullptr, "upstream PDA result must contain junction 12");
    context.expect(junction_21 != nullptr, "upstream PDA result must contain junction 21");
    if (junction_12 != nullptr)
    {
        context.expectNear(junction_12->demand_deficit_m3_per_h, 0.0,
            NumericTolerance{gallonsPerMinuteToCubicMetresPerHour(0.01), 0.0},
            comparison("upstream_golden.demand_deficit_m3_per_h", result.time_elapsed_s, "Junction", "12"));
    }
    if (junction_21 != nullptr)
    {
        context.expectNear(junction_21->demand_deficit_m3_per_h,
            gallonsPerMinuteToCubicMetresPerHour(413.67),
            NumericTolerance{gallonsPerMinuteToCubicMetresPerHour(0.01), 0.0},
            comparison("upstream_golden.demand_deficit_m3_per_h", result.time_elapsed_s, "Junction", "21"));
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(pda_fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(pda_native, wrapper_run, pda_fixture.network, context);
}

void testLeakage(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    for (HydraulicLinkPipe &pipe : fixture.network.links_pipes)
    {
        if (pipe.id != QStringLiteral("21"))
            continue;
        pipe.leak_area_mm2_per_100m = 1.0 / kMetresPerFoot;
        pipe.leak_expansion_mm2_per_m_head = 0.1 / kMetresPerFoot;
        break;
    }

    NativeReferenceConfiguration configuration = nativeConfiguration(
        NativeReferenceVariant::Leakage,
        fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    const NativeHydraulicResult &result = native_timeline.results.first();
    const NativePipeResult *pipe_21 = findPipe(result, QStringLiteral("21"));
    const NativeJunctionResult *junction_21 = findJunction(result, QStringLiteral("21"));
    const NativeJunctionResult *junction_22 = findJunction(result, QStringLiteral("22"));
    context.expect(pipe_21 != nullptr, "upstream leakage result must contain pipe 21");
    context.expect(junction_21 != nullptr, "upstream leakage result must contain junction 21");
    context.expect(junction_22 != nullptr, "upstream leakage result must contain junction 22");

    if (pipe_21 != nullptr && junction_21 != nullptr && junction_22 != nullptr)
    {
        context.expectNear(
            pipe_21->leakage_flow_m3_per_h,
            junction_21->leakage_flow_m3_per_h + junction_22->leakage_flow_m3_per_h,
            NumericTolerance{gallonsPerMinuteToCubicMetresPerHour(0.01), 0.0},
            comparison("upstream_invariant.node_leakage_sum", result.time_elapsed_s, "Pipe", "21"));

        const double area_ft2 = 1.0 / kMetresPerFoot / kMetresPerFoot / 1.0e6;
        const double expansion_ft2_per_ft = 0.1 / kMetresPerFoot / 1.0e6;
        const double pipe_sections = 5280.0 / 100.0;
        const double orifice_coefficient = 0.6 * std::sqrt(2.0 * 32.2);
        const double head_21_ft = metresToFeet(junction_21->pressure_head_m);
        const double head_22_ft = metresToFeet(junction_22->pressure_head_m);
        const double q1_cfs = orifice_coefficient * (pipe_sections / 2.0)
            * (area_ft2 + expansion_ft2_per_ft * head_21_ft) * std::sqrt(head_21_ft);
        const double q2_cfs = orifice_coefficient * (pipe_sections / 2.0)
            * (area_ft2 + expansion_ft2_per_ft * head_22_ft) * std::sqrt(head_22_ft);
        const double calculated_leakage_m3_per_h = (q1_cfs + q2_cfs)
            * kCubicMetresPerHourPerCubicFootPerSecond;

        context.expectNear(
            pipe_21->leakage_flow_m3_per_h,
            calculated_leakage_m3_per_h,
            NumericTolerance{gallonsPerMinuteToCubicMetresPerHour(0.01), 0.0},
            comparison("upstream_invariant.favad_formula", result.time_elapsed_s, "Pipe", "21"));
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

Net1Fixture overflowFixture(bool can_overflow)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 3600;
    for (HydraulicNodeTank &tank : fixture.network.nodes_tanks)
    {
        if (tank.id != QStringLiteral("2"))
            continue;
        tank.water_level_initial_m = feetToMetres(130.0);
        tank.water_level_maximum_m = feetToMetres(130.0);
        tank.can_overflow = can_overflow;
        break;
    }
    return fixture;
}

void verifyOverflowGolden(
    const NativeHydraulicTimeline &timeline,
    bool can_overflow,
    TestContext &context)
{
    context.expect(!timeline.results.isEmpty(), "upstream overflow case must return hydraulic results");
    if (timeline.results.isEmpty())
        return;

    const NativeHydraulicResult &result = timeline.results.last();
    const NativeTankResult *tank = findTank(result, QStringLiteral("2"));
    const NativePipeResult *pipe = findPipe(result, QStringLiteral("110"));
    context.expect(tank != nullptr, "upstream overflow result must contain tank 2");
    context.expect(pipe != nullptr, "upstream overflow result must contain pipe 110");
    if (tank == nullptr || pipe == nullptr)
        return;

    context.expectNear(tank->water_level_m, feetToMetres(130.0),
        NumericTolerance{feetToMetres(0.0001), 0.0},
        comparison("upstream_golden.water_level_m", result.time_elapsed_s, "Tank", "2"));

    const NumericTolerance flow_tolerance{gallonsPerMinuteToCubicMetresPerHour(0.0001), 0.0};
    if (!can_overflow)
    {
        context.expectNear(tank->net_demand_m3_per_h, 0.0, flow_tolerance,
            comparison("upstream_golden.spillage_m3_per_h", result.time_elapsed_s, "Tank", "2"));
        context.expectNear(pipe->flow_m3_per_h, 0.0, flow_tolerance,
            comparison("upstream_golden.inflow_m3_per_h", result.time_elapsed_s, "Pipe", "110"));
        return;
    }

    context.expect(tank->net_demand_m3_per_h > flow_tolerance.absolute,
        "overflow-enabled tank must spill a nonzero flow");
    context.expectNear(-pipe->flow_m3_per_h, tank->net_demand_m3_per_h, flow_tolerance,
        comparison("upstream_invariant.spillage_equals_inflow", result.time_elapsed_s, "Tank", "2"));
}

void testTankOverflow(TestContext &context)
{
    const Net1Fixture disabled_fixture = overflowFixture(false);
    NativeReferenceConfiguration disabled_configuration = nativeConfiguration(
        NativeReferenceVariant::OverflowDisabled,
        disabled_fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline disabled_native = AowisEpanetTests::runNativeEpanetReference(disabled_configuration);
    context.expect(disabled_native.success, disabled_native.error.toStdString());
    if (disabled_native.success)
    {
        verifyOverflowGolden(disabled_native, false, context);
        const EpanetResultRun wrapper_run = EpanetRunner().run(disabled_fixture.network);
        AowisEpanetTests::compareHydraulicTimelines(
            disabled_native,
            wrapper_run,
            disabled_fixture.network,
            context);
    }

    const Net1Fixture enabled_fixture = overflowFixture(true);
    NativeReferenceConfiguration enabled_configuration = nativeConfiguration(
        NativeReferenceVariant::OverflowEnabled,
        enabled_fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline enabled_native = AowisEpanetTests::runNativeEpanetReference(enabled_configuration);
    context.expect(enabled_native.success, enabled_native.error.toStdString());
    if (!enabled_native.success)
        return;

    verifyOverflowGolden(enabled_native, true, context);
    const EpanetResultRun wrapper_run = EpanetRunner().run(enabled_fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(
        enabled_native,
        wrapper_run,
        enabled_fixture.network,
        context);
}

Net1Fixture pcvFixture()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    QUuid node_from;
    QUuid node_to;
    for (int index = 0; index < fixture.network.links_pipes.size(); index++)
    {
        if (fixture.network.links_pipes.at(index).id != QStringLiteral("22"))
            continue;
        node_from = fixture.network.links_pipes.at(index).node_uuid_from;
        node_to = fixture.network.links_pipes.at(index).node_uuid_to;
        fixture.network.links_pipes.removeAt(index);
        break;
    }

    HydraulicCurveValveCharacteristic curve;
    curve.id = QStringLiteral("ValveCurve");
    curve.uuid = QUuid::createUuid();

    const double positions[] = {0.0, 25.0, 50.0, 75.0, 100.0};
    const double relative_flows[] = {0.0, 8.9, 18.4, 40.6, 100.0};
    for (int index = 0; index < 5; index++)
    {
        HydraulicCurveValveCharacteristicPoint point;
        point.position_percent = positions[index];
        point.relative_flow_percent = relative_flows[index];
        curve.points.append(point);
    }
    fixture.network.curves_valve_characteristic.append(curve);

    HydraulicLinkValve valve;
    valve.id = QStringLiteral("22");
    valve.uuid = QUuid::createUuid();
    valve.node_uuid_from = node_from;
    valve.node_uuid_to = node_to;
    valve.type = HydraulicLinkValveType::PCV;
    valve.diameter_mm = 12.0 * 25.4;
    valve.minor_loss = 0.19;
    valve.setting = 35.0;
    valve.setting_curve_uuid = curve.uuid;
    valve.initial_status = HydraulicLinkValveInitialStatus::Active;
    fixture.network.links_valves.append(valve);

    return fixture;
}

void testPcv(TestContext &context)
{
    const Net1Fixture fixture = pcvFixture();
    NativeReferenceConfiguration configuration = nativeConfiguration(
        NativeReferenceVariant::Pcv,
        fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    const NativeHydraulicResult &result = native_timeline.results.first();
    const NativeValveResult *valve = findValve(result, QStringLiteral("22"));
    context.expect(valve != nullptr, "upstream PCV result must contain converted link 22");
    if (valve != nullptr)
    {
        context.expect(valve->open, "upstream PCV must be hydraulically open");
        context.expect(valve->active, "upstream PCV must report Active status");
        context.expect(valve->flow_m3_per_h > 0.0, "upstream PCV must carry positive flow");
        context.expect(valve->velocity_m_per_s > 0.0, "upstream PCV must report positive velocity");
        context.expectNear(valve->diameter_mm, 12.0 * 25.4,
            NumericTolerance{1.0e-9, 1.0e-12},
            comparison("upstream_golden.diameter_mm", result.time_elapsed_s, "Valve", "22"),
            "upstream PCV must retain its configured 12-inch diameter");
        context.expectNear(valve->minor_loss, 0.19,
            NumericTolerance{1.0e-12, 1.0e-12},
            comparison("upstream_golden.minor_loss", result.time_elapsed_s, "Valve", "22"),
            "upstream PCV must retain its configured minor-loss coefficient");
        context.expectNear(valve->setting, 35.0, NumericTolerance{1.0e-7, 1.0e-6},
            comparison("upstream_golden.returned_setting", result.time_elapsed_s, "Valve", "22"),
            "upstream PCV must return its configured 35-percent position");
        context.expectNear(valve->head_loss_m, feetToMetres(0.0255),
            NumericTolerance{feetToMetres(0.001), 0.0},
            comparison("upstream_golden.head_loss_m", result.time_elapsed_s, "Valve", "22"),
            "upstream PCV head loss must retain the 35-percent-open golden result");
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

Net1Fixture demandPatternFixture()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 21600;
    if (!fixture.network.patterns_time.isEmpty())
        fixture.network.patterns_time.first().id = QStringLiteral("Pat1");

    HydraulicPatternTime pattern;
    pattern.id = QStringLiteral("Step3Pattern");
    pattern.uuid = QUuid::createUuid();
    pattern.factors = {3.1, 3.2, 3.3, 3.4};
    fixture.network.patterns_time.append(pattern);

    for (HydraulicNodeJunction &junction : fixture.network.nodes_junctions)
    {
        if (junction.id != QStringLiteral("12") || junction.demands.isEmpty())
            continue;
        junction.demands.first().pattern_uuid = pattern.uuid;
        break;
    }
    return fixture;
}

void testDemandPattern(TestContext &context)
{
    const Net1Fixture fixture = demandPatternFixture();
    NativeReferenceConfiguration configuration = nativeConfiguration(
        NativeReferenceVariant::DemandPattern,
        fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success)
        return;

    const NativeHydraulicResult *result_0 = findResult(native_timeline, 0);
    const NativeHydraulicResult *result_7200 = findResult(native_timeline, 7200);
    context.expect(result_0 != nullptr, "demand-pattern scenario must contain the zero-second period");
    context.expect(result_7200 != nullptr, "demand-pattern scenario must contain the 7,200-second period");

    if (result_0 != nullptr)
    {
        const NativeJunctionResult *junction = findJunction(*result_0, QStringLiteral("12"));
        context.expect(junction != nullptr, "demand-pattern scenario must contain junction 12 at zero seconds");
        if (junction != nullptr)
        {
            context.expectNear(junction->demand_requested_m3_per_h,
                gallonsPerMinuteToCubicMetresPerHour(150.0 * 3.1),
                AowisEpanetTests::HydraulicQuantity::FlowM3PerHour,
                comparison("upstream_golden.demand_requested_m3_per_h", 0, "Junction", "12"));
        }
    }

    if (result_7200 != nullptr)
    {
        const NativeJunctionResult *junction = findJunction(*result_7200, QStringLiteral("12"));
        context.expect(junction != nullptr, "demand-pattern scenario must contain junction 12 at 7,200 seconds");
        if (junction != nullptr)
        {
            context.expectNear(junction->demand_requested_m3_per_h,
                gallonsPerMinuteToCubicMetresPerHour(150.0 * 3.2),
                AowisEpanetTests::HydraulicQuantity::FlowM3PerHour,
                comparison("upstream_golden.demand_requested_m3_per_h", 7200, "Junction", "12"));
        }
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void testSimpleControl(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();

    QHash<int, QString> native_control_ids;
    native_control_ids.insert(3, QStringLiteral("NET1_OPEN_PUMP_BELOW_110_FT"));
    native_control_ids.insert(4, QStringLiteral("NET1_CLOSE_PUMP_ABOVE_140_FT"));

    NativeReferenceConfiguration configuration = nativeConfiguration(
        NativeReferenceVariant::SimpleControl,
        native_control_ids);
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    NativeReferenceConfiguration baseline_configuration = nativeConfiguration(
        NativeReferenceVariant::None,
        fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline baseline_timeline = AowisEpanetTests::runNativeEpanetReference(baseline_configuration);
    context.expect(baseline_timeline.success, baseline_timeline.error.toStdString());
    if (baseline_timeline.success && !baseline_timeline.results.isEmpty())
    {
        const NativeTankResult *baseline_tank = findTank(baseline_timeline.results.last(), QStringLiteral("2"));
        const NativeTankResult *replacement_tank = findTank(native_timeline.results.last(), QStringLiteral("2"));
        context.expect(baseline_tank != nullptr, "baseline simple-control run must contain tank 2");
        context.expect(replacement_tank != nullptr, "replacement simple-control run must contain tank 2");
        if (baseline_tank != nullptr && replacement_tank != nullptr)
        {
            context.expectNear(replacement_tank->head_m, baseline_tank->head_m,
                NumericTolerance{feetToMetres(1.0e-5), 0.0},
                comparison("upstream_invariant.final_tank_head_m", native_timeline.results.last().time_elapsed_s, "Tank", "2"),
                "upstream replacement controls must preserve final tank head");
        }
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void testHydraulicStepping(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    NativeReferenceConfiguration configuration = nativeConfiguration(
        NativeReferenceVariant::None,
        fixture.native_control_ids_by_index);
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    context.expectEqual(native_timeline.results.first().time_elapsed_s, std::int64_t{0},
        comparison("upstream_golden.first_time_s"));
    context.expectEqual(native_timeline.results.last().time_elapsed_s, std::int64_t{86400},
        comparison("upstream_golden.last_time_s"));
    context.expect(native_timeline.results.size() > 24,
        "hydraulic stepping must expose intermediate tank/control event boundaries in addition to hourly steps");

    std::int64_t previous_time_s = -1;
    for (const NativeHydraulicResult &result : native_timeline.results)
    {
        context.expect(result.time_elapsed_s > previous_time_s,
            "upstream EN_runH/EN_nextH stepping must advance monotonically");
        previous_time_s = result.time_elapsed_s;
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}
}

namespace AowisEpanetTests
{
void registerUpstreamStep3Scenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-upstream-dda-pda",
        "Ports upstream DDA/PDA stress behavior, golden deficient-node and demand-reduction checks, and full native-versus-wrapper results.",
        {"conformance", "hydraulic", "upstream", "pda"},
        &testDdaPda});
    registry.add(ScenarioDefinition{
        "conformance-upstream-leakage",
        "Ports upstream FAVAD leakage with node conservation and independent formula checks, then compares full native and wrapper results.",
        {"conformance", "hydraulic", "upstream", "leakage"},
        &testLeakage});
    registry.add(ScenarioDefinition{
        "conformance-upstream-tank-overflow",
        "Ports upstream tank overflow disabled/enabled cases, spillage conservation, and full native-versus-wrapper results.",
        {"conformance", "hydraulic", "upstream", "tank"},
        &testTankOverflow});
    registry.add(ScenarioDefinition{
        "conformance-upstream-pcv",
        "Ports the upstream 35-percent-open PCV characteristic-curve case and golden head-loss assertion with full differential comparison.",
        {"conformance", "hydraulic", "upstream", "valve"},
        &testPcv});
    registry.add(ScenarioDefinition{
        "conformance-upstream-demand-pattern",
        "Ports upstream demand-pattern assignment and factors and compares every hydraulic event and result field.",
        {"conformance", "hydraulic", "upstream", "pattern"},
        &testDemandPattern});
    registry.add(ScenarioDefinition{
        "conformance-upstream-simple-control",
        "Ports upstream replacement low/high tank-level controls, preserves the final-head invariant, and compares the complete timeline.",
        {"conformance", "hydraulic", "upstream", "control"},
        &testSimpleControl});
    registry.add(ScenarioDefinition{
        "conformance-upstream-hydraulic-stepping",
        "Ports upstream EN_runH/EN_nextH stepping and verifies the complete wrapper event timeline against native EPANET.",
        {"conformance", "hydraulic", "upstream", "timeline"},
        &testHydraulicStepping});
}
}
