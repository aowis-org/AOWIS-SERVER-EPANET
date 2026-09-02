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

#include <cstdint>
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
// MSXusehydfile. This keeps the internal handoff covered independently of
// the public EpanetRunner integration scenarios below.
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

NetworkHydraulic combinedQualityMsxNetwork()
{
    NetworkHydraulic network = cleanNet1();
    if (!network.nodes_reservoirs.isEmpty())
        network.nodes_reservoirs.first().initial_chemical_concentration_mg_per_l = 1.0;
    addSimpleChlorineMsxModel(network);
    return network;
}

WaterQualitySolverOptions chemicalQualityOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::Chemical;
    options.chemical_name = QStringLiteral("Chlorine");
    return options;
}

WaterQualitySolverOptions waterAgeQualityOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::WaterAge;
    return options;
}

template<typename EntityResult>
void compareQualityEntities(
    AowisEpanetTests::TestContext &context,
    const QList<EntityResult> &actual,
    const QList<EntityResult> &expected,
    std::int64_t time_s,
    const std::string &entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, std::string(), "count"},
        "combined and isolated standard-quality entity counts must match");
    if (actual.size() != expected.size())
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-12, 1.0e-10};
    for (qsizetype entity_index = 0; entity_index < actual.size(); entity_index++)
    {
        const EntityResult &actual_entity = actual.at(entity_index);
        const EntityResult &expected_entity = expected.at(entity_index);
        const std::string entity_id = actual_entity.id.toStdString();
        context.expectEqual(
            entity_id,
            expected_entity.id.toStdString(),
            {time_s, entity_type, entity_id, "id"},
            "combined and isolated standard-quality entity ordering must match");
        context.expect(
            actual_entity.uuid == expected_entity.uuid,
            "combined and isolated standard-quality entity UUIDs must match");
        context.expectNear(
            actual_entity.chemical_concentration_mg_per_l,
            expected_entity.chemical_concentration_mg_per_l,
            tolerance,
            {time_s, entity_type, entity_id, "chemical_concentration_mg_per_l"});
        context.expectNear(
            actual_entity.water_age_h,
            expected_entity.water_age_h,
            tolerance,
            {time_s, entity_type, entity_id, "water_age_h"});
        context.expectNear(
            actual_entity.source_trace_percent,
            expected_entity.source_trace_percent,
            tolerance,
            {time_s, entity_type, entity_id, "source_trace_percent"});
    }
}

void compareQualityTimelines(
    AowisEpanetTests::TestContext &context,
    const WaterQualitySimulationResultTimeline &actual,
    const WaterQualitySimulationResultTimeline &expected)
{
    context.expect(actual.status.success == expected.status.success, "combined and isolated standard-quality statuses must match");
    context.expect(actual.validity == expected.validity, "combined and isolated standard-quality validity must match");
    context.expect(actual.analysis == expected.analysis, "combined and isolated standard-quality analysis types must match");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        {-1, "quality", std::string(), "timesteps"},
        "combined and isolated standard-quality timestep counts must match");
    if (actual.results.size() != expected.results.size())
        return;

    for (qsizetype step_index = 0; step_index < actual.results.size(); step_index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(step_index);
        const WaterQualitySimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(actual_step.time_elapsed_s);
        context.expectEqual(
            time_s,
            static_cast<std::int64_t>(expected_step.time_elapsed_s),
            {time_s, "quality", std::string(), "time_elapsed_s"},
            "combined and isolated standard-quality timelines must use the same timestamps");
        compareQualityEntities(context, actual_step.nodes_junctions, expected_step.nodes_junctions, time_s, "junction");
        compareQualityEntities(context, actual_step.nodes_reservoirs, expected_step.nodes_reservoirs, time_s, "reservoir");
        compareQualityEntities(context, actual_step.nodes_tanks, expected_step.nodes_tanks, time_s, "tank");
        compareQualityEntities(context, actual_step.links_pipes, expected_step.links_pipes, time_s, "pipe");
        compareQualityEntities(context, actual_step.links_pumps, expected_step.links_pumps, time_s, "pump");
        compareQualityEntities(context, actual_step.links_valves, expected_step.links_valves, time_s, "valve");
    }
}

void compareMsxSpeciesValues(
    AowisEpanetTests::TestContext &context,
    const QList<MultiSpeciesResultValue> &actual,
    const QList<MultiSpeciesResultValue> &expected,
    std::int64_t time_s,
    const std::string &entity_type,
    const std::string &entity_id)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, entity_id, "species_values.size"},
        "combined and isolated MSX species counts must match");
    if (actual.size() != expected.size())
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-12, 1.0e-10};
    for (qsizetype species_index = 0; species_index < actual.size(); species_index++)
    {
        const MultiSpeciesResultValue &actual_value = actual.at(species_index);
        const MultiSpeciesResultValue &expected_value = expected.at(species_index);
        context.expect(
            actual_value.species_uuid == expected_value.species_uuid,
            "combined and isolated MSX species UUIDs must match");
        context.expectNear(
            actual_value.concentration,
            expected_value.concentration,
            tolerance,
            {time_s, entity_type, entity_id, "concentration"});
    }
}

