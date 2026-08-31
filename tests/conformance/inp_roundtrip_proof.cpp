#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance_test_framework.h"
#include "hydraulic_result_comparator.h"
#include "inp_roundtrip_proof.h"
#include "native_epanet_reference_runner.h"
#include "native_quality_reference_runner.h"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NativeHydraulicTimeline;
using AowisEpanetTests::NativeQualityReferenceStep;
using AowisEpanetTests::NativeQualityReferenceTimeline;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::TestContext;

// The focused conformance fixtures intentionally use the comparator's strict
// default tolerances. These broad official/upstream INPs are reconstructed
// object-by-object after native unit normalization; tiny hydraulic residual
// changes can choose a different iterative path and amplify during long quality
// transport runs. Keep that numerical envelope local to this proof.
constexpr double broad_hydraulic_tolerance_scale = 750.0;

NumericTolerance importProofQualityTolerance(WaterQualityAnalysisType analysis)
{
    // These tolerances cover solver-path sensitivity, not representational loss.
    // Structural fidelity and source-vs-generated native quality are checked
    // separately and with tighter criteria below.
    switch (analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        return NumericTolerance{1.0e-2, 1.0e-3};
    case WaterQualityAnalysisType::WaterAge:
        return NumericTolerance{1.0e-2, 1.0e-4};
    case WaterQualityAnalysisType::SourceTrace:
        return NumericTolerance{5.0e-3, 1.0e-4};
    case WaterQualityAnalysisType::None:
        return NumericTolerance{0.0, 0.0};
    }
    return NumericTolerance{1.0e-2, 1.0e-3};
}

NumericTolerance roundTripQualityTolerance(WaterQualityAnalysisType analysis)
{
    switch (analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        return NumericTolerance{1.0e-4, 1.0e-4};
    case WaterQualityAnalysisType::WaterAge:
        return NumericTolerance{1.0e-4, 1.0e-4};
    case WaterQualityAnalysisType::SourceTrace:
        return NumericTolerance{5.0e-3, 1.0e-4};
    case WaterQualityAnalysisType::None:
        return NumericTolerance{0.0, 0.0};
    }
    return NumericTolerance{1.0e-4, 1.0e-4};
}

struct InputInventory
{
    std::int64_t junctions = 0;
    std::int64_t reservoirs = 0;
    std::int64_t tanks = 0;
    std::int64_t pipes = 0;
    std::int64_t pumps = 0;
    std::int64_t valves = 0;
    std::int64_t patterns = 0;
    std::int64_t curves = 0;
    std::int64_t simple_controls = 0;
    std::int64_t rules = 0;
    std::int64_t demand_categories = 0;
    std::int64_t vertices = 0;
    QStringList junction_ids;
    QStringList reservoir_ids;
    QStringList tank_ids;
    QStringList pipe_ids;
    QStringList pump_ids;
    QStringList valve_ids;
    QStringList pattern_ids;
    QStringList curve_ids;
    QStringList rule_ids;
    QStringList demand_category_names;
};

ComparisonContext comparison(std::string field)
{
    ComparisonContext value;
    value.field = std::move(field);
    return value;
}

QHash<int, QString> controlIdsByNativeIndex(const NetworkHydraulic &network)
{
    QHash<int, QString> ids;
    for (int index = 0; index < network.controls_simple.size(); index++)
        ids.insert(index + 1, network.controls_simple.at(index).id);
    return ids;
}

void checkEpanet(int error, const char *operation)
{
    if (error == 0)
        return;

    std::array<char, EN_MAXMSG + 1> message{};
    EN_geterror(error, message.data(), EN_MAXMSG);
    throw std::runtime_error(
        std::string(operation) + " failed with EPANET code "
        + std::to_string(error) + ": " + message.data());
}

