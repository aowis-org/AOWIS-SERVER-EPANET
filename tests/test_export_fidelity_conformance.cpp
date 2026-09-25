#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/net1_fixture.h"
#include "conformance/export_fidelity_scenarios.h"

#include "../src/lib/internal/epanet_msx_exporter.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
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
        initialize(AowisEpanetTests::makeRunRequest(network));
    }

    NativeSavedProject(const NetworkHydraulic &network, const WaterQualitySolverOptions &quality_options)
    {
        initialize(AowisEpanetTests::makeRunRequest(network, quality_options));
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
    void initialize(const EpanetRunRequest &request)
    {
        const EpanetResultInp result = EpanetRunner().retrieveInp(request);
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

    const EpanetResultRun wrapper = EpanetRunner().run(AowisEpanetTests::makeRunRequest(network));
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

void scenarioCoordinatesWithoutBackdropRoundTrip(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.map_backdrop.enabled = false;
    network.nodes_junctions.first().coordinate_wgs84.longitude_deg = 18.192000;
    network.nodes_junctions.first().coordinate_wgs84.latitude_deg = 11.981190;
    network.nodes_reservoirs.first().coordinate_wgs84.longitude_deg = 18.190800;
    network.nodes_reservoirs.first().coordinate_wgs84.latitude_deg = 11.981190;
    network.nodes_tanks.first().coordinate_wgs84.longitude_deg = 18.195000;
    network.nodes_tanks.first().coordinate_wgs84.latitude_deg = 11.979580;

    NativeSavedProject native(network);
    const QString backdrop = sectionText(native.inpText(), QStringLiteral("BACKDROP"));
    context.expect(sectionContainsCommand(backdrop, QStringLiteral("UNITS DEGREES")),
        "Generated INP without an active backdrop must still declare WGS84 degree map units");
    context.expect(!backdrop.contains(QStringLiteral("DIMENSIONS"), Qt::CaseInsensitive),
        "Disabled backdrop must not export backdrop dimensions");
    context.expect(!backdrop.contains(QStringLiteral("FILE"), Qt::CaseInsensitive),
        "Disabled backdrop must not export a backdrop file");
    context.expect(!backdrop.contains(QStringLiteral("OFFSET"), Qt::CaseInsensitive),
        "Disabled backdrop must not export backdrop offsets");

    QTemporaryDir directory;
    context.expect(directory.isValid(), "Coordinate round-trip temporary directory must be available");
    if (!directory.isValid())
        return;

    const QString input_path = directory.filePath(QStringLiteral("coordinates-without-backdrop.inp"));
    QFile input_file(input_path);
    context.expect(input_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Generated coordinate round-trip INP must be writable");
    if (!input_file.isOpen())
        return;
    const QByteArray input_text = native.inpText().toUtf8();
    const qint64 bytes_written = input_file.write(input_text);
    context.expect(bytes_written == input_text.size(),
        "Generated coordinate round-trip INP must be written completely");
    input_file.close();
    if (bytes_written != input_text.size())
        return;

    const EpanetResultImport imported = EpanetRunner().importInp(input_path);
    context.expect(imported.status.success,
        "Generated INP without an active backdrop must re-import successfully");
    if (!imported.status.success)
        return;

    context.expect(imported.source_geometry.units_declared,
        "Round-tripped coordinate geometry must retain an explicit map-unit declaration");
    context.expectEqual(static_cast<std::int64_t>(imported.source_geometry.units),
        static_cast<std::int64_t>(EpanetImportMapUnits::Degrees),
        comparison("source_geometry.units"));
    context.expect(!imported.request.network.map_backdrop.enabled,
        "UNITS DEGREES metadata alone must not enable an AOWIS map backdrop");

    context.expectNear(imported.request.network.nodes_junctions.first().coordinate_wgs84.longitude_deg,
        18.192000, NumericTolerance{1.0e-9, 0.0}, comparison("longitude_deg", "Junction", network.nodes_junctions.first().id.toStdString()));
    context.expectNear(imported.request.network.nodes_junctions.first().coordinate_wgs84.latitude_deg,
        11.981190, NumericTolerance{1.0e-9, 0.0}, comparison("latitude_deg", "Junction", network.nodes_junctions.first().id.toStdString()));
    context.expectNear(imported.request.network.nodes_reservoirs.first().coordinate_wgs84.longitude_deg,
        18.190800, NumericTolerance{1.0e-9, 0.0}, comparison("longitude_deg", "Reservoir", network.nodes_reservoirs.first().id.toStdString()));
    context.expectNear(imported.request.network.nodes_reservoirs.first().coordinate_wgs84.latitude_deg,
        11.981190, NumericTolerance{1.0e-9, 0.0}, comparison("latitude_deg", "Reservoir", network.nodes_reservoirs.first().id.toStdString()));
    context.expectNear(imported.request.network.nodes_tanks.first().coordinate_wgs84.longitude_deg,
        18.195000, NumericTolerance{1.0e-9, 0.0}, comparison("longitude_deg", "Tank", network.nodes_tanks.first().id.toStdString()));
    context.expectNear(imported.request.network.nodes_tanks.first().coordinate_wgs84.latitude_deg,
        11.979580, NumericTolerance{1.0e-9, 0.0}, comparison("latitude_deg", "Tank", network.nodes_tanks.first().id.toStdString()));
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

void scenarioQualityInputNone(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    NativeSavedProject native(network);
    int quality_type = -1;
    int trace_node = -1;
    checkEpanet(EN_getqualtype(native.handle(), &quality_type, &trace_node), "EN_getqualtype(none)");
    context.expectEqual(static_cast<std::int64_t>(quality_type), static_cast<std::int64_t>(EN_NONE), comparison("quality.analysis"));
    context.expectEqual(static_cast<std::int64_t>(trace_node), std::int64_t{0}, comparison("quality.trace_node"));
}

void scenarioInpRejectsMultipleQualityAnalyses(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    WaterQualitySolverOptions chemical;
    chemical.analysis = WaterQualityAnalysisType::Chemical;
    chemical.chemical_name = QStringLiteral("Chlorine");

    WaterQualitySolverOptions water_age;
    water_age.analysis = WaterQualityAnalysisType::WaterAge;

    EpanetRunRequest request = AowisEpanetTests::makeRunRequest(network);
    request.quality_runs = {chemical, water_age};
    const EpanetResultInp result = EpanetRunner().retrieveInp(request);

    context.expect(!result.status.success, "INP export must reject requests containing multiple quality analyses");
    context.expect(result.inp_text.isEmpty(), "rejected multi-quality INP export must not return partial INP text");
    context.expect(result.status.stage == HydraulicSimulationStatusStage::ConfigureOptions, "multi-quality INP rejection must identify the configuration stage");
    context.expect(result.status.operation == HydraulicSimulationStatusOperation::ConfigureQuality, "multi-quality INP rejection must identify quality configuration");
}

void scenarioQualityTankMixingModels(TestContext &context)
{
    struct MixingCase
    {
        HydraulicNodeTankMixingModel model;
        int backend_model;
        double fraction;
    };

    const std::array<MixingCase, 4> cases = {{
        {HydraulicNodeTankMixingModel::CompleteMix, EN_MIX1, 1.0},
        {HydraulicNodeTankMixingModel::TwoCompartment, EN_MIX2, 0.4},
        {HydraulicNodeTankMixingModel::FirstInFirstOut, EN_FIFO, 1.0},
        {HydraulicNodeTankMixingModel::LastInFirstOut, EN_LIFO, 1.0}
    }};

    for (const MixingCase &mixing_case : cases)
    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.nodes_tanks.isEmpty(), "tank-mixing fixture requires a tank");
        if (network.nodes_tanks.isEmpty())
            return;

        WaterQualitySolverOptions quality_options;
        quality_options.analysis = WaterQualityAnalysisType::Chemical;
        quality_options.chemical_name = QStringLiteral("Chlorine");
        HydraulicNodeTank &tank = network.nodes_tanks.first();
        tank.mixing_model = mixing_case.model;
        tank.mixing_fraction = mixing_case.fraction;

        NativeSavedProject native(network, quality_options);
        double value = 0.0;
        const int tank_index = nodeIndex(native.handle(), tank.id);
        checkEpanet(EN_getnodevalue(native.handle(), tank_index, EN_MIXMODEL, &value), "EN_getnodevalue(EN_MIXMODEL mapping)");
        context.expectEqual(static_cast<std::int64_t>(value), static_cast<std::int64_t>(mixing_case.backend_model), comparison("tank.mixing_model", "Tank", tank.id.toStdString()));
        if (mixing_case.model == HydraulicNodeTankMixingModel::TwoCompartment)
        {
            checkEpanet(EN_getnodevalue(native.handle(), tank_index, EN_MIXFRACTION, &value), "EN_getnodevalue(EN_MIXFRACTION mapping)");
            context.expectNear(value, mixing_case.fraction, NumericTolerance{1.0e-12, 1.0e-9}, comparison("tank.mixing_fraction", "Tank", tank.id.toStdString()));
        }
    }
}

void scenarioQualityReactionMapping(TestContext &context)
{
    struct RoughnessCase
    {
        HydraulicHeadlossFormula formula;
        double roughness_hazen_williams;
        double roughness_darcy_weisbach_mm;
        double roughness_chezy_manning;
        double diameter_mm;
        double roughness_factor;
        double global_wall_coefficient;
        double expected_wall_coefficient;
    };

    const double darcy_roughness_mm = 0.25;
    const double darcy_diameter_mm = 250.0;
    const std::array<RoughnessCase, 4> cases = {{
        {HydraulicHeadlossFormula::HazenWilliams, 130.0, 0.0, 0.0, 250.0, -2.6, -0.1, -2.6 / 130.0},
        {HydraulicHeadlossFormula::DarcyWeisbach, 0.0, darcy_roughness_mm, 0.0, darcy_diameter_mm, -2.6, -0.1, -2.6 / std::abs(std::log(darcy_roughness_mm / darcy_diameter_mm))},
        {HydraulicHeadlossFormula::ChezyManning, 0.0, 0.0, 0.013, 250.0, -2.6, -0.1, -2.6 * 0.013},
        {HydraulicHeadlossFormula::HazenWilliams, 130.0, 0.0, 0.0, 250.0, 0.0, -0.12, -0.12}
    }};

    for (const RoughnessCase &roughness_case : cases)
    {
        NetworkHydraulic network = cleanNet1();
        context.expect(!network.links_pipes.isEmpty(), "reaction-mapping fixture requires a pipe");
        context.expect(!network.nodes_tanks.isEmpty(), "reaction-mapping fixture requires a tank");
        if (network.links_pipes.isEmpty() || network.nodes_tanks.isEmpty())
            return;

        WaterQualitySolverOptions quality_options;
        quality_options.analysis = WaterQualityAnalysisType::Chemical;
        quality_options.chemical_name = QStringLiteral("Chlorine");
        network.options_hydraulic.headloss_formula = roughness_case.formula;
        network.options_reaction.roughness_reaction_factor = roughness_case.roughness_factor;
        network.options_reaction.global_pipe_wall_reaction.coefficient = roughness_case.global_wall_coefficient;
        network.options_reaction.global_tank_bulk_reaction.coefficient = -0.33;

        HydraulicLinkPipe &pipe = network.links_pipes.first();
        pipe.override_bulk_reaction = false;
        pipe.override_wall_reaction = false;
        pipe.roughness_hazen_williams = roughness_case.roughness_hazen_williams;
        pipe.roughness_darcy_weisbach_mm = roughness_case.roughness_darcy_weisbach_mm;
        pipe.roughness_chezy_manning = roughness_case.roughness_chezy_manning;
        pipe.diameter_mm = roughness_case.diameter_mm;

        HydraulicNodeTank &tank = network.nodes_tanks.first();
        tank.override_bulk_reaction = false;

        NativeSavedProject native(network, quality_options);
        double value = 0.0;
        checkEpanet(EN_getlinkvalue(native.handle(), linkIndex(native.handle(), pipe.id), EN_KWALL, &value), "EN_getlinkvalue(EN_KWALL reaction mapping)");
        // EN_saveinpfile serializes reaction coefficients to six decimal places, so
        // native reopen parity cannot assert tighter absolute precision than the INP representation.
        context.expectNear(value, roughness_case.expected_wall_coefficient, NumericTolerance{1.0e-6, 1.0e-8}, comparison("pipe.wall_reaction.coefficient", "Pipe", pipe.id.toStdString()));
        checkEpanet(EN_getnodevalue(native.handle(), nodeIndex(native.handle(), tank.id), EN_TANK_KBULK, &value), "EN_getnodevalue(EN_TANK_KBULK global)");
        context.expectNear(value, -0.33, NumericTolerance{1.0e-12, 1.0e-9}, comparison("tank.bulk_reaction.global", "Tank", tank.id.toStdString()));
    }
}

void scenarioQualityInputChemical(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    WaterQualitySolverOptions quality_options;
    quality_options.analysis = WaterQualityAnalysisType::Chemical;
    quality_options.chemical_name = QStringLiteral("Chlorine");
    quality_options.chemical_tolerance_mg_per_l = 0.004;
    quality_options.relative_diffusivity = 1.3;
    network.timestep_quality_s = 180;

    network.options_reaction.global_pipe_bulk_reaction.coefficient = -0.2;
    network.options_reaction.global_pipe_bulk_reaction.order = 1.2;
    network.options_reaction.global_pipe_wall_reaction.coefficient = -0.1;
    network.options_reaction.global_pipe_wall_reaction.order = 1.0;
    network.options_reaction.global_tank_bulk_reaction.coefficient = -0.3;
    network.options_reaction.global_tank_bulk_reaction.order = 0.8;
    network.options_reaction.limiting_concentration_mg_per_l = 0.15;
    network.options_reaction.roughness_reaction_factor = -2.6;

    HydraulicPatternTime source_pattern;
    source_pattern.id = QStringLiteral("QPat");
    source_pattern.uuid = QUuid::createUuid();
    source_pattern.multipliers = {1.0, 1.25};
    network.patterns_time.append(source_pattern);

    context.expect(network.nodes_junctions.size() >= 3, "Net1 fixture should expose at least three junctions for quality-source mapping");
    context.expect(!network.nodes_reservoirs.isEmpty(), "Net1 fixture should expose a reservoir for quality-source mapping");
    context.expect(!network.nodes_tanks.isEmpty(), "Net1 fixture should expose a tank for quality mapping");
    context.expect(network.links_pipes.size() >= 3, "Net1 fixture should expose at least three pipes for independent reaction override mapping");
    if (network.nodes_junctions.size() < 3 || network.nodes_reservoirs.isEmpty() || network.nodes_tanks.isEmpty() || network.links_pipes.size() < 3)
        return;

    HydraulicNodeReservoir &reservoir = network.nodes_reservoirs.first();
    reservoir.initial_chemical_concentration_mg_per_l = 0.9;
    reservoir.quality_source.type = HydraulicNodeQualitySourceType::Concentration;
    reservoir.quality_source.chemical_concentration_mg_per_l = 1.1;
    reservoir.quality_source.pattern_uuid = source_pattern.uuid;

    HydraulicNodeJunction &mass = network.nodes_junctions[0];
    mass.initial_chemical_concentration_mg_per_l = 0.25;
    mass.quality_source.type = HydraulicNodeQualitySourceType::MassBooster;
    mass.quality_source.chemical_mass_flow_mg_per_min = 12.0;

    HydraulicNodeJunction &flow_paced = network.nodes_junctions[1];
    flow_paced.quality_source.type = HydraulicNodeQualitySourceType::FlowPacedBooster;
    flow_paced.quality_source.chemical_concentration_mg_per_l = 0.35;

    HydraulicNodeJunction &setpoint = network.nodes_junctions[2];
    setpoint.quality_source.type = HydraulicNodeQualitySourceType::SetpointBooster;
    setpoint.quality_source.chemical_concentration_mg_per_l = 0.8;

    HydraulicNodeTank &tank = network.nodes_tanks.first();
    tank.bottom_elevation_m = 0.0;
    tank.water_level_initial_m = 10.0;
    tank.water_level_minimum_m = 0.0;
    tank.water_level_maximum_m = 20.0;
    tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;
    tank.diameter_m = 10.0;
    tank.minimum_volume_m3 = 0.0;
    tank.volume_curve_uuid = {};
    tank.initial_chemical_concentration_mg_per_l = 0.45;
    tank.mixing_model = HydraulicNodeTankMixingModel::TwoCompartment;
    tank.mixing_fraction = 0.65;
    tank.override_bulk_reaction = true;
    tank.bulk_reaction.coefficient = -0.55;
    tank.bulk_reaction.order = network.options_reaction.global_tank_bulk_reaction.order;

    HydraulicLinkPipe &global_pipe = network.links_pipes[0];
    global_pipe.override_bulk_reaction = false;
    global_pipe.override_wall_reaction = false;

    HydraulicLinkPipe &bulk_override_pipe = network.links_pipes[1];
    bulk_override_pipe.override_bulk_reaction = true;
    bulk_override_pipe.override_wall_reaction = false;
    bulk_override_pipe.bulk_reaction.coefficient = -0.7;
    bulk_override_pipe.bulk_reaction.order = network.options_reaction.global_pipe_bulk_reaction.order;

    HydraulicLinkPipe &wall_override_pipe = network.links_pipes[2];
    wall_override_pipe.override_bulk_reaction = false;
    wall_override_pipe.override_wall_reaction = true;
    wall_override_pipe.wall_reaction.coefficient = -0.4;
    wall_override_pipe.wall_reaction.order = network.options_reaction.global_pipe_wall_reaction.order;

    NativeSavedProject native(network, quality_options);
    int quality_type = -1;
    int trace_node = -1;
    std::array<char, EN_MAXID + 1> chemical_name{};
    std::array<char, EN_MAXID + 1> chemical_units{};
    checkEpanet(EN_getqualinfo(native.handle(), &quality_type, chemical_name.data(), chemical_units.data(), &trace_node), "EN_getqualinfo");
    context.expectEqual(static_cast<std::int64_t>(quality_type), static_cast<std::int64_t>(EN_CHEM), comparison("quality.analysis"));
    context.expectEqual(std::string_view(chemical_name.data()), std::string_view("Chlorine"), comparison("quality.chemical_name"));
    context.expectEqual(std::string_view(chemical_units.data()), std::string_view("mg/L"), comparison("quality.chemical_units"));

    double value = 0.0;
    checkEpanet(EN_getoption(native.handle(), EN_TOLERANCE, &value), "EN_getoption(EN_TOLERANCE)");
    context.expectNear(value, 0.004, NumericTolerance{1.0e-12, 1.0e-9}, comparison("quality.tolerance"));
    checkEpanet(EN_getoption(native.handle(), EN_SP_DIFFUS, &value), "EN_getoption(EN_SP_DIFFUS)");
    context.expectNear(value, 1.3, NumericTolerance{1.0e-12, 1.0e-9}, comparison("quality.relative_diffusivity"));
    checkEpanet(EN_getoption(native.handle(), EN_BULKORDER, &value), "EN_getoption(EN_BULKORDER)");
    context.expectNear(value, 1.2, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.bulk_order"));
    checkEpanet(EN_getoption(native.handle(), EN_WALLORDER, &value), "EN_getoption(EN_WALLORDER)");
    context.expectNear(value, 1.0, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.wall_order"));
    checkEpanet(EN_getoption(native.handle(), EN_TANKORDER, &value), "EN_getoption(EN_TANKORDER)");
    context.expectNear(value, 0.8, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.tank_order"));
    checkEpanet(EN_getoption(native.handle(), EN_CONCENLIMIT, &value), "EN_getoption(EN_CONCENLIMIT)");
    context.expectNear(value, 0.15, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.limiting_concentration_mg_per_l"));

    long quality_step = 0;
    checkEpanet(EN_gettimeparam(native.handle(), EN_QUALSTEP, &quality_step), "EN_gettimeparam(EN_QUALSTEP)");
    context.expectEqual(static_cast<std::int64_t>(quality_step), std::int64_t{180}, comparison("quality.timestep_s"));

    const int reservoir_index = nodeIndex(native.handle(), reservoir.id);
    checkEpanet(EN_getnodevalue(native.handle(), reservoir_index, EN_INITQUAL, &value), "EN_getnodevalue(EN_INITQUAL reservoir)");
    context.expectNear(value, 0.9, NumericTolerance{1.0e-10, 1.0e-9}, comparison("initial_quality", "Reservoir", reservoir.id.toStdString()));
    checkEpanet(EN_getnodevalue(native.handle(), reservoir_index, EN_SOURCETYPE, &value), "EN_getnodevalue(EN_SOURCETYPE reservoir)");
    context.expectEqual(static_cast<std::int64_t>(value), static_cast<std::int64_t>(EN_CONCEN), comparison("source.type", "Reservoir", reservoir.id.toStdString()));
    checkEpanet(EN_getnodevalue(native.handle(), reservoir_index, EN_SOURCEQUAL, &value), "EN_getnodevalue(EN_SOURCEQUAL reservoir)");
    context.expectNear(value, 1.1, NumericTolerance{1.0e-12, 1.0e-9}, comparison("source.concentration_mg_per_l", "Reservoir", reservoir.id.toStdString()));
    checkEpanet(EN_getnodevalue(native.handle(), reservoir_index, EN_SOURCEPAT, &value), "EN_getnodevalue(EN_SOURCEPAT reservoir)");
    context.expectEqual(static_cast<std::int64_t>(value), static_cast<std::int64_t>(patternIndex(native.handle(), source_pattern.id)), comparison("source.pattern", "Reservoir", reservoir.id.toStdString()));

    const std::array<std::pair<const HydraulicNodeJunction *, int>, 3> source_nodes = {{
        {&mass, EN_MASS}, {&flow_paced, EN_FLOWPACED}, {&setpoint, EN_SETPOINT}
    }};
    for (const std::pair<const HydraulicNodeJunction *, int> &entry : source_nodes)
    {
        const int index = nodeIndex(native.handle(), entry.first->id);
        checkEpanet(EN_getnodevalue(native.handle(), index, EN_SOURCETYPE, &value), "EN_getnodevalue(EN_SOURCETYPE junction)");
        context.expectEqual(static_cast<std::int64_t>(value), static_cast<std::int64_t>(entry.second), comparison("source.type", "Junction", entry.first->id.toStdString()));
    }
    checkEpanet(EN_getnodevalue(native.handle(), nodeIndex(native.handle(), mass.id), EN_SOURCEQUAL, &value), "EN_getnodevalue(EN_SOURCEQUAL mass)");
    context.expectNear(value, 12.0, NumericTolerance{1.0e-12, 1.0e-9}, comparison("source.mass_mg_per_min", "Junction", mass.id.toStdString()));

    const int tank_index = nodeIndex(native.handle(), tank.id);
    checkEpanet(EN_getnodevalue(native.handle(), tank_index, EN_MIXMODEL, &value), "EN_getnodevalue(EN_MIXMODEL)");
    context.expectEqual(static_cast<std::int64_t>(value), static_cast<std::int64_t>(EN_MIX2), comparison("tank.mixing_model", "Tank", tank.id.toStdString()));
    checkEpanet(EN_getnodevalue(native.handle(), tank_index, EN_MIXFRACTION, &value), "EN_getnodevalue(EN_MIXFRACTION)");
    context.expectNear(value, 0.65, NumericTolerance{1.0e-12, 1.0e-9}, comparison("tank.mixing_fraction", "Tank", tank.id.toStdString()));
    checkEpanet(EN_getnodevalue(native.handle(), tank_index, EN_TANK_KBULK, &value), "EN_getnodevalue(EN_TANK_KBULK)");
    context.expectNear(value, -0.55, NumericTolerance{1.0e-12, 1.0e-9}, comparison("tank.bulk_reaction.coefficient", "Tank", tank.id.toStdString()));

    double initial_volume_before_tank_data_round_trip = 0.0;
    double initial_volume_after_tank_data_round_trip = 0.0;
    checkEpanet(
        EN_getnodevalue(native.handle(), tank_index, EN_INITVOLUME, &initial_volume_before_tank_data_round_trip),
        "EN_getnodevalue(EN_INITVOLUME before EN_settankdata)");
    checkEpanet(
        EN_settankdata(
            native.handle(),
            tank_index,
            tank.bottom_elevation_m,
            tank.water_level_initial_m,
            tank.water_level_minimum_m,
            tank.water_level_maximum_m,
            tank.diameter_m,
            tank.minimum_volume_m3,
            ""),
        "EN_settankdata identical cylindrical tank data");
    checkEpanet(
        EN_getnodevalue(native.handle(), tank_index, EN_INITVOLUME, &initial_volume_after_tank_data_round_trip),
        "EN_getnodevalue(EN_INITVOLUME after EN_settankdata)");
    context.expectNear(
        initial_volume_after_tank_data_round_trip,
        initial_volume_before_tank_data_round_trip,
        NumericTolerance{0.0, 0.0},
        comparison("tank.initial_volume.setter_round_trip", "Tank", tank.id.toStdString()),
        "EN_settankdata must preserve the parser-created cylindrical tank volume exactly when identical data is reapplied");

    const int global_pipe_index = linkIndex(native.handle(), global_pipe.id);
    checkEpanet(EN_getlinkvalue(native.handle(), global_pipe_index, EN_KBULK, &value), "EN_getlinkvalue(EN_KBULK global)");
    context.expectNear(value, -0.2, NumericTolerance{1.0e-12, 1.0e-9}, comparison("pipe.bulk_reaction.coefficient", "Pipe", global_pipe.id.toStdString()));
    checkEpanet(EN_getlinkvalue(native.handle(), global_pipe_index, EN_KWALL, &value), "EN_getlinkvalue(EN_KWALL roughness)");
    context.expectNear(value, -2.6 / global_pipe.roughness_hazen_williams, NumericTolerance{1.0e-10, 1.0e-8}, comparison("pipe.wall_reaction.roughness_correlated", "Pipe", global_pipe.id.toStdString()));

    const int bulk_override_pipe_index = linkIndex(native.handle(), bulk_override_pipe.id);
    checkEpanet(EN_getlinkvalue(native.handle(), bulk_override_pipe_index, EN_KBULK, &value), "EN_getlinkvalue(EN_KBULK bulk-only override)");
    context.expectNear(value, -0.7, NumericTolerance{1.0e-12, 1.0e-9}, comparison("pipe.bulk_reaction.override", "Pipe", bulk_override_pipe.id.toStdString()));
    checkEpanet(EN_getlinkvalue(native.handle(), bulk_override_pipe_index, EN_KWALL, &value), "EN_getlinkvalue(EN_KWALL bulk-only fallback)");
    context.expectNear(value, -2.6 / bulk_override_pipe.roughness_hazen_williams, NumericTolerance{1.0e-10, 1.0e-8}, comparison("pipe.wall_reaction.bulk_only_fallback", "Pipe", bulk_override_pipe.id.toStdString()));

    const int wall_override_pipe_index = linkIndex(native.handle(), wall_override_pipe.id);
    checkEpanet(EN_getlinkvalue(native.handle(), wall_override_pipe_index, EN_KBULK, &value), "EN_getlinkvalue(EN_KBULK wall-only fallback)");
    context.expectNear(value, -0.2, NumericTolerance{1.0e-12, 1.0e-9}, comparison("pipe.bulk_reaction.wall_only_fallback", "Pipe", wall_override_pipe.id.toStdString()));
    checkEpanet(EN_getlinkvalue(native.handle(), wall_override_pipe_index, EN_KWALL, &value), "EN_getlinkvalue(EN_KWALL wall-only override)");
    context.expectNear(value, -0.4, NumericTolerance{1.0e-12, 1.0e-9}, comparison("pipe.wall_reaction.override", "Pipe", wall_override_pipe.id.toStdString()));

    checkEpanet(EN_setoption(native.handle(), EN_BULKORDER, -1.0), "EN_setoption(EN_BULKORDER negative)");
    checkEpanet(EN_getoption(native.handle(), EN_BULKORDER, &value), "EN_getoption(EN_BULKORDER negative)");
    context.expectNear(value, -1.0, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.bulk_order.negative"));
    checkEpanet(EN_setoption(native.handle(), EN_TANKORDER, -1.0), "EN_setoption(EN_TANKORDER negative)");
    checkEpanet(EN_getoption(native.handle(), EN_TANKORDER, &value), "EN_getoption(EN_TANKORDER negative)");
    context.expectNear(value, -1.0, NumericTolerance{1.0e-12, 1.0e-9}, comparison("reaction.tank_order.negative"));
}

void scenarioQualityInputWaterAge(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    WaterQualitySolverOptions quality_options;
    quality_options.analysis = WaterQualityAnalysisType::WaterAge;
    quality_options.water_age_tolerance_h = 0.025;
    network.nodes_junctions.first().initial_water_age_h = 2.5;

    NativeSavedProject native(network, quality_options);
    int quality_type = -1;
    int trace_node = -1;
    checkEpanet(EN_getqualtype(native.handle(), &quality_type, &trace_node), "EN_getqualtype(age)");
    context.expectEqual(static_cast<std::int64_t>(quality_type), static_cast<std::int64_t>(EN_AGE), comparison("quality.analysis"));
    double value = 0.0;
    checkEpanet(EN_getoption(native.handle(), EN_TOLERANCE, &value), "EN_getoption(EN_TOLERANCE age)");
    context.expectNear(value, 0.025, NumericTolerance{1.0e-12, 1.0e-9}, comparison("quality.water_age_tolerance_h"));
    const HydraulicNodeJunction &junction = network.nodes_junctions.first();
    checkEpanet(EN_getnodevalue(native.handle(), nodeIndex(native.handle(), junction.id), EN_INITQUAL, &value), "EN_getnodevalue(EN_INITQUAL age)");
    context.expectNear(value, 2.5, NumericTolerance{1.0e-12, 1.0e-9}, comparison("initial_water_age_h", "Junction", junction.id.toStdString()));
}

void scenarioQualityInputSourceTrace(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeReservoir &trace_source = network.nodes_reservoirs.first();
    WaterQualitySolverOptions quality_options;
    quality_options.analysis = WaterQualityAnalysisType::SourceTrace;
    quality_options.trace_node_uuid = trace_source.uuid;
    quality_options.source_trace_tolerance_percent = 0.2;

    NativeSavedProject native(network, quality_options);
    int quality_type = -1;
    int trace_node = -1;
    checkEpanet(EN_getqualtype(native.handle(), &quality_type, &trace_node), "EN_getqualtype(trace)");
    context.expectEqual(static_cast<std::int64_t>(quality_type), static_cast<std::int64_t>(EN_TRACE), comparison("quality.analysis"));
    context.expectEqual(static_cast<std::int64_t>(trace_node), static_cast<std::int64_t>(nodeIndex(native.handle(), trace_source.id)), comparison("quality.trace_node"));
    double value = 0.0;
    checkEpanet(EN_getoption(native.handle(), EN_TOLERANCE, &value), "EN_getoption(EN_TOLERANCE trace)");
    context.expectNear(value, 0.2, NumericTolerance{1.0e-12, 1.0e-9}, comparison("quality.source_trace_tolerance_percent"));
    const HydraulicNodeJunction &junction = network.nodes_junctions.first();
    checkEpanet(EN_getnodevalue(native.handle(), nodeIndex(native.handle(), junction.id), EN_INITQUAL, &value), "EN_getnodevalue(EN_INITQUAL trace)");
    context.expectNear(value, 0.0, NumericTolerance{1.0e-12, 1.0e-9}, comparison("source_trace_initqual", "Junction", junction.id.toStdString()), "source-trace mode must not expose an arbitrary per-node initial trace input");
}

void scenarioMsxExportBasicSections(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeJunction &junction = network.nodes_junctions.first();
    const HydraulicLinkPipe &pipe = network.links_pipes.first();
    network.multi_species.options.timestep_s = 0.125;
    network.multi_species.options.maximum_segments = 750;
    network.multi_species.options.peclet_number_threshold = 250.0;

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    chlorine.type = MultiSpeciesSpeciesType::Bulk;
    chlorine.units = MultiSpeciesUnits::Milligrams;
    chlorine.molecular_diffusivity_m2_per_s = 1.198449216e-9;
    network.multi_species.species.append(chlorine);

    MultiSpeciesSpecies tracer;
    tracer.id = QStringLiteral("TR");
    tracer.uuid = QUuid::createUuid();
    tracer.type = MultiSpeciesSpeciesType::Bulk;
    tracer.units = MultiSpeciesUnits::Millimoles;
    network.multi_species.species.append(tracer);

    MultiSpeciesSpecies microgram_species;
    microgram_species.id = QStringLiteral("UGS");
    microgram_species.uuid = QUuid::createUuid();
    microgram_species.type = MultiSpeciesSpeciesType::Bulk;
    microgram_species.units = MultiSpeciesUnits::Micrograms;
    network.multi_species.species.append(microgram_species);

    MultiSpeciesConstant kb;
    kb.id = QStringLiteral("Kb");
    kb.uuid = QUuid::createUuid();
    kb.value = 0.5;
    network.multi_species.constants.append(kb);

    MultiSpeciesTerm term;
    term.id = QStringLiteral("T1");
    term.uuid = QUuid::createUuid();
    term.expression = QStringLiteral("Kb * CL2");
    network.multi_species.terms.append(term);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = chlorine.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("-T1");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = chlorine.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("-Kb * CL2");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesPattern pattern;
    pattern.id = QStringLiteral("SRC1");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers = {1.0, 1.0, 0.5};
    network.multi_species.patterns.append(pattern);

    MultiSpeciesNodeSource source;
    source.node_uuid = junction.uuid;
    source.species_uuid = chlorine.uuid;
    source.type = MultiSpeciesSourceType::Concentration;
    source.value = 1.2;
    source.pattern_uuid = pattern.uuid;
    network.multi_species.sources.append(source);

    MultiSpeciesGlobalInitialQuality global_initial;
    global_initial.species_uuid = chlorine.uuid;
    global_initial.value = 0.8;
    network.multi_species.initial_quality_global.append(global_initial);

    MultiSpeciesGlobalInitialQuality microgram_initial;
    microgram_initial.species_uuid = microgram_species.uuid;
    microgram_initial.value = 0.002;
    network.multi_species.initial_quality_global.append(microgram_initial);

    MultiSpeciesNodeSource microgram_source;
    microgram_source.node_uuid = junction.uuid;
    microgram_source.species_uuid = microgram_species.uuid;
    microgram_source.type = MultiSpeciesSourceType::Mass;
    microgram_source.value = 0.003;
    network.multi_species.sources.append(microgram_source);

    MultiSpeciesPipeInitialQuality pipe_initial;
    pipe_initial.pipe_uuid = pipe.uuid;
    pipe_initial.species_uuid = chlorine.uuid;
    pipe_initial.value = 0.9;
    network.multi_species.initial_quality_pipes.append(pipe_initial);

    MultiSpeciesParameter param;
    param.id = QStringLiteral("Kw");
    param.uuid = QUuid::createUuid();
    param.default_value = 1.0;
    network.multi_species.parameters.append(param);

    MultiSpeciesParameterOverridePipe param_override;
    param_override.pipe_uuid = pipe.uuid;
    param_override.parameter_uuid = param.uuid;
    param_override.value = 2.5;
    network.multi_species.parameter_overrides_pipes.append(param_override);

    QString msx_text;
    const HydraulicSimulationStatus status = retrieveEpanetMsxText(network, MultiSpeciesRunOptions{}, msx_text);

    context.expect(status.success, "a well-formed multi-species model must export successfully");
    context.expect(msx_text.contains(QStringLiteral("[SPECIES]")), "export must contain a [SPECIES] section");
    context.expect(msx_text.contains(QStringLiteral("BULK CL2 MG")), "export must declare the bulk species with its units");
    context.expect(msx_text.contains(QStringLiteral("BULK TR MMOL")), "export must support EPANET-MSX millimole species units");
    context.expect(msx_text.contains(QStringLiteral("TIMESTEP 0.125")), "export must preserve fractional-second MSX timesteps");
    context.expect(msx_text.contains(QStringLiteral("SEGMENTS 750")), "export must write the MSX maximum segment count");
    context.expect(msx_text.contains(QStringLiteral("PECLET 250")), "export must write the MSX Peclet-number threshold");
    context.expect(msx_text.contains(QStringLiteral("[DIFFUSIVITY]")), "export must write a [DIFFUSIVITY] section when dispersion is configured");
    context.expect(msx_text.contains(QStringLiteral("CL2 1")), "export must convert canonical molecular diffusivity to the MSX relative diffusivity representation");
    context.expect(msx_text.contains(QStringLiteral("CONSTANT Kb 0.5")), "export must declare the constant coefficient");
    context.expect(msx_text.contains(QStringLiteral("T1 Kb * CL2")), "export must declare the term expression");
    context.expect(msx_text.contains(QStringLiteral("RATE CL2 -T1")), "export must declare the pipe reaction under [PIPES]");
    context.expect(msx_text.contains(QStringLiteral("RATE CL2 -Kb * CL2")), "export must declare the tank reaction under [TANKS]");
    context.expect(msx_text.contains(QStringLiteral("CONC %1 CL2 1.2 SRC1").arg(junction.id)), "export must declare the node source with its pattern");
    context.expect(msx_text.contains(QStringLiteral("GLOBAL CL2 0.8")), "export must declare global initial quality");
    context.expect(msx_text.contains(QStringLiteral("GLOBAL UGS 2")), "export must convert canonical mg/L input to configured microgram solver units");
    context.expect(msx_text.contains(QStringLiteral("MASS %1 UGS 3").arg(junction.id)), "export must convert canonical mg/min source strength to configured microgram solver units");
    context.expect(msx_text.contains(QStringLiteral("LINK %1 CL2 0.9").arg(pipe.id)), "export must declare pipe initial quality");
    context.expect(msx_text.contains(QStringLiteral("PIPE %1 Kw 2.5").arg(pipe.id)), "export must declare the per-pipe parameter override");
    context.expect(msx_text.contains(QStringLiteral("SRC1 1 1 0.5")), "export must declare the source pattern's multipliers");
    context.expect(msx_text.trimmed().endsWith(QStringLiteral("[END]")), "export must terminate with [END]");

    const int species_index = msx_text.indexOf(QStringLiteral("[SPECIES]"));
    const int pipes_index = msx_text.indexOf(QStringLiteral("[PIPES]"));
    const int sources_index = msx_text.indexOf(QStringLiteral("[SOURCES]"));
    context.expect(species_index >= 0 && pipes_index > species_index && sources_index > pipes_index, "sections must appear in the documented MSX order");
}

void scenarioMsxExportCanonicalSpeciesTolerances(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.multi_species.options.area_units = MultiSpeciesAreaUnits::SquareFeet;
    network.multi_species.options.default_absolute_tolerance = 0.01;
    network.multi_species.options.default_relative_tolerance = 0.001;

    MultiSpeciesSpecies milligram_default;
    milligram_default.id = QStringLiteral("MGDEF");
    milligram_default.uuid = QUuid::createUuid();
    milligram_default.type = MultiSpeciesSpeciesType::Bulk;
    milligram_default.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(milligram_default);

    MultiSpeciesSpecies microgram_default;
    microgram_default.id = QStringLiteral("UGDEF");
    microgram_default.uuid = QUuid::createUuid();
    microgram_default.type = MultiSpeciesSpeciesType::Bulk;
    microgram_default.units = MultiSpeciesUnits::Micrograms;
    network.multi_species.species.append(microgram_default);

    MultiSpeciesSpecies mole_default;
    mole_default.id = QStringLiteral("MOLDEF");
    mole_default.uuid = QUuid::createUuid();
    mole_default.type = MultiSpeciesSpeciesType::Bulk;
    mole_default.units = MultiSpeciesUnits::Moles;
    network.multi_species.species.append(mole_default);

    MultiSpeciesSpecies millimole_default;
    millimole_default.id = QStringLiteral("MMDEF");
    millimole_default.uuid = QUuid::createUuid();
    millimole_default.type = MultiSpeciesSpeciesType::Bulk;
    millimole_default.units = MultiSpeciesUnits::Millimoles;
    network.multi_species.species.append(millimole_default);

    MultiSpeciesSpecies wall_microgram_default;
    wall_microgram_default.id = QStringLiteral("UWALL");
    wall_microgram_default.uuid = QUuid::createUuid();
    wall_microgram_default.type = MultiSpeciesSpeciesType::Wall;
    wall_microgram_default.units = MultiSpeciesUnits::Micrograms;
    network.multi_species.species.append(wall_microgram_default);

    MultiSpeciesSpecies microgram_absolute_override;
    microgram_absolute_override.id = QStringLiteral("UGABS");
    microgram_absolute_override.uuid = QUuid::createUuid();
    microgram_absolute_override.type = MultiSpeciesSpeciesType::Bulk;
    microgram_absolute_override.units = MultiSpeciesUnits::Micrograms;
    microgram_absolute_override.absolute_tolerance = 0.002;
    network.multi_species.species.append(microgram_absolute_override);

    MultiSpeciesSpecies relative_override;
    relative_override.id = QStringLiteral("MGREL");
    relative_override.uuid = QUuid::createUuid();
    relative_override.type = MultiSpeciesSpeciesType::Bulk;
    relative_override.units = MultiSpeciesUnits::Milligrams;
    relative_override.relative_tolerance = 0.004;
    network.multi_species.species.append(relative_override);

    QString msx_text;
    const HydraulicSimulationStatus status = retrieveEpanetMsxText(network, MultiSpeciesRunOptions{}, msx_text);

    context.expect(status.success, "canonical multi-species tolerances must export successfully");
    context.expect(!msx_text.contains(QStringLiteral("\nATOL ")), "export must not map the canonical AOWIS absolute default to MSX's raw global ATOL option");
    context.expect(!msx_text.contains(QStringLiteral("\nRTOL ")), "export must materialize relative tolerance per species instead of relying on the MSX global RTOL option");
    context.expect(msx_text.contains(QStringLiteral("BULK MGDEF MG 0.01 0.001")), "milligram bulk species must receive the canonical defaults unchanged");
    context.expect(msx_text.contains(QStringLiteral("BULK UGDEF UG 10 0.001")), "microgram bulk species must receive the absolute default converted from canonical mg/L");
    context.expect(msx_text.contains(QStringLiteral("BULK MOLDEF MOLE 1e-05 0.001")), "mole bulk species must receive the absolute default converted from canonical mmol/L");
    context.expect(msx_text.contains(QStringLiteral("BULK MMDEF MMOL 0.01 0.001")), "millimole bulk species must receive the canonical amount-concentration default unchanged");
    context.expect(msx_text.contains(QStringLiteral("WALL UWALL UG 0.9290304 0.001")), "wall-species absolute tolerance must include both mass and configured area-unit conversion");
    context.expect(msx_text.contains(QStringLiteral("BULK UGABS UG 2 0.001")), "a species absolute override must be converted while independently inheriting the default relative tolerance");
    context.expect(msx_text.contains(QStringLiteral("BULK MGREL MG 0.01 0.004")), "a species relative override must independently inherit the default canonical absolute tolerance");
}

void scenarioMsxExportSpeciesSelection(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeJunction &junction = network.nodes_junctions.first();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    network.multi_species.species.append(chlorine);

    MultiSpeciesSpecies fluoride;
    fluoride.id = QStringLiteral("F");
    fluoride.uuid = QUuid::createUuid();
    network.multi_species.species.append(fluoride);

    MultiSpeciesReaction chlorine_reaction;
    chlorine_reaction.uuid = QUuid::createUuid();
    chlorine_reaction.species_uuid = chlorine.uuid;
    chlorine_reaction.location = MultiSpeciesReactionLocation::Pipe;
    chlorine_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    chlorine_reaction.expression = QStringLiteral("-0.5 * CL2");
    network.multi_species.reactions.append(chlorine_reaction);

    MultiSpeciesReaction fluoride_reaction;
    fluoride_reaction.uuid = QUuid::createUuid();
    fluoride_reaction.species_uuid = fluoride.uuid;
    fluoride_reaction.location = MultiSpeciesReactionLocation::Pipe;
    fluoride_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    fluoride_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(fluoride_reaction);

    MultiSpeciesNodeSource chlorine_source;
    chlorine_source.node_uuid = junction.uuid;
    chlorine_source.species_uuid = chlorine.uuid;
    chlorine_source.type = MultiSpeciesSourceType::Concentration;
    chlorine_source.value = 1.0;
    network.multi_species.sources.append(chlorine_source);

    MultiSpeciesNodeSource fluoride_source;
    fluoride_source.node_uuid = junction.uuid;
    fluoride_source.species_uuid = fluoride.uuid;
    fluoride_source.type = MultiSpeciesSourceType::Concentration;
    fluoride_source.value = 0.7;
    network.multi_species.sources.append(fluoride_source);

    MultiSpeciesConstant shared_constant;
    shared_constant.id = QStringLiteral("K1");
    shared_constant.uuid = QUuid::createUuid();
    shared_constant.value = 3.0;
    network.multi_species.constants.append(shared_constant);

    MultiSpeciesRunOptions run_options;
    run_options.output_species_uuids.append(chlorine.uuid);

    QString msx_text;
    const HydraulicSimulationStatus status = retrieveEpanetMsxText(network, run_options, msx_text);

    context.expect(status.success, "selecting one output species must still export successfully");
    context.expect(msx_text.contains(QStringLiteral("BULK CL2")), "the requested output species must appear in [SPECIES]");
    context.expect(msx_text.contains(QStringLiteral("BULK F ")), "an unrequested output species must remain in [SPECIES] so output filtering cannot alter chemistry");
    context.expect(msx_text.contains(QStringLiteral("RATE CL2 -0.5 * CL2")), "the requested output species' reaction must be exported");
    context.expect(msx_text.contains(QStringLiteral("RATE F 0")), "an unrequested output species' reaction must remain in the solved chemistry");
    context.expect(msx_text.contains(QStringLiteral("CONC %1 CL2 1").arg(junction.id)), "the requested output species' source must be exported");
    context.expect(msx_text.contains(QStringLiteral("CONC %1 F 0.7").arg(junction.id)), "an unrequested output species' source must remain in the solved chemistry");
    context.expect(msx_text.contains(QStringLiteral("CONSTANT K1 3")), "coefficients must remain in the complete MSX chemistry model");
}

void scenarioMsxExportRejectsUnknownSpeciesSelection(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    network.multi_species.species.append(chlorine);

    MultiSpeciesRunOptions run_options;
    run_options.output_species_uuids.append(QUuid::createUuid());

    QString msx_text;
    const HydraulicSimulationStatus status = retrieveEpanetMsxText(network, run_options, msx_text);

    context.expect(!status.success, "selecting an unresolved species UUID must be rejected");
    context.expect(msx_text.isEmpty(), "a rejected export must not return partial MSX text");
    context.expect(status.operation == HydraulicSimulationStatusOperation::ConfigureMultiSpecies, "the rejection must identify multi-species configuration");
    context.expect(status.entity.type == HydraulicSimulationStatusEntityType::MultiSpeciesSolver, "the rejection must identify the multi-species solver as the entity");
}

void scenarioMsxExportRejectsBrokenReactionReference(TestContext &context)
{
    NetworkHydraulic network = cleanNet1();

    MultiSpeciesReaction orphan_reaction;
    orphan_reaction.uuid = QUuid::createUuid();
    orphan_reaction.species_uuid = QUuid::createUuid();
    orphan_reaction.location = MultiSpeciesReactionLocation::Pipe;
    orphan_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    orphan_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(orphan_reaction);

    QString msx_text;
    const HydraulicSimulationStatus status = retrieveEpanetMsxText(network, MultiSpeciesRunOptions{}, msx_text);

    context.expect(!status.success, "a reaction referencing an unresolved species UUID must be rejected even though no species selection was requested");
    context.expect(msx_text.isEmpty(), "a rejected export must not return partial MSX text");
    context.expect(status.operation == HydraulicSimulationStatusOperation::ConfigureMultiSpecies, "the rejection must identify multi-species configuration");
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
        "conformance-quality-input-none",
        "Maps a request without a water-quality analysis into EPANET's no-quality mode.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityInputNone});
    registry.add(ScenarioDefinition{
        "conformance-export-multiple-quality-rejected",
        "Reject INP export requests containing more than one active water-quality analysis.",
        {"conformance", "quality", "export", "negative"},
        &scenarioInpRejectsMultipleQualityAnalyses});
    registry.add(ScenarioDefinition{
        "conformance-quality-input-tank-mixing-models",
        "Maps every supported tank water-quality mixing model into EPANET.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityTankMixingModels});
    registry.add(ScenarioDefinition{
        "conformance-quality-input-reactions",
        "Maps global, override, and roughness-correlated reaction coefficients for every hydraulic headloss formula.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityReactionMapping});
    registry.add(ScenarioDefinition{
        "conformance-quality-input-chemical",
        "Maps chemical quality analysis, all source types, tank mixing, and reactions into a native-reopenable EPANET project.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityInputChemical});
    registry.add(ScenarioDefinition{
        "conformance-quality-input-water-age",
        "Maps water-age analysis, initial age, and age tolerance into EPANET.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityInputWaterAge});
    registry.add(ScenarioDefinition{
        "conformance-quality-input-source-trace",
        "Maps source-trace analysis, trace-node reference, initial trace percentage, and tolerance into EPANET.",
        {"conformance", "quality", "mapping", "export"},
        &scenarioQualityInputSourceTrace});
    registry.add(ScenarioDefinition{
        "conformance-export-coordinates-without-backdrop-roundtrip",
        "Export WGS84 geometry without an active backdrop, declare degree units explicitly, and re-import at the original geographic coordinates.",
        {"conformance", "export", "import", "coordinate", "proof"},
        &scenarioCoordinatesWithoutBackdropRoundTrip});
    registry.add(ScenarioDefinition{
        "conformance-export-report-options",
        "Persist general, selection, and typed report options in a native-reopenable generated INP.",
        {"conformance", "hydraulic", "upstream", "export"},
        &scenarioReportOptions});
    registry.add(ScenarioDefinition{
        "contract-msx-export-basic-sections",
        "Format a multi-species reaction model into MSX 2.0 sections in the documented order.",
        {"contract", "quality"},
        &scenarioMsxExportBasicSections});
    registry.add(ScenarioDefinition{
        "contract-msx-export-canonical-species-tolerances",
        "Convert canonical AOWIS absolute tolerances into explicit per-species MSX units while keeping relative tolerances dimensionless.",
        {"contract", "quality"},
        &scenarioMsxExportCanonicalSpeciesTolerances});
    registry.add(ScenarioDefinition{
        "contract-msx-export-species-selection",
        "Validate a requested output-species subset without pruning any species, reactions, sources, or initial quality from the MSX chemistry model.",
        {"contract", "quality"},
        &scenarioMsxExportSpeciesSelection});
    registry.add(ScenarioDefinition{
        "contract-msx-export-rejects-unknown-species-selection",
        "Reject a multi_species_run species selection that does not resolve against the network's species list.",
        {"contract", "quality", "negative"},
        &scenarioMsxExportRejectsUnknownSpeciesSelection});
    registry.add(ScenarioDefinition{
        "contract-msx-export-rejects-broken-reaction-reference",
        "Reject a reaction referencing an unresolved species UUID even when no species selection narrows the run.",
        {"contract", "quality", "negative"},
        &scenarioMsxExportRejectsBrokenReactionReference});
}
}
