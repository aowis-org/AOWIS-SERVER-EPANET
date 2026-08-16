#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/upstream_step6_scenarios.h"

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
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeReferenceVariant;
using AowisEpanetTests::NativeValveResult;
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

const NativeValveResult *findValve(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeValveResult &valve : result.links_valves)
    {
        if (valve.id == id)
            return &valve;
    }
    return nullptr;
}

Net1Fixture valveFixture(const QString &pipe_id, HydraulicLinkValveType type, double diameter_mm,
    double minor_loss, double setting, HydraulicLinkValveInitialStatus initial_status)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    QUuid node_from;
    QUuid node_to;
    for (int index = 0; index < fixture.network.links_pipes.size(); index++)
    {
        if (fixture.network.links_pipes.at(index).id != pipe_id)
            continue;
        node_from = fixture.network.links_pipes.at(index).node_uuid_from;
        node_to = fixture.network.links_pipes.at(index).node_uuid_to;
        fixture.network.links_pipes.removeAt(index);
        break;
    }

    HydraulicLinkValve valve;
    valve.id = pipe_id;
    valve.uuid = QUuid::createUuid();
    valve.node_uuid_from = node_from;
    valve.node_uuid_to = node_to;
    valve.type = type;
    valve.diameter_mm = diameter_mm;
    valve.minor_loss = minor_loss;
    valve.setting = setting;
    valve.initial_status = initial_status;
    fixture.network.links_valves.append(valve);
    return fixture;
}

NativeHydraulicTimeline runNative(const Net1Fixture &fixture, NativeReferenceVariant variant, TestContext &context)
{
    const NativeHydraulicTimeline timeline = AowisEpanetTests::runNativeEpanetReference(
        nativeConfiguration(variant, fixture.native_control_ids_by_index));
    context.expect(timeline.success, timeline.error.toStdString());
    context.expect(!timeline.results.isEmpty(), "native step-6 timeline must contain at least one hydraulic result");
    return timeline;
}