class NativeInputProject
{
public:
    explicit NativeInputProject(const QString &input_file)
    {
        if (!this->directory_.isValid())
            throw std::runtime_error("Could not create native INP proof temporary directory");

        checkEpanet(EN_createproject(&this->project_), "EN_createproject");
        const QByteArray input_path = QFile::encodeName(input_file);
        const QByteArray report_path = QFile::encodeName(
            this->directory_.filePath(QStringLiteral("native.rpt")));
        const int open_error = EN_open(
            this->project_, input_path.constData(), report_path.constData(), "");
        if (open_error != 0)
        {
            EN_deleteproject(this->project_);
            this->project_ = nullptr;
            checkEpanet(open_error, "EN_open");
        }
        this->opened_ = true;
    }

    ~NativeInputProject()
    {
        if (this->project_ == nullptr)
            return;
        if (this->opened_)
            EN_close(this->project_);
        EN_deleteproject(this->project_);
    }

    NativeInputProject(const NativeInputProject &) = delete;
    NativeInputProject &operator=(const NativeInputProject &) = delete;

    EN_Project handle() const
    {
        return this->project_;
    }

private:
    QTemporaryDir directory_;
    EN_Project project_ = nullptr;
    bool opened_ = false;
};

InputInventory nativeInventory(const QString &input_file)
{
    NativeInputProject native(input_file);
    InputInventory inventory;

    int node_count = 0;
    checkEpanet(EN_getcount(native.handle(), EN_NODECOUNT, &node_count), "EN_getcount(EN_NODECOUNT)");
    for (int node_index = 1; node_index <= node_count; node_index++)
    {
        int node_type = EN_JUNCTION;
        checkEpanet(EN_getnodetype(native.handle(), node_index, &node_type), "EN_getnodetype");
        char node_id_value[EN_MAXID + 1] = {};
        checkEpanet(EN_getnodeid(native.handle(), node_index, node_id_value), "EN_getnodeid");
        const QString node_id = QString::fromUtf8(node_id_value);
        if (node_type == EN_JUNCTION)
        {
            inventory.junctions++;
            inventory.junction_ids.append(node_id);
            int demand_count = 0;
            checkEpanet(
                EN_getnumdemands(native.handle(), node_index, &demand_count),
                "EN_getnumdemands");
            inventory.demand_categories += demand_count;
            for (int demand_index = 1; demand_index <= demand_count; demand_index++)
            {
                char demand_name[EN_MAXID + 1] = {};
                checkEpanet(
                    EN_getdemandname(native.handle(), node_index, demand_index, demand_name),
                    "EN_getdemandname");
                inventory.demand_category_names.append(
                    QStringLiteral("%1|%2|%3")
                        .arg(node_id)
                        .arg(demand_index)
                        .arg(QString::fromUtf8(demand_name)));
            }
        }
        else if (node_type == EN_RESERVOIR)
        {
            inventory.reservoirs++;
            inventory.reservoir_ids.append(node_id);
        }
        else if (node_type == EN_TANK)
        {
            inventory.tanks++;
            inventory.tank_ids.append(node_id);
        }
    }

    int link_count = 0;
    checkEpanet(EN_getcount(native.handle(), EN_LINKCOUNT, &link_count), "EN_getcount(EN_LINKCOUNT)");
    for (int link_index = 1; link_index <= link_count; link_index++)
    {
        int link_type = EN_PIPE;
        checkEpanet(EN_getlinktype(native.handle(), link_index, &link_type), "EN_getlinktype");
        char link_id_value[EN_MAXID + 1] = {};
        checkEpanet(EN_getlinkid(native.handle(), link_index, link_id_value), "EN_getlinkid");
        const QString link_id = QString::fromUtf8(link_id_value);
        if (link_type == EN_PIPE || link_type == EN_CVPIPE)
        {
            inventory.pipes++;
            inventory.pipe_ids.append(link_id);
        }
        else if (link_type == EN_PUMP)
        {
            inventory.pumps++;
            inventory.pump_ids.append(link_id);
        }
        else
        {
            inventory.valves++;
            inventory.valve_ids.append(link_id);
        }

        int vertex_count = 0;
        checkEpanet(
            EN_getvertexcount(native.handle(), link_index, &vertex_count),
            "EN_getvertexcount");
        inventory.vertices += vertex_count;
    }

    int count = 0;
    checkEpanet(EN_getcount(native.handle(), EN_PATCOUNT, &count), "EN_getcount(EN_PATCOUNT)");
    inventory.patterns = count;
    for (int pattern_index = 1; pattern_index <= count; pattern_index++)
    {
        char pattern_id[EN_MAXID + 1] = {};
        checkEpanet(EN_getpatternid(native.handle(), pattern_index, pattern_id), "EN_getpatternid");
        inventory.pattern_ids.append(QString::fromUtf8(pattern_id));
    }

    checkEpanet(EN_getcount(native.handle(), EN_CURVECOUNT, &count), "EN_getcount(EN_CURVECOUNT)");
    inventory.curves = count;
    for (int curve_index = 1; curve_index <= count; curve_index++)
    {
        char curve_id[EN_MAXID + 1] = {};
        checkEpanet(EN_getcurveid(native.handle(), curve_index, curve_id), "EN_getcurveid");
        inventory.curve_ids.append(QString::fromUtf8(curve_id));
    }

    checkEpanet(EN_getcount(native.handle(), EN_CONTROLCOUNT, &count), "EN_getcount(EN_CONTROLCOUNT)");
    inventory.simple_controls = count;
    checkEpanet(EN_getcount(native.handle(), EN_RULECOUNT, &count), "EN_getcount(EN_RULECOUNT)");
    inventory.rules = count;
    for (int rule_index = 1; rule_index <= count; rule_index++)
    {
        char rule_id[EN_MAXID + 1] = {};
        checkEpanet(EN_getruleID(native.handle(), rule_index, rule_id), "EN_getruleID");
        inventory.rule_ids.append(QString::fromUtf8(rule_id));
    }

    return inventory;
}

