#include "epanet_msx_validator.h"

#include "epanet_network_validator_support.h"
#include "epanet_status_helpers.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <cmath>

namespace
{
constexpr int kMsxMaximumInputLineLength = 1024;

HydraulicSimulationStatus msxValidationFailure(
    const NetworkHydraulic &network,
    const QString &message,
    const QStringList &details = {})
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        HydraulicSimulationStatusStage::ConfigureOptions,
        HydraulicSimulationStatusOperation::ConfigureMultiSpecies,
        HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
        network.id,
        network.uuid,
        message);
    status.details = details;
    return status;
}

void appendFailure(
    QList<HydraulicSimulationStatus> &failures,
    const HydraulicSimulationStatus &status)
{
    if (!status.success)
        failures.append(status);
}

bool modelHasContent(const NetworkMultiSpecies &model)
{
    return !model.species.isEmpty()
        || !model.constants.isEmpty()
        || !model.parameters.isEmpty()
        || !model.parameter_overrides_pipes.isEmpty()
        || !model.parameter_overrides_tanks.isEmpty()
        || !model.terms.isEmpty()
        || !model.reactions.isEmpty()
        || !model.patterns.isEmpty()
        || !model.initial_quality_global.isEmpty()
        || !model.initial_quality_nodes.isEmpty()
        || !model.initial_quality_pipes.isEmpty()
        || !model.sources.isEmpty();
}

bool isAsciiLetter(QChar value)
{
    const char16_t code = value.unicode();
    return (code >= static_cast<char16_t>('A') && code <= static_cast<char16_t>('Z'))
        || (code >= static_cast<char16_t>('a') && code <= static_cast<char16_t>('z'));
}

bool isAsciiDigit(QChar value)
{
    const char16_t code = value.unicode();
    return code >= static_cast<char16_t>('0') && code <= static_cast<char16_t>('9');
}

bool isSymbolStart(QChar value)
{
    return isAsciiLetter(value) || value == QLatin1Char('_');
}

bool isSymbolPart(QChar value)
{
    return isSymbolStart(value) || isAsciiDigit(value);
}

bool validIdentifier(const QString &id)
{
    if (id.isEmpty() || id.trimmed() != id
        || id.contains(QLatin1Char(';'))
        || id.contains(QLatin1Char('\"'))
        || id.contains(QLatin1Char('['))
        || id.contains(QLatin1Char(']')))
    {
        return false;
    }

    for (const QChar value : id)
    {
        if (value.isSpace())
            return false;
    }
    return true;
}

bool reservedSymbol(const QString &id)
{
    // This mirrors EPANET-MSX checkID(): hydraulic variables are reserved
    // case-insensitively, while math-function names are only special when
    // they actually appear inside an expression.
    static const QSet<QString> reserved = {
        QStringLiteral("D"), QStringLiteral("Q"), QStringLiteral("U"),
        QStringLiteral("RE"), QStringLiteral("US"), QStringLiteral("FF"),
        QStringLiteral("AV"), QStringLiteral("KC"), QStringLiteral("LEN")
    };
    return reserved.contains(id.toUpper());
}

template<typename Entity>
QSet<QUuid> uuidsOf(const QList<Entity> &entities)
{
    QSet<QUuid> result;
    for (const Entity &entity : entities)
        result.insert(entity.uuid);
    return result;
}

template<typename Entity>
QSet<QUuid> enabledUuidsOf(const QList<Entity> &entities)
{
    QSet<QUuid> result;
    for (const Entity &entity : entities)
    {
        if (entity.metadata.enabled)
            result.insert(entity.uuid);
    }
    return result;
}

QSet<QUuid> nodeUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> result = uuidsOf(network.nodes_junctions);
    result.unite(uuidsOf(network.nodes_reservoirs));
    result.unite(uuidsOf(network.nodes_tanks));
    return result;
}

QSet<QUuid> enabledNodeUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> result = enabledUuidsOf(network.nodes_junctions);
    result.unite(enabledUuidsOf(network.nodes_reservoirs));
    result.unite(enabledUuidsOf(network.nodes_tanks));
    return result;
}

