#include "generated_quality_stress_fixture.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace AowisEpanetTests
{
namespace
{
struct QualityNodeSpecification
{
    QString id;
    double initial_value = 0.0;
    HydraulicNodeQualitySourceType source_type = HydraulicNodeQualitySourceType::None;
    double source_strength = 0.0;
    QString source_pattern_id;
};

struct QualityPipeReactionSpecification
{
    QString id;
    double bulk_coefficient = 0.0;
    double wall_coefficient = 0.0;
};

struct QualityStressSpecification
{
    WaterQualityAnalysisType analysis = WaterQualityAnalysisType::None;
    QString chemical_name;
    QString trace_node_id;
    double tolerance = 0.01;
    double relative_diffusivity = 1.0;

    double global_bulk_coefficient = 0.0;
    double global_bulk_order = 1.0;
    double global_wall_coefficient = 0.0;
    double global_wall_order = 1.0;
    double global_tank_coefficient = 0.0;
    double global_tank_order = 1.0;
    double limiting_concentration_mg_per_l = 0.0;
    double roughness_reaction_factor = 0.0;

    std::vector<QualityNodeSpecification> nodes;
    std::vector<QualityPipeReactionSpecification> pipe_reactions;
};

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString timeToken(int seconds)
{
    if (seconds < 0)
        throw std::runtime_error("Generated quality stress time cannot be negative");

    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remaining_seconds = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remaining_seconds, 2, 10, QLatin1Char('0'));
}

QString sourceTypeToken(HydraulicNodeQualitySourceType type)
{
    switch (type)
    {
    case HydraulicNodeQualitySourceType::Concentration:
        return QStringLiteral("CONCEN");
    case HydraulicNodeQualitySourceType::MassBooster:
        return QStringLiteral("MASS");
    case HydraulicNodeQualitySourceType::FlowPacedBooster:
        return QStringLiteral("FLOWPACED");
    case HydraulicNodeQualitySourceType::SetpointBooster:
        return QStringLiteral("SETPOINT");
    case HydraulicNodeQualitySourceType::None:
        break;
    }
    throw std::runtime_error("Generated quality source token requested for source type None");
}

QString analysisOption(const QualityStressSpecification &quality)
{
    switch (quality.analysis)
    {
    case WaterQualityAnalysisType::None:
        return QStringLiteral(" QUALITY NONE\n");
    case WaterQualityAnalysisType::Chemical:
        return QStringLiteral(" QUALITY %1 mg/L\n").arg(quality.chemical_name);
    case WaterQualityAnalysisType::WaterAge:
        return QStringLiteral(" QUALITY AGE\n");
    case WaterQualityAnalysisType::SourceTrace:
        return QStringLiteral(" QUALITY TRACE %1\n").arg(quality.trace_node_id);
    }
    throw std::runtime_error("Unsupported generated quality analysis type");
}

QualityNodeSpecification *findNodeSpecification(QualityStressSpecification &quality, const QString &id)
{
    for (QualityNodeSpecification &node : quality.nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

QualityStressSpecification generateQualitySpecification(const GeneratedQualityStressCase &definition, const GeneratedStressFixture &hydraulic_fixture)
{
    QualityStressSpecification quality;
    quality.analysis = definition.analysis;
    quality.relative_diffusivity = 1.0;

    for (const HydraulicNodeJunction &node : hydraulic_fixture.network.nodes_junctions)
    {
        QualityNodeSpecification specification;
        specification.id = node.id;
        quality.nodes.push_back(specification);
    }
    for (const HydraulicNodeReservoir &node : hydraulic_fixture.network.nodes_reservoirs)
    {
        QualityNodeSpecification specification;
        specification.id = node.id;
        quality.nodes.push_back(specification);
    }

    if (definition.analysis == WaterQualityAnalysisType::Chemical)
    {
        quality.chemical_name = QStringLiteral("Chlorine");
        quality.tolerance = 0.01;

        if (hydraulic_fixture.network.nodes_reservoirs.isEmpty())
            throw std::runtime_error("Generated chemical stress case requires a reservoir");

        const QString primary_id = hydraulic_fixture.network.nodes_reservoirs.first().id;
        QualityNodeSpecification *primary = findNodeSpecification(quality, primary_id);
        if (primary == nullptr)
            throw std::runtime_error("Generated chemical stress primary source is missing");
        primary->initial_value = 1.25;
        primary->source_type = HydraulicNodeQualitySourceType::Concentration;
        primary->source_strength = 1.25;
        if (definition.patterned_sources && !hydraulic_fixture.network.patterns_time.isEmpty())
            primary->source_pattern_id = hydraulic_fixture.network.patterns_time.first().id;

        if (hydraulic_fixture.network.nodes_reservoirs.size() > 1)
        {
            const QString secondary_id = hydraulic_fixture.network.nodes_reservoirs.last().id;
            QualityNodeSpecification *secondary = findNodeSpecification(quality, secondary_id);
            if (secondary == nullptr)
                throw std::runtime_error("Generated chemical stress secondary source is missing");
            secondary->initial_value = 0.65;
            secondary->source_type = HydraulicNodeQualitySourceType::Concentration;
            secondary->source_strength = 0.65;
            if (definition.patterned_sources && hydraulic_fixture.network.patterns_time.size() > 1)
                secondary->source_pattern_id = hydraulic_fixture.network.patterns_time.at(1).id;
        }

        const int junction_count = static_cast<int>(hydraulic_fixture.network.nodes_junctions.size());
        const int stride = std::max(1, junction_count / 7);
        for (int index = 0; index < junction_count; index += stride)
        {
            QualityNodeSpecification *node = findNodeSpecification(quality, hydraulic_fixture.network.nodes_junctions.at(index).id);
            if (node != nullptr)
                node->initial_value = 0.03 + 0.01 * static_cast<double>(index % 5);
        }

        if (definition.reactions)
        {
            quality.global_bulk_coefficient = -0.18;
            quality.global_wall_coefficient = -0.05;
            quality.global_tank_coefficient = -0.12;
            quality.limiting_concentration_mg_per_l = 0.02;
            quality.roughness_reaction_factor = -1.4;

            if (!hydraulic_fixture.network.links_pipes.isEmpty())
            {
                QualityPipeReactionSpecification reaction;
                reaction.id = hydraulic_fixture.network.links_pipes.first().id;
                reaction.bulk_coefficient = -0.31;
                reaction.wall_coefficient = -0.14;
                quality.pipe_reactions.push_back(reaction);
            }
            if (hydraulic_fixture.network.links_pipes.size() > 3)
            {
                QualityPipeReactionSpecification reaction;
                reaction.id = hydraulic_fixture.network.links_pipes.at(hydraulic_fixture.network.links_pipes.size() / 2).id;
                reaction.bulk_coefficient = -0.27;
                reaction.wall_coefficient = -0.09;
                quality.pipe_reactions.push_back(reaction);
            }
        }
    }
    else if (definition.analysis == WaterQualityAnalysisType::WaterAge)
    {
        quality.tolerance = 0.01;
        const int junction_count = static_cast<int>(hydraulic_fixture.network.nodes_junctions.size());
        const int stride = std::max(1, junction_count / 6);
        for (int index = 0; index < junction_count; index += stride)
        {
            QualityNodeSpecification *node = findNodeSpecification(quality, hydraulic_fixture.network.nodes_junctions.at(index).id);
            if (node != nullptr)
                node->initial_value = 0.1 * static_cast<double>((index % 4) + 1);
        }
    }
    else if (definition.analysis == WaterQualityAnalysisType::SourceTrace)
    {
        quality.tolerance = 0.01;
        if (hydraulic_fixture.network.nodes_reservoirs.isEmpty())
            throw std::runtime_error("Generated source-trace stress case requires a reservoir");
        quality.trace_node_id = hydraulic_fixture.network.nodes_reservoirs.first().id;
    }

    return quality;
}

QUuid patternUuid(const NetworkHydraulic &network, const QString &id)
{
    if (id.isEmpty())
        return QUuid();
    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        if (pattern.id == id)
            return pattern.uuid;
    }
    throw std::runtime_error("Generated quality stress pattern ID is unresolved in Model fixture");
}

void applyNodeSpecification(HydraulicNodeJunction &node, const QualityNodeSpecification &quality, const NetworkHydraulic &network, WaterQualityAnalysisType analysis)
{
    if (analysis == WaterQualityAnalysisType::Chemical)
        node.initial_chemical_concentration_mg_per_l = quality.initial_value;
    else if (analysis == WaterQualityAnalysisType::WaterAge)
        node.initial_water_age_h = quality.initial_value;
    else if (analysis == WaterQualityAnalysisType::SourceTrace)
        node.initial_source_trace_percent = quality.initial_value;

    node.quality_source.type = quality.source_type;
    if (quality.source_type == HydraulicNodeQualitySourceType::MassBooster)
        node.quality_source.chemical_mass_flow_mg_per_min = quality.source_strength;
    else
        node.quality_source.chemical_concentration_mg_per_l = quality.source_strength;
    node.quality_source.pattern_uuid = patternUuid(network, quality.source_pattern_id);
}

void applyNodeSpecification(HydraulicNodeReservoir &node, const QualityNodeSpecification &quality, const NetworkHydraulic &network, WaterQualityAnalysisType analysis)
{
    if (analysis == WaterQualityAnalysisType::Chemical)
        node.initial_chemical_concentration_mg_per_l = quality.initial_value;
    else if (analysis == WaterQualityAnalysisType::WaterAge)
        node.initial_water_age_h = quality.initial_value;
    else if (analysis == WaterQualityAnalysisType::SourceTrace)
        node.initial_source_trace_percent = quality.initial_value;

    node.quality_source.type = quality.source_type;
    if (quality.source_type == HydraulicNodeQualitySourceType::MassBooster)
        node.quality_source.chemical_mass_flow_mg_per_min = quality.source_strength;
    else
        node.quality_source.chemical_concentration_mg_per_l = quality.source_strength;
    node.quality_source.pattern_uuid = patternUuid(network, quality.source_pattern_id);
}

void applyQualityToModel(NetworkHydraulic &network, const QualityStressSpecification &quality)
{
    network.options_quality.analysis = quality.analysis;
    network.options_quality.chemical_name = quality.chemical_name;
    network.options_quality.relative_diffusivity = quality.relative_diffusivity;
    if (quality.analysis == WaterQualityAnalysisType::Chemical)
        network.options_quality.chemical_tolerance_mg_per_l = quality.tolerance;
    else if (quality.analysis == WaterQualityAnalysisType::WaterAge)
        network.options_quality.water_age_tolerance_h = quality.tolerance;
    else if (quality.analysis == WaterQualityAnalysisType::SourceTrace)
        network.options_quality.source_trace_tolerance_percent = quality.tolerance;

    if (quality.analysis == WaterQualityAnalysisType::SourceTrace)
    {
        bool trace_node_found = false;
        for (const HydraulicNodeReservoir &node : network.nodes_reservoirs)
        {
            if (node.id == quality.trace_node_id)
            {
                network.options_quality.trace_node_uuid = node.uuid;
                trace_node_found = true;
                break;
            }
        }
        if (!trace_node_found)
            throw std::runtime_error("Generated source-trace stress node is unresolved in Model fixture");
    }

    for (HydraulicNodeJunction &node : network.nodes_junctions)
    {
        for (const QualityNodeSpecification &quality_node : quality.nodes)
        {
            if (quality_node.id == node.id)
            {
                applyNodeSpecification(node, quality_node, network, quality.analysis);
                break;
            }
        }
    }
    for (HydraulicNodeReservoir &node : network.nodes_reservoirs)
    {
        for (const QualityNodeSpecification &quality_node : quality.nodes)
        {
            if (quality_node.id == node.id)
            {
                applyNodeSpecification(node, quality_node, network, quality.analysis);
                break;
            }
        }
    }

    network.options_reaction.global_pipe_bulk_reaction.coefficient = quality.global_bulk_coefficient;
    network.options_reaction.global_pipe_bulk_reaction.order = quality.global_bulk_order;
    network.options_reaction.global_pipe_wall_reaction.coefficient = quality.global_wall_coefficient;
    network.options_reaction.global_pipe_wall_reaction.order = quality.global_wall_order;
    network.options_reaction.global_tank_bulk_reaction.coefficient = quality.global_tank_coefficient;
    network.options_reaction.global_tank_bulk_reaction.order = quality.global_tank_order;
    network.options_reaction.limiting_concentration_mg_per_l = quality.limiting_concentration_mg_per_l;
    network.options_reaction.roughness_reaction_factor = quality.roughness_reaction_factor;

    for (HydraulicLinkPipe &pipe : network.links_pipes)
    {
        for (const QualityPipeReactionSpecification &reaction : quality.pipe_reactions)
        {
            if (reaction.id != pipe.id)
                continue;
            pipe.override_reactions = true;
            pipe.bulk_reaction.coefficient = reaction.bulk_coefficient;
            pipe.bulk_reaction.order = quality.global_bulk_order;
            pipe.wall_reaction.coefficient = reaction.wall_coefficient;
            pipe.wall_reaction.order = quality.global_wall_order;
            break;
        }
    }
}

QString qualitySections(const QualityStressSpecification &quality)
{
    QString text;
    text += QStringLiteral("\n[QUALITY]\n;Node InitialQuality\n");
    for (const QualityNodeSpecification &node : quality.nodes)
    {
        if (node.initial_value == 0.0)
            continue;
        text += QLatin1Char(' ');
        text += node.id;
        text += QLatin1Char(' ');
        text += number(node.initial_value);
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("\n[SOURCES]\n;Node Type Strength Pattern\n");
    for (const QualityNodeSpecification &node : quality.nodes)
    {
        if (node.source_type == HydraulicNodeQualitySourceType::None)
            continue;
        text += QLatin1Char(' ');
        text += node.id;
        text += QLatin1Char(' ');
        text += sourceTypeToken(node.source_type);
        text += QLatin1Char(' ');
        text += number(node.source_strength);
        if (!node.source_pattern_id.isEmpty())
        {
            text += QLatin1Char(' ');
            text += node.source_pattern_id;
        }
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("\n[REACTIONS]\n");
    text += QStringLiteral(" Order Bulk ") + number(quality.global_bulk_order) + QLatin1Char('\n');
    text += QStringLiteral(" Order Wall ") + number(quality.global_wall_order) + QLatin1Char('\n');
    text += QStringLiteral(" Order Tank ") + number(quality.global_tank_order) + QLatin1Char('\n');
    text += QStringLiteral(" Global Bulk ") + number(quality.global_bulk_coefficient) + QLatin1Char('\n');
    text += QStringLiteral(" Global Wall ") + number(quality.global_wall_coefficient) + QLatin1Char('\n');
    text += QStringLiteral(" Limiting Potential ") + number(quality.limiting_concentration_mg_per_l) + QLatin1Char('\n');
    text += QStringLiteral(" Roughness Correlation ") + number(quality.roughness_reaction_factor) + QLatin1Char('\n');
    for (const QualityPipeReactionSpecification &reaction : quality.pipe_reactions)
    {
        text += QStringLiteral(" Bulk ") + reaction.id + QLatin1Char(' ') + number(reaction.bulk_coefficient) + QLatin1Char('\n');
        text += QStringLiteral(" Wall ") + reaction.id + QLatin1Char(' ') + number(reaction.wall_coefficient) + QLatin1Char('\n');
    }
    return text;
}

QString buildNativeQualityInp(QString text, const GeneratedQualityStressCase &definition, const QualityStressSpecification &quality)
{
    const int times_index = text.indexOf(QStringLiteral("\n[TIMES]\n"));
    if (times_index < 0)
        throw std::runtime_error("Generated hydraulic stress INP has no [TIMES] section");
    text.insert(times_index, qualitySections(quality));

    text.replace(QStringLiteral(" DURATION 6:00\n"), QStringLiteral(" DURATION %1\n").arg(timeToken(definition.duration_s)));
    text.replace(QStringLiteral(" HYDRAULIC TIMESTEP 1:00\n"), QStringLiteral(" HYDRAULIC TIMESTEP %1\n").arg(timeToken(definition.hydraulic_timestep_s)));
    text.replace(QStringLiteral(" QUALITY TIMESTEP 0:05\n"), QStringLiteral(" QUALITY TIMESTEP %1\n").arg(timeToken(definition.quality_timestep_s)));
    text.replace(QStringLiteral(" QUALITY NONE\n"), analysisOption(quality));
    text.replace(QStringLiteral(" VISCOSITY 1\n"), QStringLiteral(" VISCOSITY 1\n DIFFUSIVITY %1\n TOLERANCE %2\n")
        .arg(number(quality.relative_diffusivity))
        .arg(number(quality.tolerance)));
    return text;
}
}

GeneratedQualityStressFixture makeGeneratedQualityStressFixture(const GeneratedQualityStressCase &definition)
{
    if (definition.duration_s <= 0 || definition.hydraulic_timestep_s <= 0 || definition.quality_timestep_s <= 0)
        throw std::runtime_error("Generated quality stress timesteps and duration must be positive");
    if (definition.duration_s % definition.quality_timestep_s != 0)
        throw std::runtime_error("Generated quality stress duration must be divisible by quality timestep");

    GeneratedStressCase hydraulic_definition;
    hydraulic_definition.scenario_name = definition.scenario_name;
    hydraulic_definition.topology = definition.topology;
    hydraulic_definition.junction_count = definition.junction_count;
    hydraulic_definition.grid_rows = definition.grid_rows;
    hydraulic_definition.grid_columns = definition.grid_columns;
    hydraulic_definition.seed = definition.seed;
    hydraulic_definition.headloss_formula = definition.headloss_formula;

    const GeneratedStressFixture hydraulic_fixture = makeGeneratedStressFixture(hydraulic_definition);
    const QualityStressSpecification quality = generateQualitySpecification(definition, hydraulic_fixture);

    GeneratedQualityStressFixture fixture;
    fixture.network = hydraulic_fixture.network;
    fixture.network.duration_s = definition.duration_s;
    fixture.network.timestep_hydraulic_s = definition.hydraulic_timestep_s;
    fixture.network.timestep_quality_s = definition.quality_timestep_s;
    applyQualityToModel(fixture.network, quality);
    fixture.native_inp_text = buildNativeQualityInp(hydraulic_fixture.native_inp_text, definition, quality);
    fixture.expected_quality_sample_count = definition.duration_s / definition.quality_timestep_s;
    return fixture;
}
}
