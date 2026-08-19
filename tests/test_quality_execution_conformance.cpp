#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/net1_fixture.h"
#include "conformance/quality_execution_scenarios.h"

#include "../src/lib/internal/epanet_index_registry.h"
#include "../src/lib/internal/epanet_network_builder.h"
#include "../src/lib/internal/epanet_network_preparer.h"
#include "../src/lib/internal/epanet_project.h"
#include "../src/lib/internal/epanet_report_collector.h"

#include <QByteArray>
#include <QHash>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr NumericTolerance kQualityTolerance{1.0e-8, 1.0e-7};
constexpr NumericTolerance kMassBalanceTolerance{1.0e-8, 1.0e-7};
constexpr NumericTolerance kSourceMassTolerance{1.0e-8, 1.0e-7};

ComparisonContext comparison(std::int64_t time_s, std::string field, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

void checkEpanet(int error, const char *operation)
{
    if (error == 0)
        return;

    std::array<char, EN_MAXMSG + 1> message{};
    EN_geterror(error, message.data(), EN_MAXMSG);
    throw std::runtime_error(std::string(operation) + " failed with EPANET code " + std::to_string(error) + ": " + message.data());
}

HydraulicLinkValve replacePipeWithValve(NetworkHydraulic &network, const QString &pipe_id)
{
    HydraulicLinkValve valve;
    for (int index = 0; index < network.links_pipes.size(); index++)
    {
        const HydraulicLinkPipe &pipe = network.links_pipes.at(index);
        if (pipe.id != pipe_id)
            continue;

        valve.id = QStringLiteral("V") + pipe.id;
        valve.uuid = QUuid::createUuid();
        valve.node_uuid_from = pipe.node_uuid_from;
        valve.node_uuid_to = pipe.node_uuid_to;
        network.links_pipes.removeAt(index);
        break;
    }

    if (valve.uuid.isNull())
        throw std::runtime_error("quality execution fixture could not find pipe to replace with valve");

    valve.type = HydraulicLinkValveType::TCV;
    valve.diameter_mm = 254.0;
    valve.minor_loss_coefficient = 0.0;
    valve.setting_loss_coefficient = 1.0;
    valve.initial_status = HydraulicLinkValveInitialStatus::Open;
    network.links_valves.append(valve);
    return valve;
}

NetworkHydraulic qualityNetwork(WaterQualityAnalysisType analysis)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    NetworkHydraulic network = fixture.network;
    network.controls_simple.clear();
    network.controls_rules.clear();
    network.duration_s = 1800;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;
    network.timestep_report_s = 1800;
    network.options_quality.analysis = analysis;
    replacePipeWithValve(network, QStringLiteral("111"));

    if (analysis == WaterQualityAnalysisType::Chemical)
    {
        network.options_quality.chemical_name = QStringLiteral("Chlorine");
        network.nodes_reservoirs.first().initial_chemical_concentration_mg_per_l = 1.0;
        network.nodes_reservoirs.first().quality_source.type = HydraulicNodeQualitySourceType::Concentration;
        network.nodes_reservoirs.first().quality_source.chemical_concentration_mg_per_l = 1.0;

        network.nodes_junctions.first().quality_source.type = HydraulicNodeQualitySourceType::MassBooster;
        network.nodes_junctions.first().quality_source.chemical_mass_flow_mg_per_min = 6.0;

        network.nodes_tanks.first().quality_source.type = HydraulicNodeQualitySourceType::SetpointBooster;
        network.nodes_tanks.first().quality_source.chemical_concentration_mg_per_l = 0.5;
    }
    else if (analysis == WaterQualityAnalysisType::SourceTrace)
    {
        network.options_quality.trace_node_uuid = network.nodes_reservoirs.first().uuid;
    }

    return network;
}