void validateIdentity(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures,
    QSet<QUuid> &uuids,
    QSet<QString> &ids,
    const QString &kind,
    const QString &id,
    const QUuid &uuid,
    bool reject_reserved)
{
    if (uuid.isNull())
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species %1 has no UUID").arg(kind),
            {QStringLiteral("ID: %1").arg(id)}));
    }
    else if (uuids.contains(uuid))
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species UUID is duplicated"),
            {QStringLiteral("%1 ID: %2").arg(kind, id),
             QStringLiteral("Duplicate UUID: %1").arg(uuid.toString(QUuid::WithoutBraces))}));
    }
    else
    {
        uuids.insert(uuid);
    }

    if (!validIdentifier(id))
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species %1 has an invalid identifier").arg(kind),
            {QStringLiteral("ID: %1").arg(id),
             QStringLiteral("Identifiers must be non-empty single MSX tokens without whitespace, comments, quotes, or section delimiters")}));
        return;
    }

    if (reject_reserved && reservedSymbol(id))
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species %1 uses a reserved MSX identifier").arg(kind),
            {QStringLiteral("ID: %1").arg(id)}));
    }

    if (ids.contains(id))
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species identifier is duplicated in the MSX namespace"),
            {QStringLiteral("%1 ID: %2").arg(kind, id)}));
    }
    else
    {
        ids.insert(id);
    }
}

void validateIdentities(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures)
{
    const NetworkMultiSpecies &model = network.multi_species;
    QSet<QUuid> uuids;
    QSet<QString> symbol_ids;
    QSet<QString> pattern_ids;

    for (const MultiSpeciesSpecies &species : model.species)
        validateIdentity(network, failures, uuids, symbol_ids, QStringLiteral("species"), species.id, species.uuid, true);
    for (const MultiSpeciesConstant &constant : model.constants)
        validateIdentity(network, failures, uuids, symbol_ids, QStringLiteral("constant"), constant.id, constant.uuid, true);
    for (const MultiSpeciesParameter &parameter : model.parameters)
        validateIdentity(network, failures, uuids, symbol_ids, QStringLiteral("parameter"), parameter.id, parameter.uuid, true);
    for (const MultiSpeciesTerm &term : model.terms)
        validateIdentity(network, failures, uuids, symbol_ids, QStringLiteral("term"), term.id, term.uuid, true);
    for (const MultiSpeciesPattern &pattern : model.patterns)
        validateIdentity(network, failures, uuids, pattern_ids, QStringLiteral("pattern"), pattern.id, pattern.uuid, false);

    for (const MultiSpeciesReaction &reaction : model.reactions)
    {
        if (reaction.uuid.isNull())
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species reaction has no UUID")));
        }
        else if (uuids.contains(reaction.uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species UUID is duplicated"),
                {QStringLiteral("Reaction UUID: %1").arg(reaction.uuid.toString(QUuid::WithoutBraces))}));
        }
        else
        {
            uuids.insert(reaction.uuid);
        }
    }
}

void appendReferenceFailure(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures,
    const QSet<QUuid> &all_uuids,
    const QSet<QUuid> &enabled_uuids,
    const QUuid &uuid,
    const QString &relationship)
{
    const HydraulicSimulationStatus status = EpanetNetworkValidatorSupport::validateReference(
        all_uuids,
        enabled_uuids,
        uuid,
        HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
        network.id,
        network.uuid,
        relationship);
    if (!status.success)
    {
        HydraulicSimulationStatus adjusted = status;
        adjusted.stage = HydraulicSimulationStatusStage::ConfigureOptions;
        adjusted.operation = HydraulicSimulationStatusOperation::ConfigureMultiSpecies;
        failures.append(adjusted);
    }
}