InputInventory modelInventory(const NetworkHydraulic &network)
{
    InputInventory inventory;
    inventory.junctions = network.nodes_junctions.size();
    inventory.reservoirs = network.nodes_reservoirs.size();
    inventory.tanks = network.nodes_tanks.size();
    inventory.pipes = network.links_pipes.size();
    inventory.pumps = network.links_pumps.size();
    inventory.valves = network.links_valves.size();
    inventory.patterns = network.patterns_time.size();
    inventory.curves = network.curves_tank_volume.size()
        + network.curves_pump_head.size()
        + network.curves_pump_efficiency.size()
        + network.curves_valve_headloss.size()
        + network.curves_valve_characteristic.size()
        + network.curves_generic.size();
    inventory.simple_controls = network.controls_simple.size();
    inventory.rules = network.controls_rules.size();

    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        inventory.junction_ids.append(junction.id);
        inventory.demand_categories += junction.demands.size();
        for (int demand_index = 0; demand_index < junction.demands.size(); demand_index++)
        {
            inventory.demand_category_names.append(
                QStringLiteral("%1|%2|%3")
                    .arg(junction.id)
                    .arg(demand_index + 1)
                    .arg(junction.demands.at(demand_index).category_name));
        }
    }
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        inventory.reservoir_ids.append(reservoir.id);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        inventory.tank_ids.append(tank.id);
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        inventory.pipe_ids.append(pipe.id);
        inventory.vertices += pipe.vertices.size();
    }
    for (const HydraulicLinkPump &pump : network.links_pumps)
    {
        inventory.pump_ids.append(pump.id);
        inventory.vertices += pump.vertices.size();
    }
    for (const HydraulicLinkValve &valve : network.links_valves)
    {
        inventory.valve_ids.append(valve.id);
        inventory.vertices += valve.vertices.size();
    }
    for (const HydraulicPatternTime &pattern : network.patterns_time)
        inventory.pattern_ids.append(pattern.id);
    for (const HydraulicCurveTankVolume &curve : network.curves_tank_volume)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicCurvePumpEfficiency &curve : network.curves_pump_efficiency)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicCurveValveHeadloss &curve : network.curves_valve_headloss)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicCurveValveCharacteristic &curve : network.curves_valve_characteristic)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicCurveGeneric &curve : network.curves_generic)
        inventory.curve_ids.append(curve.id);
    for (const HydraulicControlRule &rule : network.controls_rules)
        inventory.rule_ids.append(rule.id);

    return inventory;
}

