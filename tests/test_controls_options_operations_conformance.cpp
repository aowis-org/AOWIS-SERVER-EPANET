#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/controls_options_operations_scenarios.h"

#include <QFile>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef AOWIS_EPANET_TEST_NET1_INP
#error "AOWIS_EPANET_TEST_NET1_INP must identify the vendored Net1 fixture"
#endif

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NativeHydraulicTimeline;
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeReferenceVariant;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr int kNativeFirstPremiseLogicalOperator = 2;
constexpr int kRuleLogicalAnd = 2;
constexpr int kRuleLogicalOr = 3;

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
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
    throw std::runtime_error(std::string(operation) + " failed with EPANET code " + std::to_string(error));
}

class NativeSavedProject
{
public:
    explicit NativeSavedProject(const NetworkHydraulic &network)
    {
        const EpanetResultInp result = EpanetRunner().retrieveInp(network);
        if (!result.status.success)
            throw std::runtime_error((QStringLiteral("retrieveInp failed: ") + result.status.message).toStdString());
        if (!this->directory_.isValid())
            throw std::runtime_error("Could not create native round-trip temporary directory");

        const QString input_path = this->directory_.filePath(QStringLiteral("network.inp"));
        QFile input_file(input_path);
        if (!input_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error("Could not write native round-trip input file");
        const QByteArray input_text = result.inp_text.toUtf8();
        if (input_file.write(input_text) != input_text.size())
            throw std::runtime_error("Could not write complete native round-trip input file");
        input_file.close();

        checkEpanet(EN_createproject(&this->project_), "EN_createproject");
        const QByteArray input_path_bytes = input_path.toUtf8();
        const QByteArray report_path_bytes = this->directory_.filePath(QStringLiteral("network.rpt")).toUtf8();
        const int open_error = EN_open(this->project_, input_path_bytes.constData(), report_path_bytes.constData(), "");
        if (open_error != 0)
        {
            EN_deleteproject(this->project_);
            this->project_ = nullptr;
            checkEpanet(open_error, "EN_open");
        }
        this->opened_ = true;
    }

    ~NativeSavedProject()
    {
        if (this->project_ == nullptr)
            return;
        if (this->opened_)
            EN_close(this->project_);
        EN_deleteproject(this->project_);
    }

    NativeSavedProject(const NativeSavedProject &) = delete;
    NativeSavedProject &operator=(const NativeSavedProject &) = delete;

    EN_Project handle() const
    {
        return this->project_;
    }

private:
    QTemporaryDir directory_;
    EN_Project project_ = nullptr;
    bool opened_ = false;
};

struct ControlReadback
{
    int type = -1;
    int link_index = 0;
    double setting = 0.0;
    int node_index = 0;
    double level = 0.0;
};

struct PremiseReadback
{
    int logical_operator = 0;
    int object = 0;
    int object_index = 0;
    int variable = 0;
    int comparison = 0;
    int status = 0;
    double value = 0.0;
};

ControlReadback readControl(EN_Project project, int index)
{
    ControlReadback result;
    checkEpanet(
        EN_getcontrol(project, index, &result.type, &result.link_index, &result.setting, &result.node_index, &result.level),
        "EN_getcontrol");
    return result;
}

PremiseReadback readPremise(EN_Project project, int rule_index, int premise_index)
{
    PremiseReadback result;
    checkEpanet(
        EN_getpremise(project, rule_index, premise_index, &result.logical_operator, &result.object,
            &result.object_index, &result.variable, &result.comparison, &result.status, &result.value),
        "EN_getpremise");
    return result;
}

NetworkHydraulic cleanNet1()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

QUuid nodeUuid(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &node : network.nodes_junctions)
    {
        if (node.id == id)
            return node.uuid;
    }
    for (const HydraulicNodeReservoir &node : network.nodes_reservoirs)
    {
        if (node.id == id)
            return node.uuid;
    }
    for (const HydraulicNodeTank &node : network.nodes_tanks)
    {
        if (node.id == id)
            return node.uuid;
    }
    throw std::runtime_error((QStringLiteral("Could not resolve node ") + id).toStdString());
}

QUuid linkUuid(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPipe &link : network.links_pipes)
    {
        if (link.id == id)
            return link.uuid;
    }
    for (const HydraulicLinkPump &link : network.links_pumps)
    {
        if (link.id == id)
            return link.uuid;
    }
    for (const HydraulicLinkValve &link : network.links_valves)
    {
        if (link.id == id)
            return link.uuid;
    }
    throw std::runtime_error((QStringLiteral("Could not resolve link ") + id).toStdString());
}

HydraulicControlSimple makeSimpleControl(
    const NetworkHydraulic &network,
    HydraulicControlSimpleType type,
    HydraulicControlActionType action,
    bool enabled = true)
{
    HydraulicControlSimple control;
    control.id = QStringLiteral("SIMPLE_CONTROL");
    control.uuid = QUuid::createUuid();
    control.type = type;
    control.link_uuid = linkUuid(network, QStringLiteral("9"));
    control.action = action;
    control.setting = 0.83;
    control.enabled = enabled;

    if (type == HydraulicControlSimpleType::LowLevel || type == HydraulicControlSimpleType::HighLevel)
    {
        control.trigger_node_uuid = nodeUuid(network, QStringLiteral("2"));
        control.trigger_level_or_pressure_head_m = type == HydraulicControlSimpleType::LowLevel ? 34.0 : 40.0;
    }
    else if (type == HydraulicControlSimpleType::Timer)
    {
        control.trigger_time_s = 1800;
    }
    else
    {
        control.trigger_time_s = 7 * 3600;
    }

    return control;
}

HydraulicControlRuleAction closePumpAction(const NetworkHydraulic &network)
{
    HydraulicControlRuleAction action;
    action.link_uuid = linkUuid(network, QStringLiteral("9"));
    action.status = HydraulicControlRuleStatus::Closed;
    return action;
}

HydraulicControlRuleAction openPumpAction(const NetworkHydraulic &network)
{
    HydraulicControlRuleAction action;
    action.link_uuid = linkUuid(network, QStringLiteral("9"));
    action.status = HydraulicControlRuleStatus::Open;
    return action;
}

HydraulicControlRule makeRule(
    const NetworkHydraulic &network,
    const QList<HydraulicControlRulePremise> &premises,
    bool with_else = false,
    double priority = 0.0,
    bool enabled = true)
{
    HydraulicControlRule rule;
    rule.id = QStringLiteral("STRUCTURED_RULE");
    rule.uuid = QUuid::createUuid();
    rule.premises = premises;
    rule.actions_then.append(closePumpAction(network));
    if (with_else)
        rule.actions_else.append(openPumpAction(network));
    rule.priority = priority;
    rule.enabled = enabled;
    return rule;
}

HydraulicControlRulePremise numericPremise(
    HydraulicControlRuleLogicalOperator logical_operator,
    HydraulicControlRuleObject object,
    const QUuid &object_uuid,
    HydraulicControlRuleVariable variable,
    HydraulicControlRuleOperator comparison_operator,
    double value)
{
    HydraulicControlRulePremise premise;
    premise.logical_operator = logical_operator;
    premise.object = object;
    premise.object_uuid = object_uuid;
    premise.variable = variable;
    premise.comparison = comparison_operator;
    premise.value = value;
    return premise;
}

HydraulicControlRulePremise statusPremise(
    HydraulicControlRuleLogicalOperator logical_operator,
    const QUuid &link_uuid,
    HydraulicControlRuleOperator comparison_operator,
    HydraulicControlRuleStatus status)
{
    HydraulicControlRulePremise premise;
    premise.logical_operator = logical_operator;
    premise.object = HydraulicControlRuleObject::Link;
    premise.object_uuid = link_uuid;
    premise.variable = HydraulicControlRuleVariable::Status;
    premise.comparison = comparison_operator;
    premise.status = status;
    return premise;
}

void expectPumpOpen(TestContext &context, const EpanetResultRun &run, bool expected_open, std::string_view message)
{
    context.expect(!run.result_timeline.results.isEmpty(), "pump-state test must return a hydraulic result");
    if (run.result_timeline.results.isEmpty())
        return;
    const HydraulicSimulationResult &result = run.result_timeline.results.first();
    context.expect(!result.links_pumps.isEmpty(), "pump-state test must return a pump result");
    if (result.links_pumps.isEmpty())
        return;
    context.expectEqual(result.links_pumps.first().open, expected_open,
        comparison("pump.open", static_cast<std::int64_t>(result.time_elapsed_s), "Pump", "9"), message);
}