void validateReferencesAndAssignments(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures)
{
    const NetworkMultiSpecies &model = network.multi_species;
    const QSet<QUuid> species_uuids = uuidsOf(model.species);
    const QSet<QUuid> parameter_uuids = uuidsOf(model.parameters);
    const QSet<QUuid> pattern_uuids = uuidsOf(model.patterns);
    const QSet<QUuid> all_nodes = nodeUuids(network);
    const QSet<QUuid> enabled_nodes = enabledNodeUuids(network);
    const QSet<QUuid> all_pipes = uuidsOf(network.links_pipes);
    const QSet<QUuid> enabled_pipes = enabledUuidsOf(network.links_pipes);
    const QSet<QUuid> all_tanks = uuidsOf(network.nodes_tanks);
    const QSet<QUuid> enabled_tanks = enabledUuidsOf(network.nodes_tanks);
    QHash<QUuid, MultiSpeciesSpeciesType> species_types;
    QSet<QString> reaction_assignments;
    QSet<QString> pipe_parameter_assignments;
    QSet<QString> tank_parameter_assignments;
    QSet<QString> global_quality_assignments;
    QSet<QString> node_quality_assignments;
    QSet<QString> pipe_quality_assignments;
    QSet<QString> source_assignments;

    for (const MultiSpeciesSpecies &species : model.species)
        species_types.insert(species.uuid, species.type);

    for (const MultiSpeciesReaction &reaction : model.reactions)
    {
        appendReferenceFailure(network, failures, species_uuids, species_uuids, reaction.species_uuid, QStringLiteral("reaction species"));
        if (reaction.location == MultiSpeciesReactionLocation::Tank
            && species_types.value(reaction.species_uuid, MultiSpeciesSpeciesType::Bulk) == MultiSpeciesSpeciesType::Wall
            && species_uuids.contains(reaction.species_uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Wall species cannot define a tank reaction because EPANET-MSX tank chemistry only processes bulk species"),
                {QStringLiteral("Species UUID: %1").arg(reaction.species_uuid.toString(QUuid::WithoutBraces))}));
        }
        const QString key = reaction.species_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + QString::number(static_cast<int>(reaction.location));
        if (reaction_assignments.contains(key))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species model defines more than one reaction for the same species and location"),
                {QStringLiteral("Species UUID: %1").arg(reaction.species_uuid.toString(QUuid::WithoutBraces)),
                 QStringLiteral("Location: %1").arg(static_cast<int>(reaction.location))}));
        }
        else
        {
            reaction_assignments.insert(key);
        }
    }

    for (const MultiSpeciesParameterOverridePipe &override_value : model.parameter_overrides_pipes)
    {
        appendReferenceFailure(network, failures, all_pipes, enabled_pipes, override_value.pipe_uuid, QStringLiteral("parameter-override pipe"));
        appendReferenceFailure(network, failures, parameter_uuids, parameter_uuids, override_value.parameter_uuid, QStringLiteral("parameter-override parameter"));
        const QString key = override_value.pipe_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + override_value.parameter_uuid.toString(QUuid::WithoutBraces);
        if (pipe_parameter_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species pipe parameter override is duplicated")));
        else
            pipe_parameter_assignments.insert(key);
    }

    for (const MultiSpeciesParameterOverrideTank &override_value : model.parameter_overrides_tanks)
    {
        appendReferenceFailure(network, failures, all_tanks, enabled_tanks, override_value.tank_uuid, QStringLiteral("parameter-override tank"));
        appendReferenceFailure(network, failures, parameter_uuids, parameter_uuids, override_value.parameter_uuid, QStringLiteral("parameter-override parameter"));
        const QString key = override_value.tank_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + override_value.parameter_uuid.toString(QUuid::WithoutBraces);
        if (tank_parameter_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species tank parameter override is duplicated")));
        else
            tank_parameter_assignments.insert(key);
    }

    for (const MultiSpeciesGlobalInitialQuality &initial : model.initial_quality_global)
    {
        appendReferenceFailure(network, failures, species_uuids, species_uuids, initial.species_uuid, QStringLiteral("global initial-quality species"));
        const QString key = initial.species_uuid.toString(QUuid::WithoutBraces);
        if (global_quality_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species global initial quality is duplicated for a species")));
        else
            global_quality_assignments.insert(key);
    }

    for (const MultiSpeciesNodeInitialQuality &initial : model.initial_quality_nodes)
    {
        appendReferenceFailure(network, failures, all_nodes, enabled_nodes, initial.node_uuid, QStringLiteral("initial-quality node"));
        appendReferenceFailure(network, failures, species_uuids, species_uuids, initial.species_uuid, QStringLiteral("initial-quality species"));
        if (species_types.value(initial.species_uuid, MultiSpeciesSpeciesType::Bulk) == MultiSpeciesSpeciesType::Wall
            && species_uuids.contains(initial.species_uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Wall species cannot define node initial quality because EPANET-MSX ignores it")));
        }
        const QString key = initial.node_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + initial.species_uuid.toString(QUuid::WithoutBraces);
        if (node_quality_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species node initial quality is duplicated")));
        else
            node_quality_assignments.insert(key);
    }

    for (const MultiSpeciesPipeInitialQuality &initial : model.initial_quality_pipes)
    {
        appendReferenceFailure(network, failures, all_pipes, enabled_pipes, initial.pipe_uuid, QStringLiteral("initial-quality pipe"));
        appendReferenceFailure(network, failures, species_uuids, species_uuids, initial.species_uuid, QStringLiteral("initial-quality species"));
        const QString key = initial.pipe_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + initial.species_uuid.toString(QUuid::WithoutBraces);
        if (pipe_quality_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species pipe initial quality is duplicated")));
        else
            pipe_quality_assignments.insert(key);
    }

    for (const MultiSpeciesNodeSource &source : model.sources)
    {
        appendReferenceFailure(network, failures, all_nodes, enabled_nodes, source.node_uuid, QStringLiteral("source node"));
        appendReferenceFailure(network, failures, species_uuids, species_uuids, source.species_uuid, QStringLiteral("source species"));
        if (!source.pattern_uuid.isNull())
        {
            appendReferenceFailure(network, failures, pattern_uuids, pattern_uuids, source.pattern_uuid, QStringLiteral("source pattern"));
        }
        if (species_types.value(source.species_uuid, MultiSpeciesSpeciesType::Bulk) == MultiSpeciesSpeciesType::Wall
            && species_uuids.contains(source.species_uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Wall species cannot define a node source because EPANET-MSX ignores it")));
        }
        const QString key = source.node_uuid.toString(QUuid::WithoutBraces)
            + QLatin1Char(':') + source.species_uuid.toString(QUuid::WithoutBraces);
        if (source_assignments.contains(key))
            failures.append(msxValidationFailure(network, QStringLiteral("Multi-species node source is duplicated for a species")));
        else
            source_assignments.insert(key);
    }
}

HydraulicSimulationStatus validateFiniteValue(
    const NetworkHydraulic &network,
    double value,
    const QString &field,
    bool non_negative)
{
    if (std::isfinite(value) && (!non_negative || value >= 0.0))
        return makeEpanetSuccess();

    return msxValidationFailure(
        network,
        QStringLiteral("Multi-species model contains an invalid numeric value"),
        {QStringLiteral("Field: %1").arg(field),
         non_negative ? QStringLiteral("Value must be finite and non-negative") : QStringLiteral("Value must be finite")});
}

void validateNumerics(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures)
{
    const NetworkMultiSpecies &model = network.multi_species;

    if (!std::isfinite(model.options.timestep_s) || model.options.timestep_s < 0.001)
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species timestep must be finite and at least 0.001 seconds"),
            {QStringLiteral("Field: multi_species.options.timestep_s")}));
    }

    if (!std::isfinite(model.options.peclet_number_threshold) || model.options.peclet_number_threshold < 1.0)
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species Peclet-number threshold must be finite and at least 1"),
            {QStringLiteral("Field: multi_species.options.peclet_number_threshold")}));
    }

    if (model.options.maximum_segments < 50)
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("Multi-species maximum segment count must be at least 50"),
            {QStringLiteral("Field: multi_species.options.maximum_segments")}));
    }

    appendFailure(failures, validateFiniteValue(network, model.options.default_absolute_tolerance, QStringLiteral("multi_species.options.default_absolute_tolerance"), true));
    appendFailure(failures, validateFiniteValue(network, model.options.default_relative_tolerance, QStringLiteral("multi_species.options.default_relative_tolerance"), true));

    for (int index = 0; index < model.species.size(); index++)
    {
        const MultiSpeciesSpecies &species = model.species.at(index);
        appendFailure(failures, validateFiniteValue(network, species.absolute_tolerance, QStringLiteral("multi_species.species[%1].absolute_tolerance").arg(index), true));
        appendFailure(failures, validateFiniteValue(network, species.relative_tolerance, QStringLiteral("multi_species.species[%1].relative_tolerance").arg(index), true));
        if (species.molecular_diffusivity_m2_per_s.has_value())
        {
            appendFailure(failures, validateFiniteValue(
                network,
                species.molecular_diffusivity_m2_per_s.value(),
                QStringLiteral("multi_species.species[%1].molecular_diffusivity_m2_per_s").arg(index),
                true));
        }
        if (species.longitudinal_dispersion_coefficient_m2_per_s.has_value())
        {
            appendFailure(failures, validateFiniteValue(
                network,
                species.longitudinal_dispersion_coefficient_m2_per_s.value(),
                QStringLiteral("multi_species.species[%1].longitudinal_dispersion_coefficient_m2_per_s").arg(index),
                true));
        }
        if (species.type == MultiSpeciesSpeciesType::Wall
            && (species.molecular_diffusivity_m2_per_s.has_value()
                || species.longitudinal_dispersion_coefficient_m2_per_s.has_value()))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species dispersion coefficients are valid only for bulk species"),
                {QStringLiteral("Species: %1").arg(species.id)}));
        }
        if (species.molecular_diffusivity_m2_per_s.has_value()
            && species.longitudinal_dispersion_coefficient_m2_per_s.has_value())
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species species cannot define both molecular diffusivity and fixed longitudinal dispersion"),
                {QStringLiteral("Species: %1").arg(species.id)}));
        }
    }

    for (int index = 0; index < model.constants.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.constants.at(index).value, QStringLiteral("multi_species.constants[%1].value").arg(index), false));
    for (int index = 0; index < model.parameters.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.parameters.at(index).default_value, QStringLiteral("multi_species.parameters[%1].default_value").arg(index), false));
    for (int index = 0; index < model.parameter_overrides_pipes.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.parameter_overrides_pipes.at(index).value, QStringLiteral("multi_species.parameter_overrides_pipes[%1].value").arg(index), false));
    for (int index = 0; index < model.parameter_overrides_tanks.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.parameter_overrides_tanks.at(index).value, QStringLiteral("multi_species.parameter_overrides_tanks[%1].value").arg(index), false));

    for (int pattern_index = 0; pattern_index < model.patterns.size(); pattern_index++)
    {
        const MultiSpeciesPattern &pattern = model.patterns.at(pattern_index);
        if (pattern.multipliers.isEmpty())
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species pattern must contain at least one multiplier"),
                {QStringLiteral("Pattern: %1").arg(pattern.id)}));
        }
        for (int multiplier_index = 0; multiplier_index < pattern.multipliers.size(); multiplier_index++)
        {
            appendFailure(failures, validateFiniteValue(
                network,
                pattern.multipliers.at(multiplier_index),
                QStringLiteral("multi_species.patterns[%1].multipliers[%2]").arg(pattern_index).arg(multiplier_index),
                false));
        }
    }

    for (int index = 0; index < model.initial_quality_global.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.initial_quality_global.at(index).value, QStringLiteral("multi_species.initial_quality_global[%1].value").arg(index), true));
    for (int index = 0; index < model.initial_quality_nodes.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.initial_quality_nodes.at(index).value, QStringLiteral("multi_species.initial_quality_nodes[%1].value").arg(index), true));
    for (int index = 0; index < model.initial_quality_pipes.size(); index++)
        appendFailure(failures, validateFiniteValue(network, model.initial_quality_pipes.at(index).value, QStringLiteral("multi_species.initial_quality_pipes[%1].value").arg(index), true));

    for (int index = 0; index < model.sources.size(); index++)
    {
        const MultiSpeciesNodeSource &source = model.sources.at(index);
        appendFailure(failures, validateFiniteValue(network, source.value, QStringLiteral("multi_species.sources[%1].value").arg(index), true));
    }
}

