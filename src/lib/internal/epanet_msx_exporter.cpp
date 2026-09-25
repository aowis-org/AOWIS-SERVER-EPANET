#include "epanet_msx_exporter.h"

#include "epanet_status_helpers.h"
#include "epanet_msx_validator.h"
#include "epanet_msx_units.h"

#include <QHash>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic.h>

namespace
{
QString mapNumber(double value)
{
    return QString::number(value, 'g', 15);
}

QString areaUnitsToken(MultiSpeciesAreaUnits value)
{
    switch (value)
    {
    case MultiSpeciesAreaUnits::SquareFeet:
        return QStringLiteral("FT2");
    case MultiSpeciesAreaUnits::SquareMetres:
        return QStringLiteral("M2");
    case MultiSpeciesAreaUnits::SquareCentimetres:
        return QStringLiteral("CM2");
    }
    return QStringLiteral("FT2");
}

QString rateUnitsToken(MultiSpeciesRateUnits value)
{
    switch (value)
    {
    case MultiSpeciesRateUnits::Seconds:
        return QStringLiteral("SEC");
    case MultiSpeciesRateUnits::Minutes:
        return QStringLiteral("MIN");
    case MultiSpeciesRateUnits::Hours:
        return QStringLiteral("HR");
    case MultiSpeciesRateUnits::Days:
        return QStringLiteral("DAY");
    }
    return QStringLiteral("DAY");
}

QString solverMethodToken(MultiSpeciesSolverMethod value)
{
    switch (value)
    {
    case MultiSpeciesSolverMethod::Euler:
        return QStringLiteral("EUL");
    case MultiSpeciesSolverMethod::RungeKutta5:
        return QStringLiteral("RK5");
    case MultiSpeciesSolverMethod::Rosenbrock2:
        return QStringLiteral("ROS2");
    }
    return QStringLiteral("RK5");
}

QString couplingMethodToken(MultiSpeciesCouplingMethod value)
{
    switch (value)
    {
    case MultiSpeciesCouplingMethod::Full:
        return QStringLiteral("FULL");
    case MultiSpeciesCouplingMethod::None:
        return QStringLiteral("NONE");
    }
    return QStringLiteral("FULL");
}

QString speciesTypeToken(MultiSpeciesSpeciesType value)
{
    return value == MultiSpeciesSpeciesType::Wall ? QStringLiteral("WALL") : QStringLiteral("BULK");
}

QString speciesUnitsToken(MultiSpeciesUnits value)
{
    switch (value)
    {
    case MultiSpeciesUnits::Milligrams:
        return QStringLiteral("MG");
    case MultiSpeciesUnits::Micrograms:
        return QStringLiteral("UG");
    case MultiSpeciesUnits::Moles:
        return QStringLiteral("MOLE");
    case MultiSpeciesUnits::Millimoles:
        return QStringLiteral("MMOL");
    }
    return QStringLiteral("MG");
}

QString reactionExpressionTypeToken(MultiSpeciesReactionExpressionType value)
{
    switch (value)
    {
    case MultiSpeciesReactionExpressionType::Rate:
        return QStringLiteral("RATE");
    case MultiSpeciesReactionExpressionType::Formula:
        return QStringLiteral("FORMULA");
    case MultiSpeciesReactionExpressionType::Equilibrium:
        return QStringLiteral("EQUIL");
    }
    return QStringLiteral("RATE");
}

QString sourceTypeToken(MultiSpeciesSourceType value)
{
    switch (value)
    {
    case MultiSpeciesSourceType::Concentration:
        return QStringLiteral("CONC");
    case MultiSpeciesSourceType::Mass:
        return QStringLiteral("MASS");
    case MultiSpeciesSourceType::FlowPaced:
        return QStringLiteral("FLOWPACED");
    case MultiSpeciesSourceType::Setpoint:
        return QStringLiteral("SETPOINT");
    }
    return QStringLiteral("CONC");
}

template<typename Entity>
QHash<QUuid, QString> idsByUuid(const QList<Entity> &entities)
{
    QHash<QUuid, QString> ids;
    for (const Entity &entity : entities)
        ids.insert(entity.uuid, entity.id);
    return ids;
}

QHash<QUuid, QString> nodeIdsByUuid(const NetworkHydraulic &network)
{
    QHash<QUuid, QString> ids;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        ids.insert(junction.uuid, junction.id);
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        ids.insert(reservoir.uuid, reservoir.id);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        ids.insert(tank.uuid, tank.id);
    return ids;
}

QHash<QUuid, QString> tankIdsByUuid(const NetworkHydraulic &network)
{
    QHash<QUuid, QString> ids;
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        ids.insert(tank.uuid, tank.id);
    return ids;
}

QHash<QUuid, QString> pipeIdsByUuid(const NetworkHydraulic &network)
{
    return idsByUuid(network.links_pipes);
}

struct MsxLookups
{
    QHash<QUuid, QString> node_ids;
    QHash<QUuid, QString> tank_ids;
    QHash<QUuid, QString> pipe_ids;
    QHash<QUuid, QString> species_ids;
    QHash<QUuid, QString> constant_ids;
    QHash<QUuid, QString> parameter_ids;
    QHash<QUuid, QString> pattern_ids;
    QHash<QUuid, MultiSpeciesSpecies> species_by_uuid;
};

MsxLookups buildLookups(const NetworkHydraulic &network)
{
    MsxLookups lookups;
    lookups.node_ids = nodeIdsByUuid(network);
    lookups.tank_ids = tankIdsByUuid(network);
    lookups.pipe_ids = pipeIdsByUuid(network);
    lookups.species_ids = idsByUuid(network.multi_species.species);
    lookups.constant_ids = idsByUuid(network.multi_species.constants);
    lookups.parameter_ids = idsByUuid(network.multi_species.parameters);
    lookups.pattern_ids = idsByUuid(network.multi_species.patterns);
    for (const MultiSpeciesSpecies &species : network.multi_species.species)
        lookups.species_by_uuid.insert(species.uuid, species);
    return lookups;
}

// A single point of failure for every UUID reference this exporter resolves,
// so every "broken reference" diagnostic looks the same regardless of which
// section produced it.
HydraulicSimulationStatus resolveOrFail(
    const QHash<QUuid, QString> &ids,
    const QUuid &uuid,
    const NetworkHydraulic &network,
    const QString &what,
    QString &resolved_id)
{
    const QHash<QUuid, QString>::const_iterator found = ids.constFind(uuid);
    if (found == ids.constEnd())
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Multi-species model references an unresolved %1 UUID %2")
                .arg(what, uuid.toString(QUuid::WithoutBraces)));
    }

    resolved_id = found.value();
    return makeEpanetSuccess();
}

