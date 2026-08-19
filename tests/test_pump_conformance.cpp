#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/pump_scenarios.h"

#include <QUuid>

#include <algorithm>
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
using AowisEpanetTests::NativePumpEnergyUsage;
using AowisEpanetTests::NativePumpResult;
using AowisEpanetTests::NativePumpState;
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeReferenceVariant;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

NativeReferenceConfiguration nativeConfiguration(NativeReferenceVariant variant, const QHash<int, QString> &control_ids)
{
    NativeReferenceConfiguration configuration;
    configuration.input_file = QString::fromUtf8(AOWIS_EPANET_TEST_NET1_INP);
    configuration.control_ids_by_index = control_ids;
    configuration.variant = variant;
    return configuration;
}

HydraulicLinkPump *findModelPump(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicLinkPump &pump : network.links_pumps)
    {
        if (pump.id == id)
            return &pump;
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

const NativePumpResult *findPump(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativePumpResult &pump : result.links_pumps)
    {
        if (pump.id == id)
            return &pump;
    }
    return nullptr;
}

const NativePumpEnergyUsage *findPumpEnergy(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativePumpEnergyUsage &usage : result.links_pump_energy_usage)
    {
        if (usage.pump_id == id)
            return &usage;
    }
    return nullptr;
}

NativeHydraulicTimeline runNative(const Net1Fixture &fixture, NativeReferenceVariant variant, TestContext &context)
{
    const NativeHydraulicTimeline timeline = AowisEpanetTests::runNativeEpanetReference(
        nativeConfiguration(variant, fixture.native_control_ids_by_index));
    context.expect(timeline.success, timeline.error.toStdString());
    context.expect(!timeline.results.isEmpty(), "native pump-conformance timeline must contain at least one hydraulic result");
    return timeline;
}