void testSimpleControlType(
    TestContext &context,
    HydraulicControlSimpleType type,
    int expected_type,
    double expected_level)
{
    NetworkHydraulic network = cleanNet1();
    network.controls_simple.append(makeSimpleControl(network, type, HydraulicControlActionType::Open));
    const NativeSavedProject native_project(network);
    const ControlReadback readback = readControl(native_project.handle(), 1);

    context.expectEqual(static_cast<std::int64_t>(readback.type), static_cast<std::int64_t>(expected_type),
        comparison("control.type"));
    context.expect(readback.node_index > 0, "level control must retain its trigger node");
    if (readback.node_index > 0)
    {
        char node_id[EN_MAXID + 1] = {};
        checkEpanet(EN_getnodeid(native_project.handle(), readback.node_index, node_id), "EN_getnodeid");
        context.expectEqual(std::string_view(node_id), std::string_view("2"), comparison("control.trigger_node_id"));
    }
    context.expectNear(readback.level, expected_level, NumericTolerance{1.0e-12, 1.0e-12}, comparison("control.trigger_level_m"));
}

void testSimpleLowLevel(TestContext &context)
{
    testSimpleControlType(context, HydraulicControlSimpleType::LowLevel, EN_LOWLEVEL, 34.0);
}

void testSimpleHighLevel(TestContext &context)
{
    testSimpleControlType(context, HydraulicControlSimpleType::HighLevel, EN_HILEVEL, 40.0);
}

void testSimpleTimer(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 3600;
    network.controls_simple.append(makeSimpleControl(network, HydraulicControlSimpleType::Timer, HydraulicControlActionType::Close));

    const NativeSavedProject native_project(network);
    const ControlReadback readback = readControl(native_project.handle(), 1);
    context.expectEqual(static_cast<std::int64_t>(readback.type), static_cast<std::int64_t>(EN_TIMER), comparison("control.type"));
    context.expectNear(readback.level, 1800.0, NumericTolerance{0.0, 0.0}, comparison("control.timer_seconds"));

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.results.isEmpty(), "timer control must return results");
    if (run.result_timeline.results.isEmpty())
        return;
    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    context.expect(first.event_next.type == HydraulicSimulationTimestepEventType::ControlEvent,
        "timer must be identified as the next hydraulic control event");
    context.expectEqual(static_cast<std::int64_t>(first.event_next.time_until_event_s), std::int64_t{1800},
        comparison("event_next.time_until_event_s", first.time_elapsed_s));
}

void testSimpleTimeOfDay(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 7200;
    network.timestep_hydraulic_s = 7200;
    network.timestep_report_s = 7200;
    network.start_time_of_day_s = 6 * 3600;
    network.controls_simple.append(makeSimpleControl(network, HydraulicControlSimpleType::TimeOfDay, HydraulicControlActionType::Close));

    const NativeSavedProject native_project(network);
    const ControlReadback readback = readControl(native_project.handle(), 1);
    context.expectEqual(static_cast<std::int64_t>(readback.type), static_cast<std::int64_t>(EN_TIMEOFDAY), comparison("control.type"));
    context.expectNear(readback.level, 7.0 * 3600.0, NumericTolerance{0.0, 0.0}, comparison("control.clock_seconds"));

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.results.isEmpty(), "time-of-day control must return results");
    bool saw_controlled_time = false;
    for (const HydraulicSimulationResult &result : run.result_timeline.results)
    {
        if (result.time_elapsed_s != 3600 || result.links_pumps.isEmpty())
            continue;
        saw_controlled_time = true;
        context.expect(!result.links_pumps.first().open,
            "07:00 time-of-day control with a 06:00 simulation start must close the pump after one hour");
    }
    context.expect(saw_controlled_time, "time-of-day control must create a hydraulic result at its 07:00 activation time");
}

void testSimpleAction(TestContext &context, HydraulicControlActionType action, double expected_setting)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 0;
    if (action == HydraulicControlActionType::Open && !network.links_pumps.isEmpty())
        network.links_pumps.first().initial_status = HydraulicLinkPumpInitialStatus::Off;

    HydraulicControlSimple control = makeSimpleControl(network, HydraulicControlSimpleType::Timer, action);
    control.trigger_time_s = 0;
    network.controls_simple.append(control);

    const NativeSavedProject native_project(network);
    const ControlReadback readback = readControl(native_project.handle(), 1);
    context.expectNear(readback.setting, expected_setting, NumericTolerance{0.0, 0.0}, comparison("control.setting"));

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.results.isEmpty(), "simple-control action test must return a hydraulic result");
    if (run.result_timeline.results.isEmpty() || run.result_timeline.results.first().links_pumps.isEmpty())
        return;
    const HydraulicSimulationResultLinkPump &pump = run.result_timeline.results.first().links_pumps.first();
    if (action == HydraulicControlActionType::Open)
        context.expect(pump.open, "zero-time OPEN control must open an initially-off pump");
    else if (action == HydraulicControlActionType::Close)
        context.expect(!pump.open, "zero-time CLOSE control must close the pump");
    else
        context.expectNear(pump.speed_ratio, 0.83, NumericTolerance{1.0e-9, 1.0e-9}, comparison("pump.speed_ratio", 0, "Pump", "9"));
}

void testActionOpen(TestContext &context)
{
    // EN_saveinpfile serializes OPEN/CLOSED controls as ordinary 1/0 settings.
    testSimpleAction(context, HydraulicControlActionType::Open, EN_OPEN);
}

void testActionClose(TestContext &context)
{
    // EN_saveinpfile serializes OPEN/CLOSED controls as ordinary 1/0 settings.
    testSimpleAction(context, HydraulicControlActionType::Close, EN_CLOSED);
}

void testActionSetting(TestContext &context)
{
    testSimpleAction(context, HydraulicControlActionType::Setting, 0.83);
}

void testDisabledSimpleControl(TestContext &context)
{
    NetworkHydraulic enabled_network = cleanNet1();
    enabled_network.duration_s = 0;
    HydraulicControlSimple enabled_control = makeSimpleControl(
        enabled_network, HydraulicControlSimpleType::Timer, HydraulicControlActionType::Close, true);
    enabled_control.trigger_time_s = 0;
    enabled_network.controls_simple.append(enabled_control);

    NetworkHydraulic disabled_network = enabled_network;
    disabled_network.controls_simple[0].enabled = false;

    const EpanetResultRun enabled_run = EpanetRunner().run(enabled_network);
    const EpanetResultRun disabled_run = EpanetRunner().run(disabled_network);
    expectPumpOpen(context, enabled_run, false, "enabled zero-time simple control must close the pump");
    expectPumpOpen(context, disabled_run, true, "disabled zero-time simple control must not close the pump");
}

void testRulePremiseRoundTrip(
    TestContext &context,
    HydraulicControlRuleObject object,
    HydraulicControlRuleVariable variable,
    HydraulicControlRuleOperator comparison_operator,
    int expected_object,
    int expected_variable,
    int expected_comparison)
{
    NetworkHydraulic network = cleanNet1();

    QUuid object_uuid;
    if (object == HydraulicControlRuleObject::Node)
    {
        object_uuid = variable == HydraulicControlRuleVariable::Level
                || variable == HydraulicControlRuleVariable::FillTime
                || variable == HydraulicControlRuleVariable::DrainTime
            ? nodeUuid(network, QStringLiteral("2"))
            : nodeUuid(network, QStringLiteral("11"));
    }
    else if (object == HydraulicControlRuleObject::Link)
    {
        object_uuid = variable == HydraulicControlRuleVariable::Flow
            ? linkUuid(network, QStringLiteral("10"))
            : linkUuid(network, QStringLiteral("9"));
    }

    QList<HydraulicControlRulePremise> premises;
    if (variable == HydraulicControlRuleVariable::Status)
    {
        premises.append(statusPremise(
            HydraulicControlRuleLogicalOperator::If,
            object_uuid,
            comparison_operator,
            HydraulicControlRuleStatus::Open));
    }
    else
    {
        const double value = variable == HydraulicControlRuleVariable::Time
                || variable == HydraulicControlRuleVariable::ClockTime
                || variable == HydraulicControlRuleVariable::FillTime
                || variable == HydraulicControlRuleVariable::DrainTime
            ? 3600.0
            : 1.0;
        premises.append(numericPremise(
            HydraulicControlRuleLogicalOperator::If,
            object,
            object_uuid,
            variable,
            comparison_operator,
            value));
    }
    network.controls_rules.append(makeRule(network, premises));

    const NativeSavedProject native_project(network);
    const PremiseReadback readback = readPremise(native_project.handle(), 1, 1);
    // EPANET stores the first textual IF premise with its internal AND code.
    context.expectEqual(static_cast<std::int64_t>(readback.logical_operator), std::int64_t{kNativeFirstPremiseLogicalOperator}, comparison("premise.logical_operator"));
    context.expectEqual(static_cast<std::int64_t>(readback.object), static_cast<std::int64_t>(expected_object), comparison("premise.object"));
    context.expectEqual(static_cast<std::int64_t>(readback.variable), static_cast<std::int64_t>(expected_variable), comparison("premise.variable"));
    context.expectEqual(static_cast<std::int64_t>(readback.comparison), static_cast<std::int64_t>(expected_comparison), comparison("premise.comparison"));
    if (variable == HydraulicControlRuleVariable::Status)
    {
        context.expectEqual(static_cast<std::int64_t>(readback.status), static_cast<std::int64_t>(EN_R_IS_OPEN), comparison("premise.status"));
    }
    else
    {
        const double expected_value = variable == HydraulicControlRuleVariable::Time
                || variable == HydraulicControlRuleVariable::ClockTime
                || variable == HydraulicControlRuleVariable::FillTime
                || variable == HydraulicControlRuleVariable::DrainTime
            ? 3600.0
            : 1.0;
        context.expectNear(readback.value, expected_value, NumericTolerance{1.0e-12, 1.0e-12}, comparison("premise.value"));
    }
}