template<typename EntityResult>
void compareMsxEntities(
    AowisEpanetTests::TestContext &context,
    const QList<EntityResult> &actual,
    const QList<EntityResult> &expected,
    std::int64_t time_s,
    const std::string &entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, std::string(), "count"},
        "combined and isolated MSX entity counts must match");
    if (actual.size() != expected.size())
        return;

    for (qsizetype entity_index = 0; entity_index < actual.size(); entity_index++)
    {
        const EntityResult &actual_entity = actual.at(entity_index);
        const EntityResult &expected_entity = expected.at(entity_index);
        const std::string entity_id = actual_entity.id.toStdString();
        context.expectEqual(
            entity_id,
            expected_entity.id.toStdString(),
            {time_s, entity_type, entity_id, "id"},
            "combined and isolated MSX entity ordering must match");
        context.expect(
            actual_entity.uuid == expected_entity.uuid,
            "combined and isolated MSX entity UUIDs must match");
        compareMsxSpeciesValues(
            context,
            actual_entity.species_values,
            expected_entity.species_values,
            time_s,
            entity_type,
            entity_id);
    }
}

void compareMsxTimelines(
    AowisEpanetTests::TestContext &context,
    const MultiSpeciesSimulationResultTimeline &actual,
    const MultiSpeciesSimulationResultTimeline &expected)
{
    context.expect(actual.status.success == expected.status.success, "combined and isolated MSX statuses must match");
    context.expect(actual.validity == expected.validity, "combined and isolated MSX validity must match");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        {-1, "msx", std::string(), "timesteps"},
        "combined and isolated MSX timestep counts must match");
    if (actual.results.size() != expected.results.size())
        return;

    for (qsizetype step_index = 0; step_index < actual.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &actual_step = actual.results.at(step_index);
        const MultiSpeciesSimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(actual_step.time_elapsed_s);
        context.expectEqual(
            time_s,
            static_cast<std::int64_t>(expected_step.time_elapsed_s),
            {time_s, "msx", std::string(), "time_elapsed_s"},
            "combined and isolated MSX timelines must use the same timestamps");
        compareMsxEntities(context, actual_step.nodes_junctions, expected_step.nodes_junctions, time_s, "junction");
        compareMsxEntities(context, actual_step.nodes_reservoirs, expected_step.nodes_reservoirs, time_s, "reservoir");
        compareMsxEntities(context, actual_step.nodes_tanks, expected_step.nodes_tanks, time_s, "tank");
        compareMsxEntities(context, actual_step.links_pipes, expected_step.links_pipes, time_s, "pipe");
        compareMsxEntities(context, actual_step.links_pumps, expected_step.links_pumps, time_s, "pump");
        compareMsxEntities(context, actual_step.links_valves, expected_step.links_valves, time_s, "valve");
    }
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

void scenarioMsxWithStandardQuality(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();
    EpanetRunRequest request;
    request.network = network;
    request.quality_runs = {chemicalQualityOptions(), waterAgeQualityOptions()};
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "a valid combined standard-quality + MSX request must succeed");
    context.expect(result.state == EpanetRunState::Success, "a valid combined standard-quality + MSX request must have Success aggregate state");
    context.expect(result.result_timeline.status.success, "combined execution must retain a successful hydraulic timeline");
    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{2},
        {-1, "quality", std::string(), "quality_results.size"},
        "both standard EPANET quality runs must be preserved");
    for (const EpanetQualityResult &quality_result : result.quality_results)
        context.expect(quality_result.state == EpanetRunState::Success, "each standard EPANET quality run must succeed before MSX executes");

    context.expect(result.multi_species_result.has_value(), "combined execution must populate multi_species_result");
    if (result.multi_species_result.has_value())
    {
        context.expect(result.multi_species_result->state == EpanetRunState::Success, "MSX must succeed after the standard EPANET quality runs");
        context.expect(result.multi_species_result->result_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "combined MSX timeline must be Valid");
        context.expect(timelineHasSpeciesValue(result.multi_species_result->result_timeline), "combined MSX execution must contain species values");
    }
}