void appendTitleSection(QStringList &lines, const NetworkHydraulic &network)
{
    if (network.title_line_1.isEmpty())
        return;

    lines.append(QStringLiteral("[TITLE]"));
    lines.append(network.title_line_1);
    lines.append(QString());
}

void appendOptionsSection(QStringList &lines, const MultiSpeciesOptions &options)
{
    lines.append(QStringLiteral("[OPTIONS]"));
    lines.append(QStringLiteral("AREA_UNITS %1").arg(areaUnitsToken(options.area_units)));
    lines.append(QStringLiteral("RATE_UNITS %1").arg(rateUnitsToken(options.rate_units)));
    lines.append(QStringLiteral("SOLVER %1").arg(solverMethodToken(options.solver_method)));
    lines.append(QStringLiteral("COUPLING %1").arg(couplingMethodToken(options.coupling_method)));
    lines.append(QStringLiteral("TIMESTEP %1").arg(mapNumber(options.timestep_s)));
    lines.append(QStringLiteral("SEGMENTS %1").arg(options.maximum_segments));
    lines.append(QStringLiteral("PECLET %1").arg(mapNumber(options.peclet_number_threshold)));
    lines.append(QStringLiteral("ATOL %1").arg(mapNumber(options.default_absolute_tolerance)));
    lines.append(QStringLiteral("RTOL %1").arg(mapNumber(options.default_relative_tolerance)));
    lines.append(QString());
}