#define DEFINE_PREMISE_TEST(function_name, object_value, variable_value, comparison_value, native_object, native_variable, native_comparison) \
    void function_name(TestContext &context) \
    { \
        testRulePremiseRoundTrip(context, object_value, variable_value, comparison_value, native_object, native_variable, native_comparison); \
    }

DEFINE_PREMISE_TEST(testPremiseNodeDemand, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::Demand, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_DEMAND, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodeHead, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::Head, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_HEAD, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodeGrade, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::Grade, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_GRADE, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodeLevel, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::Level, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_LEVEL, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodePressure, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::Pressure, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_PRESSURE, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodeFillTime, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::FillTime, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_FILLTIME, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseNodeDrainTime, HydraulicControlRuleObject::Node, HydraulicControlRuleVariable::DrainTime, HydraulicControlRuleOperator::Greater, EN_R_NODE, EN_R_DRAINTIME, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseLinkFlow, HydraulicControlRuleObject::Link, HydraulicControlRuleVariable::Flow, HydraulicControlRuleOperator::Greater, EN_R_LINK, EN_R_FLOW, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseLinkStatus, HydraulicControlRuleObject::Link, HydraulicControlRuleVariable::Status, HydraulicControlRuleOperator::Is, EN_R_LINK, EN_R_STATUS, EN_R_EQ)
DEFINE_PREMISE_TEST(testPremiseLinkSetting, HydraulicControlRuleObject::Link, HydraulicControlRuleVariable::Setting, HydraulicControlRuleOperator::Greater, EN_R_LINK, EN_R_SETTING, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseSystemDemand, HydraulicControlRuleObject::System, HydraulicControlRuleVariable::Demand, HydraulicControlRuleOperator::Greater, EN_R_SYSTEM, EN_R_DEMAND, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseSystemTime, HydraulicControlRuleObject::System, HydraulicControlRuleVariable::Time, HydraulicControlRuleOperator::Greater, EN_R_SYSTEM, EN_R_TIME, EN_R_GT)
DEFINE_PREMISE_TEST(testPremiseSystemClockTime, HydraulicControlRuleObject::System, HydraulicControlRuleVariable::ClockTime, HydraulicControlRuleOperator::Greater, EN_R_SYSTEM, EN_R_CLOCKTIME, EN_R_GT)

#undef DEFINE_PREMISE_TEST

void testRuleComparisonNumeric(TestContext &context, HydraulicControlRuleOperator comparison_operator, int expected_comparison)
{
    testRulePremiseRoundTrip(
        context,
        HydraulicControlRuleObject::Node,
        HydraulicControlRuleVariable::Pressure,
        comparison_operator,
        EN_R_NODE,
        EN_R_PRESSURE,
        expected_comparison);
}

void testRuleComparisonStatus(TestContext &context, HydraulicControlRuleOperator comparison_operator, int expected_comparison)
{
    testRulePremiseRoundTrip(
        context,
        HydraulicControlRuleObject::Link,
        HydraulicControlRuleVariable::Status,
        comparison_operator,
        EN_R_LINK,
        EN_R_STATUS,
        expected_comparison);
}

#define DEFINE_NUMERIC_OPERATOR_TEST(function_name, operator_value, native_operator) \
    void function_name(TestContext &context) \
    { \
        testRuleComparisonNumeric(context, operator_value, native_operator); \
    }

DEFINE_NUMERIC_OPERATOR_TEST(testOperatorEqual, HydraulicControlRuleOperator::Equal, EN_R_EQ)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorNotEqual, HydraulicControlRuleOperator::NotEqual, EN_R_NE)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorLessOrEqual, HydraulicControlRuleOperator::LessOrEqual, EN_R_LE)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorGreaterOrEqual, HydraulicControlRuleOperator::GreaterOrEqual, EN_R_GE)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorLess, HydraulicControlRuleOperator::Less, EN_R_LT)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorGreater, HydraulicControlRuleOperator::Greater, EN_R_GT)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorBelow, HydraulicControlRuleOperator::Below, EN_R_LT)
DEFINE_NUMERIC_OPERATOR_TEST(testOperatorAbove, HydraulicControlRuleOperator::Above, EN_R_GT)

#undef DEFINE_NUMERIC_OPERATOR_TEST

void testOperatorIs(TestContext &context)
{
    testRuleComparisonStatus(context, HydraulicControlRuleOperator::Is, EN_R_EQ);
}

void testOperatorIsNot(TestContext &context)
{
    testRuleComparisonStatus(context, HydraulicControlRuleOperator::IsNot, EN_R_NE);
}

void testRuleLogicalIf(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));
    network.controls_rules.append(makeRule(network, premises));
    const NativeSavedProject native_project(network);
    const PremiseReadback readback = readPremise(native_project.handle(), 1, 1);
    // EPANET stores the first textual IF premise with its internal AND code.
    context.expectEqual(static_cast<std::int64_t>(readback.logical_operator), std::int64_t{kNativeFirstPremiseLogicalOperator}, comparison("premise.logical_operator"));
}

void testRuleLogicalCompound(TestContext &context, HydraulicControlRuleLogicalOperator logical_operator, int expected_operator)
{
    NetworkHydraulic network = cleanNet1();
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::Node,
        nodeUuid(network, QStringLiteral("11")),
        HydraulicControlRuleVariable::Pressure,
        HydraulicControlRuleOperator::Greater,
        1.0));
    premises.append(numericPremise(
        logical_operator,
        HydraulicControlRuleObject::Node,
        nodeUuid(network, QStringLiteral("12")),
        HydraulicControlRuleVariable::Pressure,
        HydraulicControlRuleOperator::Greater,
        1.0));
    network.controls_rules.append(makeRule(network, premises));

    const NativeSavedProject native_project(network);
    int premise_count = 0;
    int then_count = 0;
    int else_count = 0;
    double priority = 0.0;
    checkEpanet(EN_getrule(native_project.handle(), 1, &premise_count, &then_count, &else_count, &priority), "EN_getrule");
    context.expectEqual(static_cast<std::int64_t>(premise_count), std::int64_t{2}, comparison("rule.premise_count"));
    const PremiseReadback readback = readPremise(native_project.handle(), 1, 2);
    context.expectEqual(static_cast<std::int64_t>(readback.logical_operator), static_cast<std::int64_t>(expected_operator), comparison("premise.logical_operator"));
}

void testRuleLogicalAnd(TestContext &context)
{
    testRuleLogicalCompound(context, HydraulicControlRuleLogicalOperator::And, kRuleLogicalAnd);
}

void testRuleLogicalOr(TestContext &context)
{
    testRuleLogicalCompound(context, HydraulicControlRuleLogicalOperator::Or, kRuleLogicalOr);
}

void testRuleThenAction(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));
    network.controls_rules.append(makeRule(network, premises));

    const NativeSavedProject native_project(network);
    int link_index = 0;
    int status = 0;
    double setting = 0.0;
    checkEpanet(EN_getthenaction(native_project.handle(), 1, 1, &link_index, &status, &setting), "EN_getthenaction");
    context.expect(link_index > 0, "THEN action must retain its controlled link");
    context.expectEqual(static_cast<std::int64_t>(status), static_cast<std::int64_t>(EN_R_IS_CLOSED), comparison("then.status"));
}

void testRuleElseAction(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));
    network.controls_rules.append(makeRule(network, premises, true));

    const NativeSavedProject native_project(network);
    int link_index = 0;
    int status = 0;
    double setting = 0.0;
    checkEpanet(EN_getelseaction(native_project.handle(), 1, 1, &link_index, &status, &setting), "EN_getelseaction");
    context.expect(link_index > 0, "ELSE action must retain its controlled link");
    context.expectEqual(static_cast<std::int64_t>(status), static_cast<std::int64_t>(EN_R_IS_OPEN), comparison("else.status"));
}