void clearChemicalInputs(NetworkHydraulic &network)
{
    for (HydraulicNodeJunction &node : network.nodes_junctions)
    {
        node.initial_chemical_concentration_mg_per_l = 0.0;
        node.quality_source = HydraulicNodeQualitySource();
    }
    for (HydraulicNodeReservoir &node : network.nodes_reservoirs)
    {
        node.initial_chemical_concentration_mg_per_l = 0.0;
        node.quality_source = HydraulicNodeQualitySource();
    }
    for (HydraulicNodeTank &node : network.nodes_tanks)
    {
        node.initial_chemical_concentration_mg_per_l = 0.0;
        node.quality_source = HydraulicNodeQualitySource();
    }
}

NetworkHydraulic sourceTypeNetwork(HydraulicNodeQualitySourceType source_type)
{
    NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::Chemical);
    clearChemicalInputs(network);
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;

    if (source_type == HydraulicNodeQualitySourceType::Concentration)
    {
        HydraulicNodeReservoir &reservoir = network.nodes_reservoirs.first();
        reservoir.quality_source.type = source_type;
        reservoir.quality_source.chemical_concentration_mg_per_l = 1.25;
    }
    else
    {
        HydraulicNodeJunction &junction = network.nodes_junctions.first();
        junction.quality_source.type = source_type;
        if (source_type == HydraulicNodeQualitySourceType::MassBooster)
            junction.quality_source.chemical_mass_flow_mg_per_min = 12.0;
        else
            junction.quality_source.chemical_concentration_mg_per_l = 0.75;
    }

    return network;
}

NetworkHydraulic patternedSourceNetwork()
{
    NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::Concentration);
    network.duration_s = 2400;
    network.timestep_pattern_s = 600;
    network.timestep_quality_s = 300;

    HydraulicPatternTime pattern;
    pattern.id = QStringLiteral("QualityPattern");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers = {0.25, 1.0, 1.75, 0.5};
    network.patterns_time.append(pattern);
    network.nodes_reservoirs.first().quality_source.pattern_uuid = pattern.uuid;
    return network;
}

NetworkHydraulic tankMixingNetwork(HydraulicNodeTankMixingModel mixing_model)
{
    NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::Concentration);
    network.duration_s = 6 * 3600;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;
    HydraulicNodeTank &tank = network.nodes_tanks.first();
    tank.initial_chemical_concentration_mg_per_l = 0.15;
    tank.mixing_model = mixing_model;
    tank.mixing_fraction = mixing_model == HydraulicNodeTankMixingModel::TwoCompartment ? 0.35 : 1.0;
    return network;
}

NetworkHydraulic reactionNetwork(HydraulicHeadlossFormula formula)
{
    NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::Concentration);
    network.duration_s = 4 * 3600;
    network.options_hydraulic.headloss_formula = formula;
    network.options_reaction.global_pipe_bulk_reaction.coefficient = -0.35;
    network.options_reaction.global_pipe_bulk_reaction.order = 1.0;
    network.options_reaction.global_pipe_wall_reaction.coefficient = -0.08;
    network.options_reaction.global_pipe_wall_reaction.order = 1.0;
    network.options_reaction.global_tank_bulk_reaction.coefficient = -0.20;
    network.options_reaction.global_tank_bulk_reaction.order = 1.0;
    network.options_reaction.limiting_concentration_mg_per_l = 0.10;
    network.options_reaction.roughness_reaction_factor = -2.4;

    if (!network.links_pipes.isEmpty())
    {
        HydraulicLinkPipe &override_pipe = network.links_pipes.first();
        override_pipe.override_reactions = true;
        override_pipe.bulk_reaction.coefficient = -0.55;
        override_pipe.bulk_reaction.order = 1.0;
        override_pipe.wall_reaction.coefficient = -0.22;
        override_pipe.wall_reaction.order = 1.0;
    }

    if (!network.nodes_tanks.isEmpty())
    {
        HydraulicNodeTank &tank = network.nodes_tanks.first();
        tank.override_bulk_reaction = true;
        tank.bulk_reaction.coefficient = -0.45;
        tank.bulk_reaction.order = 1.0;
    }

    return network;
}