void scenarioMsxStandardQualityIsolatedEquivalence(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();
    const WaterQualitySolverOptions chemical = chemicalQualityOptions();
    const WaterQualitySolverOptions water_age = waterAgeQualityOptions();

    EpanetRunRequest combined_request;
    combined_request.network = network;
    combined_request.quality_runs = {chemical, water_age};
    combined_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest quality_only_request;
    quality_only_request.network = network;
    quality_only_request.quality_runs = {chemical, water_age};

    EpanetRunRequest msx_only_request;
    msx_only_request.network = network;
    msx_only_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun combined = EpanetRunner().run(combined_request);
    const EpanetResultRun quality_only = EpanetRunner().run(quality_only_request);
    const EpanetResultRun msx_only = EpanetRunner().run(msx_only_request);

    context.expect(combined.status.success, "combined reference run must succeed");
    context.expect(quality_only.status.success, "isolated standard-quality reference run must succeed");
    context.expect(msx_only.status.success, "isolated MSX reference run must succeed");
    context.expectEqual(
        static_cast<std::int64_t>(combined.quality_results.size()),
        static_cast<std::int64_t>(quality_only.quality_results.size()),
        {-1, "quality", std::string(), "quality_results.size"},
        "combined and isolated runs must return the same number of standard-quality results");

    if (combined.quality_results.size() == quality_only.quality_results.size())
    {
        for (qsizetype quality_index = 0; quality_index < combined.quality_results.size(); quality_index++)
        {
            const EpanetQualityResult &combined_quality = combined.quality_results.at(quality_index);
            const EpanetQualityResult &isolated_quality = quality_only.quality_results.at(quality_index);
            context.expect(combined_quality.state == isolated_quality.state, "combined and isolated standard-quality run states must match");
            compareQualityTimelines(context, combined_quality.result_timeline, isolated_quality.result_timeline);
        }
    }

    context.expect(combined.multi_species_result.has_value(), "combined run must contain MSX results for equivalence comparison");
    context.expect(msx_only.multi_species_result.has_value(), "isolated MSX run must contain MSX results for equivalence comparison");
    if (combined.multi_species_result.has_value() && msx_only.multi_species_result.has_value())
    {
        context.expect(combined.multi_species_result->state == msx_only.multi_species_result->state, "combined and isolated MSX run states must match");
        compareMsxTimelines(
            context,
            combined.multi_species_result->result_timeline,
            msx_only.multi_species_result->result_timeline);
    }
}

void scenarioMsxStandardQualityFailureIsolation(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();

    WaterQualitySolverOptions invalid_trace;
    invalid_trace.analysis = WaterQualityAnalysisType::SourceTrace;
    invalid_trace.trace_node_uuid = QUuid::createUuid();

    EpanetRunRequest combined_request;
    combined_request.network = network;
    combined_request.quality_runs = {invalid_trace};
    combined_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest msx_only_request;
    msx_only_request.network = network;
    msx_only_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun combined = EpanetRunner().run(combined_request);
    const EpanetResultRun msx_only = EpanetRunner().run(msx_only_request);

    context.expect(!combined.status.success, "an invalid standard-quality child must still make the aggregate combined run report failure");
    context.expect(combined.state == EpanetRunState::Error, "an invalid standard-quality child must make the aggregate combined state Error");
    context.expect(combined.result_timeline.status.success, "standard-quality failure must not invalidate the already-completed hydraulics");
    context.expectEqual(
        static_cast<std::int64_t>(combined.quality_results.size()),
        std::int64_t{1},
        {-1, "quality", std::string(), "quality_results.size"});
    if (!combined.quality_results.isEmpty())
        context.expect(combined.quality_results.first().state == EpanetRunState::Error, "the invalid standard-quality child must be isolated as Error");

    context.expect(combined.multi_species_result.has_value(), "MSX must still execute after an isolated standard-quality validation failure");
    context.expect(msx_only.multi_species_result.has_value(), "isolated MSX reference must produce a result");
    if (combined.multi_species_result.has_value() && msx_only.multi_species_result.has_value())
    {
        context.expect(combined.multi_species_result->state == EpanetRunState::Success, "MSX must succeed despite the independent standard-quality child failure");
        compareMsxTimelines(
            context,
            combined.multi_species_result->result_timeline,
            msx_only.multi_species_result->result_timeline);
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
    registry.add(ScenarioDefinition{
        "contract-msx-with-standard-quality",
        "Run standard EPANET chemical and water-age analyses followed by MSX in one request against one hydraulic solution, and require all child results to succeed.",
        {"contract", "hydraulic", "quality"},
        &scenarioMsxWithStandardQuality});
    registry.add(ScenarioDefinition{
        "contract-msx-standard-quality-isolated-equivalence",
        "Compare standard-quality and MSX outputs from one combined request against isolated reference requests, proving sequential orchestration does not change either solver's results.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxStandardQualityIsolatedEquivalence});
    registry.add(ScenarioDefinition{
        "contract-msx-standard-quality-failure-isolation",
        "Fail one standard-quality child after hydraulics and prove MSX still executes successfully against the same saved hydraulic solution with results matching an isolated MSX run.",
        {"contract", "hydraulic", "quality", "proof", "negative"},
        &scenarioMsxStandardQualityFailureIsolation});
}
}
