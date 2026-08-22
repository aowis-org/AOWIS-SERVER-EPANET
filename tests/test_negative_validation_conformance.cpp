#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/net1_fixture.h"
#include "conformance/negative_validation_scenarios.h"

#include <QString>
#include <QStringList>
#include <QUuid>

#include <limits>

namespace
{
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

NetworkHydraulic cleanNet1()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

HydraulicLinkPipe *findPipe(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

HydraulicLinkPump *firstPump(NetworkHydraulic &network)
{
    if (network.links_pumps.isEmpty())
        return nullptr;
    return &network.links_pumps.first();
}

bool inpSectionContainsId(const QString &inp_text, const QString &section_name, const QString &id)
{
    bool in_section = false;
    const QStringList lines = inp_text.split(QChar('\n'));
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('[')))
        {
            in_section = trimmed.compare(section_name, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_section || trimmed.isEmpty() || trimmed.startsWith(QChar(';')))
            continue;

        const QString first_token = trimmed.simplified().section(QChar(' '), 0, 0);
        if (first_token == id)
            return true;
    }
    return false;
}

void expectRejected(
    TestContext &context,
    const NetworkHydraulic &network,
    HydraulicSimulationStatusEntityType expected_entity_type,
    const QString &expected_entity_id,
    const QUuid &expected_entity_uuid,
    const QString &message_fragment,
    HydraulicSimulationStatusStage expected_stage = HydraulicSimulationStatusStage::BuildNetwork)
{
    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));

    context.expect(!run.result_timeline.status.success, "invalid network must return a failing structured status");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid, "pre-simulation validation failure must produce invalid results");
    context.expect(run.result_timeline.results.isEmpty(), "pre-simulation validation failure must not return hydraulic timesteps");
    context.expect(run.result_timeline.status.stage == expected_stage, "negative validation status must identify the expected stage");
    context.expect(run.result_timeline.status.entity.type == expected_entity_type, "negative validation status must identify the failing entity type");
    context.expect(run.result_timeline.status.entity.id == expected_entity_id, "negative validation status must identify the failing entity ID");
    context.expect(run.result_timeline.status.entity.uuid == expected_entity_uuid, "negative validation status must identify the failing entity UUID");
    context.expect(run.result_timeline.status.message.contains(message_fragment, Qt::CaseInsensitive), "negative validation status must contain an actionable reason");
    context.expect(!run.result_timeline.diagnostics.isEmpty(), "negative validation failure must be retained as a structured diagnostic");

    if (run.result_timeline.diagnostics.isEmpty())
        return;

    const HydraulicSimulationDiagnostic &diagnostic = run.result_timeline.diagnostics.first();
    context.expect(diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error, "negative validation diagnostic must have error severity");
    context.expect(diagnostic.stage == run.result_timeline.status.stage, "diagnostic stage must match the failing status");
    context.expect(diagnostic.operation == run.result_timeline.status.operation, "diagnostic operation must match the failing status");
    context.expect(diagnostic.entity.type == expected_entity_type, "diagnostic must retain entity type");
    context.expect(diagnostic.entity.id == expected_entity_id, "diagnostic must retain entity ID");
    context.expect(diagnostic.entity.uuid == expected_entity_uuid, "diagnostic must retain entity UUID");
    context.expect(diagnostic.message == run.result_timeline.status.message, "diagnostic must retain the actionable status message");
}

void expectQualityRejected(
    TestContext &context,
    const NetworkHydraulic &network,
    const WaterQualitySolverOptions &options,
    const QString &message_fragment)
{
    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network, options));
    context.expect(run.result_timeline.status.success, "invalid quality-run configuration must not invalidate completed hydraulics");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "invalid quality-run configuration must preserve valid hydraulics");
    context.expectEqual(
        static_cast<std::int64_t>(run.quality_results.size()),
        std::int64_t{1},
        AowisEpanetTests::ComparisonContext{},
        "quality validation fixture must return exactly one quality child");
    if (run.quality_results.size() != 1)
        return;

    const EpanetQualityResult &quality_result = run.quality_results.constFirst();
    context.expect(quality_result.state == EpanetRunState::Error, "invalid quality-run configuration must fail its quality child");
    context.expect(!quality_result.result_timeline.status.success, "invalid quality-run configuration must return a failing quality status");
    context.expect(quality_result.result_timeline.validity == WaterQualitySimulationResultValidity::Invalid, "quality validation failure must produce invalid quality results");
    context.expect(quality_result.result_timeline.results.isEmpty(), "quality validation failure must not return quality timesteps");
    context.expect(quality_result.result_timeline.status.stage == HydraulicSimulationStatusStage::ConfigureOptions, "quality validation status must identify request configuration rather than network construction");
    context.expect(quality_result.result_timeline.status.entity.type == HydraulicSimulationStatusEntityType::QualitySolver, "quality validation status must identify the quality solver");
    context.expect(quality_result.result_timeline.status.message.contains(message_fragment, Qt::CaseInsensitive), "quality validation status must contain an actionable reason");
    context.expect(!quality_result.result_timeline.diagnostics.isEmpty(), "quality validation failure must be retained as a structured diagnostic");
}

