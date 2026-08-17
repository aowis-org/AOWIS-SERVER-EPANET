#include "generated_network_stress_fixture.h"

#include <QByteArray>
#include <QChar>
#include <QUuid>

#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace AowisEpanetTests
{
namespace
{
struct StressPattern
{
    QString id;
    QUuid uuid;
    std::vector<double> factors;
};

struct StressJunction
{
    QString id;
    QUuid uuid;
    double elevation_m = 0.0;
    double demand_m3_per_h = 0.0;
    int pattern_index = 0;
};

struct StressReservoir
{
    QString id;
    QUuid uuid;
    double head_m = 0.0;
    int pattern_index = -1;
};

struct StressPipe
{
    QString id;
    QUuid uuid;
    QString from_id;
    QUuid from_uuid;
    QString to_id;
    QUuid to_uuid;
    double length_m = 0.0;
    double diameter_mm = 0.0;
    double roughness = 0.0;
    double minor_loss = 0.0;
};

struct StressSpecification
{
    QString title;
    HydraulicHeadlossFormula headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    std::vector<StressPattern> patterns;
    std::vector<StressJunction> junctions;
    std::vector<StressReservoir> reservoirs;
    std::vector<StressPipe> pipes;
};

QUuid deterministicUuid(std::uint64_t seed, unsigned char family, std::uint32_t index)
{
    QByteArray bytes(16, '\0');
    bytes[0] = static_cast<char>(0x8c);
    bytes[1] = static_cast<char>(family);
    for (int byte_index = 0; byte_index < 8; byte_index++)
        bytes[2 + byte_index] = static_cast<char>((seed >> (byte_index * 8)) & 0xffU);
    for (int byte_index = 0; byte_index < 4; byte_index++)
        bytes[10 + byte_index] = static_cast<char>((index >> (byte_index * 8)) & 0xffU);
    bytes[14] = static_cast<char>(0x80U | (family & 0x3fU));
    bytes[15] = static_cast<char>(0x01U);
    return QUuid::fromRfc4122(bytes);
}

double randomReal(std::mt19937_64 &generator, double minimum, double maximum)
{
    std::uniform_real_distribution<double> distribution(minimum, maximum);
    return distribution(generator);
}

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString headlossToken(HydraulicHeadlossFormula formula)
{
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        return QStringLiteral("H-W");
    case HydraulicHeadlossFormula::DarcyWeisbach:
        return QStringLiteral("D-W");
    case HydraulicHeadlossFormula::ChezyManning:
        return QStringLiteral("C-M");
    }
    throw std::runtime_error("Unsupported generated-stress headloss formula");
}

double generatedRoughness(HydraulicHeadlossFormula formula, std::mt19937_64 &generator)
{
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        return randomReal(generator, 118.0, 145.0);
    case HydraulicHeadlossFormula::DarcyWeisbach:
        return randomReal(generator, 0.08, 0.45);
    case HydraulicHeadlossFormula::ChezyManning:
        return randomReal(generator, 0.011, 0.017);
    }
    throw std::runtime_error("Unsupported generated-stress roughness formula");
}

void addPattern(StressSpecification &specification, std::uint64_t seed, int pattern_index, std::vector<double> factors)
{
    StressPattern pattern;
    pattern.id = QStringLiteral("PAT%1").arg(pattern_index + 1);
    pattern.uuid = deterministicUuid(seed, 1U, static_cast<std::uint32_t>(pattern_index + 1));
    pattern.factors = std::move(factors);
    specification.patterns.push_back(std::move(pattern));
}

void addJunctions(StressSpecification &specification, const GeneratedStressCase &definition, std::mt19937_64 &generator)
{
    for (int index = 0; index < definition.junction_count; index++)
    {
        StressJunction junction;
        junction.id = QStringLiteral("J%1").arg(index + 1, 3, 10, QChar('0'));
        junction.uuid = deterministicUuid(definition.seed, 2U, static_cast<std::uint32_t>(index + 1));
        const double topology_wave = 5.0 * std::sin(static_cast<double>(index) * 0.43);
        junction.elevation_m = 88.0 + topology_wave + randomReal(generator, -5.0, 15.0);
        junction.demand_m3_per_h = randomReal(generator, 1.2, 5.8);
        junction.pattern_index = index % static_cast<int>(specification.patterns.size());
        specification.junctions.push_back(std::move(junction));
    }
}

