#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/deterministic_quality_stress_scenarios.h"
#include "conformance/generated_quality_stress_fixture.h"
#include "conformance/native_quality_reference_runner.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::GeneratedQualityStressCase;
using AowisEpanetTests::GeneratedQualityStressFixture;
using AowisEpanetTests::GeneratedStressTopology;
using AowisEpanetTests::NativeQualityReferenceStep;
using AowisEpanetTests::NativeQualityReferenceTimeline;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr NumericTolerance kQualityTolerance{1.0e-8, 1.0e-7};
constexpr NumericTolerance kMassBalanceTolerance{1.0e-8, 1.0e-7};
constexpr NumericTolerance kSourceMassTolerance{1.0e-8, 1.0e-7};

constexpr std::array<GeneratedQualityStressCase, 6> kQualityStressCases = {{
    {"conformance-quality-stress-chain-chemical-pattern", GeneratedStressTopology::Chain, 12, 0, 0, 0x94000001ULL, HydraulicHeadlossFormula::HazenWilliams, WaterQualityAnalysisType::Chemical, 6 * 3600, 1800, 300, true, false},
    {"conformance-quality-stress-branch-water-age", GeneratedStressTopology::Branch, 31, 0, 0, 0x94000002ULL, HydraulicHeadlossFormula::DarcyWeisbach, WaterQualityAnalysisType::WaterAge, 8 * 3600, 3600, 600, false, false},
    {"conformance-quality-stress-ring-source-trace", GeneratedStressTopology::Ring, 24, 0, 0, 0x94000003ULL, HydraulicHeadlossFormula::ChezyManning, WaterQualityAnalysisType::SourceTrace, 4 * 3600, 1200, 120, false, false},
    {"conformance-quality-stress-grid-reactions-dw", GeneratedStressTopology::Grid, 49, 7, 7, 0x94000004ULL, HydraulicHeadlossFormula::DarcyWeisbach, WaterQualityAnalysisType::Chemical, 8 * 3600, 1800, 300, true, true},
    {"conformance-quality-stress-dual-source-reactions-hw", GeneratedStressTopology::DualSourceGrid, 48, 6, 8, 0x94000005ULL, HydraulicHeadlossFormula::HazenWilliams, WaterQualityAnalysisType::Chemical, 12 * 3600, 3600, 900, true, true},
    {"conformance-quality-stress-grid-large-long-cm", GeneratedStressTopology::Grid, 100, 10, 10, 0x94000006ULL, HydraulicHeadlossFormula::ChezyManning, WaterQualityAnalysisType::Chemical, 24 * 3600, 3600, 600, true, false},
}};