void appendSpeciesSection(
    QStringList &lines,
    const NetworkHydraulic &network)
{
    lines.append(QStringLiteral("[SPECIES]"));
    for (const MultiSpeciesSpecies &species : network.multi_species.species)
    {
        QString line = QStringLiteral("%1 %2 %3")
            .arg(speciesTypeToken(species.type), species.id, speciesUnitsToken(species.units));
        // Omitting the trailing tolerance pair means MSX applies the
        // [OPTIONS] ATOL/RTOL default, matching this species' own
        // absolute_tolerance/relative_tolerance being left at zero.
        if (species.absolute_tolerance > 0.0 || species.relative_tolerance > 0.0)
        {
            line += QStringLiteral(" %1 %2")
                .arg(mapNumber(species.absolute_tolerance), mapNumber(species.relative_tolerance));
        }
        lines.append(line);
    }
    lines.append(QString());
}

void appendCoefficientsSection(QStringList &lines, const NetworkMultiSpecies &model)
{
    lines.append(QStringLiteral("[COEFFICIENTS]"));
    for (const MultiSpeciesConstant &constant : model.constants)
        lines.append(QStringLiteral("CONSTANT %1 %2").arg(constant.id, mapNumber(constant.value)));
    for (const MultiSpeciesParameter &parameter : model.parameters)
        lines.append(QStringLiteral("PARAMETER %1 %2").arg(parameter.id, mapNumber(parameter.default_value)));
    lines.append(QString());
}

void appendTermsSection(QStringList &lines, const NetworkMultiSpecies &model)
{
    lines.append(QStringLiteral("[TERMS]"));
    for (const MultiSpeciesTerm &term : model.terms)
        lines.append(QStringLiteral("%1 %2").arg(term.id, term.expression));
    lines.append(QString());
}

HydraulicSimulationStatus appendReactionSection(
    QStringList &lines,
    const QString &section_name,
    MultiSpeciesReactionLocation location,
    const NetworkHydraulic &network,
    const MsxLookups &lookups)
{
    lines.append(QStringLiteral("[%1]").arg(section_name));
    for (const MultiSpeciesReaction &reaction : network.multi_species.reactions)
    {
        if (reaction.location != location)
            continue;

        // Every reaction remains part of the model regardless of which
        // species the caller wants returned. Reaction expressions can couple
        // species, so output selection must never alter solver chemistry.
        QString species_id;
        const HydraulicSimulationStatus status = resolveOrFail(
            lookups.species_ids, reaction.species_uuid, network, QStringLiteral("reaction species"), species_id);
        if (!status.success)
            return status;

        lines.append(QStringLiteral("%1 %2 %3")
            .arg(reactionExpressionTypeToken(reaction.expression_type), species_id, reaction.expression));
    }
    lines.append(QString());
    return makeEpanetSuccess();
}

HydraulicSimulationStatus appendSourcesSection(
    QStringList &lines,
    const NetworkHydraulic &network,
    const MsxLookups &lookups)
{
    lines.append(QStringLiteral("[SOURCES]"));
    for (const MultiSpeciesNodeSource &source : network.multi_species.sources)
    {
        QString species_id;
        HydraulicSimulationStatus status = resolveOrFail(
            lookups.species_ids, source.species_uuid, network, QStringLiteral("source species"), species_id);
        if (!status.success)
            return status;

        QString node_id;
        status = resolveOrFail(
            lookups.node_ids, source.node_uuid, network, QStringLiteral("source node"), node_id);
        if (!status.success)
            return status;

        const MultiSpeciesSpecies species = lookups.species_by_uuid.value(source.species_uuid);
        const double strength = EpanetMsxUnits::bulkSourceValueToSolver(source.value, species.units);

        QString line = QStringLiteral("%1 %2 %3 %4")
            .arg(sourceTypeToken(source.type), node_id, species_id, mapNumber(strength));

        if (!source.pattern_uuid.isNull())
        {
            QString pattern_id;
            status = resolveOrFail(
                lookups.pattern_ids, source.pattern_uuid, network, QStringLiteral("source pattern"), pattern_id);
            if (!status.success)
                return status;
            line += QLatin1Char(' ') + pattern_id;
        }

        lines.append(line);
    }
    lines.append(QString());
    return makeEpanetSuccess();
}