void addReservoir(StressSpecification &specification, const GeneratedStressCase &definition, int reservoir_index, double head_m, int pattern_index = -1)
{
    StressReservoir reservoir;
    reservoir.id = QStringLiteral("R%1").arg(reservoir_index + 1);
    reservoir.uuid = deterministicUuid(definition.seed, 3U, static_cast<std::uint32_t>(reservoir_index + 1));
    reservoir.head_m = head_m;
    reservoir.pattern_index = pattern_index;
    specification.reservoirs.push_back(std::move(reservoir));
}

void addPipe(StressSpecification &specification,
    const GeneratedStressCase &definition,
    std::mt19937_64 &generator,
    const QString &from_id,
    const QUuid &from_uuid,
    const QString &to_id,
    const QUuid &to_uuid,
    int pipe_index,
    double length_scale = 1.0,
    double diameter_scale = 1.0)
{
    StressPipe pipe;
    pipe.id = QStringLiteral("P%1").arg(pipe_index + 1, 4, 10, QChar('0'));
    pipe.uuid = deterministicUuid(definition.seed, 4U, static_cast<std::uint32_t>(pipe_index + 1));
    pipe.from_id = from_id;
    pipe.from_uuid = from_uuid;
    pipe.to_id = to_id;
    pipe.to_uuid = to_uuid;
    pipe.length_m = randomReal(generator, 180.0, 920.0) * length_scale;
    pipe.diameter_mm = randomReal(generator, 240.0, 430.0) * diameter_scale;
    pipe.roughness = generatedRoughness(definition.headloss_formula, generator);
    pipe.minor_loss = randomReal(generator, 0.0, 0.75);
    specification.pipes.push_back(std::move(pipe));
}

void addSourceConnection(StressSpecification &specification, const GeneratedStressCase &definition, std::mt19937_64 &generator, int reservoir_index, int junction_index, int &pipe_index)
{
    const StressReservoir &reservoir = specification.reservoirs.at(static_cast<std::size_t>(reservoir_index));
    const StressJunction &junction = specification.junctions.at(static_cast<std::size_t>(junction_index));
    addPipe(specification, definition, generator,
        reservoir.id, reservoir.uuid,
        junction.id, junction.uuid,
        pipe_index++, 0.55, 1.35);
}

void addJunctionConnection(StressSpecification &specification, const GeneratedStressCase &definition, std::mt19937_64 &generator, int from_index, int to_index, int &pipe_index, double length_scale = 1.0, double diameter_scale = 1.0)
{
    const StressJunction &from = specification.junctions.at(static_cast<std::size_t>(from_index));
    const StressJunction &to = specification.junctions.at(static_cast<std::size_t>(to_index));
    addPipe(specification, definition, generator,
        from.id, from.uuid,
        to.id, to.uuid,
        pipe_index++, length_scale, diameter_scale);
}