ComparisonContext comparison(std::int64_t time_s, std::string field, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

template<typename ResultType>
double qualityValue(const ResultType &result, WaterQualityAnalysisType analysis)
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

template<typename ResultType>
void compareNodes(TestContext &context, const QList<ResultType> &actual, const NativeQualityReferenceStep &expected, WaterQualityAnalysisType analysis, const char *entity_type)
{
    for (const ResultType &result : actual)
    {
        context.expect(expected.node_quality.contains(result.id), "generated native quality reference is missing an AOWIS node ID");
        if (!expected.node_quality.contains(result.id))
            continue;

        context.expectNear(
            qualityValue(result, analysis),
            expected.node_quality.value(result.id),
            kQualityTolerance,
            comparison(expected.time_s, "quality", entity_type, result.id.toStdString()));
        context.expectNear(
            result.source_mass_flow_mg_per_min,
            expected.node_source_mass_mg_per_min.value(result.id, 0.0),
            kSourceMassTolerance,
            comparison(expected.time_s, "source_mass_flow_mg_per_min", entity_type, result.id.toStdString()));
    }
}

template<typename ResultType>
void compareLinks(TestContext &context, const QList<ResultType> &actual, const NativeQualityReferenceStep &expected, WaterQualityAnalysisType analysis, const char *entity_type)
{
    for (const ResultType &result : actual)
    {
        context.expect(expected.link_quality.contains(result.id), "generated native quality reference is missing an AOWIS link ID");
        if (!expected.link_quality.contains(result.id))
            continue;

        context.expectNear(
            qualityValue(result, analysis),
            expected.link_quality.value(result.id),
            kQualityTolerance,
            comparison(expected.time_s, "quality", entity_type, result.id.toStdString()));
    }
}

void compareGeneratedQualityCase(const GeneratedQualityStressCase &definition, TestContext &context)
{
    const GeneratedQualityStressFixture fixture = AowisEpanetTests::makeGeneratedQualityStressFixture(definition);
    const GeneratedQualityStressFixture repeated_fixture = AowisEpanetTests::makeGeneratedQualityStressFixture(definition);

    context.expect(fixture.native_inp_text == repeated_fixture.native_inp_text,
        "fixed-seed quality stress generation must produce byte-identical native INP input");
    context.expect(fixture.network.uuid == repeated_fixture.network.uuid,
        "fixed-seed quality stress generation must produce a stable network UUID");

    QTemporaryDir temporary_directory;
    context.expect(temporary_directory.isValid(), "quality stress scenario must create a temporary native EPANET directory");
    if (!temporary_directory.isValid())
        return;

    const QString input_path = temporary_directory.filePath(QStringLiteral("generated-quality-stress.inp"));
    QFile input_file(input_path);
    context.expect(input_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "quality stress scenario must create the independent native EPANET input file");
    if (!input_file.isOpen())
        return;

    const QByteArray input_bytes = fixture.native_inp_text.toUtf8();
    const qint64 bytes_written = input_file.write(input_bytes);
    input_file.close();
    context.expect(bytes_written == input_bytes.size(),
        "quality stress scenario must write the complete independent native EPANET input file");
    if (bytes_written != input_bytes.size())
        return;

    const NativeQualityReferenceTimeline native = AowisEpanetTests::runNativeQualityReference(input_path, fixture.network);
    context.expect(native.success, native.error.toStdString());
    if (!native.success)
        return;

    const EpanetResultRun actual_run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(fixture.network, fixture.quality_options));
    context.expectEqual(
        static_cast<std::int64_t>(actual_run.quality_results.size()),
        std::int64_t{1},
        comparison(-1, "quality_results.size"));
    if (actual_run.quality_results.size() != 1)
        return;
    const WaterQualitySimulationResultTimeline &actual = actual_run.quality_results.constFirst().result_timeline;
    context.expect(actual_run.result_timeline.validity == HydraulicSimulationResultValidity::Valid,
        "generated quality stress run must preserve a valid hydraulic timeline");
    context.expect(actual.status.success, "generated quality stress run must finish quality successfully");
    context.expect(actual.validity == WaterQualitySimulationResultValidity::Valid,
        "generated quality stress run must produce a valid quality timeline");
    context.expect(actual.analysis == definition.analysis,
        "generated quality stress timeline must retain the configured analysis type");
    context.expectEqual(
        static_cast<std::int64_t>(native.results.size()),
        static_cast<std::int64_t>(fixture.expected_quality_sample_count),
        comparison(-1, "native_quality_timeline.size"));
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(native.results.size()),
        comparison(-1, "quality_timeline.size"));

    const int step_count = qMin(actual.results.size(), native.results.size());
    for (int index = 0; index < step_count; index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(index);
        const NativeQualityReferenceStep &expected_step = native.results.at(index);
        const std::int64_t expected_regular_time_s = static_cast<std::int64_t>(index) * definition.quality_timestep_s;

        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            expected_step.time_s,
            comparison(expected_step.time_s, "quality_timestep.native"));
        context.expectEqual(
            expected_step.time_s,
            expected_regular_time_s,
            comparison(expected_step.time_s, "quality_timestep.regular"));
        context.expect(actual_step.status.success, "every generated quality stress timestep must carry a successful status");
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.nodes_junctions.size() + actual_step.nodes_reservoirs.size() + actual_step.nodes_tanks.size()),
            static_cast<std::int64_t>(expected_step.node_quality.size()),
            comparison(expected_step.time_s, "node_quality_result_count"));
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.links_pipes.size() + actual_step.links_pumps.size() + actual_step.links_valves.size()),
            static_cast<std::int64_t>(expected_step.link_quality.size()),
            comparison(expected_step.time_s, "link_quality_result_count"));

        compareNodes(context, actual_step.nodes_junctions, expected_step, actual.analysis, "Junction");
        compareNodes(context, actual_step.nodes_reservoirs, expected_step, actual.analysis, "Reservoir");
        compareNodes(context, actual_step.nodes_tanks, expected_step, actual.analysis, "Tank");
        compareLinks(context, actual_step.links_pipes, expected_step, actual.analysis, "Pipe");
        compareLinks(context, actual_step.links_pumps, expected_step, actual.analysis, "Pump");
        compareLinks(context, actual_step.links_valves, expected_step, actual.analysis, "Valve");
        context.expectNear(
            actual_step.statistics.mass_balance_ratio,
            expected_step.mass_balance_ratio,
            kMassBalanceTolerance,
            comparison(expected_step.time_s, "mass_balance_ratio", "QualitySolver"));
        context.expect(std::isfinite(actual_step.statistics.mass_balance_ratio),
            "generated quality stress mass-balance ratio must remain finite");
    }
}