void testRuleSettingAction(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));

    HydraulicControlRule rule;
    rule.id = QStringLiteral("RULE_SETTING_ACTION");
    rule.uuid = QUuid::createUuid();
    rule.premises = premises;
    HydraulicControlRuleAction action;
    action.link_uuid = linkUuid(network, QStringLiteral("9"));
    action.setting = 0.77;
    rule.actions_then.append(action);
    network.controls_rules.append(rule);

    const NativeSavedProject native_project(network);
    int link_index = 0;
    int status = 0;
    double setting = 0.0;
    checkEpanet(EN_getthenaction(native_project.handle(), 1, 1, &link_index, &status, &setting), "EN_getthenaction");
    context.expect(link_index > 0, "SETTING action must retain its controlled link");
    context.expectNear(setting, 0.77, NumericTolerance{1.0e-12, 1.0e-12}, comparison("then.setting"));
}

void testRuleActiveAction(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    int pipe_index = -1;
    for (int index = 0; index < network.links_pipes.size(); index++)
    {
        if (network.links_pipes.at(index).id == QStringLiteral("121"))
        {
            pipe_index = index;
            break;
        }
    }
    context.expect(pipe_index >= 0, "active-action fixture requires Net1 pipe 121");
    if (pipe_index < 0)
        return;

    const HydraulicLinkPipe replaced_pipe = network.links_pipes.takeAt(pipe_index);
    HydraulicLinkValve valve;
    valve.id = QStringLiteral("ACTIVE_VALVE");
    valve.uuid = QUuid::createUuid();
    valve.node_uuid_from = replaced_pipe.node_uuid_from;
    valve.node_uuid_to = replaced_pipe.node_uuid_to;
    valve.type = HydraulicLinkValveType::PRV;
    valve.setting = 20.0;
    valve.initial_status = HydraulicLinkValveInitialStatus::Active;
    valve.diameter_mm = 450.0;
    valve.minor_loss_coefficient = 0.15;
    network.links_valves.append(valve);

    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));

    HydraulicControlRule rule;
    rule.id = QStringLiteral("RULE_ACTIVE_ACTION");
    rule.uuid = QUuid::createUuid();
    rule.premises = premises;
    HydraulicControlRuleAction action;
    action.link_uuid = valve.uuid;
    action.status = HydraulicControlRuleStatus::Active;
    rule.actions_then.append(action);
    network.controls_rules.append(rule);

    const NativeSavedProject native_project(network);
    int link_index = 0;
    int status = 0;
    double setting = 0.0;
    checkEpanet(EN_getthenaction(native_project.handle(), 1, 1, &link_index, &status, &setting), "EN_getthenaction");
    context.expect(link_index > 0, "ACTIVE action must retain its controlled valve");
    context.expectEqual(static_cast<std::int64_t>(status), static_cast<std::int64_t>(EN_R_IS_ACTIVE), comparison("then.status"));
}

void testRuleSourceText(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicControlRule rule;
    rule.id = QStringLiteral("SOURCE_TEXT_RULE");
    rule.uuid = QUuid::createUuid();
    rule.source_text = QStringLiteral(
        "RULE SOURCE_TEXT_RULE\n"
        "IF SYSTEM TIME >= 0.5\n"
        "THEN PUMP 9 STATUS = CLOSED\n"
        "PRIORITY 3.5\n");
    network.controls_rules.append(rule);

    const NativeSavedProject native_project(network);
    char rule_id[EN_MAXID + 1] = {};
    checkEpanet(EN_getruleID(native_project.handle(), 1, rule_id), "EN_getruleID");
    context.expectEqual(std::string_view(rule_id), std::string_view("SOURCE_TEXT_RULE"), comparison("rule.id"));

    int premise_count = 0;
    int then_count = 0;
    int else_count = 0;
    double priority = 0.0;
    checkEpanet(EN_getrule(native_project.handle(), 1, &premise_count, &then_count, &else_count, &priority), "EN_getrule");
    context.expectEqual(static_cast<std::int64_t>(premise_count), std::int64_t{1}, comparison("rule.premise_count"));
    context.expectEqual(static_cast<std::int64_t>(then_count), std::int64_t{1}, comparison("rule.then_count"));
    context.expectEqual(static_cast<std::int64_t>(else_count), std::int64_t{0}, comparison("rule.else_count"));
    context.expectNear(priority, 3.5, NumericTolerance{0.0, 0.0}, comparison("rule.priority"));

    const PremiseReadback premise = readPremise(native_project.handle(), 1, 1);
    context.expectEqual(static_cast<std::int64_t>(premise.object), static_cast<std::int64_t>(EN_R_SYSTEM), comparison("premise.object"));
    context.expectEqual(static_cast<std::int64_t>(premise.variable), static_cast<std::int64_t>(EN_R_TIME), comparison("premise.variable"));
    context.expectEqual(static_cast<std::int64_t>(premise.comparison), static_cast<std::int64_t>(EN_R_GE), comparison("premise.comparison"));
    context.expectNear(premise.value, 1800.0, NumericTolerance{1.0e-12, 1.0e-12}, comparison("premise.value"));

    int link_index = 0;
    int status = 0;
    double setting = 0.0;
    checkEpanet(EN_getthenaction(native_project.handle(), 1, 1, &link_index, &status, &setting), "EN_getthenaction");
    context.expect(link_index > 0, "preserved source rule must retain its pump action");
    context.expectEqual(static_cast<std::int64_t>(status), static_cast<std::int64_t>(EN_R_IS_CLOSED), comparison("then.status"));
}

void testRulePriority(TestContext &context)
{
    NetworkHydraulic open_wins = cleanNet1();
    open_wins.duration_s = 3600;
    open_wins.timestep_hydraulic_s = 3600;
    open_wins.timestep_rule_s = 60;

    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));

    HydraulicControlRule close_rule = makeRule(open_wins, premises, false, 1.0);
    close_rule.id = QStringLiteral("PRIORITY_CLOSE_RULE");
    HydraulicControlRule open_rule = makeRule(open_wins, premises, false, 5.0);
    open_rule.id = QStringLiteral("PRIORITY_OPEN_RULE");
    open_rule.actions_then.clear();
    open_rule.actions_then.append(openPumpAction(open_wins));
    open_wins.controls_rules.append(close_rule);
    open_wins.controls_rules.append(open_rule);

    const NativeSavedProject native_project(open_wins);
    int premise_count = 0;
    int then_count = 0;
    int else_count = 0;
    double close_priority = 0.0;
    double open_priority = 0.0;
    checkEpanet(EN_getrule(native_project.handle(), 1, &premise_count, &then_count, &else_count, &close_priority), "EN_getrule(close)");
    checkEpanet(EN_getrule(native_project.handle(), 2, &premise_count, &then_count, &else_count, &open_priority), "EN_getrule(open)");
    context.expectNear(close_priority, 1.0, NumericTolerance{0.0, 0.0}, comparison("rule.close_priority"));
    context.expectNear(open_priority, 5.0, NumericTolerance{0.0, 0.0}, comparison("rule.open_priority"));

    const EpanetResultRun open_wins_run = EpanetRunner().run(open_wins);
    bool saw_open_at_priority_event = false;
    for (const HydraulicSimulationResult &result : open_wins_run.result_timeline.results)
    {
        if (result.time_elapsed_s < 1800 || result.links_pumps.isEmpty())
            continue;
        saw_open_at_priority_event = result.links_pumps.first().open;
        break;
    }
    context.expect(saw_open_at_priority_event, "higher-priority OPEN rule must win a conflicting rule event");

    NetworkHydraulic close_wins = open_wins;
    close_wins.controls_rules[0].priority = 5.0;
    close_wins.controls_rules[1].priority = 1.0;
    const EpanetResultRun close_wins_run = EpanetRunner().run(close_wins);
    bool saw_closed_at_priority_event = false;
    for (const HydraulicSimulationResult &result : close_wins_run.result_timeline.results)
    {
        if (result.time_elapsed_s < 1800 || result.links_pumps.isEmpty())
            continue;
        saw_closed_at_priority_event = !result.links_pumps.first().open;
        break;
    }
    context.expect(saw_closed_at_priority_event, "higher-priority CLOSE rule must win after priorities are reversed");
}