void compareStringList(
    TestContext &context,
    QStringList expected,
    QStringList actual,
    const std::string &field)
{
    expected.sort(Qt::CaseSensitive);
    actual.sort(Qt::CaseSensitive);
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        comparison(field + ".size"));

    const int comparable = qMin(expected.size(), actual.size());
    for (int index = 0; index < comparable; index++)
    {
        context.expectEqual(
            actual.at(index).toStdString(),
            expected.at(index).toStdString(),
            comparison(field + ".item"));
    }
}

void compareInventory(
    TestContext &context,
    const InputInventory &expected,
    const NetworkHydraulic &actual,
    const std::string &phase)
{
    const InputInventory model = modelInventory(actual);
    context.expectEqual(model.junctions, expected.junctions, comparison(phase + ".junctions"));
    context.expectEqual(model.reservoirs, expected.reservoirs, comparison(phase + ".reservoirs"));
    context.expectEqual(model.tanks, expected.tanks, comparison(phase + ".tanks"));
    context.expectEqual(model.pipes, expected.pipes, comparison(phase + ".pipes"));
    context.expectEqual(model.pumps, expected.pumps, comparison(phase + ".pumps"));
    context.expectEqual(model.valves, expected.valves, comparison(phase + ".valves"));
    context.expectEqual(model.patterns, expected.patterns, comparison(phase + ".patterns"));
    context.expectEqual(model.curves, expected.curves, comparison(phase + ".curves"));
    context.expectEqual(
        model.simple_controls, expected.simple_controls,
        comparison(phase + ".simple_controls"));
    context.expectEqual(model.rules, expected.rules, comparison(phase + ".rules"));
    context.expectEqual(
        model.demand_categories, expected.demand_categories,
        comparison(phase + ".demand_categories"));
    context.expectEqual(model.vertices, expected.vertices, comparison(phase + ".vertices"));
    compareStringList(context, expected.junction_ids, model.junction_ids, phase + ".junction_ids");
    compareStringList(context, expected.reservoir_ids, model.reservoir_ids, phase + ".reservoir_ids");
    compareStringList(context, expected.tank_ids, model.tank_ids, phase + ".tank_ids");
    compareStringList(context, expected.pipe_ids, model.pipe_ids, phase + ".pipe_ids");
    compareStringList(context, expected.pump_ids, model.pump_ids, phase + ".pump_ids");
    compareStringList(context, expected.valve_ids, model.valve_ids, phase + ".valve_ids");
    compareStringList(context, expected.pattern_ids, model.pattern_ids, phase + ".pattern_ids");
    compareStringList(context, expected.curve_ids, model.curve_ids, phase + ".curve_ids");
    compareStringList(context, expected.rule_ids, model.rule_ids, phase + ".rule_ids");
    compareStringList(
        context, expected.demand_category_names, model.demand_category_names,
        phase + ".demand_category_names");
}

template<typename ResultType>
double importedQualityValue(const ResultType &result, WaterQualityAnalysisType analysis)
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

ComparisonContext qualityComparison(
    std::int64_t time_s,
    const QString &entity_type,
    const QString &entity_id,
    const char *field)
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = entity_type.toStdString();
    value.entity_id = entity_id.toStdString();
    value.field = field;
    return value;
}