NetworkHydraulic qualityCancellationNetwork()
{
    NetworkHydraulic network;
    network.id = QStringLiteral("quality-cancellation");
    network.uuid = QUuid::createUuid();
    network.duration_s = 1800;
    network.timestep_hydraulic_s = 1800;
    network.timestep_quality_s = 300;
    network.timestep_report_s = 1800;
    network.options_quality.analysis = WaterQualityAnalysisType::Chemical;
    network.options_quality.chemical_name = QStringLiteral("Chlorine");

    HydraulicNodeReservoir reservoir;
    reservoir.id = QStringLiteral("R1");
    reservoir.uuid = QUuid::createUuid();
    reservoir.hydraulic_head_m = 100.0;
    reservoir.initial_chemical_concentration_mg_per_l = 1.0;
    reservoir.quality_source.type = HydraulicNodeQualitySourceType::Concentration;
    reservoir.quality_source.chemical_concentration_mg_per_l = 1.0;
    network.nodes_reservoirs.append(reservoir);

    HydraulicNodeJunction junction;
    junction.id = QStringLiteral("J1");
    junction.uuid = QUuid::createUuid();
    junction.elevation_m = 10.0;
    HydraulicNodeJunctionDemand demand;
    demand.base_demand_m3_per_h = 10.0;
    junction.demands.append(demand);
    network.nodes_junctions.append(junction);

    HydraulicLinkPipe pipe;
    pipe.id = QStringLiteral("P1");
    pipe.uuid = QUuid::createUuid();
    pipe.node_uuid_from = reservoir.uuid;
    pipe.node_uuid_to = junction.uuid;
    pipe.length_calculated_m = 1000.0;
    pipe.diameter_mm = 200.0;
    pipe.roughness_hazen_williams = 120.0;
    pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
    network.links_pipes.append(pipe);

    return network;
}

bool nodeHasSource(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &node : network.nodes_junctions)
    {
        if (node.id == id)
            return node.quality_source.type != HydraulicNodeQualitySourceType::None;
    }
    for (const HydraulicNodeReservoir &node : network.nodes_reservoirs)
    {
        if (node.id == id)
            return node.quality_source.type != HydraulicNodeQualitySourceType::None;
    }
    for (const HydraulicNodeTank &node : network.nodes_tanks)
    {
        if (node.id == id)
            return node.quality_source.type != HydraulicNodeQualitySourceType::None;
    }
    return false;
}

struct NativeQualityStep
{
    std::int64_t time_s = 0;
    QHash<QString, double> node_quality;
    QHash<QString, double> node_source_mass_mg_per_min;
    QHash<QString, double> link_quality;
    double mass_balance_ratio = 0.0;
};

class NativeQualityRun
{
public:
    explicit NativeQualityRun(const NetworkHydraulic &network)
    {
        // Q1 separately proves Model-to-EPANET input mapping. Build the runtime
        // reference in memory so EN_saveinpfile rounding cannot contaminate
        // the native-vs-AOWIS quality execution comparison.
        NetworkHydraulic prepared_network;
        QList<HydraulicSimulationStatus> validation_failures;
        HydraulicSimulationStatus status = prepareEpanetNetwork(network, prepared_network, &validation_failures);
        checkStatus(status, "prepareEpanetNetwork(native quality)");

        status = this->project_.create();
        checkStatus(status, "EpanetProject::create(native quality)");
        status = this->project_.initialize(prepared_network, this->report_collector_);
        checkStatus(status, "EpanetProject::initialize(native quality)");

        EpanetIndexRegistry indices;
        EpanetNetworkBuilder builder(this->project_, indices);
        status = builder.build(prepared_network);
        checkStatus(status, "EpanetNetworkBuilder::build(native quality)");

        checkEpanet(EN_solveH(this->project_.handle()), "EN_solveH(native quality)");
        checkEpanet(EN_openQ(this->project_.handle()), "EN_openQ(native quality)");
        this->quality_open_ = true;
        checkEpanet(EN_initQ(this->project_.handle(), EN_NOSAVE), "EN_initQ(native quality)");

        long time_left_s = 0;
        do
        {
            long current_time_s = 0;
            checkEpanet(EN_runQ(this->project_.handle(), &current_time_s), "EN_runQ(native quality)");

            NativeQualityStep step;
            step.time_s = current_time_s;
            collectNodeValues(network, step);
            collectLinkValues(network, step);
            checkEpanet(EN_getstatistic(this->project_.handle(), EN_MASSBALANCE, &step.mass_balance_ratio), "EN_getstatistic(EN_MASSBALANCE native quality)");
            this->steps_.append(step);

            checkEpanet(EN_stepQ(this->project_.handle(), &time_left_s), "EN_stepQ(native quality)");
        } while (time_left_s > 0);

        checkEpanet(EN_closeQ(this->project_.handle()), "EN_closeQ(native quality)");
        this->quality_open_ = false;
    }