void scenarioDuplicateNodeId(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.nodes_junctions.isEmpty() && !network.nodes_reservoirs.isEmpty(), "Net1 duplicate-node fixture requires junction and reservoir");
    if (network.nodes_junctions.isEmpty() || network.nodes_reservoirs.isEmpty())
        return;

    const QString duplicate_id = network.nodes_junctions.first().id;
    HydraulicNodeReservoir &reservoir = network.nodes_reservoirs.first();
    reservoir.id = duplicate_id;
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("duplicated"));
}

void scenarioDuplicateLinkId(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.links_pipes.isEmpty() && !network.links_pumps.isEmpty(), "Net1 duplicate-link fixture requires pipe and pump");
    if (network.links_pipes.isEmpty() || network.links_pumps.isEmpty())
        return;

    HydraulicLinkPump &pump = network.links_pumps.first();
    pump.id = network.links_pipes.first().id;
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("duplicated"));
}

void scenarioDuplicateUuidAcrossFamilies(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.nodes_junctions.isEmpty() && !network.patterns_time.isEmpty(), "Net1 duplicate-UUID fixture requires junction and pattern");
    if (network.nodes_junctions.isEmpty() || network.patterns_time.isEmpty())
        return;

    HydraulicPatternTime &pattern = network.patterns_time.first();
    pattern.uuid = network.nodes_junctions.first().uuid;
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("duplicated"));
}

void scenarioDuplicateCurveId(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.curves_pump_head.isEmpty(), "duplicate-curve fixture requires a pump curve");
    if (network.curves_pump_head.isEmpty())
        return;

    HydraulicCurveGeneric curve;
    curve.id = network.curves_pump_head.first().id;
    curve.uuid = QUuid::createUuid();
    HydraulicCurveGenericPoint point;
    point.x = 1.0;
    point.y = 2.0;
    curve.points.append(point);
    network.curves_generic.append(curve);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("duplicated"));
}

void scenarioBrokenNodeReference(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.links_pipes.isEmpty(), "Net1 broken-reference fixture requires a pipe");
    if (network.links_pipes.isEmpty())
        return;

    HydraulicLinkPipe &pipe = network.links_pipes.first();
    pipe.node_uuid_from = QUuid::createUuid();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("missing start node"));
}

void scenarioDisabledNodeReference(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.links_pipes.isEmpty(), "Net1 disabled-reference fixture requires a pipe");
    if (network.links_pipes.isEmpty())
        return;

    HydraulicLinkPipe &pipe = network.links_pipes.first();
    HydraulicNodeJunction *junction = nullptr;
    for (HydraulicNodeJunction &candidate : network.nodes_junctions)
    {
        if (candidate.uuid == pipe.node_uuid_from || candidate.uuid == pipe.node_uuid_to)
        {
            junction = &candidate;
            break;
        }
    }
    if (junction == nullptr)
    {
        context.expect(false, "disabled-node fixture requires one pipe endpoint to be a junction");
        return;
    }

    junction->metadata.enabled = false;
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("disabled"));
}

