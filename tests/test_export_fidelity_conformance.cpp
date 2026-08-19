#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/net1_fixture.h"
#include "conformance/export_fidelity_scenarios.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

ComparisonContext comparison(std::string field, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
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

NetworkHydraulic cleanNet1()
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

HydraulicLinkValve replacePipeWithMetadataValve(NetworkHydraulic &network, const QString &pipe_id)
{
    HydraulicLinkValve valve;
    for (int index = 0; index < network.links_pipes.size(); index++)
    {
        if (network.links_pipes.at(index).id != pipe_id)
            continue;

        valve.id = network.links_pipes.at(index).id;
        valve.uuid = QUuid::createUuid();
        valve.node_uuid_from = network.links_pipes.at(index).node_uuid_from;
        valve.node_uuid_to = network.links_pipes.at(index).node_uuid_to;
        network.links_pipes.removeAt(index);
        break;
    }

    valve.type = HydraulicLinkValveType::TCV;
    valve.diameter_mm = 200.0;
    valve.minor_loss_coefficient = 0.15;
    valve.setting_loss_coefficient = 3.0;
    valve.initial_status = HydraulicLinkValveInitialStatus::Open;
    valve.metadata.comment = QStringLiteral("valve export comment");
    valve.metadata.tag = QStringLiteral("valve-tag");
    network.links_valves.append(valve);
    return valve;
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
            throw std::runtime_error("Could not create native export-verification temporary directory");

        this->inp_text_ = result.inp_text;
        const QString input_path = this->directory_.filePath(QStringLiteral("network.inp"));
        QFile input_file(input_path);
        if (!input_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw std::runtime_error("Could not write generated INP for native reopen");
        const QByteArray input_text = this->inp_text_.toUtf8();
        if (input_file.write(input_text) != input_text.size())
            throw std::runtime_error("Could not write complete generated INP for native reopen");
        input_file.close();

        checkEpanet(EN_createproject(&this->project_), "EN_createproject");
        const QByteArray input_path_bytes = QFile::encodeName(input_path);
        const QByteArray report_path_bytes = QFile::encodeName(this->directory_.filePath(QStringLiteral("network.rpt")));
        const int open_error = EN_open(this->project_, input_path_bytes.constData(), report_path_bytes.constData(), "");
        if (open_error != 0)
        {
            EN_deleteproject(this->project_);
            this->project_ = nullptr;
            checkEpanet(open_error, "EN_open(generated INP)");
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

    const QString &inpText() const
    {
        return this->inp_text_;
    }

private:
    QTemporaryDir directory_;
    EN_Project project_ = nullptr;
    bool opened_ = false;
    QString inp_text_;
};

int nodeIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getnodeindex(project, id_utf8.constData(), &index), "EN_getnodeindex");
    return index;
}

int linkIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getlinkindex(project, id_utf8.constData(), &index), "EN_getlinkindex");
    return index;
}

int patternIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getpatternindex(project, id_utf8.constData(), &index), "EN_getpatternindex");
    return index;
}

int curveIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getcurveindex(project, id_utf8.constData(), &index), "EN_getcurveindex");
    return index;
}

std::string objectComment(EN_Project project, int object_type, int index)
{
    std::array<char, EN_MAXMSG + 1> value{};
    checkEpanet(EN_getcomment(project, object_type, index, value.data()), "EN_getcomment");
    return value.data();
}

std::string objectTag(EN_Project project, int object_type, int index)
{
    std::array<char, EN_MAXMSG + 1> value{};
    checkEpanet(EN_gettag(project, object_type, index, value.data()), "EN_gettag");
    return value.data();
}

const HydraulicSimulationResultLinkPipe *findPipe(const HydraulicSimulationResult &result, const QString &id)
{
    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

QString sectionText(const QString &inp_text, const QString &section_name)
{
    const QStringList lines = inp_text.split(QChar('\n'));
    const QString header = QStringLiteral("[") + section_name + QLatin1Char(']');
    QStringList section_lines;
    bool inside = false;
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('[')))
        {
            if (inside)
                break;
            inside = trimmed.compare(header, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (inside)
            section_lines.append(trimmed);
    }
    return section_lines.join(QChar('\n'));
}