    ~NativeQualityRun()
    {
        if (this->quality_open_)
            EN_closeQ(this->project_.handle());
    }

    NativeQualityRun(const NativeQualityRun &) = delete;
    NativeQualityRun &operator=(const NativeQualityRun &) = delete;

    const QList<NativeQualityStep> &steps() const
    {
        return this->steps_;
    }

private:
    static void checkStatus(const HydraulicSimulationStatus &status, const char *operation)
    {
        if (status.success)
            return;

        throw std::runtime_error(std::string(operation) + " failed: " + status.message.toStdString());
    }

    int nodeIndex(const QString &id) const
    {
        const QByteArray id_utf8 = id.toUtf8();
        int index = 0;
        checkEpanet(EN_getnodeindex(this->project_.handle(), id_utf8.constData(), &index), "EN_getnodeindex(native quality)");
        return index;
    }

    int linkIndex(const QString &id) const
    {
        const QByteArray id_utf8 = id.toUtf8();
        int index = 0;
        checkEpanet(EN_getlinkindex(this->project_.handle(), id_utf8.constData(), &index), "EN_getlinkindex(native quality)");
        return index;
    }

    void collectNodeValues(const NetworkHydraulic &network, NativeQualityStep &step) const
    {
        const std::function<void(const QString &)> collect = [this, &network, &step](const QString &id)
        {
            const int index = nodeIndex(id);
            double value = 0.0;
            checkEpanet(EN_getnodevalue(this->project_.handle(), index, EN_QUALITY, &value), "EN_getnodevalue(EN_QUALITY native quality)");
            step.node_quality.insert(id, value);

            if (nodeHasSource(network, id))
            {
                checkEpanet(EN_getnodevalue(this->project_.handle(), index, EN_SOURCEMASS, &value), "EN_getnodevalue(EN_SOURCEMASS native quality)");
                step.node_source_mass_mg_per_min.insert(id, value);
            }
            else
            {
                step.node_source_mass_mg_per_min.insert(id, 0.0);
            }
        };

        for (const HydraulicNodeJunction &node : network.nodes_junctions)
            collect(node.id);
        for (const HydraulicNodeReservoir &node : network.nodes_reservoirs)
            collect(node.id);
        for (const HydraulicNodeTank &node : network.nodes_tanks)
            collect(node.id);
    }

    void collectLinkValues(const NetworkHydraulic &network, NativeQualityStep &step) const
    {
        const std::function<void(const QString &)> collect = [this, &step](const QString &id)
        {
            const int index = linkIndex(id);
            double value = 0.0;
            checkEpanet(EN_getlinkvalue(this->project_.handle(), index, EN_LINKQUAL, &value), "EN_getlinkvalue(EN_LINKQUAL native quality)");
            step.link_quality.insert(id, value);
        };

        for (const HydraulicLinkPipe &link : network.links_pipes)
            collect(link.id);
        for (const HydraulicLinkPump &link : network.links_pumps)
            collect(link.id);
        for (const HydraulicLinkValve &link : network.links_valves)
            collect(link.id);
    }