void scenarioDisabledEntityPruning(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPipe *pipe = findPipe(network, QStringLiteral("112"));
    context.expect(pipe != nullptr, "Net1 disabled-entity fixture requires pipe 112");
    if (pipe == nullptr)
        return;

    const QString disabled_pipe_id = pipe->id;
    pipe->metadata.enabled = false;
    pipe->diameter_mm = std::numeric_limits<double>::quiet_NaN();
    network.options_report.selection_links.mode = HydraulicSimulationReportSelectionMode::Selected;
    network.options_report.selection_links.uuids = {pipe->uuid};

    HydraulicNodeJunction disabled_junction;
    disabled_junction.id = QStringLiteral("__DISABLED_JUNCTION");
    disabled_junction.uuid = QUuid::createUuid();
    disabled_junction.metadata.enabled = false;
    disabled_junction.elevation_m = std::numeric_limits<double>::quiet_NaN();
    network.nodes_junctions.append(disabled_junction);

    HydraulicNodeReservoir disabled_reservoir;
    disabled_reservoir.id = QStringLiteral("__DISABLED_RESERVOIR");
    disabled_reservoir.uuid = QUuid::createUuid();
    disabled_reservoir.metadata.enabled = false;
    disabled_reservoir.hydraulic_head_m = std::numeric_limits<double>::quiet_NaN();
    network.nodes_reservoirs.append(disabled_reservoir);

    HydraulicNodeTank disabled_tank;
    disabled_tank.id = QStringLiteral("__DISABLED_TANK");
    disabled_tank.uuid = QUuid::createUuid();
    disabled_tank.metadata.enabled = false;
    disabled_tank.water_level_initial_m = std::numeric_limits<double>::quiet_NaN();
    network.nodes_tanks.append(disabled_tank);

    HydraulicLinkPipe disabled_pipe;
    disabled_pipe.id = QStringLiteral("__DISABLED_PIPE");
    disabled_pipe.uuid = QUuid::createUuid();
    disabled_pipe.metadata.enabled = false;
    disabled_pipe.node_uuid_from = QUuid::createUuid();
    disabled_pipe.node_uuid_to = QUuid::createUuid();
    disabled_pipe.diameter_mm = std::numeric_limits<double>::quiet_NaN();
    network.links_pipes.append(disabled_pipe);

    HydraulicLinkPump disabled_pump;
    disabled_pump.id = QStringLiteral("__DISABLED_PUMP");
    disabled_pump.uuid = QUuid::createUuid();
    disabled_pump.metadata.enabled = false;
    disabled_pump.node_uuid_from = QUuid::createUuid();
    disabled_pump.node_uuid_to = QUuid::createUuid();
    disabled_pump.initial_speed_ratio = std::numeric_limits<double>::quiet_NaN();
    network.links_pumps.append(disabled_pump);

    HydraulicLinkValve disabled_valve;
    disabled_valve.id = QStringLiteral("__DISABLED_VALVE");
    disabled_valve.uuid = QUuid::createUuid();
    disabled_valve.metadata.enabled = false;
    disabled_valve.node_uuid_from = QUuid::createUuid();
    disabled_valve.node_uuid_to = QUuid::createUuid();
    disabled_valve.diameter_mm = std::numeric_limits<double>::quiet_NaN();
    network.links_valves.append(disabled_valve);

    const QStringList unique_disabled_ids = {
        disabled_junction.id,
        disabled_reservoir.id,
        disabled_tank.id,
        disabled_pipe.id,
        disabled_pump.id,
        disabled_valve.id
    };

    const EpanetResultInp inp = EpanetRunner().retrieveInp(AowisEpanetTests::makeRunRequest(network));
    context.expect(inp.status.success, "disabled nodes/links and their selected report references must be pruned before INP construction");
    context.expect(!inpSectionContainsId(inp.inp_text, QStringLiteral("[PIPES]"), disabled_pipe_id), "disabled existing pipe must not appear in the generated [PIPES] section");
    context.expect(!inp.inp_text.contains(QStringLiteral("LINKS %1").arg(disabled_pipe_id), Qt::CaseInsensitive), "disabled selected link must be pruned from generated report selection");
    for (const QString &disabled_id : unique_disabled_ids)
        context.expect(!inp.inp_text.contains(disabled_id), "disabled hydraulic entity must not appear in generated EPANET input");

    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));
    context.expect(run.result_timeline.status.success, "network must remain runnable after disabled hydraulic entities are omitted");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid, "disabled-entity pruning must preserve valid hydraulic results");
}

void scenarioDisabledControlLinkReference(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPipe *pipe = findPipe(network, QStringLiteral("112"));
    context.expect(pipe != nullptr, "disabled-control-reference fixture requires pipe 112");
    if (pipe == nullptr)
        return;

    pipe->metadata.enabled = false;

    HydraulicControlSimple control;
    control.id = QStringLiteral("C_DISABLED_LINK");
    control.uuid = QUuid::createUuid();
    control.type = HydraulicControlSimpleType::Timer;
    control.link_uuid = pipe->uuid;
    control.action = HydraulicControlActionType::Close;
    control.trigger_elapsed_time_s = 3600;
    network.controls_simple.append(control);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("disabled controlled link"));
}

void scenarioDisabledRuleLinkReference(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPipe *pipe = findPipe(network, QStringLiteral("112"));
    context.expect(pipe != nullptr, "disabled-rule-reference fixture requires pipe 112");
    if (pipe == nullptr)
        return;

    pipe->metadata.enabled = false;

    HydraulicControlRule rule;
    rule.id = QStringLiteral("R_DISABLED_LINK");
    rule.uuid = QUuid::createUuid();

    HydraulicControlRulePremise premise;
    premise.logical_operator = HydraulicControlRuleLogicalOperator::If;
    premise.object = HydraulicControlRuleObject::System;
    premise.variable = HydraulicControlRuleVariable::Time;
    premise.comparison = HydraulicControlRuleOperator::GreaterOrEqual;
    premise.elapsed_time_s = 1800;
    rule.premises.append(premise);

    HydraulicControlRuleAction action;
    action.link_uuid = pipe->uuid;
    action.status = HydraulicControlRuleStatus::Closed;
    rule.actions_then.append(action);
    network.controls_rules.append(rule);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Rule, rule.id, rule.uuid, QStringLiteral("disabled THEN-action link"));
}

void scenarioMissingDemandPattern(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.nodes_junctions.isEmpty(), "missing-pattern fixture requires a junction");
    if (network.nodes_junctions.isEmpty())
        return;

    HydraulicNodeJunction &junction = network.nodes_junctions.first();
    context.expect(!junction.demands.isEmpty(), "missing-pattern fixture requires a junction demand");
    if (junction.demands.isEmpty())
        return;

    junction.demands.first().pattern_mode = HydraulicTimePatternMode::TimePattern;
    junction.demands.first().pattern_uuid = QUuid();
    const EpanetResultInp inherited_default = EpanetRunner().retrieveInp(AowisEpanetTests::makeRunRequest(network));
    context.expect(inherited_default.status.success,
        "null per-demand pattern UUID must inherit the configured default demand pattern");

    junction.demands.first().pattern_uuid = QUuid::createUuid();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("missing demand pattern"));
}