void buildTopology(StressSpecification &specification, const GeneratedStressCase &definition, std::mt19937_64 &generator)
{
    int pipe_index = 0;
    addSourceConnection(specification, definition, generator, 0, 0, pipe_index);

    switch (definition.topology)
    {
    case GeneratedStressTopology::Chain:
        for (int index = 1; index < definition.junction_count; index++)
            addJunctionConnection(specification, definition, generator, index - 1, index, pipe_index, 0.75, 1.15);
        break;

    case GeneratedStressTopology::Branch:
        for (int index = 1; index < definition.junction_count; index++)
        {
            const int parent_index = (index - 1) / 2;
            addJunctionConnection(specification, definition, generator, parent_index, index, pipe_index, 0.9, 1.08);
        }
        break;

    case GeneratedStressTopology::Ring:
        for (int index = 0; index < definition.junction_count; index++)
            addJunctionConnection(specification, definition, generator, index, (index + 1) % definition.junction_count, pipe_index, 0.72, 1.02);
        for (int index = 0; index < definition.junction_count / 2; index += 3)
            addJunctionConnection(specification, definition, generator, index, index + definition.junction_count / 2, pipe_index, 1.15, 0.95);
        break;

    case GeneratedStressTopology::Grid:
    case GeneratedStressTopology::DualSourceGrid:
        if (definition.grid_rows <= 0 || definition.grid_columns <= 0
            || definition.grid_rows * definition.grid_columns != definition.junction_count)
        {
            throw std::runtime_error("Generated grid stress case has inconsistent dimensions");
        }
        for (int row = 0; row < definition.grid_rows; row++)
        {
            for (int column = 0; column < definition.grid_columns; column++)
            {
                const int index = row * definition.grid_columns + column;
                if (column + 1 < definition.grid_columns)
                    addJunctionConnection(specification, definition, generator, index, index + 1, pipe_index, 0.68, 1.0);
                if (row + 1 < definition.grid_rows)
                    addJunctionConnection(specification, definition, generator, index, index + definition.grid_columns, pipe_index, 0.68, 1.0);
            }
        }
        if (definition.topology == GeneratedStressTopology::DualSourceGrid)
        {
            addSourceConnection(specification, definition, generator, 1, definition.junction_count - 1, pipe_index);
            for (int row = 0; row + 2 < definition.grid_rows; row += 2)
            {
                const int left = row * definition.grid_columns;
                const int right = (row + 2) * definition.grid_columns + definition.grid_columns - 1;
                addJunctionConnection(specification, definition, generator, left, right, pipe_index, 1.4, 0.92);
            }
        }
        break;
    }
}

StressSpecification generateSpecification(const GeneratedStressCase &definition)
{
    if (definition.junction_count <= 0)
        throw std::runtime_error("Generated stress case must contain junctions");

    std::mt19937_64 generator(definition.seed);
    StressSpecification specification;
    specification.title = QStringLiteral("AOWIS deterministic stress %1 seed %2")
                              .arg(QString::fromUtf8(definition.scenario_name))
                              .arg(QString::number(definition.seed));
    specification.headloss_formula = definition.headloss_formula;

    addPattern(specification, definition.seed, 0, {0.72, 0.93, 1.16, 1.31, 1.08, 0.84, 0.67});
    addPattern(specification, definition.seed, 1, {1.18, 1.04, 0.88, 0.76, 0.91, 1.12, 1.26});
    if (definition.topology == GeneratedStressTopology::DualSourceGrid)
        addPattern(specification, definition.seed, 2, {1.000, 1.006, 1.012, 1.004, 0.996, 0.990, 0.998});

    addJunctions(specification, definition, generator);
    addReservoir(specification, definition, 0, 205.0 + randomReal(generator, -3.0, 3.0));
    if (definition.topology == GeneratedStressTopology::DualSourceGrid)
        addReservoir(specification, definition, 1, 201.0 + randomReal(generator, -2.0, 2.0), 2);

    buildTopology(specification, definition, generator);
    return specification;
}