void testDisabledRule(TestContext &context)
{
    NetworkHydraulic enabled_network = cleanNet1();
    enabled_network.duration_s = 3600;
    enabled_network.timestep_hydraulic_s = 3600;
    enabled_network.timestep_rule_s = 60;
    QList<HydraulicControlRulePremise> premises;
    premises.append(numericPremise(
        HydraulicControlRuleLogicalOperator::If,
        HydraulicControlRuleObject::System,
        QUuid(),
        HydraulicControlRuleVariable::Time,
        HydraulicControlRuleOperator::GreaterOrEqual,
        1800.0));
    enabled_network.controls_rules.append(makeRule(enabled_network, premises, false, 0.0, true));

    NetworkHydraulic disabled_network = enabled_network;
    disabled_network.controls_rules[0].enabled = false;

    const EpanetResultRun enabled_run = EpanetRunner().run(enabled_network);
    const EpanetResultRun disabled_run = EpanetRunner().run(disabled_network);

    bool enabled_saw_closed = false;
    for (const HydraulicSimulationResult &result : enabled_run.result_timeline.results)
    {
        if (result.time_elapsed_s < 1800 || result.links_pumps.isEmpty())
            continue;
        if (!result.links_pumps.first().open)
            enabled_saw_closed = true;
    }
    context.expect(enabled_saw_closed, "enabled 1800-second rule must close the pump");

    bool disabled_saw_closed = false;
    for (const HydraulicSimulationResult &result : disabled_run.result_timeline.results)
    {
        if (result.time_elapsed_s < 1800 || result.links_pumps.isEmpty())
            continue;
        if (!result.links_pumps.first().open)
            disabled_saw_closed = true;
    }
    context.expect(!disabled_saw_closed, "disabled 1800-second rule must not close the pump");
}

void testNextHydraulicEvent(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 3600;
    HydraulicControlSimple control = makeSimpleControl(network, HydraulicControlSimpleType::Timer, HydraulicControlActionType::Close);
    control.id = QStringLiteral("NEXT_EVENT_CONTROL");
    network.controls_simple.append(control);

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.results.isEmpty(), "next-event test must return a hydraulic result");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResult &first = run.result_timeline.results.first();
    context.expect(first.event_next.type == HydraulicSimulationTimestepEventType::ControlEvent,
        "next hydraulic event must be a control event");
    context.expectEqual(static_cast<std::int64_t>(first.event_next.time_until_event_s), std::int64_t{1800},
        comparison("event_next.time_until_event_s", first.time_elapsed_s));
}

void testControlEventIdentification(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 3600;
    HydraulicControlSimple control = makeSimpleControl(network, HydraulicControlSimpleType::Timer, HydraulicControlActionType::Close);
    control.id = QStringLiteral("IDENTIFIED_CONTROL");
    const QUuid expected_uuid = control.uuid;
    network.controls_simple.append(control);

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.results.isEmpty(), "control-event identification test must return a hydraulic result");
    if (run.result_timeline.results.isEmpty())
        return;

    const HydraulicSimulationResultTimestepEvent &event = run.result_timeline.results.first().event_next;
    context.expectEqual(event.control_id.toStdString(), "IDENTIFIED_CONTROL", comparison("event_next.control_id"));
    context.expect(event.control_uuid == expected_uuid, "control event must retain the stable Model UUID");
}

enum class TimeParameterCase
{
    Duration,
    HydraulicStep,
    QualityStep,
    PatternStep,
    PatternStart,
    ReportStep,
    ReportStart,
    RuleStep,
    StartTimeOfDay
};

void testTimeParameter(TestContext &context, TimeParameterCase test_case)
{
    NetworkHydraulic network = cleanNet1();
    int backend_parameter = EN_DURATION;
    std::int64_t expected = 0;

    switch (test_case)
    {
    case TimeParameterCase::Duration:
        network.duration_s = 172800;
        backend_parameter = EN_DURATION;
        expected = 172800;
        break;
    case TimeParameterCase::HydraulicStep:
        network.timestep_hydraulic_s = 1800;
        backend_parameter = EN_HYDSTEP;
        expected = 1800;
        break;
    case TimeParameterCase::QualityStep:
        network.timestep_quality_s = 120;
        backend_parameter = EN_QUALSTEP;
        expected = 120;
        break;
    case TimeParameterCase::PatternStep:
        network.timestep_pattern_s = 5400;
        backend_parameter = EN_PATTERNSTEP;
        expected = 5400;
        break;
    case TimeParameterCase::PatternStart:
        network.start_pattern_s = 900;
        backend_parameter = EN_PATTERNSTART;
        expected = 900;
        break;
    case TimeParameterCase::ReportStep:
        network.timestep_report_s = 7200;
        backend_parameter = EN_REPORTSTEP;
        expected = 7200;
        break;
    case TimeParameterCase::ReportStart:
        network.start_report_s = 1800;
        backend_parameter = EN_REPORTSTART;
        expected = 1800;
        break;
    case TimeParameterCase::RuleStep:
        network.timestep_rule_s = 120;
        backend_parameter = EN_RULESTEP;
        expected = 120;
        break;
    case TimeParameterCase::StartTimeOfDay:
        network.start_time_of_day_s = 21600;
        backend_parameter = EN_STARTTIME;
        expected = 21600;
        break;
    }

    const NativeSavedProject native_project(network);
    long actual = 0;
    checkEpanet(EN_gettimeparam(native_project.handle(), backend_parameter, &actual), "EN_gettimeparam");
    context.expectEqual(static_cast<std::int64_t>(actual), expected, comparison("time_parameter"));
}

#define DEFINE_TIME_TEST(function_name, test_case) \
    void function_name(TestContext &context) \
    { \
        testTimeParameter(context, TimeParameterCase::test_case); \
    }

DEFINE_TIME_TEST(testTimeDuration, Duration)
DEFINE_TIME_TEST(testTimeHydraulicStep, HydraulicStep)
DEFINE_TIME_TEST(testTimeQualityStep, QualityStep)
DEFINE_TIME_TEST(testTimePatternStep, PatternStep)
DEFINE_TIME_TEST(testTimePatternStart, PatternStart)
DEFINE_TIME_TEST(testTimeReportStep, ReportStep)
DEFINE_TIME_TEST(testTimeReportStart, ReportStart)
DEFINE_TIME_TEST(testTimeRuleStep, RuleStep)
DEFINE_TIME_TEST(testTimeStartTimeOfDay, StartTimeOfDay)

#undef DEFINE_TIME_TEST

void testReportStatistic(TestContext &context, HydraulicSimulationReportStatistic statistic, int expected_statistic)
{
    NetworkHydraulic network = cleanNet1();
    network.report_statistic = statistic;
    const NativeSavedProject native_project(network);
    long actual = -1;
    checkEpanet(EN_gettimeparam(native_project.handle(), EN_STATISTIC, &actual), "EN_gettimeparam(EN_STATISTIC)");
    context.expectEqual(static_cast<std::int64_t>(actual), static_cast<std::int64_t>(expected_statistic), comparison("report_statistic"));
}

void testReportStatisticSeries(TestContext &context)
{
    testReportStatistic(context, HydraulicSimulationReportStatistic::Series, EN_SERIES);
}

void testReportStatisticAverage(TestContext &context)
{
    testReportStatistic(context, HydraulicSimulationReportStatistic::Average, EN_AVERAGE);
}

void testReportStatisticMinimum(TestContext &context)
{
    testReportStatistic(context, HydraulicSimulationReportStatistic::Minimum, EN_MINIMUM);
}

void testReportStatisticMaximum(TestContext &context)
{
    testReportStatistic(context, HydraulicSimulationReportStatistic::Maximum, EN_MAXIMUM);
}

void testReportStatisticRange(TestContext &context)
{
    testReportStatistic(context, HydraulicSimulationReportStatistic::Range, EN_RANGE);
}

double readOption(const NetworkHydraulic &network, int option)
{
    const NativeSavedProject native_project(network);
    double value = 0.0;
    checkEpanet(EN_getoption(native_project.handle(), option, &value), "EN_getoption");
    return value;
}

void expectOption(TestContext &context, const NetworkHydraulic &network, int option, double expected)
{
    context.expectNear(readOption(network, option), expected, NumericTolerance{1.0e-12, 1.0e-12}, comparison("option"));
}

void testHeadlossHazenWilliams(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    expectOption(context, network, EN_HEADLOSSFORM, EN_HW);
}

void testDemandModel(TestContext &context, HydraulicDemandModel model, int expected_model)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.demand_model = model;
    network.options_hydraulic.minimum_pressure_head_m = 2.0;
    network.options_hydraulic.required_pressure_head_m = 20.0;
    network.options_hydraulic.pressure_exponent = 0.55;

    const NativeSavedProject native_project(network);
    int actual_model = -1;
    double minimum_pressure = 0.0;
    double required_pressure = 0.0;
    double exponent = 0.0;
    checkEpanet(EN_getdemandmodel(native_project.handle(), &actual_model, &minimum_pressure, &required_pressure, &exponent), "EN_getdemandmodel");
    context.expectEqual(static_cast<std::int64_t>(actual_model), static_cast<std::int64_t>(expected_model), comparison("demand_model"));
}

void testDemandModelDda(TestContext &context)
{
    testDemandModel(context, HydraulicDemandModel::DemandDriven, EN_DDA);
}

void testDemandModelPda(TestContext &context)
{
    testDemandModel(context, HydraulicDemandModel::PressureDriven, EN_PDA);
}