template<typename ResultType>
void compareQualityNodes(
    TestContext &context,
    const QList<ResultType> &actual,
    const NativeQualityReferenceStep &expected,
    WaterQualityAnalysisType analysis,
    const QString &entity_type,
    NumericTolerance tolerance)
{
    for (const ResultType &result : actual)
    {
        context.expect(
            expected.node_quality.contains(result.id),
            "native quality proof must contain every imported node");
        if (expected.node_quality.contains(result.id))
        {
            context.expectNear(
                importedQualityValue(result, analysis),
                expected.node_quality.value(result.id),
                tolerance,
                qualityComparison(expected.time_s, entity_type, result.id, "quality"));
        }

        context.expect(
            expected.node_source_mass_mg_per_min.contains(result.id),
            "native quality proof must contain source-mass output for every imported node");
        if (expected.node_source_mass_mg_per_min.contains(result.id))
        {
            context.expectNear(
                result.source_mass_flow_mg_per_min,
                expected.node_source_mass_mg_per_min.value(result.id),
                tolerance,
                qualityComparison(
                    expected.time_s, entity_type, result.id,
                    "source_mass_flow_mg_per_min"));
        }
    }
}

template<typename ResultType>
void compareQualityLinks(
    TestContext &context,
    const QList<ResultType> &actual,
    const NativeQualityReferenceStep &expected,
    WaterQualityAnalysisType analysis,
    const QString &entity_type,
    NumericTolerance tolerance)
{
    for (const ResultType &result : actual)
    {
        context.expect(
            expected.link_quality.contains(result.id),
            "native quality proof must contain every imported link");
        if (!expected.link_quality.contains(result.id))
            continue;

        context.expectNear(
            importedQualityValue(result, analysis),
            expected.link_quality.value(result.id),
            tolerance,
            qualityComparison(expected.time_s, entity_type, result.id, "quality"));
    }
}

void compareQualityTimeline(
    TestContext &context,
    const NativeQualityReferenceTimeline &native,
    const EpanetResultRun &actual_run,
    WaterQualityAnalysisType analysis,
    const std::string &phase)
{
    context.expect(native.success, phase + ": native quality solve must succeed");
    context.expect(actual_run.status.success, phase + ": AOWIS quality solve must succeed");
    context.expectEqual(
        static_cast<std::int64_t>(actual_run.quality_results.size()),
        std::int64_t{1},
        comparison(phase + ".quality_results"));
    if (!native.success || !actual_run.status.success || actual_run.quality_results.size() != 1)
        return;

    context.expectNear(
        actual_run.quality_results.constFirst().options.relative_diffusivity,
        native.relative_diffusivity,
        NumericTolerance{1.0e-12, 1.0e-9},
        comparison(phase + ".quality_relative_diffusivity"));

    const WaterQualitySimulationResultTimeline &actual =
        actual_run.quality_results.constFirst().result_timeline;
    context.expect(
        actual.validity == WaterQualitySimulationResultValidity::Valid,
        phase + ": quality timeline must be valid");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(native.results.size()),
        comparison(phase + ".quality_steps"));

    const NumericTolerance tolerance = importProofQualityTolerance(analysis);
    const int step_count = qMin(actual.results.size(), native.results.size());
    for (int index = 0; index < step_count; index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(index);
        const NativeQualityReferenceStep &expected_step = native.results.at(index);
        context.expectEqual(
            static_cast<std::int64_t>(actual_step.time_elapsed_s),
            expected_step.time_s,
            comparison(phase + ".quality_time"));
        compareQualityNodes(
            context, actual_step.nodes_junctions, expected_step, analysis,
            QStringLiteral("Junction"), tolerance);
        compareQualityNodes(
            context, actual_step.nodes_reservoirs, expected_step, analysis,
            QStringLiteral("Reservoir"), tolerance);
        compareQualityNodes(
            context, actual_step.nodes_tanks, expected_step, analysis,
            QStringLiteral("Tank"), tolerance);
        compareQualityLinks(
            context, actual_step.links_pipes, expected_step, analysis,
            QStringLiteral("Pipe"), tolerance);
        compareQualityLinks(
            context, actual_step.links_pumps, expected_step, analysis,
            QStringLiteral("Pump"), tolerance);
        compareQualityLinks(
            context, actual_step.links_valves, expected_step, analysis,
            QStringLiteral("Valve"), tolerance);
    }
}