struct ExpressionAnalysis
{
    bool valid = true;
    QString error;
    QSet<QString> symbols;
};

bool mathFunction(const QString &id)
{
    static const QSet<QString> functions = {
        QStringLiteral("COS"), QStringLiteral("SIN"), QStringLiteral("TAN"), QStringLiteral("COT"),
        QStringLiteral("ABS"), QStringLiteral("SGN"), QStringLiteral("SQRT"), QStringLiteral("LOG"),
        QStringLiteral("EXP"), QStringLiteral("ASIN"), QStringLiteral("ACOS"), QStringLiteral("ATAN"),
        QStringLiteral("ACOT"), QStringLiteral("SINH"), QStringLiteral("COSH"), QStringLiteral("TANH"),
        QStringLiteral("COTH"), QStringLiteral("LOG10"), QStringLiteral("STEP")
    };
    return functions.contains(id.toUpper());
}

bool hydraulicVariable(const QString &id)
{
    static const QSet<QString> variables = {
        QStringLiteral("D"), QStringLiteral("Q"), QStringLiteral("U"), QStringLiteral("RE"),
        QStringLiteral("US"), QStringLiteral("FF"), QStringLiteral("AV"), QStringLiteral("KC"),
        QStringLiteral("LEN")
    };
    return variables.contains(id.toUpper());
}