void scenarioMissingPumpCurve(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPump *pump = firstPump(network);
    context.expect(pump != nullptr, "missing-curve fixture requires a pump");
    if (pump == nullptr)
        return;

    pump->head_curve_uuid = QUuid::createUuid();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pump, pump->id, pump->uuid, QStringLiteral("missing head curve"));
}

void scenarioMissingValveCurve(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPipe *pipe = findPipe(network, QStringLiteral("112"));
    context.expect(pipe != nullptr, "missing-valve-curve fixture requires pipe 112");
    if (pipe == nullptr)
        return;

    HydraulicLinkValve valve;
    valve.id = pipe->id;
    valve.uuid = QUuid::createUuid();
    valve.node_uuid_from = pipe->node_uuid_from;
    valve.node_uuid_to = pipe->node_uuid_to;
    valve.type = HydraulicLinkValveType::GPV;
    valve.diameter_mm = 200.0;
    valve.minor_loss_coefficient = 0.0;
    valve.initial_status = HydraulicLinkValveInitialStatus::Open;
    valve.head_loss_curve_uuid = QUuid::createUuid();

    for (int index = 0; index < network.links_pipes.size(); index++)
    {
        if (network.links_pipes.at(index).id == valve.id)
        {
            network.links_pipes.removeAt(index);
            break;
        }
    }
    network.links_valves.append(valve);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("missing GPV head-loss curve"));
}

void scenarioInvalidPatternNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    HydraulicPatternTime pattern;
    pattern.id = QStringLiteral("BAD_PATTERN");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers.append(std::numeric_limits<double>::quiet_NaN());
    network.patterns_time.append(pattern);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pattern, pattern.id, pattern.uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidCurveNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    HydraulicCurveGeneric curve;
    curve.id = QStringLiteral("BAD_CURVE");
    curve.uuid = QUuid::createUuid();
    HydraulicCurveGenericPoint point;
    point.x = 1.0;
    point.y = std::numeric_limits<double>::infinity();
    curve.points.append(point);
    network.curves_generic.append(curve);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid, QStringLiteral("invalid numeric"));
}


void scenarioInvalidCurveShape(TestContext &context)
{
    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurveTankVolume curve;
        curve.id = QStringLiteral("BAD_TANK_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurveTankVolumePoint point_1;
        point_1.water_level_m = 0.0;
        point_1.volume_m3 = 0.0;
        HydraulicCurveTankVolumePoint point_2;
        point_2.water_level_m = 0.0;
        point_2.volume_m3 = 10.0;
        curve.points = {point_1, point_2};
        network.curves_tank_volume.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("levels must increase"), HydraulicSimulationStatusStage::AddCurve);
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurvePumpHead curve;
        curve.id = QStringLiteral("BAD_PUMP_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurvePumpHeadPoint point_1;
        point_1.flow_m3_per_h = 0.0;
        point_1.head_gain_m = 30.0;
        HydraulicCurvePumpHeadPoint point_2;
        point_2.flow_m3_per_h = 100.0;
        point_2.head_gain_m = 35.0;
        curve.points = {point_1, point_2};
        network.curves_pump_head.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("heads must decrease"), HydraulicSimulationStatusStage::AddCurve);
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurvePumpEfficiency curve;
        curve.id = QStringLiteral("BAD_EFFICIENCY_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurvePumpEfficiencyPoint point;
        point.flow_m3_per_h = 100.0;
        point.efficiency_percent = 101.0;
        curve.points.append(point);
        network.curves_pump_efficiency.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("efficiencies in (0, 100]"), HydraulicSimulationStatusStage::AddCurve);
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurveValveHeadloss curve;
        curve.id = QStringLiteral("BAD_HEADLOSS_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurveValveHeadlossPoint point;
        point.flow_m3_per_h = 100.0;
        point.head_loss_m = -1.0;
        curve.points.append(point);
        network.curves_valve_headloss.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("cannot be negative"), HydraulicSimulationStatusStage::AddCurve);
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurveValveCharacteristic curve;
        curve.id = QStringLiteral("BAD_CHARACTERISTIC_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurveValveCharacteristicPoint point;
        point.position_percent = 101.0;
        point.relative_flow_percent = 50.0;
        curve.points.append(point);
        network.curves_valve_characteristic.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("must be in [0, 100] percent"), HydraulicSimulationStatusStage::AddCurve);
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicCurveGeneric curve;
        curve.id = QStringLiteral("BAD_GENERIC_CURVE_SHAPE");
        curve.uuid = QUuid::createUuid();
        HydraulicCurveGenericPoint point_1;
        point_1.x = 2.0;
        point_1.y = 10.0;
        HydraulicCurveGenericPoint point_2;
        point_2.x = 1.0;
        point_2.y = 20.0;
        curve.points = {point_1, point_2};
        network.curves_generic.append(curve);
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Curve, curve.id, curve.uuid,
            QStringLiteral("x values must increase"), HydraulicSimulationStatusStage::AddCurve);
    }
}