void compareNativeQualityTimelines(
    TestContext &context,
    const NativeQualityReferenceTimeline &expected,
    const NativeQualityReferenceTimeline &actual,
    WaterQualityAnalysisType analysis)
{
    context.expect(expected.success, "canonical source native quality solve must succeed");
    context.expect(actual.success, "generated INP native quality solve must succeed");
    if (!expected.success || !actual.success)
        return;

    context.expectNear(
        actual.relative_diffusivity,
        expected.relative_diffusivity,
        NumericTolerance{1.0e-12, 1.0e-9},
        comparison("roundtrip.native_quality_relative_diffusivity"));

    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        comparison("roundtrip.native_quality_steps"));
    const int step_count = qMin(expected.results.size(), actual.results.size());
    const NumericTolerance tolerance = roundTripQualityTolerance(analysis);
    for (int index = 0; index < step_count; index++)
    {
        const NativeQualityReferenceStep &expected_step = expected.results.at(index);
        const NativeQualityReferenceStep &actual_step = actual.results.at(index);
        context.expectEqual(
            actual_step.time_s,
            expected_step.time_s,
            comparison("roundtrip.native_quality_time"));
        if (actual_step.time_s != expected_step.time_s)
            continue;

        for (auto iterator = expected_step.node_quality.cbegin(); iterator != expected_step.node_quality.cend(); ++iterator)
        {
            context.expect(
                actual_step.node_quality.contains(iterator.key()),
                "generated native quality proof must retain every source node");
            if (actual_step.node_quality.contains(iterator.key()))
            {
                context.expectNear(
                    actual_step.node_quality.value(iterator.key()),
                    iterator.value(),
                    tolerance,
                    qualityComparison(expected_step.time_s, QStringLiteral("Node"), iterator.key(), "roundtrip_native_quality"));
            }
        }
        for (auto iterator = expected_step.link_quality.cbegin(); iterator != expected_step.link_quality.cend(); ++iterator)
        {
            context.expect(
                actual_step.link_quality.contains(iterator.key()),
                "generated native quality proof must retain every source link");
            if (actual_step.link_quality.contains(iterator.key()))
            {
                context.expectNear(
                    actual_step.link_quality.value(iterator.key()),
                    iterator.value(),
                    tolerance,
                    qualityComparison(expected_step.time_s, QStringLiteral("Link"), iterator.key(), "roundtrip_native_quality"));
            }
        }
    }
}