ExpressionAnalysis analyzeExpression(const QString &expression)
{
    ExpressionAnalysis analysis;
    int index = 0;
    int parentheses = 0;

    if (expression.trimmed().isEmpty())
    {
        analysis.valid = false;
        analysis.error = QStringLiteral("expression must not be empty");
        return analysis;
    }

    while (index < expression.size())
    {
        const QChar current = expression.at(index);
        if (current == QLatin1Char('\n') || current == QLatin1Char('\r') || current == QLatin1Char(';'))
        {
            analysis.valid = false;
            analysis.error = QStringLiteral("expression must not contain newlines or MSX comment delimiters");
            return analysis;
        }
        if (current.isSpace())
        {
            index++;
            continue;
        }
        if (current == QLatin1Char('('))
        {
            parentheses++;
            index++;
            continue;
        }
        if (current == QLatin1Char(')'))
        {
            parentheses--;
            if (parentheses < 0)
            {
                analysis.valid = false;
                analysis.error = QStringLiteral("expression has an unmatched closing parenthesis");
                return analysis;
            }
            index++;
            continue;
        }
        if (current == QLatin1Char('+') || current == QLatin1Char('-')
            || current == QLatin1Char('*') || current == QLatin1Char('/')
            || current == QLatin1Char('^'))
        {
            index++;
            continue;
        }
        if (isAsciiDigit(current) || current == QLatin1Char('.'))
        {
            bool digit_seen = false;
            while (index < expression.size() && isAsciiDigit(expression.at(index)))
            {
                digit_seen = true;
                index++;
            }
            if (index < expression.size() && expression.at(index) == QLatin1Char('.'))
            {
                index++;
                while (index < expression.size() && isAsciiDigit(expression.at(index)))
                {
                    digit_seen = true;
                    index++;
                }
            }
            if (!digit_seen)
            {
                analysis.valid = false;
                analysis.error = QStringLiteral("expression contains an invalid number");
                return analysis;
            }
            if (index < expression.size() && (expression.at(index) == QLatin1Char('e') || expression.at(index) == QLatin1Char('E')))
            {
                index++;
                if (index < expression.size() && (expression.at(index) == QLatin1Char('+') || expression.at(index) == QLatin1Char('-')))
                    index++;
                const int exponent_start = index;
                while (index < expression.size() && isAsciiDigit(expression.at(index)))
                    index++;
                if (index == exponent_start)
                {
                    analysis.valid = false;
                    analysis.error = QStringLiteral("expression contains an invalid scientific-notation exponent");
                    return analysis;
                }
            }
            continue;
        }
        if (isSymbolStart(current))
        {
            const int start = index;
            index++;
            while (index < expression.size() && isSymbolPart(expression.at(index)))
                index++;
            const QString symbol = expression.mid(start, index - start);
            if (!mathFunction(symbol) && !hydraulicVariable(symbol))
                analysis.symbols.insert(symbol);
            continue;
        }

        analysis.valid = false;
        analysis.error = QStringLiteral("expression contains an unsupported character '%1'").arg(current);
        return analysis;
    }

    if (parentheses != 0)
    {
        analysis.valid = false;
        analysis.error = QStringLiteral("expression has unbalanced parentheses");
    }
    return analysis;
}