enum class DemandParameterCase
{
    MinimumPressure,
    RequiredPressure,
    PressureExponent
};

void testDemandParameter(TestContext &context, DemandParameterCase test_case)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.demand_model = HydraulicDemandModel::PressureDriven;
    network.options_hydraulic.minimum_pressure_head_m = 2.75;
    network.options_hydraulic.required_pressure_head_m = 24.5;
    network.options_hydraulic.pressure_exponent = 0.63;

    const NativeSavedProject native_project(network);
    int model = -1;
    double minimum_pressure = 0.0;
    double required_pressure = 0.0;
    double exponent = 0.0;
    checkEpanet(EN_getdemandmodel(native_project.handle(), &model, &minimum_pressure, &required_pressure, &exponent), "EN_getdemandmodel");

    if (test_case == DemandParameterCase::MinimumPressure)
        context.expectNear(minimum_pressure, 2.75, NumericTolerance{1.0e-12, 1.0e-12}, comparison("minimum_pressure_head_m"));
    else if (test_case == DemandParameterCase::RequiredPressure)
        context.expectNear(required_pressure, 24.5, NumericTolerance{1.0e-12, 1.0e-12}, comparison("required_pressure_head_m"));
    else
        context.expectNear(exponent, 0.63, NumericTolerance{1.0e-12, 1.0e-12}, comparison("pressure_exponent"));
}

void testDemandMinimumPressure(TestContext &context)
{
    testDemandParameter(context, DemandParameterCase::MinimumPressure);
}

void testDemandRequiredPressure(TestContext &context)
{
    testDemandParameter(context, DemandParameterCase::RequiredPressure);
}

void testDemandPressureExponent(TestContext &context)
{
    testDemandParameter(context, DemandParameterCase::PressureExponent);
}

void testOptionMaximumTrials(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.maximum_trials = 73;
    expectOption(context, network, EN_TRIALS, 73.0);
}

void testOptionAccuracy(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.accuracy = 0.00037;
    expectOption(context, network, EN_ACCURACY, 0.00037);
}

void testOptionUnbalancedStop(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Stop;
    expectOption(context, network, EN_UNBALANCED, -1.0);
}

void testOptionUnbalancedContinue(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
    network.options_hydraulic.unbalanced_extra_trials = 17;
    expectOption(context, network, EN_UNBALANCED, 17.0);
}

void testOptionCheckFrequency(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.check_frequency = 5;
    expectOption(context, network, EN_CHECKFREQ, 5.0);
}

void testOptionMaximumCheck(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.maximum_check = 19;
    expectOption(context, network, EN_MAXCHECK, 19.0);
}

void testOptionDampingLimit(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.damping_limit = 0.35;
    expectOption(context, network, EN_DAMPLIMIT, 0.35);
}

void testOptionMaximumHeadError(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.maximum_head_error_m = 0.0042;
    expectOption(context, network, EN_HEADERROR, 0.0042);
}

void testOptionMaximumFlowChange(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.maximum_flow_change_m3_per_h = 0.27;
    expectOption(context, network, EN_FLOWCHANGE, 0.27);
}

void testOptionDemandMultiplier(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.demand_multiplier = 1.37;
    expectOption(context, network, EN_DEMANDMULT, 1.37);
}

void testOptionEmitterExponent(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.emitter_exponent = 0.73;
    expectOption(context, network, EN_EMITEXPON, 0.73);
}

void testOptionEmitterBackflowDisabled(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.emitters_can_backflow = false;
    expectOption(context, network, EN_EMITBACKFLOW, EN_FALSE);
}

void testOptionEmitterBackflowEnabled(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.emitters_can_backflow = true;
    expectOption(context, network, EN_EMITBACKFLOW, EN_TRUE);
}

void testOptionSpecificGravity(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.specific_gravity = 1.08;
    expectOption(context, network, EN_SP_GRAVITY, 1.08);
}

void testOptionRelativeViscosity(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.relative_viscosity = 1.17;
    expectOption(context, network, EN_SP_VISCOS, 1.17);
}

void testOptionDefaultDemandPatternNone(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.default_demand_pattern_uuid = QUuid();
    expectOption(context, network, EN_DEMANDPATTERN, 0.0);
}

void testOptionDefaultDemandPattern(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    context.expect(!network.patterns_time.isEmpty(), "Net1 must provide its demand pattern");
    if (network.patterns_time.isEmpty())
        return;
    network.options_hydraulic.default_demand_pattern_uuid = network.patterns_time.first().uuid;

    const NativeSavedProject native_project(network);
    double pattern_index_value = 0.0;
    checkEpanet(EN_getoption(native_project.handle(), EN_DEMANDPATTERN, &pattern_index_value), "EN_getoption(EN_DEMANDPATTERN)");
    const int pattern_index = static_cast<int>(pattern_index_value);
    context.expect(pattern_index > 0, "default demand pattern must resolve to a native pattern index");
    if (pattern_index <= 0)
        return;
    char pattern_id[EN_MAXID + 1] = {};
    checkEpanet(EN_getpatternid(native_project.handle(), pattern_index, pattern_id), "EN_getpatternid");
    context.expectEqual(std::string_view(pattern_id), network.patterns_time.first().id.toStdString(), comparison("default_demand_pattern.id"));
}

NativeReferenceConfiguration nativeNet1Configuration()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    NativeReferenceConfiguration configuration;
    configuration.input_file = QString::fromUtf8(AOWIS_EPANET_TEST_NET1_INP);
    configuration.control_ids_by_index = fixture.native_control_ids_by_index;
    configuration.variant = NativeReferenceVariant::None;
    return configuration;
}

void testStatistics(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(nativeNet1Configuration());
    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
    context.expect(!wrapper_run.result_timeline.results.isEmpty(), "statistics test must return results");
    if (wrapper_run.result_timeline.results.isEmpty())
        return;
    const HydraulicSimulationResultStatistics &statistics = wrapper_run.result_timeline.results.first().statistics;
    context.expect(statistics.hydraulic_iterations > 0, "hydraulic iteration statistic must be populated");
    context.expect(statistics.relative_error >= 0.0, "relative-error statistic must be non-negative");
    context.expect(statistics.maximum_head_error_m >= 0.0, "maximum-head-error statistic must be non-negative");
    context.expect(statistics.maximum_flow_change_m3_per_h >= 0.0, "maximum-flow-change statistic must be non-negative");
}

void testFlowBalance(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    const NativeHydraulicTimeline native_timeline = AowisEpanetTests::runNativeEpanetReference(nativeNet1Configuration());
    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
    context.expect(!wrapper_run.result_timeline.results.isEmpty(), "flow-balance test must return results");
    if (wrapper_run.result_timeline.results.isEmpty())
        return;
    const HydraulicSimulationResultFlowBalance &balance = wrapper_run.result_timeline.results.last().flow_balance;
    context.expect(balance.total_inflow_m3_per_h > 0.0, "flow balance must contain total inflow");
    context.expect(balance.total_outflow_m3_per_h > 0.0, "flow balance must contain total outflow");
    context.expect(balance.consumer_demand_m3_per_h > 0.0, "flow balance must contain consumer demand");
    context.expect(std::isfinite(balance.flow_balance_ratio), "flow-balance ratio must be finite");
    context.expectNear(balance.flow_balance_ratio, 1.0, NumericTolerance{0.0, 1.0e-4}, comparison("flow_balance_ratio"));
}

void testWarningDiagnostics(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;
    fixture.network.options_hydraulic.demand_multiplier = 10.0;
    fixture.network.options_hydraulic.demand_model = HydraulicDemandModel::DemandDriven;

    const EpanetResultRun run = EpanetRunner().run(fixture.network);
    context.expect(run.result_timeline.status.success, "EPANET warning must not become a fatal wrapper status");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Valid,
        "warning-only hydraulic run must retain valid numerical results");

    bool found_warning = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : run.result_timeline.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Warning)
        {
            found_warning = true;
            context.expect(diagnostic.backend_name == QStringLiteral("EPANET"), "warning diagnostic must retain the backend name");
            context.expect(diagnostic.backend_error_code > 0 && diagnostic.backend_error_code < 100, "warning diagnostic must retain the native EPANET warning code");
            context.expect(!diagnostic.backend_operation.isEmpty(), "warning diagnostic must retain the native EPANET operation");
            context.expect(!diagnostic.message_backend.isEmpty(), "warning diagnostic must retain the native EPANET message");
            break;
        }
    }
    context.expect(found_warning, "warning-producing hydraulic run must retain a structured warning diagnostic");
}

