#include "conformance/msx_vendor_smoke_scenarios.h"
#include "conformance/net1_fixture.h"

#include <aowis/epanet/epanet_runner.h>

#include "../src/lib/internal/epanet_msx_project.h"
#include "../src/lib/internal/epanet_multi_quality_run_executor.h"
#include "../src/lib/internal/epanet_prepared_project.h"

#include <epanetmsx.h>

#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include <functional>
#include <string>
#include <utility>

namespace
{
std::string msxErrorMessage(int code)
{
    std::string buffer(256, '\0');
    MSXgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

// Mirrors the identically-named helper in other test files (e.g.
// test_export_fidelity_conformance.cpp): each file defines its own, since
// it is a small anonymous-namespace convenience, not a shared fixture.
NetworkHydraulic cleanNet1()
{
    AowisEpanetTests::Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

void addSimpleChlorineMsxModel(NetworkHydraulic &network)
{
    const HydraulicNodeReservoir &source_node = network.nodes_reservoirs.first();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    chlorine.type = MultiSpeciesSpeciesType::Bulk;
    chlorine.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(chlorine);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = chlorine.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = chlorine.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesNodeInitialQuality initial_quality;
    initial_quality.node_uuid = source_node.uuid;
    initial_quality.species_uuid = chlorine.uuid;
    initial_quality.concentration = 1.0;
    network.multi_species.initial_quality_nodes.append(initial_quality);
}

bool timelineHasSpeciesValue(const MultiSpeciesSimulationResultTimeline &timeline)
{
    for (const MultiSpeciesSimulationResult &step : timeline.results)
    {
        for (const MultiSpeciesSimulationResultNodeJunction &junction_result : step.nodes_junctions)
        {
            if (!junction_result.species_values.isEmpty())
                return true;
        }
    }
    return false;
}

// Exercises the raw vendored MSXENopen/MSXopen/MSXsolveH/MSXinit/MSXstep/
// MSXgetqual/MSXclose/MSXENclose sequence directly, with none of AOWIS's own
// adapter code involved. This is deliberately not a conformance or contract
// scenario: it proves the EPANET-MSX submodule is present, builds, links
// against this adapter's own vendored EPANET rather than MSX's bundled
// EPANET2.2 copy, and can run a real reaction model to a real result -- the
// foundation the AOWIS MSX adapter is built on, not a claim about AOWIS's own
// translation of that foundation.
void scenarioMsxVendorOpenInitStepClose(AowisEpanetTests::TestContext &context)
{
    QTemporaryDir scratch_dir;
    context.expect(scratch_dir.isValid(), "must be able to create a scratch directory for MSX report/output files");
    if (!scratch_dir.isValid())
        return;

    const std::string inp_path = AOWIS_EPANET_MSX_EXAMPLE_INP;
    const std::string rpt_path = (scratch_dir.path() + QStringLiteral("/example.rpt")).toStdString();
    const std::string out_path = (scratch_dir.path() + QStringLiteral("/example.out")).toStdString();
    std::string msx_path = AOWIS_EPANET_MSX_EXAMPLE_MSX;
    std::string as3_species_name = "AS3";
    std::string node_d_name = "D";

    int errcode = MSXENopen(inp_path.c_str(), rpt_path.c_str(), out_path.c_str());
    context.expect(errcode == 0, "MSXENopen must open the vendored EPANET-MSX example network without error: " + msxErrorMessage(errcode));
    if (errcode != 0)
        return;

    errcode = MSXopen(msx_path.data());
    context.expect(errcode == 0, "MSXopen must open the vendored EPANET-MSX example reaction model without error: " + msxErrorMessage(errcode));

    if (errcode == 0)
    {
        errcode = MSXsolveH();
        context.expect(errcode == 0, "MSXsolveH must solve hydraulics for the example network without error: " + msxErrorMessage(errcode));
    }

    int species_index = -1;
    int node_index = -1;
    if (errcode == 0)
    {
        const int species_errcode = MSXgetindex(MSX_SPECIES, as3_species_name.data(), &species_index);
        context.expect(species_errcode == 0 && species_index > 0, "MSXgetindex must resolve the AS3 species declared in the example reaction model");

        // MSXgetindex only resolves MSX's own object types (species,
        // constants, parameters, patterns) -- its switch has no MSX_NODE
        // case, so it would return ERR_INVALID_OBJECT_TYPE for a node
        // lookup. Nodes belong to the underlying EPANET project, so they are
        // resolved through the legacy EN toolkit MSX itself is built on.
        const int node_errcode = ENgetnodeindex(node_d_name.c_str(), &node_index);
        context.expect(node_errcode == 0 && node_index > 0, "ENgetnodeindex must resolve junction D declared in the example network");
    }

    if (errcode == 0)
    {
        errcode = MSXinit(0);
        context.expect(errcode == 0, "MSXinit must initialize water-quality state without error: " + msxErrorMessage(errcode));
    }

    int steps_taken = 0;
    if (errcode == 0)
    {
        double t = 0.0;
        double tleft = 1.0;
        const int max_steps = 100000;
        while (errcode == 0 && tleft > 0.0 && steps_taken < max_steps)
        {
            errcode = MSXstep(&t, &tleft);
            steps_taken++;
        }
        context.expect(errcode == 0, "MSXstep must advance through the full example duration without error: " + msxErrorMessage(errcode));
        context.expect(steps_taken > 1 && steps_taken < max_steps, "MSXstep must take a bounded, non-trivial number of quality steps to cover the example's duration");
    }

    if (errcode == 0 && species_index > 0 && node_index > 0)
    {
        double concentration = -1.0;
        const int qual_errcode = MSXgetqual(MSX_NODE, node_index, species_index, &concentration);
        context.expect(qual_errcode == 0, "MSXgetqual must read an AS3 result at junction D without error: " + msxErrorMessage(qual_errcode));
        context.expect(concentration >= 0.0, "AS3 concentration at junction D must be a physically valid non-negative value after the full run");
    }

    MSXclose();
    MSXENclose();
}

// Proves the Lew-style handoff directly at AOWIS's internal boundary:
// hydraulics are solved by the normal handle-based AOWIS EPANET executor,
// persisted once, and EpanetMsxProject consumes that exact file through
// MSXusehydfile. EpanetRunner is intentionally not involved yet.
void scenarioMsxAowisHydraulicHandoff(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    addSimpleChlorineMsxModel(network);

    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    context.expect(status.success, "AOWIS project must prepare before producing hydraulics for MSX");
    if (!status.success)
        return;

    EpanetMultiQualityRunExecutor hydraulic_executor(prepared_project, true);
    EpanetResultRun hydraulic_result;
    hydraulic_result = hydraulic_executor.run(std::move(hydraulic_result));
    context.expect(hydraulic_result.result_timeline.status.success, "AOWIS hydraulics must succeed before the MSX handoff");
    context.expect(hydraulic_executor.hasHydraulicFile(), "AOWIS hydraulic executor must persist the .hyd file requested for MSX");
    if (!hydraulic_result.result_timeline.status.success || !hydraulic_executor.hasHydraulicFile())
        return;

    const QString hydraulic_file_path = hydraulic_executor.hydraulicFilePath();
    context.expect(QFileInfo(hydraulic_file_path).size() > 0, "MSX handoff hydraulic file must be non-empty");

    EpanetMsxProject msx_project;
    MultiSpeciesSimulationResultTimeline timeline;
    bool cancelled = false;
    status = msx_project.run(
        network,
        MultiSpeciesRunOptions{},
        hydraulic_file_path,
        timeline,
        std::function<bool()>(),
        cancelled);

    context.expect(status.success, "EpanetMsxProject must consume AOWIS hydraulics through MSXusehydfile without solving hydraulics again");
    context.expect(!cancelled, "MSX hydraulic-handoff proof must complete without cancellation");
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "MSX hydraulic-handoff timeline must be Valid");
    context.expect(!timeline.results.isEmpty(), "MSX hydraulic-handoff timeline must contain quality timesteps");
    context.expect(timelineHasSpeciesValue(timeline), "MSX hydraulic-handoff results must contain at least one junction species value");
}

// Runs a real reaction model through AOWIS's own EpanetRunner -- not the raw
// MSX toolkit directly, as above, but the actual translation path a caller
// of this adapter would use: NetworkHydraulic with multi_species attached,
// EpanetRunRequest::multi_species_run set, EpanetRunner::run(). Proves
// EpanetMsxProject's export -> MSXENopen/MSXopen/MSXusehydfile/MSXinit/MSXstep
// -> result-reading sequence produces real, non-trivial species
// concentrations, not just that the pieces individually work in isolation.
void scenarioMsxIntegrationEndToEnd(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeReservoir &source_node = network.nodes_reservoirs.first();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    chlorine.type = MultiSpeciesSpeciesType::Bulk;
    chlorine.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(chlorine);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = chlorine.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = chlorine.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(tank_reaction);

    // A reservoir's water quality belongs in its initial-quality value, not
    // a SOURCES entry: EPANET's own documentation is explicit that CONCEN
    // sources are for modeling a booster injecting quality into external
    // (negative-demand) inflow at a node, not for setting what concentration
    // a reservoir already contains -- and the vendored EPANET-MSX example
    // (Examples/example.msx) sets its own reservoir's arsenic level the same
    // way, via [QUALITY] NODE, with no [SOURCES] entry at all.
    MultiSpeciesNodeInitialQuality initial_quality;
    initial_quality.node_uuid = source_node.uuid;
    initial_quality.species_uuid = chlorine.uuid;
    initial_quality.concentration = 1.0;
    network.multi_species.initial_quality_nodes.append(initial_quality);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "a network with a valid multi-species model must run successfully end to end");
    context.expect(result.result_timeline.status.success, "the normal hydraulic timeline must also succeed alongside the multi-species run");
    context.expect(result.multi_species_result.has_value(), "a request with multi_species_run set must populate multi_species_result");

    if (result.multi_species_result.has_value())
    {
        const EpanetMultiSpeciesResult &multi_species_result = result.multi_species_result.value();
        context.expect(multi_species_result.state == EpanetRunState::Success, "multi_species_result.state must be Success for a valid run");
        context.expect(multi_species_result.result_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "multi_species_result's timeline must be Valid");
        context.expect(!multi_species_result.result_timeline.results.isEmpty(), "multi_species_result must contain at least one timestep of results");

        bool found_species_value = false;
        bool found_nonzero_concentration = false;
        for (const MultiSpeciesSimulationResult &step : multi_species_result.result_timeline.results)
        {
            for (const MultiSpeciesSimulationResultNodeJunction &junction_result : step.nodes_junctions)
            {
                for (const MultiSpeciesResultValue &value : junction_result.species_values)
                {
                    found_species_value = true;
                    if (value.concentration > 0.0)
                        found_nonzero_concentration = true;
                }
            }
        }
        context.expect(found_species_value, "at least one junction result must carry a CL2 concentration value");
        context.expect(found_nonzero_concentration, "chlorine dosed at the reservoir must show up as a non-zero concentration somewhere in the network over the 24-hour run");
    }
}
}

namespace AowisEpanetTests
{
void registerMsxVendorSmokeScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "proof-msx-vendor-open-init-step-close",
        "Open, solve, step, and read a result from the vendored EPANET-MSX example network and reaction model through the raw MSX toolkit sequence, proving the submodule is correctly vendored, built against this adapter's own EPANET rather than MSX's bundled copy, and linked.",
        {"proof", "quality"},
        &scenarioMsxVendorOpenInitStepClose});
    registry.add(ScenarioDefinition{
        "proof-msx-aowis-hydraulic-handoff",
        "Solve hydraulics once through AOWIS EPANET, persist the resulting .hyd file, and prove EpanetMsxProject consumes that exact file through MSXusehydfile to produce MSX quality results.",
        {"proof", "hydraulic", "quality"},
        &scenarioMsxAowisHydraulicHandoff});
}

void registerMsxIntegrationScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "contract-msx-integration-end-to-end",
        "Run a network with a real reaction model through EpanetRunner::run() and confirm multi_species_result comes back Valid with real, non-zero species concentrations.",
        {"contract", "quality"},
        &scenarioMsxIntegrationEndToEnd});
}
}
