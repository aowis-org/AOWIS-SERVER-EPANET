#include "native_quality_reference_runner.h"

#include <epanet2_2.h>

#include <QByteArray>
#include <QTemporaryDir>

#include <array>
#include <stdexcept>
#include <string>

namespace AowisEpanetTests
{
namespace
{
void checkEpanet(int error, const char *operation)
{
    if (error == 0)
        return;

    std::array<char, EN_MAXMSG + 1> message{};
    EN_geterror(error, message.data(), EN_MAXMSG);
    throw std::runtime_error(std::string(operation) + " failed with EPANET code " + std::to_string(error) + ": " + message.data());
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

int nodeIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getnodeindex(project, id_utf8.constData(), &index), "EN_getnodeindex(native generated quality)");
    return index;
}

int linkIndex(EN_Project project, const QString &id)
{
    const QByteArray id_utf8 = id.toUtf8();
    int index = 0;
    checkEpanet(EN_getlinkindex(project, id_utf8.constData(), &index), "EN_getlinkindex(native generated quality)");
    return index;
}

template<typename NodeType>
void collectNodes(EN_Project project, const QList<NodeType> &nodes, const NetworkHydraulic &network, NativeQualityReferenceStep &step)
{
    for (const NodeType &node : nodes)
    {
        const int index = nodeIndex(project, node.id);
        double value = 0.0;
        checkEpanet(EN_getnodevalue(project, index, EN_QUALITY, &value), "EN_getnodevalue(EN_QUALITY native generated quality)");
        step.node_quality.insert(node.id, value);

        if (nodeHasSource(network, node.id))
        {
            checkEpanet(EN_getnodevalue(project, index, EN_SOURCEMASS, &value), "EN_getnodevalue(EN_SOURCEMASS native generated quality)");
            step.node_source_mass_mg_per_min.insert(node.id, value);
        }
        else
        {
            step.node_source_mass_mg_per_min.insert(node.id, 0.0);
        }
    }
}

template<typename LinkType>
void collectLinks(EN_Project project, const QList<LinkType> &links, NativeQualityReferenceStep &step)
{
    for (const LinkType &link : links)
    {
        const int index = linkIndex(project, link.id);
        double value = 0.0;
        checkEpanet(EN_getlinkvalue(project, index, EN_LINKQUAL, &value), "EN_getlinkvalue(EN_LINKQUAL native generated quality)");
        step.link_quality.insert(link.id, value);
    }
}
}

NativeQualityReferenceTimeline runNativeQualityReference(const QString &input_file, const NetworkHydraulic &network)
{
    NativeQualityReferenceTimeline timeline;
    EN_Project project = nullptr;
    bool project_created = false;
    bool project_open = false;
    bool quality_open = false;

    try
    {
        checkEpanet(EN_createproject(&project), "EN_createproject(native generated quality)");
        project_created = true;

        QTemporaryDir temporary_directory;
        if (!temporary_directory.isValid())
            throw std::runtime_error("Could not create native generated-quality report directory");
        const QByteArray input_utf8 = input_file.toUtf8();
        const QByteArray report_utf8 = temporary_directory.filePath(QStringLiteral("native-quality.rpt")).toUtf8();
        checkEpanet(EN_open(project, input_utf8.constData(), report_utf8.constData(), ""), "EN_open(native generated quality)");
        project_open = true;

        checkEpanet(EN_solveH(project), "EN_solveH(native generated quality)");
        checkEpanet(EN_openQ(project), "EN_openQ(native generated quality)");
        quality_open = true;
        checkEpanet(EN_initQ(project, EN_NOSAVE), "EN_initQ(native generated quality)");

        long time_left_s = 0;
        do
        {
            long current_time_s = 0;
            checkEpanet(EN_runQ(project, &current_time_s), "EN_runQ(native generated quality)");

            NativeQualityReferenceStep step;
            step.time_s = current_time_s;
            collectNodes(project, network.nodes_junctions, network, step);
            collectNodes(project, network.nodes_reservoirs, network, step);
            collectNodes(project, network.nodes_tanks, network, step);
            collectLinks(project, network.links_pipes, step);
            collectLinks(project, network.links_pumps, step);
            collectLinks(project, network.links_valves, step);
            checkEpanet(EN_getstatistic(project, EN_MASSBALANCE, &step.mass_balance_ratio), "EN_getstatistic(EN_MASSBALANCE native generated quality)");
            timeline.results.append(step);

            checkEpanet(EN_stepQ(project, &time_left_s), "EN_stepQ(native generated quality)");
        } while (time_left_s > 0);

        checkEpanet(EN_closeQ(project), "EN_closeQ(native generated quality)");
        quality_open = false;
        checkEpanet(EN_close(project), "EN_close(native generated quality)");
        project_open = false;
        checkEpanet(EN_deleteproject(project), "EN_deleteproject(native generated quality)");
        project_created = false;
        timeline.success = true;
    }
    catch (const std::exception &exception)
    {
        timeline.error = QString::fromUtf8(exception.what());
    }

    if (quality_open)
        EN_closeQ(project);
    if (project_open)
        EN_close(project);
    if (project_created)
        EN_deleteproject(project);
    return timeline;
}
}