void scenarioMissingTypedCurveReferences(TestContext &context)
{
    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.nodes_tanks.isEmpty(), "missing-typed-curve fixture requires a tank");
        if (!network.nodes_tanks.isEmpty())
        {
            HydraulicNodeTank &tank = network.nodes_tanks.first();
            tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeCurve;
            tank.volume_curve_uuid = QUuid::createUuid();
            expectRejected(context, network, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid,
                QStringLiteral("missing tank volume curve"));
        }
    }

    {
        NetworkHydraulic network = cleanNet1();
        HydraulicLinkPump *pump = firstPump(network);
        context.expect(pump != nullptr, "missing-typed-curve fixture requires a pump");
        if (pump != nullptr)
        {
            pump->efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Curve;
            pump->efficiency_curve_uuid = QUuid::createUuid();
            expectRejected(context, network, HydraulicSimulationStatusEntityType::Pump, pump->id, pump->uuid,
                QStringLiteral("missing efficiency curve"));
        }
    }

    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.links_pipes.isEmpty(), "missing-typed-curve fixture requires a pipe");
        if (!network.links_pipes.isEmpty())
        {
            const HydraulicLinkPipe &pipe = network.links_pipes.first();
            HydraulicLinkValve valve;
            valve.id = QStringLiteral("MISSING_PCV_CURVE");
            valve.uuid = QUuid::createUuid();
            valve.node_uuid_from = pipe.node_uuid_from;
            valve.node_uuid_to = pipe.node_uuid_to;
            valve.type = HydraulicLinkValveType::PCV;
            valve.diameter_mm = 300.0;
            valve.setting_position_percent = 50.0;
            valve.characteristic_curve_uuid = QUuid::createUuid();
            valve.initial_status = HydraulicLinkValveInitialStatus::Active;
            network.links_valves.append(valve);
            expectRejected(context, network, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid,
                QStringLiteral("missing PCV characteristic curve"));
        }
    }
}

void scenarioInvalidPipeNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.links_pipes.isEmpty(), "invalid-pipe-numeric fixture requires a pipe");
    if (network.links_pipes.isEmpty())
        return;

    HydraulicLinkPipe &pipe = network.links_pipes.first();
    pipe.diameter_mm = std::numeric_limits<double>::quiet_NaN();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidNodeNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.nodes_junctions.isEmpty(), "invalid-node-numeric fixture requires a junction");
    if (network.nodes_junctions.isEmpty())
        return;

    HydraulicNodeJunction &junction = network.nodes_junctions.first();
    junction.elevation_m = std::numeric_limits<double>::quiet_NaN();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidEmitterConfiguration(TestContext &context)
{
    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.nodes_junctions.isEmpty(), "invalid-emitter fixture requires a junction");
        if (network.nodes_junctions.isEmpty())
            return;

        HydraulicNodeJunction &junction = network.nodes_junctions.first();
        junction.emitter.coefficient = 1.0;
        junction.emitter.pressure_exponent = 0.0;
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("invalid numeric value"));
    }

    {
        NetworkHydraulic network = cleanNet1();
        context.expect(network.nodes_junctions.size() >= 2, "inconsistent-emitter fixture requires two junctions");
        if (network.nodes_junctions.size() < 2)
            return;

        HydraulicNodeJunction &junction_a = network.nodes_junctions[0];
        HydraulicNodeJunction &junction_b = network.nodes_junctions[1];
        junction_a.emitter.coefficient = 1.0;
        junction_a.emitter.pressure_exponent = 0.5;
        junction_b.emitter.coefficient = 1.0;
        junction_b.emitter.pressure_exponent = 0.7;
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Junction, junction_b.id, junction_b.uuid, QStringLiteral("common pressure exponent"));
    }
}

void scenarioInvalidQualityConfiguration(TestContext &context)
{
    {
        const NetworkHydraulic network = cleanNet1();
        WaterQualitySolverOptions options;
        options.analysis = WaterQualityAnalysisType::SourceTrace;
        options.trace_node_uuid = QUuid::createUuid();
        expectQualityRejected(context, network, options, QStringLiteral("source-trace node"));
    }

    {
        const NetworkHydraulic network = cleanNet1();
        WaterQualitySolverOptions options;
        options.analysis = WaterQualityAnalysisType::Chemical;
        options.chemical_name.clear();
        expectQualityRejected(context, network, options, QStringLiteral("chemical name"));
    }

    {
        const NetworkHydraulic network = cleanNet1();
        WaterQualitySolverOptions options;
        options.analysis = WaterQualityAnalysisType::Chemical;
        options.chemical_name = QStringLiteral("Chlorine");
        options.chemical_tolerance_mg_per_l = std::numeric_limits<double>::quiet_NaN();
        expectQualityRejected(context, network, options, QStringLiteral("invalid numeric"));
    }

    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.nodes_junctions.isEmpty(), "quality-source pattern fixture requires a junction");
        if (network.nodes_junctions.isEmpty())
            return;
        HydraulicNodeJunction &junction = network.nodes_junctions.first();
        junction.quality_source.type = HydraulicNodeQualitySourceType::Concentration;
        junction.quality_source.chemical_concentration_mg_per_l = 0.5;
        junction.quality_source.pattern_uuid = QUuid::createUuid();
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("quality-source pattern"));
    }

    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.nodes_tanks.isEmpty(), "tank mixing fixture requires a tank");
        if (network.nodes_tanks.isEmpty())
            return;
        HydraulicNodeTank &tank = network.nodes_tanks.first();
        tank.mixing_fraction = 1.1;
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("invalid numeric"));
    }

    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.links_pipes.isEmpty(), "reaction-order fixture requires a pipe");
        if (network.links_pipes.isEmpty())
            return;
        HydraulicLinkPipe &pipe = network.links_pipes.first();
        pipe.override_reactions = true;
        pipe.bulk_reaction.order = network.options_reaction.global_pipe_bulk_reaction.order + 0.5;
        expectRejected(context, network, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("network-wide EPANET bulk reaction order"));
    }
}

void scenarioInvalidMapNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    HydraulicMapLabel label;
    label.id = QStringLiteral("BAD_MAP_LABEL");
    label.uuid = QUuid::createUuid();
    label.coordinate_wgs84.longitude_deg = 181.0;
    label.coordinate_wgs84.latitude_deg = 50.0;
    label.text = QStringLiteral("Invalid map label");
    network.map_labels.append(label);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("invalid numeric"));
}

void scenarioMissingMapLabelAnchor(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    HydraulicMapLabel label;
    label.id = QStringLiteral("BAD_MAP_ANCHOR");
    label.uuid = QUuid::createUuid();
    label.coordinate_wgs84.longitude_deg = 8.0;
    label.coordinate_wgs84.latitude_deg = 50.0;
    label.text = QStringLiteral("Missing anchor");
    label.anchor_node_uuid = QUuid::createUuid();
    network.map_labels.append(label);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Network, label.id, label.uuid, QStringLiteral("missing map-label anchor node"));
}

void scenarioInvalidPumpNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPump *pump = firstPump(network);
    context.expect(pump != nullptr, "invalid-pump-numeric fixture requires a pump");
    if (pump == nullptr)
        return;

    pump->initial_speed_ratio = std::numeric_limits<double>::infinity();
    expectRejected(context, network, HydraulicSimulationStatusEntityType::Pump, pump->id, pump->uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidValveNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPipe *pipe = findPipe(network, QStringLiteral("112"));
    context.expect(pipe != nullptr, "invalid-valve-numeric fixture requires pipe 112");
    if (pipe == nullptr)
        return;

    HydraulicLinkValve valve;
    valve.id = QStringLiteral("BAD_VALVE");
    valve.uuid = QUuid::createUuid();
    valve.node_uuid_from = pipe->node_uuid_from;
    valve.node_uuid_to = pipe->node_uuid_to;
    valve.type = HydraulicLinkValveType::TCV;
    valve.diameter_mm = 200.0;
    valve.setting_loss_coefficient = std::numeric_limits<double>::quiet_NaN();
    network.links_valves.append(valve);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidControlNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicLinkPump *pump = firstPump(network);
    context.expect(pump != nullptr, "invalid-control-numeric fixture requires a pump");
    if (pump == nullptr)
        return;

    HydraulicControlSimple control;
    control.id = QStringLiteral("BAD_CONTROL");
    control.uuid = QUuid::createUuid();
    control.type = HydraulicControlSimpleType::Timer;
    control.link_uuid = pump->uuid;
    control.action = HydraulicControlActionType::Setting;
    control.setting.pump_speed_ratio = std::numeric_limits<double>::quiet_NaN();
    control.trigger_elapsed_time_s = 1800;
    network.controls_simple.append(control);

    expectRejected(context, network, HydraulicSimulationStatusEntityType::Control, control.id, control.uuid, QStringLiteral("invalid numeric"));
}

void scenarioInvalidSolverNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.accuracy = std::numeric_limits<double>::quiet_NaN();

    expectRejected(
        context,
        network,
        HydraulicSimulationStatusEntityType::HydraulicSolver,
        network.id,
        network.uuid,
        QStringLiteral("invalid numeric"));
}

void scenarioInvalidReportNumeric(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_report.fields_link.flow.below_m3_per_h = std::numeric_limits<double>::quiet_NaN();

    expectRejected(
        context,
        network,
        HydraulicSimulationStatusEntityType::Report,
        network.id,
        network.uuid,
        QStringLiteral("invalid numeric"));
}

void scenarioUnsupportedConfiguration(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.headloss_formula = static_cast<HydraulicHeadlossFormula>(999);

    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));
    context.expect(!run.result_timeline.status.success, "unsupported headloss formula must be rejected");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid, "unsupported configuration must not produce valid results");
    context.expect(run.result_timeline.status.stage == HydraulicSimulationStatusStage::ConfigureOptions, "unsupported headloss formula must identify option configuration stage");
    context.expect(run.result_timeline.status.entity.type == HydraulicSimulationStatusEntityType::HydraulicSolver, "unsupported headloss formula must identify hydraulic solver entity");
    context.expect(run.result_timeline.status.message.contains(QStringLiteral("Unsupported hydraulic headloss formula"), Qt::CaseInsensitive), "unsupported configuration must return an explicit reason");
    context.expect(!run.result_timeline.diagnostics.isEmpty(), "unsupported configuration must produce a structured diagnostic");
}