bool sectionContainsCommand(const QString &section, const QString &command)
{
    const QString expected = command.simplified();
    for (const QString &line : section.split(QChar('\n')))
    {
        if (line.simplified().compare(expected, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void scenarioGeneratedInpNativeReopen(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 0;

    NativeSavedProject native(network);
    checkEpanet(EN_solveH(native.handle()), "EN_solveH(generated INP)");

    const EpanetResultRun wrapper = EpanetRunner().run(network);
    context.expect(wrapper.result_timeline.validity == HydraulicSimulationResultValidity::Valid,
        "AOWIS run must succeed for the generated-INP native reopen fixture");
    context.expect(!wrapper.result_timeline.results.isEmpty(), "AOWIS run must return a hydraulic result");
    if (wrapper.result_timeline.validity != HydraulicSimulationResultValidity::Valid
        || wrapper.result_timeline.results.isEmpty())
        return;

    const int native_pipe_index = linkIndex(native.handle(), QStringLiteral("11"));
    double native_flow = 0.0;
    checkEpanet(EN_getlinkvalue(native.handle(), native_pipe_index, EN_FLOW, &native_flow), "EN_getlinkvalue(EN_FLOW)");

    const HydraulicSimulationResultLinkPipe *wrapper_pipe = findPipe(wrapper.result_timeline.results.first(), QStringLiteral("11"));
    context.expect(wrapper_pipe != nullptr, "AOWIS result must contain pipe 11");
    if (wrapper_pipe != nullptr)
        context.expectNear(wrapper_pipe->flow_m3_per_h, native_flow, NumericTolerance{1.0e-6, 1.0e-6}, comparison("native_reopen.flow_m3_per_h", "Pipe", "11"));
}

void scenarioTitlesCommentsTags(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.title_line_1 = QStringLiteral("export fidelity title line one");
    network.title_line_2 = QStringLiteral("export fidelity title line two");
    network.title_line_3 = QStringLiteral("export fidelity title line three");

    network.nodes_junctions.first().metadata.comment = QStringLiteral("junction export comment");
    network.nodes_junctions.first().metadata.tag = QStringLiteral("junction-tag");
    network.nodes_reservoirs.first().metadata.comment = QStringLiteral("reservoir export comment");
    network.nodes_reservoirs.first().metadata.tag = QStringLiteral("reservoir-tag");
    network.nodes_tanks.first().metadata.comment = QStringLiteral("tank export comment");
    network.nodes_tanks.first().metadata.tag = QStringLiteral("tank-tag");
    network.links_pipes.first().metadata.comment = QStringLiteral("pipe export comment");
    network.links_pipes.first().metadata.tag = QStringLiteral("pipe-tag");
    network.links_pumps.first().metadata.comment = QStringLiteral("pump export comment");
    network.links_pumps.first().metadata.tag = QStringLiteral("pump-tag");
    const HydraulicLinkValve valve = replacePipeWithMetadataValve(network, QStringLiteral("121"));

    NativeSavedProject native(network);

    std::array<char, EN_MAXMSG + 1> line_1{};
    std::array<char, EN_MAXMSG + 1> line_2{};
    std::array<char, EN_MAXMSG + 1> line_3{};
    checkEpanet(EN_gettitle(native.handle(), line_1.data(), line_2.data(), line_3.data()), "EN_gettitle");
    context.expectEqual(std::string_view(line_1.data()), std::string_view("export fidelity title line one"), comparison("title.line_1"));
    context.expectEqual(std::string_view(line_2.data()), std::string_view("export fidelity title line two"), comparison("title.line_2"));
    context.expectEqual(std::string_view(line_3.data()), std::string_view("export fidelity title line three"), comparison("title.line_3"));

    const std::array<std::pair<QString, std::pair<std::string, std::string>>, 3> nodes = {{
        {network.nodes_junctions.first().id, {"junction export comment", "junction-tag"}},
        {network.nodes_reservoirs.first().id, {"reservoir export comment", "reservoir-tag"}},
        {network.nodes_tanks.first().id, {"tank export comment", "tank-tag"}}
    }};
    for (const std::pair<QString, std::pair<std::string, std::string>> &item : nodes)
    {
        const int index = nodeIndex(native.handle(), item.first);
        context.expectEqual(objectComment(native.handle(), EN_NODE, index), item.second.first, comparison("metadata.comment", "Node", item.first.toStdString()));
        context.expectEqual(objectTag(native.handle(), EN_NODE, index), item.second.second, comparison("metadata.tag", "Node", item.first.toStdString()));
    }

    const std::array<std::pair<QString, std::pair<std::string, std::string>>, 3> links = {{
        {network.links_pipes.first().id, {"pipe export comment", "pipe-tag"}},
        {network.links_pumps.first().id, {"pump export comment", "pump-tag"}},
        {valve.id, {"valve export comment", "valve-tag"}}
    }};
    for (const std::pair<QString, std::pair<std::string, std::string>> &item : links)
    {
        const int index = linkIndex(native.handle(), item.first);
        context.expectEqual(objectComment(native.handle(), EN_LINK, index), item.second.first, comparison("metadata.comment", "Link", item.first.toStdString()));
        context.expectEqual(objectTag(native.handle(), EN_LINK, index), item.second.second, comparison("metadata.tag", "Link", item.first.toStdString()));
    }
}

void scenarioPatternsCurves(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.patterns_time.first().comment = QStringLiteral("pattern export comment");
    network.curves_pump_head.first().comment = QStringLiteral("pump head export comment");

    HydraulicCurveTankVolume volume;
    volume.id = QStringLiteral("EXPORT_VOLUME_CURVE");
    volume.uuid = QUuid::createUuid();
    volume.comment = QStringLiteral("volume curve export comment");
    HydraulicCurveTankVolumePoint volume_point_1;
    volume_point_1.water_level_m = 0.0;
    volume_point_1.volume_m3 = 0.0;
    HydraulicCurveTankVolumePoint volume_point_2;
    volume_point_2.water_level_m = 10.0;
    volume_point_2.volume_m3 = 100.0;
    volume.points = {volume_point_1, volume_point_2};
    network.curves_tank_volume.append(volume);

    HydraulicCurvePumpEfficiency efficiency;
    efficiency.id = QStringLiteral("EXPORT_EFFICIENCY_CURVE");
    efficiency.uuid = QUuid::createUuid();
    efficiency.comment = QStringLiteral("efficiency curve export comment");
    HydraulicCurvePumpEfficiencyPoint efficiency_point_1;
    efficiency_point_1.flow_m3_per_h = 0.0;
    efficiency_point_1.efficiency_percent = 70.0;
    HydraulicCurvePumpEfficiencyPoint efficiency_point_2;
    efficiency_point_2.flow_m3_per_h = 30.0;
    efficiency_point_2.efficiency_percent = 82.0;
    efficiency.points = {efficiency_point_1, efficiency_point_2};
    network.curves_pump_efficiency.append(efficiency);

    HydraulicCurveValveHeadloss headloss;
    headloss.id = QStringLiteral("EXPORT_HEADLOSS_CURVE");
    headloss.uuid = QUuid::createUuid();
    headloss.comment = QStringLiteral("headloss curve export comment");
    HydraulicCurveValveHeadlossPoint headloss_point_1;
    headloss_point_1.flow_m3_per_h = 0.0;
    headloss_point_1.head_loss_m = 0.0;
    HydraulicCurveValveHeadlossPoint headloss_point_2;
    headloss_point_2.flow_m3_per_h = 30.0;
    headloss_point_2.head_loss_m = 2.0;
    headloss.points = {headloss_point_1, headloss_point_2};
    network.curves_valve_headloss.append(headloss);

    HydraulicCurveValveCharacteristic characteristic;
    characteristic.id = QStringLiteral("EXPORT_VALVE_CURVE");
    characteristic.uuid = QUuid::createUuid();
    characteristic.comment = QStringLiteral("valve curve export comment");
    HydraulicCurveValveCharacteristicPoint characteristic_point_1;
    characteristic_point_1.position_percent = 0.0;
    characteristic_point_1.relative_flow_percent = 0.0;
    HydraulicCurveValveCharacteristicPoint characteristic_point_2;
    characteristic_point_2.position_percent = 50.0;
    characteristic_point_2.relative_flow_percent = 25.0;
    HydraulicCurveValveCharacteristicPoint characteristic_point_3;
    characteristic_point_3.position_percent = 100.0;
    characteristic_point_3.relative_flow_percent = 100.0;
    characteristic.points = {characteristic_point_1, characteristic_point_2, characteristic_point_3};
    network.curves_valve_characteristic.append(characteristic);

    HydraulicCurveGeneric generic;
    generic.id = QStringLiteral("EXPORT_GENERIC_CURVE");
    generic.uuid = QUuid::createUuid();
    generic.comment = QStringLiteral("generic curve export comment");
    HydraulicCurveGenericPoint generic_point_1;
    generic_point_1.x = 1.0;
    generic_point_1.y = 10.0;
    HydraulicCurveGenericPoint generic_point_2;
    generic_point_2.x = 2.0;
    generic_point_2.y = 15.0;
    HydraulicCurveGenericPoint generic_point_3;
    generic_point_3.x = 3.0;
    generic_point_3.y = 22.0;
    generic.points = {generic_point_1, generic_point_2, generic_point_3};
    network.curves_generic.append(generic);

    NativeSavedProject native(network);

    const int pattern_index = patternIndex(native.handle(), network.patterns_time.first().id);
    context.expectEqual(objectComment(native.handle(), EN_TIMEPAT, pattern_index), std::string_view("pattern export comment"), comparison("pattern.comment", "Pattern", network.patterns_time.first().id.toStdString()));
    int pattern_length = 0;
    checkEpanet(EN_getpatternlen(native.handle(), pattern_index, &pattern_length), "EN_getpatternlen");
    context.expectEqual(static_cast<std::int64_t>(pattern_length), static_cast<std::int64_t>(network.patterns_time.first().multipliers.size()), comparison("pattern.length"));
    for (int index = 0; index < pattern_length; index++)
    {
        double value = 0.0;
        checkEpanet(EN_getpatternvalue(native.handle(), pattern_index, index + 1, &value), "EN_getpatternvalue");
        context.expectNear(value, network.patterns_time.first().multipliers.at(index), NumericTolerance{5.0e-5, 0.0}, comparison("pattern.factor"),
            "EPANET's native INP writer serializes pattern multipliers with four decimal places");
    }

    struct CurveExpectation
    {
        QString id;
        std::string comment;
        int type;
        QList<double> x_values;
        QList<double> y_values;
    };

    QList<double> pump_head_x;
    QList<double> pump_head_y;
    for (const HydraulicCurvePumpHeadPoint &point : network.curves_pump_head.first().points)
    {
        pump_head_x.append(point.flow_m3_per_h);
        pump_head_y.append(point.head_gain_m);
    }

    const std::array<CurveExpectation, 6> curves = {{
        {network.curves_pump_head.first().id, "pump head export comment", EN_PUMP_CURVE, pump_head_x, pump_head_y},
        {volume.id, "volume curve export comment", EN_VOLUME_CURVE, {0.0, 10.0}, {0.0, 100.0}},
        {efficiency.id, "efficiency curve export comment", EN_EFFIC_CURVE, {0.0, 30.0}, {70.0, 82.0}},
        {headloss.id, "headloss curve export comment", EN_HLOSS_CURVE, {0.0, 30.0}, {0.0, 2.0}},
        {characteristic.id, "valve curve export comment", EN_VALVE_CURVE, {0.0, 50.0, 100.0}, {0.0, 25.0, 100.0}},
        {generic.id, "generic curve export comment", EN_GENERIC_CURVE, {1.0, 2.0, 3.0}, {10.0, 15.0, 22.0}}
    }};

    for (const CurveExpectation &curve : curves)
    {
        const int curve_index = curveIndex(native.handle(), curve.id);
        context.expectEqual(objectComment(native.handle(), EN_CURVE, curve_index), curve.comment, comparison("curve.comment", "Curve", curve.id.toStdString()));

        int curve_type = -1;
        int point_count = 0;
        checkEpanet(EN_getcurvetype(native.handle(), curve_index, &curve_type), "EN_getcurvetype");
        checkEpanet(EN_getcurvelen(native.handle(), curve_index, &point_count), "EN_getcurvelen");
        context.expectEqual(static_cast<std::int64_t>(curve_type), static_cast<std::int64_t>(curve.type), comparison("curve.type", "Curve", curve.id.toStdString()));
        context.expectEqual(static_cast<std::int64_t>(point_count), static_cast<std::int64_t>(curve.x_values.size()), comparison("curve.point_count", "Curve", curve.id.toStdString()));
        context.expectEqual(static_cast<std::int64_t>(curve.x_values.size()), static_cast<std::int64_t>(curve.y_values.size()), comparison("curve.expected_point_arrays", "Curve", curve.id.toStdString()));

        const int values_to_check = std::min(point_count, static_cast<int>(curve.x_values.size()));
        for (int point_index = 1; point_index <= values_to_check; point_index++)
        {
            double x = 0.0;
            double y = 0.0;
            checkEpanet(EN_getcurvevalue(native.handle(), curve_index, point_index, &x, &y), "EN_getcurvevalue");
            context.expectNear(x, curve.x_values.at(point_index - 1), NumericTolerance{5.0e-5, 0.0}, comparison("curve.x", "Curve", curve.id.toStdString()),
                "EPANET's native INP writer serializes curve coordinates with four decimal places");
            context.expectNear(y, curve.y_values.at(point_index - 1), NumericTolerance{5.0e-5, 0.0}, comparison("curve.y", "Curve", curve.id.toStdString()),
                "EPANET's native INP writer serializes curve coordinates with four decimal places");
        }
    }

}

void expectLinkVertices(EN_Project project, const QString &link_id, const QList<HydraulicLinkVertex> &expected_vertices, TestContext &context)
{
    const int link_index = linkIndex(project, link_id);
    int vertex_count = 0;
    checkEpanet(EN_getvertexcount(project, link_index, &vertex_count), "EN_getvertexcount");
    context.expectEqual(static_cast<std::int64_t>(vertex_count), static_cast<std::int64_t>(expected_vertices.size()), comparison("vertices.count", "Link", link_id.toStdString()));

    const int values_to_check = std::min(vertex_count, static_cast<int>(expected_vertices.size()));
    for (int index = 0; index < values_to_check; index++)
    {
        double x = 0.0;
        double y = 0.0;
        checkEpanet(EN_getvertex(project, link_index, index + 1, &x, &y), "EN_getvertex");
        context.expectNear(x, expected_vertices.at(index).coordinate_wgs84.longitude_deg, NumericTolerance{1.0e-9, 0.0}, comparison("vertex.x", "Link", link_id.toStdString()));
        context.expectNear(y, expected_vertices.at(index).coordinate_wgs84.latitude_deg, NumericTolerance{1.0e-9, 0.0}, comparison("vertex.y", "Link", link_id.toStdString()));
    }
}

void scenarioCoordinatesVertices(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.nodes_junctions.first().coordinate_wgs84.longitude_deg = 8.123456;
    network.nodes_junctions.first().coordinate_wgs84.latitude_deg = 50.654321;
    network.nodes_reservoirs.first().coordinate_wgs84.longitude_deg = 8.223456;
    network.nodes_reservoirs.first().coordinate_wgs84.latitude_deg = 50.754321;
    network.nodes_tanks.first().coordinate_wgs84.longitude_deg = 8.323456;
    network.nodes_tanks.first().coordinate_wgs84.latitude_deg = 50.854321;

    HydraulicLinkVertex pipe_vertex_1;
    pipe_vertex_1.coordinate_wgs84.longitude_deg = 8.140001;
    pipe_vertex_1.coordinate_wgs84.latitude_deg = 50.640001;
    HydraulicLinkVertex pipe_vertex_2;
    pipe_vertex_2.coordinate_wgs84.longitude_deg = 8.150002;
    pipe_vertex_2.coordinate_wgs84.latitude_deg = 50.650002;
    HydraulicLinkVertex pipe_vertex_3;
    pipe_vertex_3.coordinate_wgs84.longitude_deg = 8.160003;
    pipe_vertex_3.coordinate_wgs84.latitude_deg = 50.660003;
    network.links_pipes.first().vertices = {pipe_vertex_1, pipe_vertex_2, pipe_vertex_3};

    HydraulicLinkVertex pump_vertex;
    pump_vertex.coordinate_wgs84.longitude_deg = 8.170004;
    pump_vertex.coordinate_wgs84.latitude_deg = 50.670004;
    network.links_pumps.first().vertices = {pump_vertex};

    const HydraulicLinkValve valve = replacePipeWithMetadataValve(network, QStringLiteral("121"));
    HydraulicLinkVertex valve_vertex_1;
    valve_vertex_1.coordinate_wgs84.longitude_deg = 8.180005;
    valve_vertex_1.coordinate_wgs84.latitude_deg = 50.680005;
    HydraulicLinkVertex valve_vertex_2;
    valve_vertex_2.coordinate_wgs84.longitude_deg = 8.190006;
    valve_vertex_2.coordinate_wgs84.latitude_deg = 50.690006;
    network.links_valves.last().vertices = {valve_vertex_1, valve_vertex_2};

    HydraulicMapLabel anchored_label;
    anchored_label.id = QStringLiteral("label-source");
    anchored_label.uuid = QUuid::createUuid();
    anchored_label.coordinate_wgs84.longitude_deg = 8.111111;
    anchored_label.coordinate_wgs84.latitude_deg = 50.611111;
    anchored_label.text = QStringLiteral("Source label");
    anchored_label.anchor_node_uuid = network.nodes_reservoirs.first().uuid;
    network.map_labels.append(anchored_label);

    HydraulicMapLabel free_label;
    free_label.id = QStringLiteral("label-free");
    free_label.uuid = QUuid::createUuid();
    free_label.coordinate_wgs84.longitude_deg = 8.222222;
    free_label.coordinate_wgs84.latitude_deg = 50.722222;
    free_label.text = QStringLiteral("Free label");
    network.map_labels.append(free_label);

    network.map_backdrop.enabled = true;
    network.map_backdrop.lower_left_wgs84.longitude_deg = 8.0;
    network.map_backdrop.lower_left_wgs84.latitude_deg = 50.5;
    network.map_backdrop.upper_right_wgs84.longitude_deg = 8.5;
    network.map_backdrop.upper_right_wgs84.latitude_deg = 51.0;
    network.map_backdrop.file = QStringLiteral("network-map.png");
    network.map_backdrop.offset_longitude_deg = 0.001;
    network.map_backdrop.offset_latitude_deg = -0.002;

    NativeSavedProject native(network);

    struct CoordinateExpectation
    {
        QString id;
        double x;
        double y;
    };
    const std::array<CoordinateExpectation, 3> coordinates = {{
        {network.nodes_junctions.first().id, 8.123456, 50.654321},
        {network.nodes_reservoirs.first().id, 8.223456, 50.754321},
        {network.nodes_tanks.first().id, 8.323456, 50.854321}
    }};
    for (const CoordinateExpectation &coordinate : coordinates)
    {
        double x = 0.0;
        double y = 0.0;
        checkEpanet(EN_getcoord(native.handle(), nodeIndex(native.handle(), coordinate.id), &x, &y), "EN_getcoord");
        context.expectNear(x, coordinate.x, NumericTolerance{1.0e-9, 0.0}, comparison("coordinate.x", "Node", coordinate.id.toStdString()));
        context.expectNear(y, coordinate.y, NumericTolerance{1.0e-9, 0.0}, comparison("coordinate.y", "Node", coordinate.id.toStdString()));
    }

    expectLinkVertices(native.handle(), network.links_pipes.first().id, network.links_pipes.first().vertices, context);
    expectLinkVertices(native.handle(), network.links_pumps.first().id, network.links_pumps.first().vertices, context);
    expectLinkVertices(native.handle(), valve.id, network.links_valves.last().vertices, context);

    const QString labels = sectionText(native.inpText(), QStringLiteral("LABELS"));
    context.expect(sectionContainsCommand(labels, QStringLiteral("8.111111 50.611111 \"Source label\" %1").arg(network.nodes_reservoirs.first().id)),
        "Generated [LABELS] must preserve anchored WGS84 labels");
    context.expect(sectionContainsCommand(labels, QStringLiteral("8.222222 50.722222 \"Free label\"")),
        "Generated [LABELS] must preserve unanchored WGS84 labels");

    const QString backdrop = sectionText(native.inpText(), QStringLiteral("BACKDROP"));
    context.expect(sectionContainsCommand(backdrop, QStringLiteral("DIMENSIONS 8 50.5 8.5 51")),
        "Generated [BACKDROP] must preserve WGS84 bounds");
    context.expect(sectionContainsCommand(backdrop, QStringLiteral("UNITS DEGREES")),
        "Generated [BACKDROP] must declare canonical WGS84 degree units");
    context.expect(sectionContainsCommand(backdrop, QStringLiteral("FILE network-map.png")),
        "Generated [BACKDROP] must preserve the image file name");
    context.expect(sectionContainsCommand(backdrop, QStringLiteral("OFFSET 0.001 -0.002")),
        "Generated [BACKDROP] must preserve WGS84 degree offsets");
}

void scenarioReportOptions(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.report_statistic = HydraulicSimulationReportStatistic::Maximum;
    HydraulicSimulationReportOptions &options = network.options_report;
    options.page_size = 77;
    options.status = HydraulicSimulationReportStatus::Full;
    options.summary = false;
    options.messages = false;
    options.energy = true;
    options.selection_nodes.mode = HydraulicSimulationReportSelectionMode::Selected;
    options.selection_nodes.uuids = {network.nodes_junctions.first().uuid};
    options.selection_links.mode = HydraulicSimulationReportSelectionMode::Selected;
    options.selection_links.uuids = {network.links_pipes.first().uuid};

    options.fields_node.elevation.enabled = false;
    options.fields_node.demand.precision = 4;
    options.fields_node.demand.below_m3_per_h = 1.25;
    options.fields_node.demand.above_m3_per_h = 99.75;
    options.fields_node.head.precision = 5;
    options.fields_node.pressure.enabled = false;
    options.fields_node.quality.enabled = false;

    options.fields_link.length.enabled = false;
    options.fields_link.diameter.precision = 3;
    options.fields_link.flow.below_m3_per_h = 2.5;
    options.fields_link.velocity.above_m_per_s = 0.75;
    options.fields_link.headloss.precision = 6;
    options.fields_link.position.enabled = false;
    options.fields_link.setting.precision = 4;
    options.fields_link.reaction.enabled = false;
    options.fields_link.friction.enabled = true;
    options.fields_link.friction.precision = 7;
    options.fields_link.friction.below_friction_factor = 0.01;
    options.fields_link.friction.above_friction_factor = 0.1;
    options.backend_commands.append(QStringLiteral("FLOW PRECISION 9"));
    options.backend_commands.append(QStringLiteral("F-FACTOR PRECISION 8"));

    NativeSavedProject native(network);
    const QString report = sectionText(native.inpText(), QStringLiteral("REPORT"));

    const std::array<QString, 26> expected_commands = {{
        QStringLiteral("PAGESIZE 77"),
        QStringLiteral("STATUS FULL"),
        QStringLiteral("SUMMARY NO"),
        QStringLiteral("ENERGY YES"),
        QStringLiteral("MESSAGES NO"),
        QStringLiteral("NODES 10"),
        QStringLiteral("LINKS 10"),
        QStringLiteral("Elevation NO"),
        QStringLiteral("Demand PRECISION 4"),
        QStringLiteral("Demand BELOW 1.250000"),
        QStringLiteral("Demand ABOVE 99.750000"),
        QStringLiteral("Head PRECISION 5"),
        QStringLiteral("Pressure NO"),
        QStringLiteral("Quality NO"),
        QStringLiteral("Length NO"),
        QStringLiteral("Diameter PRECISION 3"),
        QStringLiteral("Flow PRECISION 9"),
        QStringLiteral("Flow BELOW 2.500000"),
        QStringLiteral("Velocity ABOVE 0.750000"),
        QStringLiteral("Headloss PRECISION 6"),
        QStringLiteral("State NO"),
        QStringLiteral("Setting PRECISION 4"),
        QStringLiteral("Reaction NO"),
        QStringLiteral("F-FACTOR YES"),
        QStringLiteral("F-FACTOR PRECISION 8"),
        QStringLiteral("F-FACTOR BELOW 0.01")
    }};
    for (const QString &command : expected_commands)
        context.expect(sectionContainsCommand(report, command), (QStringLiteral("Generated [REPORT] must contain command: ") + command).toStdString());


    context.expect(sectionContainsCommand(report, QStringLiteral("F-FACTOR ABOVE 0.10000000000000001"))
            || sectionContainsCommand(report, QStringLiteral("F-FACTOR ABOVE 0.1")),
        "Generated [REPORT] must preserve the F-Factor upper limit");
    context.expect(!sectionContainsCommand(report, QStringLiteral("F-FACTOR PRECISION 7")),
        "Backend report commands must remain the final authority for the generated F-Factor configuration");

    long native_statistic = -1;
    checkEpanet(EN_gettimeparam(native.handle(), EN_STATISTIC, &native_statistic), "EN_gettimeparam(EN_STATISTIC)");
    context.expectEqual(static_cast<std::int64_t>(native_statistic), static_cast<std::int64_t>(EN_MAXIMUM), comparison("report.statistic"));

    // Construction of NativeSavedProject has already reopened the generated INP with
    // native EPANET. Exercise its parser/solver as an additional end-to-end check.
    checkEpanet(EN_solveH(native.handle()), "EN_solveH(report-options generated INP)");
}
}

namespace AowisEpanetTests
{
void registerExportFidelityScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-export-native-reopen",
        "Generate an AOWIS INP, reopen it with native EPANET, and match a hydraulic result.",
        {"conformance", "hydraulic", "upstream", "export"},
        &scenarioGeneratedInpNativeReopen});
    registry.add(ScenarioDefinition{
        "conformance-export-titles-comments-tags",
        "Persist title lines and common node/link comments and tags through native EPANET reopen.",
        {"conformance", "hydraulic", "upstream", "export"},
        &scenarioTitlesCommentsTags});
    registry.add(ScenarioDefinition{
        "conformance-export-patterns-curves",
        "Persist pattern data/comments and every AOWIS curve family, including generic curves, through native reopen.",
        {"conformance", "hydraulic", "upstream", "export", "curve"},
        &scenarioPatternsCurves});
    registry.add(ScenarioDefinition{
        "conformance-export-coordinates-vertices",
        "Persist WGS84 node coordinates, link vertices, labels, and backdrop metadata through generated INP/native EPANET reopen.",
        {"conformance", "hydraulic", "upstream", "export", "coordinate"},
        &scenarioCoordinatesVertices});
    registry.add(ScenarioDefinition{
        "conformance-export-report-options",
        "Persist general, selection, and typed report options in a native-reopenable generated INP.",
        {"conformance", "hydraulic", "upstream", "export"},
        &scenarioReportOptions});
}
}