HydraulicSimulationStatus appendQualitySection(
    QStringList &lines,
    const NetworkHydraulic &network,
    const MsxLookups &lookups)
{
    lines.append(QStringLiteral("[QUALITY]"));

    for (const MultiSpeciesGlobalInitialQuality &initial : network.multi_species.initial_quality_global)
    {
        QString species_id;
        const HydraulicSimulationStatus status = resolveOrFail(
            lookups.species_ids, initial.species_uuid, network, QStringLiteral("initial-quality species"), species_id);
        if (!status.success)
            return status;
        const MultiSpeciesSpecies species = lookups.species_by_uuid.value(initial.species_uuid);
        const double value = EpanetMsxUnits::speciesValueToSolver(
            initial.value, species.type, species.units, network.multi_species.options.area_units);
        lines.append(QStringLiteral("GLOBAL %1 %2").arg(species_id, mapNumber(value)));
    }

    for (const MultiSpeciesNodeInitialQuality &initial : network.multi_species.initial_quality_nodes)
    {
        QString species_id;
        HydraulicSimulationStatus status = resolveOrFail(
            lookups.species_ids, initial.species_uuid, network, QStringLiteral("initial-quality species"), species_id);
        if (!status.success)
            return status;
        QString node_id;
        status = resolveOrFail(
            lookups.node_ids, initial.node_uuid, network, QStringLiteral("initial-quality node"), node_id);
        if (!status.success)
            return status;
        const MultiSpeciesSpecies species = lookups.species_by_uuid.value(initial.species_uuid);
        const double value = EpanetMsxUnits::speciesValueToSolver(
            initial.value, species.type, species.units, network.multi_species.options.area_units);
        lines.append(QStringLiteral("NODE %1 %2 %3").arg(node_id, species_id, mapNumber(value)));
    }

    for (const MultiSpeciesPipeInitialQuality &initial : network.multi_species.initial_quality_pipes)
    {
        QString species_id;
        HydraulicSimulationStatus status = resolveOrFail(
            lookups.species_ids, initial.species_uuid, network, QStringLiteral("initial-quality species"), species_id);
        if (!status.success)
            return status;
        QString pipe_id;
        status = resolveOrFail(
            lookups.pipe_ids, initial.pipe_uuid, network, QStringLiteral("initial-quality pipe"), pipe_id);
        if (!status.success)
            return status;
        const MultiSpeciesSpecies species = lookups.species_by_uuid.value(initial.species_uuid);
        const double value = EpanetMsxUnits::speciesValueToSolver(
            initial.value, species.type, species.units, network.multi_species.options.area_units);
        lines.append(QStringLiteral("LINK %1 %2 %3").arg(pipe_id, species_id, mapNumber(value)));
    }

    lines.append(QString());
    return makeEpanetSuccess();
}

HydraulicSimulationStatus appendParametersSection(
    QStringList &lines,
    const NetworkHydraulic &network,
    const MsxLookups &lookups)
{
    lines.append(QStringLiteral("[PARAMETERS]"));

    for (const MultiSpeciesParameterOverridePipe &override_value : network.multi_species.parameter_overrides_pipes)
    {
        QString pipe_id;
        HydraulicSimulationStatus status = resolveOrFail(
            lookups.pipe_ids, override_value.pipe_uuid, network, QStringLiteral("parameter-override pipe"), pipe_id);
        if (!status.success)
            return status;
        QString parameter_id;
        status = resolveOrFail(
            lookups.parameter_ids, override_value.parameter_uuid, network, QStringLiteral("parameter-override parameter"), parameter_id);
        if (!status.success)
            return status;
        lines.append(QStringLiteral("PIPE %1 %2 %3").arg(pipe_id, parameter_id, mapNumber(override_value.value)));
    }

    for (const MultiSpeciesParameterOverrideTank &override_value : network.multi_species.parameter_overrides_tanks)
    {
        QString tank_id;
        HydraulicSimulationStatus status = resolveOrFail(
            lookups.tank_ids, override_value.tank_uuid, network, QStringLiteral("parameter-override tank"), tank_id);
        if (!status.success)
            return status;
        QString parameter_id;
        status = resolveOrFail(
            lookups.parameter_ids, override_value.parameter_uuid, network, QStringLiteral("parameter-override parameter"), parameter_id);
        if (!status.success)
            return status;
        lines.append(QStringLiteral("TANK %1 %2 %3").arg(tank_id, parameter_id, mapNumber(override_value.value)));
    }

    lines.append(QString());
    return makeEpanetSuccess();
}

