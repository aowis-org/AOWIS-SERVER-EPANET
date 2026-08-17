#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/generated_network_stress_fixture.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/deterministic_stress_scenarios.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cstdint>
#include <string>

namespace
{
using AowisEpanetTests::GeneratedStressCase;
using AowisEpanetTests::GeneratedStressFixture;
using AowisEpanetTests::GeneratedStressTopology;
using AowisEpanetTests::NativeHydraulicTimeline;
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr std::array<GeneratedStressCase, 8> kStressCases = {
    GeneratedStressCase{"conformance-stress-chain-small-hw", GeneratedStressTopology::Chain, 8, 0, 0, 0x8c000001ULL, HydraulicHeadlossFormula::HazenWilliams},
    GeneratedStressCase{"conformance-stress-chain-medium-dw", GeneratedStressTopology::Chain, 32, 0, 0, 0x8c000002ULL, HydraulicHeadlossFormula::DarcyWeisbach},
    GeneratedStressCase{"conformance-stress-branch-medium-hw", GeneratedStressTopology::Branch, 31, 0, 0, 0x8c000003ULL, HydraulicHeadlossFormula::HazenWilliams},
    GeneratedStressCase{"conformance-stress-ring-medium-cm", GeneratedStressTopology::Ring, 24, 0, 0, 0x8c000004ULL, HydraulicHeadlossFormula::ChezyManning},
    GeneratedStressCase{"conformance-stress-grid-small-hw", GeneratedStressTopology::Grid, 16, 4, 4, 0x8c000005ULL, HydraulicHeadlossFormula::HazenWilliams},
    GeneratedStressCase{"conformance-stress-grid-medium-dw", GeneratedStressTopology::Grid, 49, 7, 7, 0x8c000006ULL, HydraulicHeadlossFormula::DarcyWeisbach},
    GeneratedStressCase{"conformance-stress-grid-large-hw", GeneratedStressTopology::Grid, 100, 10, 10, 0x8c000007ULL, HydraulicHeadlossFormula::HazenWilliams},
    GeneratedStressCase{"conformance-stress-dual-source-mesh-hw", GeneratedStressTopology::DualSourceGrid, 48, 6, 8, 0x8c000008ULL, HydraulicHeadlossFormula::HazenWilliams},
};

void runGeneratedStressCase(const GeneratedStressCase &definition, TestContext &context)
{
    const GeneratedStressFixture fixture = AowisEpanetTests::makeGeneratedStressFixture(definition);
    const GeneratedStressFixture repeated_fixture = AowisEpanetTests::makeGeneratedStressFixture(definition);

    context.expect(fixture.native_inp_text == repeated_fixture.native_inp_text,
        "fixed-seed stress generation must produce byte-identical independent native input");
    context.expect(fixture.network.uuid == repeated_fixture.network.uuid,
        "fixed-seed stress generation must produce a stable network UUID");
    context.expect(fixture.network.nodes_junctions.size() == fixture.expected_junction_count,
        "generated stress fixture must contain the requested junction count");
    context.expect(fixture.network.nodes_reservoirs.size() == fixture.expected_reservoir_count,
        "generated stress fixture must contain the expected source count");
    context.expect(fixture.network.links_pipes.size() == fixture.expected_pipe_count,
        "generated stress fixture must contain the expected pipe count");

    QTemporaryDir temporary_directory;
    context.expect(temporary_directory.isValid(), "stress scenario must create a temporary native EPANET directory");
    if (!temporary_directory.isValid())
        return;

    const QString input_path = temporary_directory.filePath(QStringLiteral("generated-stress.inp"));
    QFile input_file(input_path);
    context.expect(input_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "stress scenario must create the independent native EPANET input file");
    if (!input_file.isOpen())
        return;

    const QByteArray input_bytes = fixture.native_inp_text.toUtf8();
    const qint64 bytes_written = input_file.write(input_bytes);
    input_file.close();
    context.expect(bytes_written == input_bytes.size(),
        "stress scenario must write the complete independent native EPANET input file");
    if (bytes_written != input_bytes.size())
        return;

    NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = input_path;
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(native_configuration);
    context.expect(native_timeline.success, native_timeline.error.toStdString());
    if (!native_timeline.success)
        return;

    context.expect(native_timeline.results.size() == 7,
        "six-hour stress networks with one-hour hydraulic steps must return seven hydraulic states");
    if (!native_timeline.results.isEmpty())
    {
        context.expect(native_timeline.results.first().nodes_junctions.size() == fixture.expected_junction_count,
            "native stress run must retain every generated junction");
        context.expect(native_timeline.results.first().nodes_reservoirs.size() == fixture.expected_reservoir_count,
            "native stress run must retain every generated reservoir");
        context.expect(native_timeline.results.first().links_pipes.size() == fixture.expected_pipe_count,
            "native stress run must retain every generated pipe");
    }

    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void stressChainSmallHw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(0), context);
}

void stressChainMediumDw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(1), context);
}

void stressBranchMediumHw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(2), context);
}

void stressRingMediumCm(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(3), context);
}

void stressGridSmallHw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(4), context);
}

void stressGridMediumDw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(5), context);
}

void stressGridLargeHw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(6), context);
}

void stressDualSourceMeshHw(TestContext &context)
{
    runGeneratedStressCase(kStressCases.at(7), context);
}
}

namespace AowisEpanetTests
{
void registerDeterministicStressScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        kStressCases.at(0).scenario_name,
        "Fixed-seed small chain network: independent native CMH/H-W input versus the AOWIS model builder over a six-hour patterned-demand timeline.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressChainSmallHw});
    registry.add(ScenarioDefinition{
        kStressCases.at(1).scenario_name,
        "Fixed-seed medium chain network using Darcy-Weisbach roughness and patterned demands.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressChainMediumDw});
    registry.add(ScenarioDefinition{
        kStressCases.at(2).scenario_name,
        "Fixed-seed medium branching tree network using Hazen-Williams pipes and patterned demands.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressBranchMediumHw});
    registry.add(ScenarioDefinition{
        kStressCases.at(3).scenario_name,
        "Fixed-seed looped ring/chord network using Chezy-Manning pipes and patterned demands.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressRingMediumCm});
    registry.add(ScenarioDefinition{
        kStressCases.at(4).scenario_name,
        "Fixed-seed small two-dimensional Hazen-Williams grid with multiple loop paths.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressGridSmallHw});
    registry.add(ScenarioDefinition{
        kStressCases.at(5).scenario_name,
        "Fixed-seed medium Darcy-Weisbach grid stressing many simultaneous loop-flow solutions.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressGridMediumDw});
    registry.add(ScenarioDefinition{
        kStressCases.at(6).scenario_name,
        "Fixed-seed 100-junction Hazen-Williams grid providing the large generated-network differential case.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressGridLargeHw});
    registry.add(ScenarioDefinition{
        kStressCases.at(7).scenario_name,
        "Fixed-seed dual-source mesh with a patterned second reservoir head and cross-grid chords.",
        {"conformance", "hydraulic", "stress", "generated", "deterministic"},
        &stressDualSourceMeshHw});
}
}