void compareWithWrapper(const Net1Fixture &fixture, const NativeHydraulicTimeline &native_timeline, TestContext &context)
{
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;
    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void expectValveInputs(const NativeValveResult &valve, double diameter_mm, double minor_loss, TestContext &context)
{
    context.expectNear(valve.diameter_mm, diameter_mm,
        NumericTolerance{1.0e-9, 1.0e-12},
        comparison("upstream_golden.diameter_mm", 0, "Valve", valve.id.toStdString()),
        "native valve must retain the configured non-default diameter");
    context.expectNear(valve.minor_loss, minor_loss,
        NumericTolerance{1.0e-12, 1.0e-12},
        comparison("upstream_golden.minor_loss", 0, "Valve", valve.id.toStdString()),
        "native valve must retain the configured minor-loss coefficient");
}

void expectActiveValve(const NativeValveResult &valve, double expected_setting, TestContext &context)
{
    context.expect(valve.open, "active valve must be hydraulically open");
    context.expect(valve.active, "active valve must report EPANET active status");
    context.expectNear(valve.setting, expected_setting, NumericTolerance{1.0e-7, 1.0e-6},
        comparison("upstream_golden.returned_setting", 0, "Valve", valve.id.toStdString()));
    context.expect(std::abs(valve.flow_m3_per_h) > 0.0, "active valve must carry nonzero flow");
    context.expect(valve.velocity_m_per_s > 0.0, "active valve must report nonzero velocity");
}

void testValvePrv(TestContext &context)
{
    constexpr double diameter_mm = 230.0;
    constexpr double setting_m = 80.0;
    const Net1Fixture fixture = valveFixture(QStringLiteral("121"), HydraulicLinkValveType::PRV,
        diameter_mm, 0.35, setting_m, HydraulicLinkValveInitialStatus::Active);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValvePrv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("121"));
        context.expect(valve != nullptr, "PRV native result must contain converted link 121");
        if (valve != nullptr)
        {
            expectActiveValve(*valve, setting_m, context);
            expectValveInputs(*valve, diameter_mm, 0.35, context);
            context.expect(valve->head_loss_m > 1.0, "PRV must create a material regulating head loss");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testValvePsv(TestContext &context)
{
    constexpr double diameter_mm = 400.0;
    constexpr double setting_m = 86.0;
    const Net1Fixture fixture = valveFixture(QStringLiteral("10"), HydraulicLinkValveType::PSV,
        diameter_mm, 0.22, setting_m, HydraulicLinkValveInitialStatus::Active);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValvePsv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("10"));
        context.expect(valve != nullptr, "PSV native result must contain converted link 10");
        if (valve != nullptr)
        {
            expectActiveValve(*valve, setting_m, context);
            expectValveInputs(*valve, diameter_mm, 0.22, context);
            context.expect(valve->head_loss_m > 1.0, "PSV must create a material sustaining head loss");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testValvePbv(TestContext &context)
{
    constexpr double diameter_mm = 225.0;
    constexpr double setting_m = 5.0;
    const Net1Fixture fixture = valveFixture(QStringLiteral("121"), HydraulicLinkValveType::PBV,
        diameter_mm, 0.33, setting_m, HydraulicLinkValveInitialStatus::Active);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValvePbv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("121"));
        context.expect(valve != nullptr, "PBV native result must contain converted link 121");
        if (valve != nullptr)
        {
            expectActiveValve(*valve, setting_m, context);
            expectValveInputs(*valve, diameter_mm, 0.33, context);
            context.expectNear(valve->head_loss_m, setting_m, NumericTolerance{1.0e-7, 1.0e-7},
                comparison("upstream_golden.fixed_head_loss_m", 0, "Valve", "121"),
                "PBV must impose its configured pressure-breaker head loss");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testValveFcv(TestContext &context)
{
    constexpr double diameter_mm = 210.0;
    constexpr double setting_m3_per_h = 20.0;
    const Net1Fixture fixture = valveFixture(QStringLiteral("121"), HydraulicLinkValveType::FCV,
        diameter_mm, 0.44, setting_m3_per_h, HydraulicLinkValveInitialStatus::Active);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValveFcv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("121"));
        context.expect(valve != nullptr, "FCV native result must contain converted link 121");
        if (valve != nullptr)
        {
            expectActiveValve(*valve, setting_m3_per_h, context);
            expectValveInputs(*valve, diameter_mm, 0.44, context);
            context.expectNear(valve->flow_m3_per_h, setting_m3_per_h, NumericTolerance{1.0e-4, 1.0e-6},
                comparison("upstream_golden.controlled_flow_m3_per_h", 0, "Valve", "121"),
                "FCV must regulate flow to its configured setting");
            context.expect(valve->head_loss_m > 0.0, "FCV must report its regulating head loss");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

void testValveTcv(TestContext &context)
{
    constexpr double diameter_mm = 205.0;
    constexpr double setting = 12.0;
    const Net1Fixture fixture = valveFixture(QStringLiteral("121"), HydraulicLinkValveType::TCV,
        diameter_mm, 0.18, setting, HydraulicLinkValveInitialStatus::Active);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValveTcv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("121"));
        context.expect(valve != nullptr, "TCV native result must contain converted link 121");
        if (valve != nullptr)
        {
            expectActiveValve(*valve, setting, context);
            expectValveInputs(*valve, diameter_mm, 0.18, context);
            context.expect(valve->head_loss_m > 0.01, "TCV setting plus minor loss must create a measurable head loss");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}

Net1Fixture gpvFixture()
{
    Net1Fixture fixture = valveFixture(QStringLiteral("121"), HydraulicLinkValveType::GPV,
        220.0, 0.26, 0.0, HydraulicLinkValveInitialStatus::Open);

    HydraulicCurveValveHeadloss curve;
    curve.id = QStringLiteral("STEP6_GPV");
    curve.uuid = QUuid::createUuid();
    const double flows[] = {0.0, 20.0, 40.0, 80.0};
    const double losses[] = {0.0, 0.4, 2.0, 8.0};
    for (int index = 0; index < 4; index++)
    {
        HydraulicCurveValveHeadlossPoint point;
        point.flow_m3_per_h = flows[index];
        point.head_loss_m = losses[index];
        curve.points.append(point);
    }
    fixture.network.curves_valve_headloss.append(curve);
    fixture.network.links_valves.first().setting_curve_uuid = curve.uuid;
    return fixture;
}

double interpolateGpvHeadLoss(double flow_m3_per_h)
{
    const double flow = std::abs(flow_m3_per_h);
    if (flow <= 20.0)
        return flow / 20.0 * 0.4;
    if (flow <= 40.0)
        return 0.4 + (flow - 20.0) / 20.0 * 1.6;
    return 2.0 + (flow - 40.0) / 40.0 * 6.0;
}

void testValveGpv(TestContext &context)
{
    constexpr double diameter_mm = 220.0;
    const Net1Fixture fixture = gpvFixture();
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ValveGpv, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeValveResult *valve = findValve(native_timeline.results.first(), QStringLiteral("121"));
        context.expect(valve != nullptr, "GPV native result must contain converted link 121");
        if (valve != nullptr)
        {
            context.expect(valve->open, "explicitly Open GPV must remain hydraulically open");
            context.expect(!valve->active, "explicitly Open GPV must exercise the non-Active initial-status path");
            context.expect(std::abs(valve->flow_m3_per_h) > 0.0, "GPV must carry nonzero flow");
            context.expect(valve->setting > 0.0, "GPV returned setting must identify its native curve");
            expectValveInputs(*valve, diameter_mm, 0.26, context);
            context.expectNear(valve->head_loss_m, interpolateGpvHeadLoss(valve->flow_m3_per_h),
                NumericTolerance{1.0e-6, 1.0e-6},
                comparison("upstream_golden.interpolated_head_loss_m", 0, "Valve", "121"),
                "GPV head loss must follow the configured non-linear curve");
        }
    }
    compareWithWrapper(fixture, native_timeline, context);
}
}

namespace AowisEpanetTests
{
void registerUpstreamStep6Scenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{"conformance-upstream-valve-prv",
        "Exercises PRV diameter, minor loss, Active status, pressure setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "prv"}, &testValvePrv});
    registry.add(ScenarioDefinition{"conformance-upstream-valve-psv",
        "Exercises PSV diameter, minor loss, Active status, sustaining setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "psv"}, &testValvePsv});
    registry.add(ScenarioDefinition{"conformance-upstream-valve-pbv",
        "Exercises PBV diameter, minor loss, Active status, fixed head-loss setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "pbv"}, &testValvePbv});
    registry.add(ScenarioDefinition{"conformance-upstream-valve-fcv",
        "Exercises FCV diameter, minor loss, Active status, controlled-flow setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "fcv"}, &testValveFcv});
    registry.add(ScenarioDefinition{"conformance-upstream-valve-tcv",
        "Exercises TCV diameter, minor loss, Active status, throttle setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "tcv"}, &testValveTcv});
    registry.add(ScenarioDefinition{"conformance-upstream-valve-gpv",
        "Exercises GPV diameter, minor loss, explicit Open status, non-linear head-loss curve, returned curve setting, and complete valve results.",
        {"conformance", "hydraulic", "upstream", "valve", "gpv", "curve"}, &testValveGpv});
}
}