void compareWithWrapper(const Net1Fixture &fixture, const NativeHydraulicTimeline &native_timeline, TestContext &context)
{
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;
    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void disablePumpControls(Net1Fixture &fixture)
{
    for (HydraulicControlSimple &control : fixture.network.controls_simple)
        control.enabled = false;
}

HydraulicCurvePumpHead makeHeadCurve(const QString &id, const QList<std::pair<double, double>> &points)
{
    HydraulicCurvePumpHead curve;
    curve.id = id;
    curve.uuid = QUuid::createUuid();
    for (const std::pair<double, double> &value : points)
    {
        HydraulicCurvePumpHeadPoint point;
        point.flow_m3_per_h = value.first;
        point.head_gain_m = value.second;
        curve.points.append(point);
    }
    return curve;
}

HydraulicLinkPump *replaceHeadCurve(Net1Fixture &fixture, HydraulicLinkPumpDefinitionType definition_type,
    const QList<std::pair<double, double>> &points)
{
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    if (pump == nullptr)
        return nullptr;
    fixture.network.curves_pump_head.clear();
    HydraulicCurvePumpHead curve = makeHeadCurve(QStringLiteral("PUMP_HEAD_CURVE"), points);
    pump->definition_type = definition_type;
    pump->head_curve_uuid = curve.uuid;
    fixture.network.curves_pump_head.append(curve);
    return pump;
}

double interpolate(double x, const QList<std::pair<double, double>> &points)
{
    if (points.isEmpty())
        return 0.0;
    if (x <= points.first().first)
        return points.first().second;
    for (int index = 1; index < points.size(); index++)
    {
        const std::pair<double, double> &left = points.at(index - 1);
        const std::pair<double, double> &right = points.at(index);
        if (x <= right.first)
        {
            const double fraction = (x - left.first) / (right.first - left.first);
            return left.second + fraction * (right.second - left.second);
        }
    }
    return points.last().second;
}

void testPumpThreePoint(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = replaceHeadCurve(fixture, HydraulicLinkPumpDefinitionType::ThreePointCurve,
        {{0.0, 90.0}, {300.0, 65.0}, {600.0, 20.0}});
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpThreePoint, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "three-point native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expect(native_pump->flow_m3_per_h > 0.0, "three-point pump must carry positive flow");
            context.expect(native_pump->head_gain_m > 0.0, "three-point pump must add positive head");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpMultiPoint(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    const QList<std::pair<double, double>> points = {{0.0, 95.0}, {200.0, 82.0}, {400.0, 55.0}, {650.0, 15.0}};
    HydraulicLinkPump *pump = replaceHeadCurve(fixture, HydraulicLinkPumpDefinitionType::MultiPointCurve, points);
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpMultiPoint, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "multipoint native result must contain pump 9");
        if (native_pump != nullptr && native_pump->flow_m3_per_h >= points.first().first
            && native_pump->flow_m3_per_h <= points.last().first)
        {
            context.expectNear(native_pump->head_gain_m, interpolate(native_pump->flow_m3_per_h, points),
                NumericTolerance{1.0e-6, 1.0e-8},
                comparison("upstream_golden.interpolated_head_gain_m", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpConstantPower(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->definition_type = HydraulicLinkPumpDefinitionType::ConstantPower;
    pump->constant_power_kw = 75.0;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpConstantPower, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "constant-power native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expect(native_pump->flow_m3_per_h > 0.0, "constant-power pump must carry positive flow");
            context.expectNear(native_pump->power_kw, 100.0, NumericTolerance{1.0e-5, 1.0e-7},
                comparison("upstream_golden.electrical_power_kw", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpInitialSpeed(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->initial_speed_ratio = 0.8;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpInitialSpeed, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "initial-speed native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expectNear(native_pump->speed_ratio, 0.8, NumericTolerance{1.0e-12, 0.0},
                comparison("upstream_golden.speed_ratio", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpSpeedPattern(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 7200;
    fixture.network.timestep_pattern_s = 3600;
    disablePumpControls(fixture);

    HydraulicPatternTime pattern;
    pattern.id = QStringLiteral("PUMP_SPEED_PATTERN");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers = {0.8, 1.0, 1.15};
    fixture.network.patterns_time.append(pattern);
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->speed_pattern_uuid = pattern.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpSpeedPattern, context);
    const std::pair<std::int64_t, double> expected[] = {{0, 0.8}, {3600, 1.0}, {7200, 1.15}};
    for (const std::pair<std::int64_t, double> &item : expected)
    {
        const NativeHydraulicResult *result = findResult(native_timeline, item.first);
        context.expect(result != nullptr, "speed-pattern timeline must contain every pattern boundary");
        if (result == nullptr)
            continue;
        const NativePumpResult *native_pump = findPump(*result, QStringLiteral("9"));
        context.expect(native_pump != nullptr, "speed-pattern result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expectNear(native_pump->speed_ratio, item.second, NumericTolerance{1.0e-12, 0.0},
                comparison("upstream_golden.pattern_speed", item.first, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpInitialOff(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->initial_status = HydraulicLinkPumpInitialStatus::Off;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpInitialOff, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "initial-off native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expect(!native_pump->open, "initially-off pump must be closed");
            context.expect(native_pump->state == NativePumpState::Closed, "initially-off pump must report Closed operating state");
            context.expectNear(native_pump->power_kw, 0.0, NumericTolerance{1.0e-12, 0.0},
                comparison("upstream_golden.power_kw", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpConstantEfficiency(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Constant;
    pump->constant_efficiency_percent = 83.0;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpConstantEfficiency, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "constant-efficiency native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expectNear(native_pump->efficiency_percent, 83.0, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.efficiency_percent", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpEfficiencyCurve(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    const QList<std::pair<double, double>> points = {{0.0, 60.0}, {200.0, 76.0}, {400.0, 88.0}, {650.0, 79.0}};

    HydraulicCurvePumpEfficiency curve;
    curve.id = QStringLiteral("PUMP_EFFICIENCY_CURVE");
    curve.uuid = QUuid::createUuid();
    for (const std::pair<double, double> &value : points)
    {
        HydraulicCurvePumpEfficiencyPoint point;
        point.flow_m3_per_h = value.first;
        point.efficiency_percent = value.second;
        curve.points.append(point);
    }
    fixture.network.curves_pump_efficiency.append(curve);

    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Curve;
    pump->efficiency_curve_uuid = curve.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpEfficiencyCurve, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "efficiency-curve native result must contain pump 9");
        if (native_pump != nullptr && native_pump->flow_m3_per_h >= points.first().first
            && native_pump->flow_m3_per_h <= points.last().first)
        {
            context.expectNear(native_pump->efficiency_percent, interpolate(native_pump->flow_m3_per_h, points),
                NumericTolerance{1.0e-6, 1.0e-8},
                comparison("upstream_golden.interpolated_efficiency_percent", 0, "Pump", "9"));
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpGlobalEnergy(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 7200;
    fixture.network.timestep_pattern_s = 3600;
    disablePumpControls(fixture);
    fixture.network.options_energy.currency_iso4217 = QStringLiteral("EUR");
    fixture.network.options_energy.global_pump_efficiency_percent = 82.0;
    fixture.network.options_energy.global_energy_price_per_kw_h = 0.2;
    fixture.network.options_energy.demand_charge_per_kw = 1.5;

    HydraulicPatternTime pattern;
    pattern.id = QStringLiteral("GLOBAL_ENERGY_PRICE_PATTERN");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers = {1.0, 2.0};
    fixture.network.patterns_time.append(pattern);
    fixture.network.options_energy.global_energy_price_pattern_uuid = pattern.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpGlobalEnergy, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeHydraulicResult &final_result = native_timeline.results.last();
        const NativePumpEnergyUsage *usage = findPumpEnergy(final_result, QStringLiteral("9"));
        context.expect(usage != nullptr, "global-energy result must contain pump 9 energy usage");
        context.expect(final_result.energy_usage.energy_cost_per_day > 0.0, "global patterned energy cost must be positive");
        context.expectNear(final_result.energy_usage.demand_charge_per_day,
            final_result.energy_usage.peak_power_kw * 1.5, NumericTolerance{1.0e-9, 1.0e-9},
            comparison("upstream_golden.demand_charge_per_day", final_result.time_elapsed_s));
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpEnergyPattern(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 7200;
    fixture.network.timestep_pattern_s = 3600;
    disablePumpControls(fixture);
    fixture.network.options_energy.currency_iso4217 = QStringLiteral("USD");
    fixture.network.options_energy.global_pump_efficiency_percent = 80.0;
    fixture.network.options_energy.global_energy_price_per_kw_h = 0.1;
    fixture.network.options_energy.demand_charge_per_kw = 0.75;

    HydraulicPatternTime global_pattern;
    global_pattern.id = QStringLiteral("UNUSED_GLOBAL_PRICE_PATTERN");
    global_pattern.uuid = QUuid::createUuid();
    global_pattern.multipliers = {4.0, 4.0};
    fixture.network.patterns_time.append(global_pattern);
    fixture.network.options_energy.global_energy_price_pattern_uuid = global_pattern.uuid;

    HydraulicPatternTime pump_pattern;
    pump_pattern.id = QStringLiteral("PUMP_ENERGY_PRICE_PATTERN");
    pump_pattern.uuid = QUuid::createUuid();
    pump_pattern.multipliers = {1.0, 0.5};
    fixture.network.patterns_time.append(pump_pattern);

    HydraulicLinkPump *pump = findModelPump(fixture.network, QStringLiteral("9"));
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;
    pump->energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Pattern;
    pump->energy_price_per_kw_h = 0.3;
    pump->price_pattern_uuid = pump_pattern.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpEnergyPattern, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeHydraulicResult &final_result = native_timeline.results.last();
        const NativePumpEnergyUsage *usage = findPumpEnergy(final_result, QStringLiteral("9"));
        context.expect(usage != nullptr, "pump-energy-pattern result must contain pump 9 energy usage");
        if (usage != nullptr)
            context.expect(usage->average_cost_per_day > 0.0, "pump-specific patterned cost must be positive");
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpCannotSupplyHead(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = replaceHeadCurve(fixture, HydraulicLinkPumpDefinitionType::OnePointCurve, {{100.0, 5.0}});
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpCannotSupplyHead, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "XHEAD native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expect(native_pump->state == NativePumpState::CannotSupplyHead,
                "weak pump curve must report CannotSupplyHead");
            context.expect(!native_pump->open, "CannotSupplyHead pump must be hydraulically closed");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testPumpCannotSupplyFlow(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    HydraulicLinkPump *pump = replaceHeadCurve(fixture, HydraulicLinkPumpDefinitionType::MultiPointCurve,
        {{0.0, 100.0}, {5.0, 95.0}, {10.0, 90.0}, {20.0, 80.0}});
    context.expect(pump != nullptr, "pump-conformance fixture must contain pump 9");
    if (pump == nullptr)
        return;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PumpCannotSupplyFlow, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativePumpResult *native_pump = findPump(native_timeline.results.first(), QStringLiteral("9"));
        context.expect(native_pump != nullptr, "XFLOW native result must contain pump 9");
        if (native_pump != nullptr)
        {
            context.expect(native_pump->state == NativePumpState::CannotSupplyFlow,
                "undersized multipoint curve must report CannotSupplyFlow");
            context.expect(native_pump->open, "CannotSupplyFlow pump remains hydraulically open");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}
}

namespace AowisEpanetTests
{
void registerPumpScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{"conformance-upstream-pump-three-point",
        "Exercises a three-point pump head definition and complete pump hydraulic result mapping.",
        {"conformance", "hydraulic", "upstream", "pump", "curve"}, &testPumpThreePoint});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-multipoint",
        "Exercises a four-point pump head curve including native curve interpolation.",
        {"conformance", "hydraulic", "upstream", "pump", "curve"}, &testPumpMultiPoint});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-constant-power",
        "Exercises the constant-power pump definition and its hydraulic/electrical power result.",
        {"conformance", "hydraulic", "upstream", "pump", "energy"}, &testPumpConstantPower});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-initial-speed",
        "Exercises non-default initial relative pump speed.",
        {"conformance", "hydraulic", "upstream", "pump", "speed"}, &testPumpInitialSpeed});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-speed-pattern",
        "Exercises time-patterned pump speed at successive pattern boundaries.",
        {"conformance", "hydraulic", "upstream", "pump", "speed", "pattern"}, &testPumpSpeedPattern});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-initial-off",
        "Exercises initial pump status Off and the Closed operating-state/result mapping.",
        {"conformance", "hydraulic", "upstream", "pump", "status"}, &testPumpInitialOff});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-constant-efficiency",
        "Exercises a pump-specific constant efficiency through an EPANET efficiency curve.",
        {"conformance", "hydraulic", "upstream", "pump", "efficiency"}, &testPumpConstantEfficiency});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-efficiency-curve",
        "Exercises pump efficiency-curve assignment and interpolated efficiency results.",
        {"conformance", "hydraulic", "upstream", "pump", "efficiency", "curve"}, &testPumpEfficiencyCurve});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-global-energy",
        "Exercises global efficiency, patterned global energy price, demand charge, and aggregate pump/system energy results.",
        {"conformance", "hydraulic", "upstream", "pump", "energy", "pattern"}, &testPumpGlobalEnergy});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-energy-pattern",
        "Exercises pump-specific energy price and price-pattern override against an independent native energy accumulator.",
        {"conformance", "hydraulic", "upstream", "pump", "energy", "pattern"}, &testPumpEnergyPattern});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-xhead",
        "Exercises the EPANET pump CannotSupplyHead operating state and closed result mapping.",
        {"conformance", "hydraulic", "upstream", "pump", "status"}, &testPumpCannotSupplyHead});
    registry.add(ScenarioDefinition{"conformance-upstream-pump-xflow",
        "Exercises the EPANET pump CannotSupplyFlow operating state and open result mapping.",
        {"conformance", "hydraulic", "upstream", "pump", "status"}, &testPumpCannotSupplyFlow});
}
}