    EpanetReportCollector report_collector_;
    EpanetProject project_;
    bool quality_open_ = false;
    QList<NativeQualityStep> steps_;
};

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
void compareNodeResults(TestContext &context, const QList<ResultType> &actual, const NativeQualityStep &expected, WaterQualityAnalysisType analysis, const char *entity_type)
{
    for (const ResultType &result : actual)
    {
        context.expect(expected.node_quality.contains(result.id), "native quality reference is missing an AOWIS node ID");
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
void compareLinkResults(TestContext &context, const QList<ResultType> &actual, const NativeQualityStep &expected, WaterQualityAnalysisType analysis, const char *entity_type)
{
    for (const ResultType &result : actual)
    {
        context.expect(expected.link_quality.contains(result.id), "native quality reference is missing an AOWIS link ID");
        if (!expected.link_quality.contains(result.id))
            continue;

        context.expectNear(
            qualityValue(result, analysis),
            expected.link_quality.value(result.id),
            kQualityTolerance,
            comparison(expected.time_s, "quality", entity_type, result.id.toStdString()));
    }
}

void compareQualityTimeline(TestContext &context, const NetworkHydraulic &network, const EpanetResultRun &actual_run)
{
    NativeQualityRun native(network);
    const QList<NativeQualityStep> &expected_steps = native.steps();
    const WaterQualitySimulationResultTimeline &actual = actual_run.quality_result_timeline;

    context.expect(actual.status.success, "AOWIS quality execution should finish successfully");
    context.expect(actual.validity == WaterQualitySimulationResultValidity::Valid, "successful quality execution should produce a valid quality timeline");
    context.expectEqual(static_cast<std::int64_t>(actual.results.size()), static_cast<std::int64_t>(expected_steps.size()), comparison(-1, "quality_timeline.size"));

    const int step_count = qMin(actual.results.size(), expected_steps.size());
    for (int index = 0; index < step_count; index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(index);
        const NativeQualityStep &expected_step = expected_steps.at(index);
        context.expect(actual_step.status.success, "every returned quality timestep must carry a successful per-step status");
        context.expectEqual(static_cast<std::int64_t>(actual_step.time_elapsed_s), expected_step.time_s, comparison(expected_step.time_s, "time_elapsed_s"));

        compareNodeResults(context, actual_step.nodes_junctions, expected_step, actual.analysis, "Junction");
        compareNodeResults(context, actual_step.nodes_reservoirs, expected_step, actual.analysis, "Reservoir");
        compareNodeResults(context, actual_step.nodes_tanks, expected_step, actual.analysis, "Tank");
        compareLinkResults(context, actual_step.links_pipes, expected_step, actual.analysis, "Pipe");
        compareLinkResults(context, actual_step.links_pumps, expected_step, actual.analysis, "Pump");
        compareLinkResults(context, actual_step.links_valves, expected_step, actual.analysis, "Valve");
        context.expectNear(actual_step.statistics.mass_balance_ratio, expected_step.mass_balance_ratio, kMassBalanceTolerance, comparison(expected_step.time_s, "mass_balance_ratio", "QualitySolver"));
    }
}

void scenarioQualityExecutionNone(TestContext &context)
{
    NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::None);
    const EpanetResultRun run = EpanetRunner().run(network);

    context.expect(run.result_timeline.status.success, "quality-disabled run must retain successful hydraulics");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "quality-disabled run must retain valid hydraulics");
    context.expect(run.quality_result_timeline.status.success, "quality-disabled timeline should have successful not-run status");
    context.expect(run.quality_result_timeline.validity == WaterQualitySimulationResultValidity::NotRun, "quality-disabled timeline must remain NotRun");
    context.expect(run.quality_result_timeline.results.isEmpty(), "quality-disabled timeline must contain no quality results");
}

void scenarioQualityExecutionChemical(TestContext &context)
{
    const NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::Chemical);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityExecutionWaterAge(TestContext &context)
{
    const NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::WaterAge);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityExecutionSourceTrace(TestContext &context)
{
    const NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::SourceTrace);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityExecutionIndependentTimeline(TestContext &context)
{
    const NetworkHydraulic network = qualityNetwork(WaterQualityAnalysisType::Chemical);
    const EpanetResultRun run = EpanetRunner().run(network);

    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "independent-timeline fixture requires valid hydraulics");
    context.expect(run.quality_result_timeline.validity == WaterQualitySimulationResultValidity::Valid, "independent-timeline fixture requires valid quality results");
    context.expect(run.quality_result_timeline.results.size() > run.result_timeline.results.size(), "quality timestep must produce more samples than the coarser hydraulic timestep");

    for (int index = 0; index < run.quality_result_timeline.results.size(); index++)
    {
        const std::int64_t expected_time_s = static_cast<std::int64_t>(index) * static_cast<std::int64_t>(network.timestep_quality_s);
        context.expectEqual(
            static_cast<std::int64_t>(run.quality_result_timeline.results.at(index).time_elapsed_s),
            expected_time_s,
            comparison(expected_time_s, "quality_timestep"));
    }
}