QString speciesIdByUuid(const NetworkMultiSpecies &model, const QUuid &species_uuid)
{
    for (const MultiSpeciesSpecies &species : model.species)
    {
        if (species.uuid == species_uuid)
            return species.id;
    }
    return QString();
}

bool findTermCycle(
    const QString &term,
    const QHash<QString, QSet<QString>> &dependencies,
    QHash<QString, int> &states,
    QStringList &stack,
    QStringList &cycle)
{
    states.insert(term, 1);
    stack.append(term);
    const QSet<QString> term_dependencies = dependencies.value(term);
    for (const QString &dependency : term_dependencies)
    {
        if (!dependencies.contains(dependency))
            continue;
        const int state = states.value(dependency, 0);
        if (state == 1)
        {
            const int cycle_start = stack.indexOf(dependency);
            cycle = stack.mid(cycle_start);
            cycle.append(dependency);
            return true;
        }
        if (state == 0 && findTermCycle(dependency, dependencies, states, stack, cycle))
            return true;
    }
    stack.removeLast();
    states.insert(term, 2);
    return false;
}

void validateExpressions(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> &failures)
{
    const NetworkMultiSpecies &model = network.multi_species;
    QSet<QString> known_symbols;
    QHash<QString, QSet<QString>> term_dependencies;

    for (const MultiSpeciesSpecies &species : model.species)
        known_symbols.insert(species.id);
    for (const MultiSpeciesConstant &constant : model.constants)
        known_symbols.insert(constant.id);
    for (const MultiSpeciesParameter &parameter : model.parameters)
        known_symbols.insert(parameter.id);
    for (const MultiSpeciesTerm &term : model.terms)
        known_symbols.insert(term.id);

    for (const MultiSpeciesTerm &term : model.terms)
    {
        const ExpressionAnalysis analysis = analyzeExpression(term.expression);
        if (!analysis.valid)
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species term has an invalid expression"),
                {QStringLiteral("Term: %1").arg(term.id), analysis.error}));
            continue;
        }
        if ((term.id + QLatin1Char(' ') + term.expression).toUtf8().size() >= kMsxMaximumInputLineLength)
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species term exceeds the EPANET-MSX input line limit"),
                {QStringLiteral("Term: %1").arg(term.id)}));
        }
        QSet<QString> dependencies;
        for (const QString &symbol : analysis.symbols)
        {
            if (!known_symbols.contains(symbol))
            {
                failures.append(msxValidationFailure(
                    network,
                    QStringLiteral("Multi-species term references an unknown expression symbol"),
                    {QStringLiteral("Term: %1").arg(term.id), QStringLiteral("Symbol: %1").arg(symbol)}));
            }
            if (known_symbols.contains(symbol))
                dependencies.insert(symbol);
        }
        term_dependencies.insert(term.id, dependencies);
    }

    for (const MultiSpeciesReaction &reaction : model.reactions)
    {
        const ExpressionAnalysis analysis = analyzeExpression(reaction.expression);
        if (!analysis.valid)
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species reaction has an invalid expression"),
                {QStringLiteral("Reaction UUID: %1").arg(reaction.uuid.toString(QUuid::WithoutBraces)), analysis.error}));
            continue;
        }
        for (const QString &symbol : analysis.symbols)
        {
            if (!known_symbols.contains(symbol))
            {
                failures.append(msxValidationFailure(
                    network,
                    QStringLiteral("Multi-species reaction references an unknown expression symbol"),
                    {QStringLiteral("Reaction UUID: %1").arg(reaction.uuid.toString(QUuid::WithoutBraces)),
                     QStringLiteral("Symbol: %1").arg(symbol)}));
            }
        }
        const QString species_id = speciesIdByUuid(model, reaction.species_uuid);
        const QString prefix = QStringLiteral("FORMULA %1 ").arg(species_id);
        if ((prefix + reaction.expression).toUtf8().size() >= kMsxMaximumInputLineLength)
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species reaction exceeds the EPANET-MSX input line limit"),
                {QStringLiteral("Reaction UUID: %1").arg(reaction.uuid.toString(QUuid::WithoutBraces))}));
        }
    }

    QHash<QString, int> states;
    for (const MultiSpeciesTerm &term : model.terms)
    {
        if (states.value(term.id, 0) != 0)
            continue;
        QStringList stack;
        QStringList cycle;
        if (findTermCycle(term.id, term_dependencies, states, stack, cycle))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("Multi-species terms contain a cyclic expression dependency"),
                {QStringLiteral("Cycle: %1").arg(cycle.join(QStringLiteral(" -> ")))}));
            break;
        }
    }
}