void appendDiffusivitySection(QStringList &lines, const NetworkMultiSpecies &model)
{
    bool has_values = false;
    for (const MultiSpeciesSpecies &species : model.species)
    {
        if (species.molecular_diffusivity_m2_per_s.has_value()
            || species.longitudinal_dispersion_coefficient_m2_per_s.has_value())
        {
            has_values = true;
            break;
        }
    }

    if (!has_values)
        return;

    lines.append(QStringLiteral("[DIFFUSIVITY]"));
    for (const MultiSpeciesSpecies &species : model.species)
    {
        if (species.molecular_diffusivity_m2_per_s.has_value())
        {
            lines.append(QStringLiteral("%1 %2").arg(
                species.id,
                mapNumber(EpanetMsxUnits::diffusivityToSolverRatio(
                    species.molecular_diffusivity_m2_per_s.value()))));
        }
        if (species.longitudinal_dispersion_coefficient_m2_per_s.has_value())
        {
            lines.append(QStringLiteral("%1 %2 FIXED").arg(
                species.id,
                mapNumber(EpanetMsxUnits::diffusivityToSolverRatio(
                    species.longitudinal_dispersion_coefficient_m2_per_s.value()))));
        }
    }
    lines.append(QString());
}

void appendPatternsSection(QStringList &lines, const NetworkMultiSpecies &model)
{
    static const int values_per_line = 6;

    lines.append(QStringLiteral("[PATTERNS]"));
    for (const MultiSpeciesPattern &pattern : model.patterns)
    {
        for (int start = 0; start < pattern.multipliers.size(); start += values_per_line)
        {
            QStringList chunk;
            const int end = qMin(start + values_per_line, pattern.multipliers.size());
            for (int index = start; index < end; index++)
                chunk.append(mapNumber(pattern.multipliers.at(index)));
            lines.append(QStringLiteral("%1 %2").arg(pattern.id, chunk.join(QLatin1Char(' '))));
        }
    }
    lines.append(QString());
}
}

HydraulicSimulationStatus retrieveEpanetMsxText(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    QString &msx_text)
{
    msx_text.clear();

    HydraulicSimulationStatus status = validateEpanetMultiSpeciesModel(network);
    if (!status.success)
        return status;
    status = validateEpanetMultiSpeciesRun(network, run_options);
    if (!status.success)
        return status;

    const MsxLookups lookups = buildLookups(network);

    QStringList lines;
    appendTitleSection(lines, network);
    appendOptionsSection(lines, network.multi_species.options);
    appendSpeciesSection(lines, network);
    appendCoefficientsSection(lines, network.multi_species);
    appendTermsSection(lines, network.multi_species);

    status = appendReactionSection(lines, QStringLiteral("PIPES"), MultiSpeciesReactionLocation::Pipe, network, lookups);
    if (!status.success)
        return status;
    status = appendReactionSection(lines, QStringLiteral("TANKS"), MultiSpeciesReactionLocation::Tank, network, lookups);
    if (!status.success)
        return status;
    status = appendSourcesSection(lines, network, lookups);
    if (!status.success)
        return status;
    status = appendQualitySection(lines, network, lookups);
    if (!status.success)
        return status;
    status = appendParametersSection(lines, network, lookups);
    if (!status.success)
        return status;

    appendPatternsSection(lines, network.multi_species);
    appendDiffusivitySection(lines, network.multi_species);
    lines.append(QStringLiteral("[END]"));

    msx_text = lines.join(QLatin1Char('\n'));
    return makeEpanetSuccess();
}
