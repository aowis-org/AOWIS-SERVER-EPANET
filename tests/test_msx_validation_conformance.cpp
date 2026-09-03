#include "conformance/msx_validation_scenarios.h"

#include "conformance/net1_fixture.h"

#include "../src/lib/internal/epanet_msx_validator.h"

#include <aowis/epanet/epanet_runner.h>

#include <limits>

namespace
{
using namespace AowisEpanetTests;

NetworkHydraulic cleanNet1()
{
    Net1Fixture fixture = makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

bool failuresContain(
    const QList<HydraulicSimulationStatus> &failures,
    const QString &text)
{
    for (const HydraulicSimulationStatus &failure : failures)
    {
        if (failure.message.contains(text, Qt::CaseInsensitive))
            return true;
        for (const QString &detail : failure.details)
        {
            if (detail.contains(text, Qt::CaseInsensitive))
                return true;
        }
    }
    return false;
}

MultiSpeciesSpecies species(const QString &id, MultiSpeciesSpeciesType type = MultiSpeciesSpeciesType::Bulk)
{
    MultiSpeciesSpecies result;
    result.id = id;
    result.uuid = QUuid::createUuid();
    result.type = type;
    result.units = MultiSpeciesUnits::Milligrams;
    return result;
}

void scenarioMsxValidationIdentities(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    MultiSpeciesSpecies reserved = species(QStringLiteral("Q"));
    network.multi_species.species.append(reserved);

    MultiSpeciesConstant duplicate_id;
    duplicate_id.id = QStringLiteral("Q");
    duplicate_id.uuid = QUuid::createUuid();
    network.multi_species.constants.append(duplicate_id);

    MultiSpeciesParameter invalid_id;
    invalid_id.id = QStringLiteral("bad id");
    invalid_id.uuid = QUuid::createUuid();
    network.multi_species.parameters.append(invalid_id);

    MultiSpeciesTerm duplicate_uuid;
    duplicate_uuid.id = QStringLiteral("T1");
    duplicate_uuid.uuid = reserved.uuid;
    duplicate_uuid.expression = QStringLiteral("1");
    network.multi_species.terms.append(duplicate_uuid);

    QList<HydraulicSimulationStatus> failures;
    const HydraulicSimulationStatus status = validateEpanetMultiSpeciesModel(network, &failures);

    context.expect(!status.success, "invalid MSX identities must fail model validation");
    context.expect(failuresContain(failures, QStringLiteral("reserved")), "reserved MSX hydraulic-variable identifiers must be rejected before MSX parsing");
    context.expect(failuresContain(failures, QStringLiteral("duplicated in the MSX namespace")), "species/constants/parameters/terms must share one unique MSX symbol namespace");
    context.expect(failuresContain(failures, QStringLiteral("invalid identifier")), "MSX identifiers must serialize as single non-comment tokens");
    context.expect(failuresContain(failures, QStringLiteral("UUID is duplicated")), "MSX UUID-bearing objects must not reuse UUIDs");
}

void scenarioMsxValidationReferencesAssignments(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeJunction &junction = network.nodes_junctions.first();

    MultiSpeciesSpecies bulk = species(QStringLiteral("S1"));
    MultiSpeciesSpecies wall = species(QStringLiteral("W1"), MultiSpeciesSpeciesType::Wall);
    network.multi_species.species.append(bulk);
    network.multi_species.species.append(wall);

    MultiSpeciesParameter parameter;
    parameter.id = QStringLiteral("K1");
    parameter.uuid = QUuid::createUuid();
    network.multi_species.parameters.append(parameter);

    MultiSpeciesReaction broken_reaction;
    broken_reaction.uuid = QUuid::createUuid();
    broken_reaction.species_uuid = QUuid::createUuid();
    broken_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(broken_reaction);

    MultiSpeciesReaction first_reaction;
    first_reaction.uuid = QUuid::createUuid();
    first_reaction.species_uuid = bulk.uuid;
    first_reaction.location = MultiSpeciesReactionLocation::Pipe;
    first_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(first_reaction);

    MultiSpeciesReaction duplicate_reaction;
    duplicate_reaction.uuid = QUuid::createUuid();
    duplicate_reaction.species_uuid = bulk.uuid;
    duplicate_reaction.location = MultiSpeciesReactionLocation::Pipe;
    duplicate_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(duplicate_reaction);

    MultiSpeciesReaction wall_tank_reaction;
    wall_tank_reaction.uuid = QUuid::createUuid();
    wall_tank_reaction.species_uuid = wall.uuid;
    wall_tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    wall_tank_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(wall_tank_reaction);

    MultiSpeciesParameterOverridePipe broken_override;
    broken_override.pipe_uuid = QUuid::createUuid();
    broken_override.parameter_uuid = parameter.uuid;
    broken_override.value = 1.0;
    network.multi_species.parameter_overrides_pipes.append(broken_override);

    MultiSpeciesNodeInitialQuality wall_initial;
    wall_initial.node_uuid = junction.uuid;
    wall_initial.species_uuid = wall.uuid;
    wall_initial.concentration = 1.0;
    network.multi_species.initial_quality_nodes.append(wall_initial);

    MultiSpeciesNodeSource wall_source;
    wall_source.node_uuid = junction.uuid;
    wall_source.species_uuid = wall.uuid;
    wall_source.concentration = 1.0;
    network.multi_species.sources.append(wall_source);

    MultiSpeciesNodeSource duplicate_source;
    duplicate_source.node_uuid = junction.uuid;
    duplicate_source.species_uuid = wall.uuid;
    duplicate_source.concentration = 2.0;
    network.multi_species.sources.append(duplicate_source);

    QList<HydraulicSimulationStatus> failures;
    const HydraulicSimulationStatus status = validateEpanetMultiSpeciesModel(network, &failures);

    context.expect(!status.success, "broken MSX references and ambiguous assignments must fail model validation");
    context.expect(failuresContain(failures, QStringLiteral("reaction species")), "reaction species UUIDs must resolve before export");
    context.expect(failuresContain(failures, QStringLiteral("parameter-override pipe")), "parameter overrides must reference an existing enabled pipe");
    context.expect(failuresContain(failures, QStringLiteral("more than one reaction")), "duplicate species/location reactions must be rejected instead of relying on MSX duplicate-expression errors");
    context.expect(failuresContain(failures, QStringLiteral("Wall species cannot define a tank reaction")), "wall-species tank reactions must be rejected because MSX tank chemistry processes bulk species only");
    context.expect(failuresContain(failures, QStringLiteral("Wall species cannot define node initial quality")), "node initial quality for wall species must be rejected instead of being silently ignored by MSX");
    context.expect(failuresContain(failures, QStringLiteral("Wall species cannot define a node source")), "wall-species node sources must be rejected instead of being silently ignored by MSX");
    context.expect(failuresContain(failures, QStringLiteral("source is duplicated")), "duplicate node/species sources must be rejected instead of last-write-wins behavior");
}

void scenarioMsxValidationNumericsExpressions(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    MultiSpeciesSpecies s1 = species(QStringLiteral("S1"));
    network.multi_species.species.append(s1);

    network.multi_species.options.timestep_s = 0;
    network.multi_species.options.default_absolute_tolerance = std::numeric_limits<double>::quiet_NaN();

    MultiSpeciesConstant constant;
    constant.id = QStringLiteral("K1");
    constant.uuid = QUuid::createUuid();
    constant.value = std::numeric_limits<double>::infinity();
    network.multi_species.constants.append(constant);

    MultiSpeciesPattern pattern;
    pattern.id = QStringLiteral("P1");
    pattern.uuid = QUuid::createUuid();
    network.multi_species.patterns.append(pattern);

    MultiSpeciesTerm t1;
    t1.id = QStringLiteral("T1");
    t1.uuid = QUuid::createUuid();
    t1.expression = QStringLiteral("T2 + S1");
    network.multi_species.terms.append(t1);

    MultiSpeciesTerm t2;
    t2.id = QStringLiteral("T2");
    t2.uuid = QUuid::createUuid();
    t2.expression = QStringLiteral("T1");
    network.multi_species.terms.append(t2);

    MultiSpeciesReaction unknown_symbol;
    unknown_symbol.uuid = QUuid::createUuid();
    unknown_symbol.species_uuid = s1.uuid;
    unknown_symbol.location = MultiSpeciesReactionLocation::Pipe;
    unknown_symbol.expression = QStringLiteral("MissingSymbol + S1");
    network.multi_species.reactions.append(unknown_symbol);

    MultiSpeciesReaction invalid_syntax;
    invalid_syntax.uuid = QUuid::createUuid();
    invalid_syntax.species_uuid = s1.uuid;
    invalid_syntax.location = MultiSpeciesReactionLocation::Tank;
    invalid_syntax.expression = QStringLiteral("S1 @ 2");
    network.multi_species.reactions.append(invalid_syntax);

    QList<HydraulicSimulationStatus> failures;
    const HydraulicSimulationStatus status = validateEpanetMultiSpeciesModel(network, &failures);

    context.expect(!status.success, "invalid MSX numerics and expression dependencies must fail model validation");
    context.expect(failuresContain(failures, QStringLiteral("timestep must be positive")), "MSX timestep zero must be rejected before file generation");
    context.expect(failuresContain(failures, QStringLiteral("invalid numeric")), "non-finite MSX numeric fields must be rejected");
    context.expect(failuresContain(failures, QStringLiteral("at least one multiplier")), "empty MSX patterns must be rejected because they cannot be serialized as a real pattern");
    context.expect(failuresContain(failures, QStringLiteral("unknown expression symbol")), "unknown reaction/term symbols must be diagnosed at the AOWIS boundary");
    context.expect(failuresContain(failures, QStringLiteral("cyclic expression dependency")), "cyclic term dependencies must be diagnosed before MSX parsing");
    context.expect(failuresContain(failures, QStringLiteral("unsupported character")), "obviously illegal expression characters must be diagnosed before MSX parsing");
}

void scenarioMsxValidationPublicPreflight(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    MultiSpeciesSpecies s1 = species(QStringLiteral("S1"));
    network.multi_species.species.append(s1);

    MultiSpeciesReaction reaction;
    reaction.uuid = QUuid::createUuid();
    reaction.species_uuid = s1.uuid;
    reaction.expression = QStringLiteral("UnknownCoefficient * S1");
    network.multi_species.reactions.append(reaction);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(!result.status.success, "the public runner must reject an invalid MSX model during AOWIS preflight validation");
    context.expect(result.status.stage == HydraulicSimulationStatusStage::ConfigureOptions, "MSX model validation must be reported as configuration failure, not a backend runtime failure");
    context.expect(result.status.operation == HydraulicSimulationStatusOperation::ConfigureMultiSpecies, "MSX model validation must identify multi-species configuration");
    context.expect(result.status.entity.type == HydraulicSimulationStatusEntityType::MultiSpeciesSolver, "MSX model validation must identify the multi-species solver boundary");
    context.expect(result.result_timeline.results.isEmpty(), "invalid MSX model state must fail before hydraulic results are produced");
}
}

namespace AowisEpanetTests
{
void registerMsxValidationScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "contract-msx-validation-identities",
        "Reject invalid, reserved, duplicate, or UUID-conflicting MSX model identities before serialization.",
        {"contract", "quality", "negative"},
        &scenarioMsxValidationIdentities});
    registry.add(ScenarioDefinition{
        "contract-msx-validation-references-assignments",
        "Reject unresolved MSX references, solver-ignored wall assignments, and duplicate last-write-wins assignments.",
        {"contract", "quality", "negative"},
        &scenarioMsxValidationReferencesAssignments});
    registry.add(ScenarioDefinition{
        "contract-msx-validation-numerics-expressions",
        "Reject invalid MSX numerics, empty patterns, unknown expression dependencies, cycles, and illegal expression characters.",
        {"contract", "quality", "negative"},
        &scenarioMsxValidationNumericsExpressions});
    registry.add(ScenarioDefinition{
        "contract-msx-validation-public-preflight",
        "Reject invalid MSX model state through EpanetRunner before hydraulics or the USEPA MSX backend are executed.",
        {"contract", "hydraulic", "quality", "negative"},
        &scenarioMsxValidationPublicPreflight});
}
}