void testErrorDiagnostics(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    HydraulicControlRule rule;
    rule.id = QStringLiteral("BROKEN_RULE_REFERENCE");
    rule.uuid = QUuid::createUuid();

    HydraulicControlRulePremise premise;
    premise.object = HydraulicControlRuleObject::Node;
    premise.object_uuid = QUuid::createUuid();
    premise.variable = HydraulicControlRuleVariable::Pressure;
    premise.comparison = HydraulicControlRuleOperator::Greater;
    premise.value = 1.0;
    rule.premises.append(premise);
    rule.actions_then.append(closePumpAction(network));
    network.controls_rules.append(rule);

    const EpanetResultRun run = EpanetRunner().run(network);
    context.expect(!run.result_timeline.status.success, "invalid rule reference must return an error status");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid,
        "pre-simulation rule-mapping error must invalidate numerical results");
    context.expect(run.result_timeline.results.isEmpty(), "pre-simulation rule-mapping error must return no hydraulic results");

    context.expect(run.result_timeline.status.stage == HydraulicSimulationStatusStage::BuildNetwork,
        "broken rule reference must be rejected during network prevalidation");
    context.expect(run.result_timeline.status.operation == HydraulicSimulationStatusOperation::ResolveEntity,
        "broken rule reference must identify entity resolution as the failing operation");
    context.expect(run.result_timeline.status.entity.type == HydraulicSimulationStatusEntityType::Rule,
        "broken rule reference status must identify the control rule");
    context.expect(run.result_timeline.status.entity.id == rule.id,
        "broken rule reference status must retain the control-rule ID");
    context.expect(run.result_timeline.status.entity.uuid == rule.uuid,
        "broken rule reference status must retain the control-rule UUID");

    bool found_error = false;
    for (const HydraulicSimulationDiagnostic &diagnostic : run.result_timeline.diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
            && diagnostic.stage == HydraulicSimulationStatusStage::BuildNetwork
            && diagnostic.operation == HydraulicSimulationStatusOperation::ResolveEntity
            && diagnostic.entity.type == HydraulicSimulationStatusEntityType::Rule
            && diagnostic.entity.id == rule.id
            && diagnostic.entity.uuid == rule.uuid)
        {
            found_error = true;
            break;
        }
    }
    context.expect(found_error, "prevalidated rule-reference error must retain a structured rule diagnostic");
}

void testCancellationBeforeResults(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    const EpanetResultRun run = EpanetRunner().run(fixture.network, []()
    {
        return true;
    });

    context.expect(run.cancelled, "immediate cancellation must be reported");
    context.expect(run.result_timeline.results.isEmpty(), "immediate cancellation must return no hydraulic results");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Invalid,
        "cancellation before any numerical result must remain invalid");
}

void testCancellationPartialResults(TestContext &context)
{
    const Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    int cancellation_checks = 0;
    const EpanetResultRun run = EpanetRunner().run(fixture.network, [&cancellation_checks]()
    {
        cancellation_checks++;
        return cancellation_checks >= 8;
    });

    context.expect(run.cancelled, "mid-run cancellation must be reported");
    context.expect(!run.result_timeline.results.isEmpty(), "mid-run cancellation must preserve already produced hydraulic results");
    context.expect(run.result_timeline.results.size() < 2,
        "deterministic partial-cancellation fixture should stop after the first hydraulic result");
    context.expect(run.result_timeline.validity == HydraulicSimulationResultValidity::Partial,
        "cancellation after numerical results must classify the timeline as partial");
}
}