bool hasErrorDiagnosticForEntity(
    const EpanetResultRun &run,
    HydraulicSimulationStatusEntityType entity_type,
    const QUuid &entity_uuid)
{
    for (const HydraulicSimulationDiagnostic &diagnostic : run.result_timeline.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
            && diagnostic.entity.type == entity_type
            && diagnostic.entity.uuid == entity_uuid)
        {
            return true;
        }
    }
    return false;
}

void scenarioMultipleValidationDiagnostics(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.nodes_junctions.isEmpty(), "multiple-diagnostic fixture requires a junction");
    context.expect(!network.nodes_reservoirs.isEmpty(), "multiple-diagnostic fixture requires a reservoir");
    context.expect(!network.links_pipes.isEmpty(), "multiple-diagnostic fixture requires a pipe");
    context.expect(!network.links_pumps.isEmpty(), "multiple-diagnostic fixture requires a pump");
    if (network.nodes_junctions.isEmpty() || network.nodes_reservoirs.isEmpty() || network.links_pipes.isEmpty() || network.links_pumps.isEmpty())
        return;

    HydraulicNodeJunction &junction = network.nodes_junctions.first();
    HydraulicNodeReservoir &reservoir = network.nodes_reservoirs.first();
    HydraulicLinkPipe &pipe = network.links_pipes.first();
    HydraulicLinkPump &pump = network.links_pumps.first();

    reservoir.id = junction.id;
    pipe.node_uuid_from = QUuid::createUuid();
    junction.coordinate_wgs84.longitude_deg = std::numeric_limits<double>::quiet_NaN();
    pump.initial_speed_ratio = std::numeric_limits<double>::quiet_NaN();

    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));

    context.expect(!run.result_timeline.status.success, "network with multiple invalid entities must fail validation");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid, "preflight validation failure must keep result validity invalid");
    context.expect(run.result_timeline.results.isEmpty(), "preflight validation failure must not attempt hydraulics");
    context.expect(run.result_timeline.diagnostics.size() >= 4, "identity, reference, and numeric validation failures must all be retained");
    context.expect(hasErrorDiagnosticForEntity(run, HydraulicSimulationStatusEntityType::Reservoir, reservoir.uuid), "reservoir identity error must be retained");
    context.expect(hasErrorDiagnosticForEntity(run, HydraulicSimulationStatusEntityType::Pipe, pipe.uuid), "pipe reference error must be retained");
    context.expect(hasErrorDiagnosticForEntity(run, HydraulicSimulationStatusEntityType::Junction, junction.uuid), "junction numeric error must be retained");
    context.expect(hasErrorDiagnosticForEntity(run, HydraulicSimulationStatusEntityType::Pump, pump.uuid), "pump numeric error must be retained");
}

void scenarioStructuredDiagnosticDetails(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.links_pipes.isEmpty(), "structured-diagnostic fixture requires a pipe");
    if (network.links_pipes.isEmpty())
        return;

    HydraulicLinkPipe &pipe = network.links_pipes.first();
    const QUuid missing_uuid = QUuid::createUuid();
    pipe.node_uuid_to = missing_uuid;

    const EpanetResultRun run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));
    context.expect(!run.result_timeline.status.success, "broken reference must fail");
    context.expect(run.result_timeline.status.operation == HydraulicSimulationStatusOperation::ResolveEntity, "broken reference must identify ResolveEntity operation");
    context.expect(!run.result_timeline.status.details.isEmpty(), "broken reference status must include structured details");
    if (!run.result_timeline.status.details.isEmpty())
        context.expect(run.result_timeline.status.details.first().contains(missing_uuid.toString(QUuid::WithoutBraces)), "structured details must include the unresolved UUID");
    context.expect(run.result_timeline.status.backend_error_code == 0, "prevalidation error must not masquerade as an EPANET backend error");
    context.expect(run.result_timeline.status.backend_operation.isEmpty(), "prevalidation error must not claim a backend operation");
    context.expect(!run.result_timeline.diagnostics.isEmpty(), "prevalidation error must be retained as a diagnostic");
    if (!run.result_timeline.diagnostics.isEmpty())
        context.expect(run.result_timeline.diagnostics.first().details == run.result_timeline.status.details, "diagnostic must preserve structured status details");
}
}