void qualityStressChainChemicalPattern(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(0), context);
}

void qualityStressBranchWaterAge(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(1), context);
}

void qualityStressRingSourceTrace(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(2), context);
}

void qualityStressGridReactionsDw(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(3), context);
}

void qualityStressDualSourceReactionsHw(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(4), context);
}

void qualityStressGridLargeLongCm(TestContext &context)
{
    compareGeneratedQualityCase(kQualityStressCases.at(5), context);
}

void qualityStressCancellationPositions(TestContext &context)
{
    GeneratedQualityStressCase definition = kQualityStressCases.at(0);
    definition.scenario_name = "conformance-quality-stress-cancellation-positions";
    definition.junction_count = 8;
    definition.duration_s = 1800;
    definition.hydraulic_timestep_s = 1800;
    definition.quality_timestep_s = 300;
    const GeneratedQualityStressFixture fixture = AowisEpanetTests::makeGeneratedQualityStressFixture(definition);

    std::vector<int> partial_result_counts;
    for (int threshold = 1; threshold <= 80 && partial_result_counts.size() < 3; threshold++)
    {
        int checks = 0;
        const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(fixture.network, fixture.quality_options), [&checks, threshold]()
        {
            checks++;
            return checks >= threshold;
        });

        if (!run.cancelled
            || run.result_timeline.validity != HydraulicSimulationResultValidity::Valid
            || run.quality_results.size() != 1
            || run.quality_results.constFirst().result_timeline.validity != WaterQualitySimulationResultValidity::Partial
            || run.quality_results.constFirst().result_timeline.results.isEmpty())
        {
            continue;
        }

        const int result_count = run.quality_results.constFirst().result_timeline.results.size();
        if (!partial_result_counts.empty() && result_count <= partial_result_counts.back())
            continue;

        partial_result_counts.push_back(result_count);
        context.expect(result_count < fixture.expected_quality_sample_count,
            "quality cancellation stress probe must stop before the complete quality timeline");
        context.expect(run.result_timeline.status.success,
            "quality cancellation stress probe must preserve successful hydraulic status");
    }

    context.expect(partial_result_counts.size() == 3,
        "quality cancellation stress must demonstrate three distinct mid-quality cancellation positions");
}
}

namespace AowisEpanetTests
{
void registerDeterministicQualityStressScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(0).scenario_name,
        "Fixed-seed chemical chain with patterned concentration dosing and a 300-second quality step.",
        {"conformance", "quality", "stress", "generated", "deterministic", "chemical", "pattern"},
        &qualityStressChainChemicalPattern});
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(1).scenario_name,
        "Fixed-seed branching Darcy-Weisbach network exercising water-age transport over an eight-hour run.",
        {"conformance", "quality", "stress", "generated", "deterministic", "age"},
        &qualityStressBranchWaterAge});
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(2).scenario_name,
        "Fixed-seed looped Chezy-Manning network exercising source trace at a 120-second quality timestep.",
        {"conformance", "quality", "stress", "generated", "deterministic", "trace"},
        &qualityStressRingSourceTrace});
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(3).scenario_name,
        "Fixed-seed 49-junction Darcy-Weisbach grid with patterned chemical dosing, roughness-correlated wall reactions, and per-pipe reaction overrides.",
        {"conformance", "quality", "stress", "generated", "deterministic", "chemical", "reaction", "pattern"},
        &qualityStressGridReactionsDw});
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(4).scenario_name,
        "Fixed-seed dual-source Hazen-Williams mesh with independent source patterns and reaction-heavy quality execution.",
        {"conformance", "quality", "stress", "generated", "deterministic", "chemical", "reaction", "pattern", "dual-source"},
        &qualityStressDualSourceReactionsHw});
    registry.add(ScenarioDefinition{
        kQualityStressCases.at(5).scenario_name,
        "Fixed-seed 100-junction Chezy-Manning grid proving a 24-hour generated chemical-quality differential timeline.",
        {"conformance", "quality", "stress", "generated", "deterministic", "chemical", "long-run"},
        &qualityStressGridLargeLongCm});
    registry.add(ScenarioDefinition{
        "conformance-quality-stress-cancellation-positions",
        "Finds three deterministic cancellation points during quality stepping and proves completed hydraulics remain valid at each point.",
        {"conformance", "quality", "stress", "generated", "deterministic", "cancellation"},
        &qualityStressCancellationPositions});
}
}