NetworkHydraulic buildModel(const StressSpecification &specification, const GeneratedStressCase &definition)
{
    NetworkHydraulic network;
    network.id = QString::fromUtf8(definition.scenario_name);
    network.uuid = deterministicUuid(definition.seed, 9U, 1U);
    network.title_line_1 = specification.title;
    network.duration_s = 21600;
    network.timestep_hydraulic_s = 3600;
    network.timestep_quality_s = 300;
    network.timestep_pattern_s = 3600;
    network.start_pattern_s = 0;
    network.timestep_report_s = 3600;
    network.start_report_s = 0;
    network.timestep_rule_s = 360;
    network.start_time_of_day_s = 0;
    network.report_statistic = HydraulicSimulationReportStatistic::Series;

    network.options_hydraulic.headloss_formula = specification.headloss_formula;
    network.options_hydraulic.demand_model = HydraulicDemandModel::DemandDriven;
    network.options_hydraulic.maximum_trials = 80;
    network.options_hydraulic.accuracy = 0.001;
    network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
    network.options_hydraulic.unbalanced_extra_trials = 10;
    network.options_hydraulic.check_frequency = 2;
    network.options_hydraulic.maximum_check = 10;
    network.options_hydraulic.damping_limit = 0.0;
    network.options_hydraulic.maximum_head_error_m = 0.0;
    network.options_hydraulic.maximum_flow_change_m3_per_h = 0.0;
    network.options_hydraulic.demand_multiplier = 1.0;
    network.options_hydraulic.emitter_exponent = 0.5;
    network.options_hydraulic.specific_gravity = 1.0;
    network.options_hydraulic.relative_viscosity = 1.0;

    for (const StressPattern &source : specification.patterns)
    {
        HydraulicPatternTime pattern;
        pattern.id = source.id;
        pattern.uuid = source.uuid;
        for (double factor : source.factors)
            pattern.factors.append(factor);
        network.patterns_time.append(pattern);
    }

    for (const StressJunction &source : specification.junctions)
    {
        HydraulicNodeJunction junction;
        junction.id = source.id;
        junction.uuid = source.uuid;
        junction.elevation_m = source.elevation_m;

        HydraulicNodeJunctionDemand demand;
        demand.category_name = QStringLiteral("Stress demand");
        demand.base_demand_m3_per_h = source.demand_m3_per_h;
        demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
        demand.pattern_uuid = specification.patterns.at(static_cast<std::size_t>(source.pattern_index)).uuid;
        junction.demands.append(demand);
        network.nodes_junctions.append(junction);
    }

    for (const StressReservoir &source : specification.reservoirs)
    {
        HydraulicNodeReservoir reservoir;
        reservoir.id = source.id;
        reservoir.uuid = source.uuid;
        reservoir.head_m = source.head_m;
        if (source.pattern_index >= 0)
        {
            reservoir.head_pattern_mode = HydraulicTimePatternMode::TimePattern;
            reservoir.head_pattern_uuid = specification.patterns.at(static_cast<std::size_t>(source.pattern_index)).uuid;
        }
        else
        {
            reservoir.head_pattern_mode = HydraulicTimePatternMode::Constant;
        }
        network.nodes_reservoirs.append(reservoir);
    }

    for (const StressPipe &source : specification.pipes)
    {
        HydraulicLinkPipe pipe;
        pipe.id = source.id;
        pipe.uuid = source.uuid;
        pipe.node_uuid_from = source.from_uuid;
        pipe.node_uuid_to = source.to_uuid;
        pipe.length_calculated_m = source.length_m;
        pipe.diameter_mm = source.diameter_mm;
        if (specification.headloss_formula == HydraulicHeadlossFormula::HazenWilliams)
            pipe.roughness_hw = source.roughness;
        else if (specification.headloss_formula == HydraulicHeadlossFormula::DarcyWeisbach)
            pipe.roughness_dw_mm = source.roughness;
        else
            pipe.roughness_cm = source.roughness;
        pipe.minor_loss = source.minor_loss;
        pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
        network.links_pipes.append(pipe);
    }

    return network;
}