namespace AowisEpanetTests
{
void registerNegativeValidationScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-negative-duplicate-node-id",
        "Reject duplicate EPANET node IDs before backend construction.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDuplicateNodeId});
    registry.add(ScenarioDefinition{
        "conformance-negative-duplicate-link-id",
        "Reject duplicate IDs across pipe/pump/valve link families before backend construction.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDuplicateLinkId});
    registry.add(ScenarioDefinition{
        "conformance-negative-duplicate-uuid",
        "Reject duplicate UUIDs across different hydraulic model entity families.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDuplicateUuidAcrossFamilies});
    registry.add(ScenarioDefinition{
        "conformance-negative-duplicate-curve-id",
        "Reject duplicate IDs across EPANET curve families before backend construction.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioDuplicateCurveId});
    registry.add(ScenarioDefinition{
        "conformance-negative-broken-node-reference",
        "Reject an enabled link whose endpoint UUID is absent from the model.",
        {"conformance", "hydraulic", "negative"},
        &scenarioBrokenNodeReference});
    registry.add(ScenarioDefinition{
        "conformance-negative-disabled-node-reference",
        "Reject an enabled link that references a disabled endpoint node.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDisabledNodeReference});
    registry.add(ScenarioDefinition{
        "conformance-negative-disabled-entity-pruning",
        "Omit an unreferenced disabled hydraulic entity while keeping the prepared network runnable.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDisabledEntityPruning});
    registry.add(ScenarioDefinition{
        "conformance-negative-disabled-control-link-reference",
        "Reject a control that references a disabled link instead of degrading to a generic missing-reference error.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDisabledControlLinkReference});
    registry.add(ScenarioDefinition{
        "conformance-negative-disabled-rule-link-reference",
        "Reject a structured rule that references a disabled link.",
        {"conformance", "hydraulic", "negative"},
        &scenarioDisabledRuleLinkReference});
    registry.add(ScenarioDefinition{
        "conformance-negative-missing-pattern",
        "Reject a TimePattern demand whose pattern UUID does not resolve.",
        {"conformance", "hydraulic", "negative"},
        &scenarioMissingDemandPattern});
    registry.add(ScenarioDefinition{
        "conformance-negative-missing-curve",
        "Reject a curve-based pump whose head-curve UUID does not resolve.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioMissingPumpCurve});
    registry.add(ScenarioDefinition{
        "conformance-negative-missing-valve-curve",
        "Reject a GPV whose head-loss curve UUID does not resolve.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioMissingValveCurve});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-pattern-numeric",
        "Reject non-finite time-pattern multipliers before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidPatternNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-curve-numeric",
        "Reject non-finite curve point values before calling EPANET.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioInvalidCurveNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-curve-shape",
        "Reject invalid ordering, range, and monotonicity across all AOWIS curve families.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioInvalidCurveShape});
    registry.add(ScenarioDefinition{
        "conformance-negative-missing-typed-curve-references",
        "Reject unresolved tank-volume, pump-efficiency, and PCV characteristic-curve references.",
        {"conformance", "hydraulic", "negative", "curve"},
        &scenarioMissingTypedCurveReferences});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-pipe-numeric",
        "Reject a non-finite pipe hydraulic input before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidPipeNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-node-numeric",
        "Reject a non-finite node hydraulic input before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidNodeNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-emitter-configuration",
        "Reject non-positive emitter exponents and per-junction exponent combinations that EPANET cannot represent.",
        {"conformance", "hydraulic", "negative", "emitter"},
        &scenarioInvalidEmitterConfiguration});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-quality-configuration",
        "Reject missing source-trace references, invalid chemical-analysis configuration, and reaction orders EPANET cannot represent per entity.",
        {"conformance", "quality", "negative"},
        &scenarioInvalidQualityConfiguration});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-map-numeric",
        "Reject out-of-range canonical WGS84 map-label coordinates before EPANET export.",
        {"conformance", "hydraulic", "negative", "coordinate"},
        &scenarioInvalidMapNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-missing-map-label-anchor",
        "Reject a map label whose anchor UUID does not resolve to an enabled node.",
        {"conformance", "hydraulic", "negative", "coordinate"},
        &scenarioMissingMapLabelAnchor});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-pump-numeric",
        "Reject a non-finite pump hydraulic input before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidPumpNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-valve-numeric",
        "Reject a non-finite valve hydraulic input before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidValveNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-control-numeric",
        "Reject a non-finite simple-control setting before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidControlNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-solver-numeric",
        "Reject a non-finite hydraulic solver option before calling EPANET.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidSolverNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-invalid-report-numeric",
        "Reject a non-finite typed report threshold before constructing backend report commands.",
        {"conformance", "hydraulic", "negative"},
        &scenarioInvalidReportNumeric});
    registry.add(ScenarioDefinition{
        "conformance-negative-unsupported-configuration",
        "Reject an unsupported hydraulic configuration with an explicit structured status.",
        {"conformance", "hydraulic", "negative"},
        &scenarioUnsupportedConfiguration});
    registry.add(ScenarioDefinition{
        "conformance-negative-multiple-diagnostics",
        "Collect all independently detectable preflight validation errors while retaining the first failure as the primary status.",
        {"conformance", "hydraulic", "negative"},
        &scenarioMultipleValidationDiagnostics});
    registry.add(ScenarioDefinition{
        "conformance-negative-structured-diagnostics",
        "Preserve stage, operation, entity, unresolved UUID detail, and backend provenance for validation errors.",
        {"conformance", "hydraulic", "negative"},
        &scenarioStructuredDiagnosticDetails});
}
}
