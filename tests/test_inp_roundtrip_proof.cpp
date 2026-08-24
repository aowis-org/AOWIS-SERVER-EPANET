#include "conformance/inp_roundtrip_proof.h"
#include "conformance/inp_roundtrip_proof_scenarios.h"

namespace
{
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

void scenarioOfficialNet1RoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(context, QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));
}

void scenarioOfficialNet2RoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(context, QStringLiteral(AOWIS_EPANET_TEST_NET2_INP));
}

void scenarioOfficialNet3RoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(context, QStringLiteral(AOWIS_EPANET_TEST_NET3_INP));
}

void scenarioUpstreamDemandCategoriesRoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(
        context, QStringLiteral(AOWIS_EPANET_TEST_UPSTREAM_DEMAND_CATEGORIES_INP));
}

void scenarioUpstreamTankOverflowRoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(
        context, QStringLiteral(AOWIS_EPANET_TEST_UPSTREAM_TANK_OVERFLOW_INP));
}

void scenarioUpstreamNetworkBuilderRoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(
        context, QStringLiteral(AOWIS_EPANET_TEST_UPSTREAM_NETWORK_BUILDER_INP));
}

void scenarioUpstreamNetworkBuilderSmallRoundTrip(TestContext &context)
{
    AowisEpanetTests::proveInpRoundTrip(
        context, QStringLiteral(AOWIS_EPANET_TEST_UPSTREAM_NETWORK_BUILDER_SMALL_INP));
}
}

namespace AowisEpanetTests
{
void registerInpRoundTripProofScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-import-quality-roundtrip-official-net1",
        "Imports official Net1, proves native-vs-AOWIS hydraulics and quality, exports it, reopens it natively, and repeats structural and numerical proof.",
        {"conformance", "import", "quality", "proof"},
        &scenarioOfficialNet1RoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-roundtrip-official-net2",
        "Imports official Net2, including its chemical source/pattern model, then proves structural, hydraulic, quality, export, and native-reopen equivalence.",
        {"conformance", "import", "quality", "proof"},
        &scenarioOfficialNet2RoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-roundtrip-official-net3",
        "Imports official Net3, including source trace and controls, then proves structural, hydraulic, quality, export, and native-reopen equivalence.",
        {"conformance", "import", "quality", "proof"},
        &scenarioOfficialNet3RoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-roundtrip-upstream-demand-categories",
        "Round-trips the upstream demand-category fixture while preserving category inventory and native hydraulic/quality behavior.",
        {"conformance", "import", "quality", "proof"},
        &scenarioUpstreamDemandCategoriesRoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-quality-roundtrip-upstream-tank-overflow",
        "Round-trips the upstream tank-overflow fixture while preserving tank overflow semantics and native hydraulic/quality behavior.",
        {"conformance", "import", "quality", "proof"},
        &scenarioUpstreamTankOverflowRoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-roundtrip-upstream-network-builder",
        "Round-trips an upstream network-builder fixture with QUALITY NONE and no source map geometry, proving structural and hydraulic equivalence after generated layout/export.",
        {"conformance", "import", "hydraulic", "proof"},
        &scenarioUpstreamNetworkBuilderRoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-import-roundtrip-upstream-network-builder-small",
        "Round-trips the upstream small network-builder fixture, including its pump/tank/curve topology, through native reopen and hydraulic equivalence proof.",
        {"conformance", "import", "hydraulic", "proof"},
        &scenarioUpstreamNetworkBuilderSmallRoundTrip});
}
}