void scenarioQualityRuntimeSourceConcentration(TestContext &context)
{
    const NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::Concentration);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityRuntimeSourceMassBooster(TestContext &context)
{
    const NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::MassBooster);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityRuntimeSourceFlowPaced(TestContext &context)
{
    const NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::FlowPacedBooster);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityRuntimeSourceSetpoint(TestContext &context)
{
    const NetworkHydraulic network = sourceTypeNetwork(HydraulicNodeQualitySourceType::SetpointBooster);
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
}

void scenarioQualityRuntimeSourcePattern(TestContext &context)
{
    const NetworkHydraulic network = patternedSourceNetwork();
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);
    context.expect(run.quality_result_timeline.results.size() == 8, "2400-second patterned source run at 300-second quality steps must return eight EN_runQ samples from time zero through 2100 seconds");
}

void scenarioQualityRuntimeTankMixingModels(TestContext &context)
{
    const std::array<HydraulicNodeTankMixingModel, 4> models = {{
        HydraulicNodeTankMixingModel::CompleteMix,
        HydraulicNodeTankMixingModel::TwoCompartment,
        HydraulicNodeTankMixingModel::FirstInFirstOut,
        HydraulicNodeTankMixingModel::LastInFirstOut
    }};

    for (const HydraulicNodeTankMixingModel model : models)
    {
        const NetworkHydraulic network = tankMixingNetwork(model);
        const EpanetResultRun run = EpanetRunner().run(network);
        compareQualityTimeline(context, network, run);
    }
}

void scenarioQualityRuntimeReactions(TestContext &context)
{
    const std::array<HydraulicHeadlossFormula, 3> formulas = {{
        HydraulicHeadlossFormula::HazenWilliams,
        HydraulicHeadlossFormula::DarcyWeisbach,
        HydraulicHeadlossFormula::ChezyManning
    }};

    for (const HydraulicHeadlossFormula formula : formulas)
    {
        const NetworkHydraulic network = reactionNetwork(formula);
        const EpanetResultRun run = EpanetRunner().run(network);
        compareQualityTimeline(context, network, run);
    }
}

void scenarioQualityRuntimeLongMultistepContract(TestContext &context)
{
    NetworkHydraulic network = patternedSourceNetwork();
    network.duration_s = 12 * 3600;
    network.timestep_hydraulic_s = 3600;
    network.timestep_quality_s = 300;
    const EpanetResultRun run = EpanetRunner().run(network);
    compareQualityTimeline(context, network, run);

    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "long quality run must preserve valid hydraulic results");
    context.expect(run.quality_result_timeline.validity == WaterQualitySimulationResultValidity::Valid, "long quality run must produce a valid quality timeline");
    context.expect(run.quality_result_timeline.results.size() == 144, "12-hour run at 300-second quality steps must return 144 EN_runQ samples from time zero through 42900 seconds");
    for (int index = 0; index < run.quality_result_timeline.results.size(); index++)
    {
        const WaterQualitySimulationResult &step = run.quality_result_timeline.results.at(index);
        context.expect(step.status.success, "every successfully returned quality timestep must carry a successful per-step status");
        context.expect(std::isfinite(step.statistics.mass_balance_ratio), "quality mass-balance ratio must be finite at every returned timestep");
        if (index == 0)
        {
            context.expectNear(step.statistics.mass_balance_ratio, 0.0, NumericTolerance{1.0e-12, 0.0}, comparison(0, "mass_balance_ratio.initial", "QualitySolver"));
        }
        else
        {
            context.expect(step.statistics.mass_balance_ratio > 0.0, "quality mass-balance ratio must be positive after the first EN_stepQ update");
        }
    }
}