namespace AowisEpanetTests
{
void registerControlsOptionsOperationsScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{"conformance-controls-simple-low-level", "Checks low-level simple-control mapping.", {"conformance", "hydraulic", "control"}, &testSimpleLowLevel});
    registry.add(ScenarioDefinition{"conformance-controls-simple-high-level", "Checks high-level simple-control mapping.", {"conformance", "hydraulic", "control"}, &testSimpleHighLevel});
    registry.add(ScenarioDefinition{"conformance-controls-simple-timer", "Checks timer simple-control mapping and event timing.", {"conformance", "hydraulic", "control"}, &testSimpleTimer});
    registry.add(ScenarioDefinition{"conformance-controls-simple-time-of-day", "Checks time-of-day simple-control mapping and clock-relative execution.", {"conformance", "hydraulic", "control"}, &testSimpleTimeOfDay});
    registry.add(ScenarioDefinition{"conformance-controls-action-open", "Checks the simple-control OPEN action.", {"conformance", "hydraulic", "control"}, &testActionOpen});
    registry.add(ScenarioDefinition{"conformance-controls-action-close", "Checks the simple-control CLOSE action.", {"conformance", "hydraulic", "control"}, &testActionClose});
    registry.add(ScenarioDefinition{"conformance-controls-action-setting", "Checks the simple-control numeric SETTING action.", {"conformance", "hydraulic", "control"}, &testActionSetting});
    registry.add(ScenarioDefinition{"conformance-controls-simple-disabled", "Checks disabled simple controls do not execute.", {"conformance", "hydraulic", "control"}, &testDisabledSimpleControl});

    registry.add(ScenarioDefinition{"conformance-controls-rule-logical-if", "Checks IF rule premise mapping.", {"conformance", "hydraulic", "rule"}, &testRuleLogicalIf});
    registry.add(ScenarioDefinition{"conformance-controls-rule-logical-and", "Checks AND rule premise mapping.", {"conformance", "hydraulic", "rule"}, &testRuleLogicalAnd});
    registry.add(ScenarioDefinition{"conformance-controls-rule-logical-or", "Checks OR rule premise mapping.", {"conformance", "hydraulic", "rule"}, &testRuleLogicalOr});
    registry.add(ScenarioDefinition{"conformance-controls-rule-then", "Checks THEN action mapping.", {"conformance", "hydraulic", "rule"}, &testRuleThenAction});
    registry.add(ScenarioDefinition{"conformance-controls-rule-else", "Checks ELSE action mapping.", {"conformance", "hydraulic", "rule"}, &testRuleElseAction});
    registry.add(ScenarioDefinition{"conformance-controls-rule-setting-action", "Checks numeric rule SETTING action mapping.", {"conformance", "hydraulic", "rule"}, &testRuleSettingAction});
    registry.add(ScenarioDefinition{"conformance-controls-rule-active-action", "Checks ACTIVE valve rule-action mapping.", {"conformance", "hydraulic", "rule"}, &testRuleActiveAction});
    registry.add(ScenarioDefinition{"conformance-controls-rule-source-text", "Checks preserved backend rule source-text parsing and identity.", {"conformance", "hydraulic", "rule"}, &testRuleSourceText});
    registry.add(ScenarioDefinition{"conformance-controls-rule-priority", "Checks native priority mapping and conflicting-rule resolution.", {"conformance", "hydraulic", "rule"}, &testRulePriority});
    registry.add(ScenarioDefinition{"conformance-controls-rule-disabled", "Checks disabled rules do not execute.", {"conformance", "hydraulic", "rule"}, &testDisabledRule});

    registry.add(ScenarioDefinition{"conformance-controls-premise-node-demand", "Checks NODE DEMAND premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeDemand});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-head", "Checks NODE HEAD premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeHead});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-grade", "Checks NODE GRADE premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeGrade});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-level", "Checks NODE LEVEL premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeLevel});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-pressure", "Checks NODE PRESSURE premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodePressure});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-fill-time", "Checks tank FILLTIME premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeFillTime});
    registry.add(ScenarioDefinition{"conformance-controls-premise-node-drain-time", "Checks tank DRAINTIME premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseNodeDrainTime});
    registry.add(ScenarioDefinition{"conformance-controls-premise-link-flow", "Checks LINK FLOW premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseLinkFlow});
    registry.add(ScenarioDefinition{"conformance-controls-premise-link-status", "Checks LINK STATUS premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseLinkStatus});
    registry.add(ScenarioDefinition{"conformance-controls-premise-link-setting", "Checks LINK SETTING premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseLinkSetting});
    registry.add(ScenarioDefinition{"conformance-controls-premise-system-demand", "Checks SYSTEM DEMAND premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseSystemDemand});
    registry.add(ScenarioDefinition{"conformance-controls-premise-system-time", "Checks SYSTEM TIME premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseSystemTime});
    registry.add(ScenarioDefinition{"conformance-controls-premise-system-clock-time", "Checks SYSTEM CLOCKTIME premise mapping.", {"conformance", "hydraulic", "rule"}, &testPremiseSystemClockTime});

    registry.add(ScenarioDefinition{"conformance-controls-operator-equal", "Checks rule '=' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorEqual});
    registry.add(ScenarioDefinition{"conformance-controls-operator-not-equal", "Checks rule '<>' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorNotEqual});
    registry.add(ScenarioDefinition{"conformance-controls-operator-less-or-equal", "Checks rule '<=' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorLessOrEqual});
    registry.add(ScenarioDefinition{"conformance-controls-operator-greater-or-equal", "Checks rule '>=' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorGreaterOrEqual});
    registry.add(ScenarioDefinition{"conformance-controls-operator-less", "Checks rule '<' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorLess});
    registry.add(ScenarioDefinition{"conformance-controls-operator-greater", "Checks rule '>' comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorGreater});
    registry.add(ScenarioDefinition{"conformance-controls-operator-is", "Checks rule IS status comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorIs});
    registry.add(ScenarioDefinition{"conformance-controls-operator-is-not", "Checks rule NOT status comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorIsNot});
    registry.add(ScenarioDefinition{"conformance-controls-operator-below", "Checks rule BELOW comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorBelow});
    registry.add(ScenarioDefinition{"conformance-controls-operator-above", "Checks rule ABOVE comparison mapping.", {"conformance", "hydraulic", "rule"}, &testOperatorAbove});

    registry.add(ScenarioDefinition{"conformance-controls-next-hydraulic-event", "Checks next-hydraulic-event type and timing.", {"conformance", "hydraulic", "event"}, &testNextHydraulicEvent});
    registry.add(ScenarioDefinition{"conformance-controls-control-event-identification", "Checks stable control ID and UUID on control events.", {"conformance", "hydraulic", "event"}, &testControlEventIdentification});

    registry.add(ScenarioDefinition{"conformance-options-time-duration", "Checks simulation duration mapping.", {"conformance", "hydraulic", "options"}, &testTimeDuration});
    registry.add(ScenarioDefinition{"conformance-options-time-hydraulic-step", "Checks hydraulic timestep mapping.", {"conformance", "hydraulic", "options"}, &testTimeHydraulicStep});
    registry.add(ScenarioDefinition{"conformance-options-time-quality-step", "Checks quality timestep mapping retained by the hydraulic project.", {"conformance", "hydraulic", "options"}, &testTimeQualityStep});
    registry.add(ScenarioDefinition{"conformance-options-time-pattern-step", "Checks pattern timestep mapping.", {"conformance", "hydraulic", "options"}, &testTimePatternStep});
    registry.add(ScenarioDefinition{"conformance-options-time-pattern-start", "Checks pattern start offset mapping.", {"conformance", "hydraulic", "options"}, &testTimePatternStart});
    registry.add(ScenarioDefinition{"conformance-options-time-report-step", "Checks report timestep mapping.", {"conformance", "hydraulic", "options"}, &testTimeReportStep});
    registry.add(ScenarioDefinition{"conformance-options-time-report-start", "Checks report start offset mapping.", {"conformance", "hydraulic", "options"}, &testTimeReportStart});
    registry.add(ScenarioDefinition{"conformance-options-time-rule-step", "Checks rule timestep mapping.", {"conformance", "hydraulic", "options"}, &testTimeRuleStep});
    registry.add(ScenarioDefinition{"conformance-options-time-start-clock", "Checks simulation start time-of-day mapping.", {"conformance", "hydraulic", "options"}, &testTimeStartTimeOfDay});

    registry.add(ScenarioDefinition{"conformance-options-report-statistic-series", "Checks SERIES report-statistic mapping.", {"conformance", "hydraulic", "options"}, &testReportStatisticSeries});
    registry.add(ScenarioDefinition{"conformance-options-report-statistic-average", "Checks AVERAGE report-statistic mapping.", {"conformance", "hydraulic", "options"}, &testReportStatisticAverage});
    registry.add(ScenarioDefinition{"conformance-options-report-statistic-minimum", "Checks MINIMUM report-statistic mapping.", {"conformance", "hydraulic", "options"}, &testReportStatisticMinimum});
    registry.add(ScenarioDefinition{"conformance-options-report-statistic-maximum", "Checks MAXIMUM report-statistic mapping.", {"conformance", "hydraulic", "options"}, &testReportStatisticMaximum});
    registry.add(ScenarioDefinition{"conformance-options-report-statistic-range", "Checks RANGE report-statistic mapping.", {"conformance", "hydraulic", "options"}, &testReportStatisticRange});

    registry.add(ScenarioDefinition{"conformance-options-hydraulic-headloss-hazen-williams", "Checks Hazen-Williams solver-option mapping; Darcy-Weisbach and Chezy-Manning have dedicated input-mapping tests.", {"conformance", "hydraulic", "options"}, &testHeadlossHazenWilliams});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-demand-model-dda", "Checks demand-driven solver-option mapping.", {"conformance", "hydraulic", "options"}, &testDemandModelDda});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-demand-model-pda", "Checks pressure-driven solver-option mapping.", {"conformance", "hydraulic", "options"}, &testDemandModelPda});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-minimum-pressure", "Checks minimum-pressure solver-option mapping.", {"conformance", "hydraulic", "options"}, &testDemandMinimumPressure});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-required-pressure", "Checks required-pressure solver-option mapping.", {"conformance", "hydraulic", "options"}, &testDemandRequiredPressure});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-pressure-exponent", "Checks pressure-exponent solver-option mapping.", {"conformance", "hydraulic", "options"}, &testDemandPressureExponent});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-maximum-trials", "Checks maximum-trials solver-option mapping.", {"conformance", "hydraulic", "options"}, &testOptionMaximumTrials});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-accuracy", "Checks accuracy solver-option mapping.", {"conformance", "hydraulic", "options"}, &testOptionAccuracy});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-unbalanced-stop", "Checks STOP unbalanced-action mapping.", {"conformance", "hydraulic", "options"}, &testOptionUnbalancedStop});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-unbalanced-continue", "Checks CONTINUE unbalanced-action and extra-trial mapping.", {"conformance", "hydraulic", "options"}, &testOptionUnbalancedContinue});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-check-frequency", "Checks status-check frequency mapping.", {"conformance", "hydraulic", "options"}, &testOptionCheckFrequency});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-maximum-check", "Checks maximum status checks mapping.", {"conformance", "hydraulic", "options"}, &testOptionMaximumCheck});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-damping-limit", "Checks damping-limit mapping.", {"conformance", "hydraulic", "options"}, &testOptionDampingLimit});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-maximum-head-error", "Checks maximum-head-error mapping.", {"conformance", "hydraulic", "options"}, &testOptionMaximumHeadError});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-maximum-flow-change", "Checks maximum-flow-change mapping.", {"conformance", "hydraulic", "options"}, &testOptionMaximumFlowChange});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-demand-multiplier", "Checks demand-multiplier mapping.", {"conformance", "hydraulic", "options"}, &testOptionDemandMultiplier});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-default-demand-pattern-none", "Checks the no-default-demand-pattern solver-option branch.", {"conformance", "hydraulic", "options"}, &testOptionDefaultDemandPatternNone});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-default-demand-pattern", "Checks default-demand-pattern solver-option mapping.", {"conformance", "hydraulic", "options"}, &testOptionDefaultDemandPattern});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-emitter-exponent", "Checks emitter-exponent mapping.", {"conformance", "hydraulic", "options"}, &testOptionEmitterExponent});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-emitter-backflow-disabled", "Checks disabled emitter-backflow mapping.", {"conformance", "hydraulic", "options"}, &testOptionEmitterBackflowDisabled});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-emitter-backflow-enabled", "Checks enabled emitter-backflow mapping.", {"conformance", "hydraulic", "options"}, &testOptionEmitterBackflowEnabled});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-specific-gravity", "Checks specific-gravity mapping.", {"conformance", "hydraulic", "options"}, &testOptionSpecificGravity});
    registry.add(ScenarioDefinition{"conformance-options-hydraulic-relative-viscosity", "Checks relative-viscosity mapping.", {"conformance", "hydraulic", "options"}, &testOptionRelativeViscosity});

    registry.add(ScenarioDefinition{"conformance-operational-statistics", "Checks every hydraulic statistic against the native Net1 timeline.", {"conformance", "hydraulic", "statistics"}, &testStatistics});
    registry.add(ScenarioDefinition{"conformance-operational-flow-balance", "Checks full-run flow-balance results against native Net1.", {"conformance", "hydraulic", "flow-balance"}, &testFlowBalance});
    registry.add(ScenarioDefinition{"conformance-operational-warning-diagnostics", "Checks warning diagnostics remain structured and non-fatal.", {"conformance", "hydraulic", "diagnostic"}, &testWarningDiagnostics});
    registry.add(ScenarioDefinition{"conformance-operational-error-diagnostics", "Checks pre-simulation errors produce structured diagnostics and invalid results.", {"conformance", "hydraulic", "diagnostic"}, &testErrorDiagnostics});
    registry.add(ScenarioDefinition{"conformance-operational-cancellation-before-results", "Checks cancellation before solving returns no results and invalid validity.", {"conformance", "hydraulic", "cancellation"}, &testCancellationBeforeResults});
    registry.add(ScenarioDefinition{"conformance-operational-cancellation-partial-results", "Checks mid-run cancellation preserves results and marks them partial.", {"conformance", "hydraulic", "cancellation"}, &testCancellationPartialResults});
}
}