QString writeGeneratedInp(QTemporaryDir &directory, const QString &inp_text)
{
    const QString path = directory.filePath(QStringLiteral("roundtrip.inp"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("Could not write generated INP for round-trip proof");
    const QByteArray content = inp_text.toUtf8();
    if (file.write(content) != content.size())
        throw std::runtime_error("Could not write complete generated INP for round-trip proof");
    file.close();
    return path;
}

void proveInpRoundTripInternal(TestContext &context, const QString &source_file)
{
    const InputInventory source_inventory = nativeInventory(source_file);
    const EpanetResultImport imported = EpanetRunner().importInp(source_file);
    context.expect(imported.status.success, "official/upstream INP must import successfully");
    context.expect(imported.complete, "official/upstream INP must import completely");
    if (!imported.status.success)
        return;

    compareInventory(context, source_inventory, imported.request.network, "import");

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = source_file;
    native_configuration.control_ids_by_index = controlIdsByNativeIndex(imported.request.network);
    native_configuration.canonical_metric_units = true;
    const NativeHydraulicTimeline source_hydraulics =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    const EpanetResultRun imported_run = EpanetRunner().run(imported.request);
    context.expect(
        source_hydraulics.success,
        source_hydraulics.success
            ? "canonical source native hydraulic solve must succeed"
            : source_hydraulics.error.toStdString());
    context.expect(imported_run.status.success, "imported AOWIS hydraulic solve must succeed");
    if (source_hydraulics.success && imported_run.status.success)
    {
        AowisEpanetTests::compareHydraulicTimelines(
            source_hydraulics, imported_run, imported.request.network, context,
            broad_hydraulic_tolerance_scale);
    }

    NativeQualityReferenceTimeline source_quality;
    WaterQualityAnalysisType source_quality_analysis = WaterQualityAnalysisType::None;
    if (imported.request.quality_runs.size() == 1
        && imported.request.quality_runs.constFirst().analysis != WaterQualityAnalysisType::None)
    {
        source_quality_analysis = imported.request.quality_runs.constFirst().analysis;
        source_quality = AowisEpanetTests::runNativeQualityReference(
            source_file, imported.request.network, true);
        compareQualityTimeline(
            context, source_quality, imported_run, source_quality_analysis, "import");
    }

    const EpanetResultInp exported = EpanetRunner().retrieveInp(imported.request);
    context.expect(exported.status.success, "imported request must export back to INP successfully");
    if (!exported.status.success)
        return;

    QTemporaryDir directory;
    context.expect(directory.isValid(), "round-trip proof temporary directory must be available");
    if (!directory.isValid())
        return;
    const QString roundtrip_file = writeGeneratedInp(directory, exported.inp_text);

    // importInp() opens the generated file through native EPANET first. A successful
    // re-import therefore proves native syntax/reopen before the second model comparison.
    const EpanetResultImport reimported = EpanetRunner().importInp(roundtrip_file);
    context.expect(reimported.status.success, "generated INP must reopen through native EPANET");
    context.expect(reimported.complete, "generated INP must re-import completely");
    if (!reimported.status.success)
        return;

    compareInventory(context, source_inventory, reimported.request.network, "roundtrip");

    AowisEpanetTests::NativeReferenceConfiguration roundtrip_native_configuration;
    roundtrip_native_configuration.input_file = roundtrip_file;
    roundtrip_native_configuration.control_ids_by_index =
        controlIdsByNativeIndex(reimported.request.network);
    roundtrip_native_configuration.canonical_metric_units = true;
    const NativeHydraulicTimeline roundtrip_hydraulics =
        AowisEpanetTests::runNativeEpanetReference(roundtrip_native_configuration);
    const EpanetResultRun roundtrip_run = EpanetRunner().run(reimported.request);
    context.expect(
        roundtrip_hydraulics.success,
        roundtrip_hydraulics.success
            ? "generated INP native hydraulic solve must succeed"
            : roundtrip_hydraulics.error.toStdString());
    context.expect(roundtrip_run.status.success, "round-trip AOWIS hydraulic solve must succeed");
    if (roundtrip_hydraulics.success && roundtrip_run.status.success)
    {
        AowisEpanetTests::compareHydraulicTimelines(
            roundtrip_hydraulics, roundtrip_run, reimported.request.network, context,
            broad_hydraulic_tolerance_scale);
    }

    if (reimported.request.quality_runs.size() == 1
        && reimported.request.quality_runs.constFirst().analysis != WaterQualityAnalysisType::None)
    {
        const WaterQualityAnalysisType analysis = reimported.request.quality_runs.constFirst().analysis;
        const NativeQualityReferenceTimeline roundtrip_quality =
            AowisEpanetTests::runNativeQualityReference(
                roundtrip_file, reimported.request.network, true);
        compareQualityTimeline(
            context, roundtrip_quality, roundtrip_run, analysis, "roundtrip");
        if (source_quality_analysis == analysis)
        {
            compareNativeQualityTimelines(
                context, source_quality, roundtrip_quality, analysis);
        }
    }
}

}

namespace AowisEpanetTests
{
void proveInpRoundTrip(TestContext &context, const QString &source_file)
{
    proveInpRoundTripInternal(context, source_file);
}
}
