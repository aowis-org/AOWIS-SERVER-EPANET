#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_conformance_scenarios.h"
#include "conformance/net1_fixture.h"

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
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeTankResult;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr double kMetresPerFoot = 0.3048;
constexpr double kCubicMetresPerHourPerCubicFootPerSecond = 101.94;
constexpr double kGallonsPerMinutePerCubicFootPerSecond = 448.831;
constexpr double kPsiPerFoot = 0.4333;

ComparisonContext comparison(std::string field, std::int64_t time_s, std::string entity_type, std::string entity_id)
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
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

const NativeHydraulicResult *findResult(const NativeHydraulicTimeline &timeline, std::int64_t time_s)
{
    for (const NativeHydraulicResult &result : timeline.results)
    {
        if (result.time_elapsed_s == time_s)
            return &result;
    }
    return nullptr;
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

void verifyUpstreamNet1GoldenValues(const NativeHydraulicTimeline &timeline, TestContext &context)
{
    const NativeHydraulicResult *result = findResult(timeline, 10800);
    context.expect(result != nullptr, "native Net1 timeline must contain the upstream 10,800-second checkpoint");
    if (result == nullptr)
        return;

    const NativeJunctionResult *junction = findJunction(*result, QStringLiteral("11"));
    const NativeTankResult *tank = findTank(*result, QStringLiteral("2"));
    context.expect(junction != nullptr, "native Net1 checkpoint is missing junction 11");
    context.expect(tank != nullptr, "native Net1 checkpoint is missing tank 2");

    const NumericTolerance flow_tolerance{gallonsPerMinuteToCubicMetresPerHour(0.002), 0.0};
    const NumericTolerance head_tolerance{0.002 * kMetresPerFoot, 0.0};
    const NumericTolerance pressure_tolerance{psiToMetresOfHead(0.002), 0.0};

    if (junction != nullptr)
    {
        context.expectNear(junction->total_demand_m3_per_h,
            gallonsPerMinuteToCubicMetresPerHour(179.999), flow_tolerance,
            comparison("upstream_golden.total_demand_m3_per_h", 10800, "Junction", "11"));
        context.expectNear(junction->hydraulic_head_m, 991.574 * kMetresPerFoot, head_tolerance,
            comparison("upstream_golden.hydraulic_head_m", 10800, "Junction", "11"));
        context.expectNear(junction->pressure_head_m, psiToMetresOfHead(122.006), pressure_tolerance,
            comparison("upstream_golden.pressure_head_m", 10800, "Junction", "11"));
    }

    if (tank != nullptr)
    {
        context.expectNear(tank->net_demand_m3_per_h,
            gallonsPerMinuteToCubicMetresPerHour(505.383), flow_tolerance,
            comparison("upstream_golden.net_demand_m3_per_h", 10800, "Tank", "2"));
        context.expectNear(tank->hydraulic_head_m, 978.138 * kMetresPerFoot, head_tolerance,
            comparison("upstream_golden.hydraulic_head_m", 10800, "Tank", "2"));
        context.expectNear(tank->pressure_head_m, psiToMetresOfHead(55.522), pressure_tolerance,
            comparison("upstream_golden.pressure_head_m", 10800, "Tank", "2"));
    }
}

void testNet1Conformance(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();

    NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QString::fromUtf8(AOWIS_EPANET_TEST_NET1_INP);
    native_configuration.control_ids_by_index = fixture.native_control_ids_by_index;
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(native_configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success)
        return;

    context.expect(!native_timeline.results.isEmpty(), "native Net1 run must return hydraulic timesteps");
    if (native_timeline.results.isEmpty())
        return;
    context.expect(native_timeline.results.first().time_elapsed_s == 0,
        "native Net1 timeline must start at zero seconds");
    context.expect(native_timeline.results.last().time_elapsed_s == 86400,
        "native Net1 timeline must end at the configured 24-hour duration");
    verifyUpstreamNet1GoldenValues(native_timeline, context);

    const EpanetResultRun wrapper_run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(fixture.network));
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}
}

namespace AowisEpanetTests
{
void registerNet1ConformanceScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-net1",
        "Runs upstream Net1 natively and through an independent AOWIS model, then compares every hydraulic event and applicable result field.",
        {"conformance", "hydraulic", "upstream", "net1"},
        &testNet1Conformance});
}
}