QString buildNativeInp(const StressSpecification &specification)
{
    QString text;
    text += QStringLiteral("[TITLE]\n");
    text += specification.title + QLatin1Char('\n');

    text += QStringLiteral("\n[JUNCTIONS]\n;ID Elev Demand Pattern\n");
    for (const StressJunction &junction : specification.junctions)
    {
        const StressPattern &pattern = specification.patterns.at(static_cast<std::size_t>(junction.pattern_index));
        text += junction.id + QLatin1Char(' ') + number(junction.elevation_m) + QLatin1Char(' ')
            + number(junction.demand_m3_per_h) + QLatin1Char(' ') + pattern.id + QLatin1Char('\n');
    }

    text += QStringLiteral("\n[RESERVOIRS]\n;ID Head Pattern\n");
    for (const StressReservoir &reservoir : specification.reservoirs)
    {
        text += reservoir.id + QLatin1Char(' ') + number(reservoir.head_m);
        if (reservoir.pattern_index >= 0)
            text += QLatin1Char(' ') + specification.patterns.at(static_cast<std::size_t>(reservoir.pattern_index)).id;
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("\n[PIPES]\n;ID Node1 Node2 Length Diameter Roughness MinorLoss Status\n");
    for (const StressPipe &pipe : specification.pipes)
    {
        text += pipe.id + QLatin1Char(' ') + pipe.from_id + QLatin1Char(' ') + pipe.to_id + QLatin1Char(' ')
            + number(pipe.length_m) + QLatin1Char(' ') + number(pipe.diameter_mm) + QLatin1Char(' ')
            + number(pipe.roughness) + QLatin1Char(' ') + number(pipe.minor_loss) + QStringLiteral(" OPEN\n");
    }

    text += QStringLiteral("\n[PATTERNS]\n");
    for (const StressPattern &pattern : specification.patterns)
    {
        for (double factor : pattern.factors)
            text += pattern.id + QLatin1Char(' ') + number(factor) + QLatin1Char('\n');
    }

    text += QStringLiteral("\n[TIMES]\n");
    text += QStringLiteral(" DURATION 6:00\n");
    text += QStringLiteral(" HYDRAULIC TIMESTEP 1:00\n");
    text += QStringLiteral(" QUALITY TIMESTEP 0:05\n");
    text += QStringLiteral(" PATTERN TIMESTEP 1:00\n");
    text += QStringLiteral(" PATTERN START 0:00\n");
    text += QStringLiteral(" REPORT TIMESTEP 1:00\n");
    text += QStringLiteral(" REPORT START 0:00\n");
    text += QStringLiteral(" RULE TIMESTEP 0:06\n");
    text += QStringLiteral(" START CLOCKTIME 12 AM\n");
    text += QStringLiteral(" STATISTIC NONE\n");

    text += QStringLiteral("\n[OPTIONS]\n");
    text += QStringLiteral(" UNITS CMH\n");
    text += QStringLiteral(" PRESSURE METERS\n");
    text += QStringLiteral(" HEADLOSS ") + headlossToken(specification.headloss_formula) + QLatin1Char('\n');
    text += QStringLiteral(" SPECIFIC GRAVITY 1\n");
    text += QStringLiteral(" VISCOSITY 1\n");
    text += QStringLiteral(" TRIALS 80\n");
    text += QStringLiteral(" ACCURACY 0.001\n");
    text += QStringLiteral(" CHECKFREQ 2\n");
    text += QStringLiteral(" MAXCHECK 10\n");
    text += QStringLiteral(" DAMPLIMIT 0\n");
    text += QStringLiteral(" UNBALANCED CONTINUE 10\n");
    text += QStringLiteral(" DEMAND MULTIPLIER 1\n");
    text += QStringLiteral(" EMITTER EXPONENT 0.5\n");
    text += QStringLiteral(" BACKFLOW ALLOWED NO\n");
    text += QStringLiteral(" DEMAND MODEL DDA\n");
    text += QStringLiteral(" QUALITY NONE\n");

    text += QStringLiteral("\n[END]\n");
    return text;
}
}

GeneratedStressFixture makeGeneratedStressFixture(const GeneratedStressCase &definition)
{
    const StressSpecification specification = generateSpecification(definition);
    GeneratedStressFixture fixture;
    fixture.network = buildModel(specification, definition);
    fixture.native_inp_text = buildNativeInp(specification);
    fixture.expected_junction_count = static_cast<int>(specification.junctions.size());
    fixture.expected_reservoir_count = static_cast<int>(specification.reservoirs.size());
    fixture.expected_pipe_count = static_cast<int>(specification.pipes.size());
    return fixture;
}
}
