#include "epanet_inp_metadata_importer.h"

#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_result_import.h>

#include <QByteArray>
#include <QList>
#include <QString>

namespace
{
HydraulicSimulationStatus metadataReadFailure(
    const EpanetProject &project,
    int error,
    const QString &backend_operation,
    const QString &message,
    HydraulicSimulationStatusEntityType entity_type)
{
    return processEpanetReturnCode(
        project,
        error,
        HydraulicSimulationStatusStage::ReadInput,
        HydraulicSimulationStatusOperation::ReadInput,
        backend_operation,
        entity_type,
        QString(),
        message);
}

template <typename Entity>
HydraulicSimulationStatus importMetadataForEntities(
    const EpanetProject &project,
    QList<Entity> &entities,
    int object_type,
    HydraulicSimulationStatusEntityType entity_type,
    bool nodes)
{
    for (Entity &entity : entities)
    {
        const QByteArray id_utf8 = entity.id.toUtf8();
        int object_index = 0;
        const int index_error = nodes
            ? EN_getnodeindex(project.handle(), id_utf8.constData(), &object_index)
            : EN_getlinkindex(project.handle(), id_utf8.constData(), &object_index);
        if (index_error != 0)
        {
            return metadataReadFailure(
                project,
                index_error,
                nodes ? QStringLiteral("EN_getnodeindex") : QStringLiteral("EN_getlinkindex"),
                QStringLiteral("Failed to resolve an EPANET entity while importing comments and tags"),
                entity_type);
        }

        char comment[EN_MAXMSG + 1] = {};
        int error = EN_getcomment(project.handle(), object_type, object_index, comment);
        if (error != 0)
        {
            return metadataReadFailure(
                project,
                error,
                QStringLiteral("EN_getcomment"),
                QStringLiteral("Failed to read an EPANET entity comment"),
                entity_type);
        }
        entity.metadata.comment = QString::fromUtf8(comment).trimmed();

        char tag[EN_MAXMSG + 1] = {};
        error = EN_gettag(project.handle(), object_type, object_index, tag);
        if (error != 0)
        {
            return metadataReadFailure(
                project,
                error,
                QStringLiteral("EN_gettag"),
                QStringLiteral("Failed to read an EPANET entity tag"),
                entity_type);
        }
        entity.metadata.tag = QString::fromUtf8(tag);
    }

    return makeEpanetSuccess();
}
}

HydraulicSimulationStatus importEpanetInpEntityMetadata(
    const EpanetProject &project, EpanetResultImport &result)
{
    NetworkHydraulic &network = result.request.network;
    HydraulicSimulationStatus status = importMetadataForEntities(
        project, network.nodes_junctions, EN_NODE,
        HydraulicSimulationStatusEntityType::Junction, true);
    if (!status.success)
        return status;
    status = importMetadataForEntities(
        project, network.nodes_reservoirs, EN_NODE,
        HydraulicSimulationStatusEntityType::Reservoir, true);
    if (!status.success)
        return status;
    status = importMetadataForEntities(
        project, network.nodes_tanks, EN_NODE,
        HydraulicSimulationStatusEntityType::Tank, true);
    if (!status.success)
        return status;
    status = importMetadataForEntities(
        project, network.links_pipes, EN_LINK,
        HydraulicSimulationStatusEntityType::Pipe, false);
    if (!status.success)
        return status;
    status = importMetadataForEntities(
        project, network.links_pumps, EN_LINK,
        HydraulicSimulationStatusEntityType::Pump, false);
    if (!status.success)
        return status;
    return importMetadataForEntities(
        project, network.links_valves, EN_LINK,
        HydraulicSimulationStatusEntityType::Valve, false);
}