HydraulicSimulationStatus finishValidation(
    const QList<HydraulicSimulationStatus> &failures,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    if (validation_failures != nullptr)
        *validation_failures = failures;
    return failures.isEmpty() ? makeEpanetSuccess() : failures.first();
}
}

HydraulicSimulationStatus validateEpanetMultiSpeciesModel(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    QList<HydraulicSimulationStatus> failures;
    const NetworkMultiSpecies &model = network.multi_species;

    if (!modelHasContent(model))
        return finishValidation(failures, validation_failures);

    if (model.species.isEmpty())
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("A non-empty multi-species model must define at least one species")));
    }

    validateIdentities(network, failures);
    validateReferencesAndAssignments(network, failures);
    validateNumerics(network, failures);
    validateExpressions(network, failures);
    return finishValidation(failures, validation_failures);
}

HydraulicSimulationStatus validateEpanetMultiSpeciesRun(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    QList<HydraulicSimulationStatus> failures;
    QSet<QUuid> available_species;
    QSet<QUuid> selected_species;

    for (const MultiSpeciesSpecies &species : network.multi_species.species)
        available_species.insert(species.uuid);

    if (available_species.isEmpty())
    {
        failures.append(msxValidationFailure(
            network,
            QStringLiteral("multi_species_run requires a multi-species model with at least one species")));
    }

    for (const QUuid &species_uuid : run_options.output_species_uuids)
    {
        if (species_uuid.isNull())
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("multi_species_run contains a null output-species UUID")));
            continue;
        }
        if (!available_species.contains(species_uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("multi_species_run selects an unresolved species UUID %1")
                    .arg(species_uuid.toString(QUuid::WithoutBraces))));
        }
        if (selected_species.contains(species_uuid))
        {
            failures.append(msxValidationFailure(
                network,
                QStringLiteral("multi_species_run selects the same output species more than once"),
                {QStringLiteral("Species UUID: %1").arg(species_uuid.toString(QUuid::WithoutBraces))}));
        }
        else
        {
            selected_species.insert(species_uuid);
        }
    }

    return finishValidation(failures, validation_failures);
}