void scenarioQualityExecutionCancellationPartial(TestContext &context)
{
    const NetworkHydraulic network = qualityCancellationNetwork();
    int cancellation_checks = 0;
    const EpanetResultRun run = EpanetRunner().run(network, [&cancellation_checks]()
    {
        cancellation_checks++;
        return cancellation_checks >= 20;
    });

    context.expect(run.cancelled, "quality-step cancellation must be reported");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "quality cancellation must not downgrade completed hydraulic results");
    context.expect(!run.quality_result_timeline.results.isEmpty(), "quality cancellation must preserve already produced quality results");
    context.expect(run.quality_result_timeline.results.size() < 7, "quality cancellation fixture must stop before the full seven-step quality timeline is produced");
    context.expect(run.quality_result_timeline.validity == WaterQualitySimulationResultValidity::Partial, "quality cancellation after numerical results must classify quality as partial");
}
}

namespace AowisEpanetTests
{
void registerQualityExecutionScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-none",
        "Skips the EPANET quality lifecycle when water-quality analysis is disabled.",
        {"conformance", "quality", "execution"},
        &scenarioQualityExecutionNone});
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-chemical",
        "Matches native EPANET chemical-quality results for every supported node/link result family.",
        {"conformance", "quality", "execution", "differential"},
        &scenarioQualityExecutionChemical});
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-water-age",
        "Matches native EPANET water-age results at every quality timestep.",
        {"conformance", "quality", "execution", "differential"},
        &scenarioQualityExecutionWaterAge});
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-source-trace",
        "Matches native EPANET source-trace results at every quality timestep.",
        {"conformance", "quality", "execution", "differential"},
        &scenarioQualityExecutionSourceTrace});
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-independent-timeline",
        "Proves quality results retain their finer timestep independently of hydraulic events.",
        {"conformance", "quality", "execution", "timeline"},
        &scenarioQualityExecutionIndependentTimeline});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-source-concentration",
        "Matches native EPANET runtime results for a concentration source.",
        {"conformance", "quality", "runtime", "source", "differential"},
        &scenarioQualityRuntimeSourceConcentration});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-source-mass-booster",
        "Matches native EPANET runtime results for a mass-booster source.",
        {"conformance", "quality", "runtime", "source", "differential"},
        &scenarioQualityRuntimeSourceMassBooster});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-source-flow-paced",
        "Matches native EPANET runtime results for a flow-paced booster source.",
        {"conformance", "quality", "runtime", "source", "differential"},
        &scenarioQualityRuntimeSourceFlowPaced});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-source-setpoint",
        "Matches native EPANET runtime results for a setpoint-booster source.",
        {"conformance", "quality", "runtime", "source", "differential"},
        &scenarioQualityRuntimeSourceSetpoint});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-source-pattern",
        "Matches native EPANET runtime dosing across changing source-pattern multipliers.",
        {"conformance", "quality", "runtime", "source", "pattern", "differential"},
        &scenarioQualityRuntimeSourcePattern});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-tank-mixing-models",
        "Matches native EPANET runtime behavior for all four tank mixing models.",
        {"conformance", "quality", "runtime", "tank", "mixing", "differential"},
        &scenarioQualityRuntimeTankMixingModels});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-reactions",
        "Matches native EPANET bulk, wall, tank, limiting-concentration, override, and roughness-correlated reaction execution.",
        {"conformance", "quality", "runtime", "reaction", "differential"},
        &scenarioQualityRuntimeReactions});
    registry.add(ScenarioDefinition{
        "conformance-quality-runtime-long-multistep-contract",
        "Proves long quality stepping, per-step status, mass-balance finiteness, and hydraulic-result isolation.",
        {"conformance", "quality", "runtime", "timeline", "contract", "differential"},
        &scenarioQualityRuntimeLongMultistepContract});
    registry.add(ScenarioDefinition{
        "conformance-quality-execution-cancellation-partial",
        "Preserves completed hydraulics and partial quality results when cancellation occurs during quality stepping.",
        {"conformance", "quality", "execution", "cancellation"},
        &scenarioQualityExecutionCancellationPartial});
}
}
