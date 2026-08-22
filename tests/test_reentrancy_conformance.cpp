#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/reentrancy_scenarios.h"

#include <array>
#include <barrier>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr std::size_t kConcurrentRunCount = 8;
constexpr NumericTolerance kReentrancyTolerance{1.0e-10, 1.0e-10};

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

const HydraulicSimulationResultNodeJunction *findJunction(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const HydraulicSimulationResultLinkPipe *findPipe(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const WaterQualitySimulationResultNodeJunction *findQualityJunction(const WaterQualitySimulationResult &result, const QString &id)
{
    for (const WaterQualitySimulationResultNodeJunction &junction : result.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

std::array<qsizetype, 3> sampleIndices(qsizetype size)
{
    if (size <= 1)
        return {0, 0, 0};
    return {0, size / 2, size - 1};
}

void compareRunToBaseline(
    const EpanetResultRun &expected,
    const EpanetResultRun &actual,
    std::string prefix,
    TestContext &context)
{
    context.expectEqual(actual.status.success, expected.status.success, comparison(prefix + ".status.success"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.state),
        static_cast<std::int64_t>(expected.state),
        comparison(prefix + ".state"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.result_timeline.validity),
        static_cast<std::int64_t>(expected.result_timeline.validity),
        comparison(prefix + ".hydraulic.validity"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.result_timeline.results.size()),
        static_cast<std::int64_t>(expected.result_timeline.results.size()),
        comparison(prefix + ".hydraulic.results.size"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.quality_results.size()),
        static_cast<std::int64_t>(expected.quality_results.size()),
        comparison(prefix + ".quality_results.size"));

    context.expectEqual(
        static_cast<std::int64_t>(actual.diagnostics.size()),
        static_cast<std::int64_t>(expected.diagnostics.size()),
        comparison(prefix + ".diagnostics.size"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.result_timeline.diagnostics.size()),
        static_cast<std::int64_t>(expected.result_timeline.diagnostics.size()),
        comparison(prefix + ".hydraulic.diagnostics.size"));

    if (actual.result_timeline.results.size() != expected.result_timeline.results.size()
        || actual.result_timeline.results.isEmpty())
    {
        return;
    }

    for (const qsizetype index : sampleIndices(actual.result_timeline.results.size()))
    {
        const HydraulicSimulationResult &expected_step = expected.result_timeline.results.at(index);
        const HydraulicSimulationResult &actual_step = actual.result_timeline.results.at(index);
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            static_cast<std::int64_t>(expected_step.time_elapsed_s),
            comparison(prefix + ".hydraulic.time", static_cast<std::int64_t>(expected_step.time_elapsed_s)));

        const HydraulicSimulationResultNodeJunction *expected_junction = findJunction(expected_step, QStringLiteral("J_MIDDLE"));
        const HydraulicSimulationResultNodeJunction *actual_junction = findJunction(actual_step, QStringLiteral("J_MIDDLE"));
        context.expect(expected_junction != nullptr && actual_junction != nullptr, "reentrancy comparison requires junction J_MIDDLE in both hydraulic timelines");
        if (expected_junction != nullptr && actual_junction != nullptr)
        {
            context.expectNear(
                actual_junction->hydraulic_head_m,
                expected_junction->hydraulic_head_m,
                kReentrancyTolerance,
                comparison(prefix + ".junction.head", static_cast<std::int64_t>(expected_step.time_elapsed_s), "Junction", "J_MIDDLE"));
            context.expectNear(
                actual_junction->total_demand_m3_per_h,
                expected_junction->total_demand_m3_per_h,
                kReentrancyTolerance,
                comparison(prefix + ".junction.demand", static_cast<std::int64_t>(expected_step.time_elapsed_s), "Junction", "J_MIDDLE"));
        }

        const HydraulicSimulationResultLinkPipe *expected_pipe = findPipe(expected_step, QStringLiteral("P_FIRST"));
        const HydraulicSimulationResultLinkPipe *actual_pipe = findPipe(actual_step, QStringLiteral("P_FIRST"));
        context.expect(expected_pipe != nullptr && actual_pipe != nullptr, "reentrancy comparison requires pipe P_FIRST in both hydraulic timelines");
        if (expected_pipe != nullptr && actual_pipe != nullptr)
        {
            context.expectNear(
                actual_pipe->flow_m3_per_h,
                expected_pipe->flow_m3_per_h,
                kReentrancyTolerance,
                comparison(prefix + ".pipe.flow", static_cast<std::int64_t>(expected_step.time_elapsed_s), "Pipe", "P_FIRST"));
        }
    }

    if (actual.quality_results.size() != expected.quality_results.size())
        return;

    for (int quality_index = 0; quality_index < actual.quality_results.size(); quality_index++)
    {
        const EpanetQualityResult &expected_quality = expected.quality_results.at(quality_index);
        const EpanetQualityResult &actual_quality = actual.quality_results.at(quality_index);
        context.expectEqual(
            static_cast<std::int64_t>(actual_quality.state),
            static_cast<std::int64_t>(expected_quality.state),
            comparison(prefix + ".quality.state"));
        context.expectEqual(
            static_cast<std::int64_t>(actual_quality.result_timeline.analysis),
            static_cast<std::int64_t>(expected_quality.result_timeline.analysis),
            comparison(prefix + ".quality.analysis"));
        context.expectEqual(
            static_cast<std::int64_t>(actual_quality.result_timeline.validity),
            static_cast<std::int64_t>(expected_quality.result_timeline.validity),
            comparison(prefix + ".quality.validity"));
        context.expectEqual(
            static_cast<std::int64_t>(actual_quality.result_timeline.results.size()),
            static_cast<std::int64_t>(expected_quality.result_timeline.results.size()),
            comparison(prefix + ".quality.results.size"));

        context.expectEqual(
            static_cast<std::int64_t>(actual_quality.result_timeline.diagnostics.size()),
            static_cast<std::int64_t>(expected_quality.result_timeline.diagnostics.size()),
            comparison(prefix + ".quality.diagnostics.size"));

        if (actual_quality.result_timeline.results.size() != expected_quality.result_timeline.results.size()
            || actual_quality.result_timeline.results.isEmpty())
        {
            continue;
        }

        for (const qsizetype index : sampleIndices(actual_quality.result_timeline.results.size()))
        {
            const WaterQualitySimulationResult &expected_step = expected_quality.result_timeline.results.at(index);
            const WaterQualitySimulationResult &actual_step = actual_quality.result_timeline.results.at(index);
            context.expectEqual(
                static_cast<std::int64_t>(actual_step.time_elapsed_s),
                static_cast<std::int64_t>(expected_step.time_elapsed_s),
                comparison(prefix + ".quality.time", static_cast<std::int64_t>(expected_step.time_elapsed_s)));

            const WaterQualitySimulationResultNodeJunction *expected_junction = findQualityJunction(expected_step, QStringLiteral("J_MIDDLE"));
            const WaterQualitySimulationResultNodeJunction *actual_junction = findQualityJunction(actual_step, QStringLiteral("J_MIDDLE"));
            context.expect(expected_junction != nullptr && actual_junction != nullptr, "reentrancy comparison requires junction J_MIDDLE in both quality timelines");
            if (expected_junction == nullptr || actual_junction == nullptr)
                continue;

            context.expectNear(
                actual_junction->chemical_concentration_mg_per_l,
                expected_junction->chemical_concentration_mg_per_l,
                kReentrancyTolerance,
                comparison(prefix + ".quality.chemical", static_cast<std::int64_t>(expected_step.time_elapsed_s), "Junction", "J_MIDDLE"));
            context.expectNear(
                actual_junction->water_age_h,
                expected_junction->water_age_h,
                kReentrancyTolerance,
                comparison(prefix + ".quality.age", static_cast<std::int64_t>(expected_step.time_elapsed_s), "Junction", "J_MIDDLE"));
        }
    }
}

NetworkHydraulic reentrancyNetwork(double source_head_m, double sink_demand_m3_per_h)
{
    NetworkHydraulic network;
    network.id = QStringLiteral("reentrancy-conformance");
    network.uuid = QUuid::createUuid();
    network.duration_s = 6 * 3600;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;
    network.timestep_report_s = 1800;

    HydraulicNodeReservoir source;
    source.id = QStringLiteral("R_SOURCE");
    source.uuid = QUuid::createUuid();
    source.hydraulic_head_m = source_head_m;
    source.initial_chemical_concentration_mg_per_l = 1.0;
    source.quality_source.type = HydraulicNodeQualitySourceType::Concentration;
    source.quality_source.chemical_concentration_mg_per_l = 1.0;

    HydraulicNodeJunction middle;
    middle.id = QStringLiteral("J_MIDDLE");
    middle.uuid = QUuid::createUuid();
    middle.elevation_m = 10.0;

    HydraulicNodeJunction sink;
    sink.id = QStringLiteral("J_SINK");
    sink.uuid = QUuid::createUuid();
    sink.elevation_m = 5.0;
    HydraulicNodeJunctionDemand demand;
    demand.base_demand_m3_per_h = sink_demand_m3_per_h;
    sink.demands.append(demand);

    HydraulicLinkPipe first_pipe;
    first_pipe.id = QStringLiteral("P_FIRST");
    first_pipe.uuid = QUuid::createUuid();
    first_pipe.node_uuid_from = source.uuid;
    first_pipe.node_uuid_to = middle.uuid;
    first_pipe.length_calculated_m = 700.0;
    first_pipe.diameter_mm = 200.0;
    first_pipe.roughness_hazen_williams = 120.0;

    HydraulicLinkPipe second_pipe;
    second_pipe.id = QStringLiteral("P_SECOND");
    second_pipe.uuid = QUuid::createUuid();
    second_pipe.node_uuid_from = middle.uuid;
    second_pipe.node_uuid_to = sink.uuid;
    second_pipe.length_calculated_m = 900.0;
    second_pipe.diameter_mm = 150.0;
    second_pipe.roughness_hazen_williams = 105.0;

    network.nodes_reservoirs.append(source);
    network.nodes_junctions.append(middle);
    network.nodes_junctions.append(sink);
    network.links_pipes.append(first_pipe);
    network.links_pipes.append(second_pipe);
    return network;
}

EpanetRunRequest chemicalRequest()
{
    WaterQualitySolverOptions quality;
    quality.analysis = WaterQualityAnalysisType::Chemical;
    quality.chemical_name = QStringLiteral("Chlorine");
    return AowisEpanetTests::makeRunRequest(reentrancyNetwork(100.0, 12.0), quality);
}

EpanetRunRequest waterAgeRequest()
{
    WaterQualitySolverOptions quality;
    quality.analysis = WaterQualityAnalysisType::WaterAge;
    return AowisEpanetTests::makeRunRequest(reentrancyNetwork(125.0, 20.0), quality);
}

void scenarioIndependentConcurrentRunners(TestContext &context)
{
    const std::array<EpanetRunRequest, 2> requests = {chemicalRequest(), waterAgeRequest()};
    const std::array<EpanetResultRun, 2> baselines = {
        EpanetRunner().run(requests.at(0)),
        EpanetRunner().run(requests.at(1))};

    context.expect(baselines.at(0).state == EpanetRunState::Success, "chemical reentrancy baseline must succeed");
    context.expect(baselines.at(1).state == EpanetRunState::Success, "water-age reentrancy baseline must succeed");
    if (baselines.at(0).result_timeline.results.isEmpty() || baselines.at(1).result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResultNodeJunction *baseline_a_junction = findJunction(
        baselines.at(0).result_timeline.results.first(),
        QStringLiteral("J_MIDDLE"));
    const HydraulicSimulationResultNodeJunction *baseline_b_junction = findJunction(
        baselines.at(1).result_timeline.results.first(),
        QStringLiteral("J_MIDDLE"));
    context.expect(baseline_a_junction != nullptr && baseline_b_junction != nullptr, "reentrancy baselines must contain junction J_MIDDLE");
    if (baseline_a_junction != nullptr && baseline_b_junction != nullptr)
    {
        context.expect(
            std::abs(baseline_a_junction->hydraulic_head_m - baseline_b_junction->hydraulic_head_m) > 1.0e-3,
            "concurrent reentrancy fixtures must produce distinct hydraulic states");
    }

    std::array<EpanetResultRun, kConcurrentRunCount> concurrent_results;
    std::barrier start_barrier(static_cast<std::ptrdiff_t>(kConcurrentRunCount));
    std::vector<std::thread> threads;
    threads.reserve(kConcurrentRunCount);

    for (std::size_t index = 0; index < kConcurrentRunCount; index++)
    {
        threads.emplace_back([&requests, &concurrent_results, &start_barrier, index]()
        {
            const std::size_t request_index = index % requests.size();
            EpanetRunner runner;
            start_barrier.arrive_and_wait();
            concurrent_results.at(index) = runner.run(requests.at(request_index));
        });
    }

    for (std::thread &thread : threads)
        thread.join();

    for (std::size_t index = 0; index < kConcurrentRunCount; index++)
    {
        const std::size_t request_index = index % requests.size();
        compareRunToBaseline(
            baselines.at(request_index),
            concurrent_results.at(index),
            std::string("concurrent-run-") + std::to_string(index),
            context);
    }
}
}

namespace AowisEpanetTests
{
void registerReentrancyScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-reentrancy-independent-runners",
        "Runs distinct hydraulic and quality requests simultaneously through independent EpanetRunner instances and compares every concurrent run against its sequential baseline.",
        {"conformance", "hydraulic", "quality", "reentrancy", "proof"},
        &scenarioIndependentConcurrentRunners});
}
}
