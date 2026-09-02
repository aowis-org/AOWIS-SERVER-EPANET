#include "epanet_msx_project.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_inp_exporter.h"
#include "epanet_msx_exporter.h"
#include "epanet_prepared_project.h"
#include "epanet_status_helpers.h"

#include <epanetmsx.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include <string>

namespace
{
const QString msxBackendName()
{
    return QStringLiteral("EPANET-MSX");
}

bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}

std::string msxErrorMessage(int code)
{
    std::string buffer(256, '\0');
    MSXgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

std::string epanetLegacyErrorMessage(int code)
{
    std::string buffer(256, '\0');
    ENgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

HydraulicSimulationStatus msxSuccessStatus()
{
    HydraulicSimulationStatus status;
    status.success = true;
    status.backend_name = msxBackendName();
    return status;
}

// backend_error_code/message_backend come from MSXgeterror unless
// from_legacy_epanet is set, in which case the failing call was one of the
// legacy EN_* functions MSX itself is built on (e.g. MSXENopen), whose error
// codes and message text come from ENgeterror instead.
HydraulicSimulationStatus msxErrorStatus(
    int return_code,
    HydraulicSimulationStatusOperation operation,
    const QString &backend_operation,
    const NetworkHydraulic &network,
    const QString &message,
    bool from_legacy_epanet = false)
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        HydraulicSimulationStatusStage::RunQuality,
        operation,
        HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
        network.id,
        network.uuid,
        message);
    status.backend_name = msxBackendName();
    status.backend_error_code = return_code;
    status.backend_operation = backend_operation;
    status.message_backend = QString::fromStdString(
        from_legacy_epanet ? epanetLegacyErrorMessage(return_code) : msxErrorMessage(return_code));
    return status;
}

// Every terminal-failure return point reports through timeline.status (the
// overall outcome) and also records the same failure in timeline.diagnostics
// -- mirroring how EpanetQualitySolver's collectQualityFailure keeps both in
// sync -- so a caller inspecting diagnostics sees this failure the same way
// it would see any other backend diagnostic.
void failTimeline(MultiSpeciesSimulationResultTimeline &timeline, const HydraulicSimulationStatus &status, MultiSpeciesSimulationResultValidity validity)
{
    timeline.status = status;
    timeline.validity = validity;
    timeline.diagnostics.append(epanetDiagnosticFromStatus(status, HydraulicSimulationDiagnosticSeverity::Fatal));
}

bool writeTextFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text.toUtf8());
    return file.error() == QFileDevice::NoError;
}

bool resolveNodeIndex(const QString &id, int &index)
{
    const QByteArray id_utf8 = id.toUtf8();
    return ENgetnodeindex(id_utf8.constData(), &index) == 0;
}

bool resolveLinkIndex(const QString &id, int &index)
{
    const QByteArray id_utf8 = id.toUtf8();
    return ENgetlinkindex(id_utf8.constData(), &index) == 0;
}

struct IndexedEntity
{
    QString id;
    QUuid uuid;
    int index = 0;
};

// Resolves every entity's EPANET index up front, once, rather than looking
// each one up again on every quality timestep.
template<typename Entity>
bool resolveNodeIndices(const QList<Entity> &entities, QList<IndexedEntity> &target)
{
    for (const Entity &entity : entities)
    {
        int index = 0;
        if (!resolveNodeIndex(entity.id, index))
            return false;
        target.append(IndexedEntity{entity.id, entity.uuid, index});
    }
    return true;
}

template<typename Entity>
bool resolveLinkIndices(const QList<Entity> &entities, QList<IndexedEntity> &target)
{
    for (const Entity &entity : entities)
    {
        int index = 0;
        if (!resolveLinkIndex(entity.id, index))
            return false;
        target.append(IndexedEntity{entity.id, entity.uuid, index});
    }
    return true;
}

QList<MultiSpeciesResultValue> readSpeciesValuesForNode(int node_index, const QList<QPair<int, QUuid>> &species_by_index)
{
    QList<MultiSpeciesResultValue> values;
    for (const QPair<int, QUuid> &species : species_by_index)
    {
        double value = 0.0;
        if (MSXgetqual(MSX_NODE, node_index, species.first, &value) == 0)
            values.append(MultiSpeciesResultValue{species.second, value});
    }
    return values;
}

QList<MultiSpeciesResultValue> readSpeciesValuesForLink(int link_index, const QList<QPair<int, QUuid>> &species_by_index)
{
    QList<MultiSpeciesResultValue> values;
    for (const QPair<int, QUuid> &species : species_by_index)
    {
        double value = 0.0;
        if (MSXgetqual(MSX_LINK, link_index, species.first, &value) == 0)
            values.append(MultiSpeciesResultValue{species.second, value});
    }
    return values;
}
}

HydraulicSimulationStatus EpanetMsxProject::run(
    const NetworkHydraulic &network,
    const MultiSpeciesRunOptions &run_options,
    const QString &hydraulic_file_path,
    MultiSpeciesSimulationResultTimeline &timeline,
    const std::function<bool()> &cancellation_requested,
    bool &cancelled)
{
    cancelled = false;
    timeline = MultiSpeciesSimulationResultTimeline();

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = MultiSpeciesSimulationResultValidity::NotRun;
        return timeline.status;
    }

    const QFileInfo hydraulic_file_info(hydraulic_file_path);
    if (hydraulic_file_path.isEmpty() || !hydraulic_file_info.exists() || !hydraulic_file_info.isFile())
    {
        const HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::RunMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("A reusable EPANET hydraulic results file is required for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    // Build the same AOWIS network through the normal handle-based pipeline
    // only to obtain the native-generated INP topology that legacy MSX needs
    // beside the caller-supplied .hyd file. This does not solve hydraulics.
    // It is independent of MSX's process-global state, so it deliberately
    // runs before the mutex below is acquired.
    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    if (!status.success)
    {
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QString inp_text;
    status = retrieveEpanetInpText(prepared_project.project(), prepared_project.network(), inp_text);
    if (!status.success)
    {
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QString msx_text;
    status = retrieveEpanetMsxText(prepared_project.network(), run_options, msx_text);
    if (!status.success)
    {
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QTemporaryDir scratch_dir;
    if (!scratch_dir.isValid())
    {
        status = makeEpanetStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Failed to create a temporary directory for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    const QString inp_path = scratch_dir.filePath(QStringLiteral("network.inp"));
    const QString msx_path = scratch_dir.filePath(QStringLiteral("network.msx"));
    const QString rpt_path = scratch_dir.filePath(QStringLiteral("network.rpt"));
    const QString out_path = scratch_dir.filePath(QStringLiteral("network.out"));

    if (!writeTextFile(inp_path, inp_text) || !writeTextFile(msx_path, msx_text))
    {
        status = makeEpanetStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::OpenMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Failed to write the temporary INP/MSX files for the multi-species run"));
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = MultiSpeciesSimulationResultValidity::NotRun;
        return timeline.status;
    }

    // Everything from here on touches MSX's process-global state.
    static QMutex msx_mutex;
    QMutexLocker locker(&msx_mutex);

    const QByteArray inp_path_native = QFile::encodeName(inp_path);
    const QByteArray rpt_path_native = QFile::encodeName(rpt_path);
    const QByteArray out_path_native = QFile::encodeName(out_path);
    std::string msx_path_mutable = QFile::encodeName(msx_path).toStdString();
    std::string hydraulic_path_mutable = QFile::encodeName(hydraulic_file_path).toStdString();

    int error = MSXENopen(inp_path_native.constData(), rpt_path_native.constData(), out_path_native.constData());
    if (error != 0)
    {
        status = msxErrorStatus(error, HydraulicSimulationStatusOperation::OpenMultiSpecies, QStringLiteral("MSXENopen"), network, QStringLiteral("Failed to open the EPANET project for multi-species execution"), true);
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXopen(msx_path_mutable.data());
    if (error != 0)
    {
        status = msxErrorStatus(error, HydraulicSimulationStatusOperation::OpenMultiSpecies, QStringLiteral("MSXopen"), network, QStringLiteral("Failed to open the multi-species reaction model"));
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXusehydfile(hydraulic_path_mutable.data());
    if (error != 0)
    {
        status = msxErrorStatus(
            error,
            HydraulicSimulationStatusOperation::RunMultiSpecies,
            QStringLiteral("MSXusehydfile"),
            network,
            QStringLiteral("Failed to load the AOWIS EPANET hydraulic results for the multi-species run"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    // Resolve species indices from whatever MSXopen actually parsed, rather
    // than re-deriving the run's selection here -- the parsed model is the
    // single source of truth for which species this run actually covers.
    int species_count = 0;
    error = MSXgetcount(MSX_SPECIES, &species_count);
    if (error != 0)
    {
        status = msxErrorStatus(error, HydraulicSimulationStatusOperation::InitializeMultiSpecies, QStringLiteral("MSXgetcount"), network, QStringLiteral("Failed to read the multi-species species count"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    QHash<QString, QUuid> species_uuid_by_id;
    for (const MultiSpeciesSpecies &species : prepared_project.network().multi_species.species)
        species_uuid_by_id.insert(species.id, species.uuid);

    QList<QPair<int, QUuid>> species_by_index;
    for (int species_index = 1; species_index <= species_count; species_index++)
    {
        int id_len = 0;
        MSXgetIDlen(MSX_SPECIES, species_index, &id_len);
        std::string id_buffer(static_cast<std::size_t>(id_len) + 1, '\0');
        MSXgetID(MSX_SPECIES, species_index, id_buffer.data(), id_len + 1);
        const QString species_id = QString::fromStdString(id_buffer.c_str());
        const QUuid species_uuid = species_uuid_by_id.value(species_id);
        if (!species_uuid.isNull())
            species_by_index.append(QPair<int, QUuid>(species_index, species_uuid));
    }

    QList<IndexedEntity> node_junctions;
    QList<IndexedEntity> node_reservoirs;
    QList<IndexedEntity> node_tanks;
    QList<IndexedEntity> link_pipes;
    QList<IndexedEntity> link_pumps;
    QList<IndexedEntity> link_valves;

    const bool identities_resolved =
        resolveNodeIndices(prepared_project.network().nodes_junctions, node_junctions) &&
        resolveNodeIndices(prepared_project.network().nodes_reservoirs, node_reservoirs) &&
        resolveNodeIndices(prepared_project.network().nodes_tanks, node_tanks) &&
        resolveLinkIndices(prepared_project.network().links_pipes, link_pipes) &&
        resolveLinkIndices(prepared_project.network().links_pumps, link_pumps) &&
        resolveLinkIndices(prepared_project.network().links_valves, link_valves);

    if (!identities_resolved)
    {
        status = makeEpanetStatus(
            HydraulicSimulationStatusStage::RunQuality,
            HydraulicSimulationStatusOperation::InitializeMultiSpecies,
            HydraulicSimulationStatusEntityType::MultiSpeciesSolver,
            network.id,
            network.uuid,
            QStringLiteral("Failed to resolve a node or link index while preparing to read multi-species results"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    error = MSXinit(0);
    if (error != 0)
    {
        status = msxErrorStatus(error, HydraulicSimulationStatusOperation::InitializeMultiSpecies, QStringLiteral("MSXinit"), network, QStringLiteral("Failed to initialize multi-species water-quality state"));
        MSXclose();
        MSXENclose();
        failTimeline(timeline, status, MultiSpeciesSimulationResultValidity::Invalid);
        return status;
    }

    HydraulicSimulationStatus first_failure = msxSuccessStatus();
    bool step_loop_cancelled = false;
    double t = 0.0;
    double tleft = 1.0;

    while (tleft > 0.0)
    {
        if (cancellationRequested(cancellation_requested))
        {
            step_loop_cancelled = true;
            break;
        }

        error = MSXstep(&t, &tleft);
        if (error != 0)
        {
            first_failure = msxErrorStatus(error, HydraulicSimulationStatusOperation::StepMultiSpecies, QStringLiteral("MSXstep"), network, QStringLiteral("Failed to advance the multi-species timestep"));
            break;
        }

        MultiSpeciesSimulationResult result;
        result.time_elapsed_s = static_cast<quint64>(t);
        result.status = msxSuccessStatus();

        for (const IndexedEntity &entity : node_junctions)
        {
            MultiSpeciesSimulationResultNodeJunction node_result;
            node_result.id = entity.id;
            node_result.uuid = entity.uuid;
            node_result.species_values = readSpeciesValuesForNode(entity.index, species_by_index);
            result.nodes_junctions.append(node_result);
        }
        for (const IndexedEntity &entity : node_reservoirs)
        {
            MultiSpeciesSimulationResultNodeReservoir node_result;
            node_result.id = entity.id;
            node_result.uuid = entity.uuid;
            node_result.species_values = readSpeciesValuesForNode(entity.index, species_by_index);
            result.nodes_reservoirs.append(node_result);
        }
        for (const IndexedEntity &entity : node_tanks)
        {
            MultiSpeciesSimulationResultNodeTank node_result;
            node_result.id = entity.id;
            node_result.uuid = entity.uuid;
            node_result.species_values = readSpeciesValuesForNode(entity.index, species_by_index);
            result.nodes_tanks.append(node_result);
        }
        for (const IndexedEntity &entity : link_pipes)
        {
            MultiSpeciesSimulationResultLinkPipe link_result;
            link_result.id = entity.id;
            link_result.uuid = entity.uuid;
            link_result.species_values = readSpeciesValuesForLink(entity.index, species_by_index);
            result.links_pipes.append(link_result);
        }
        for (const IndexedEntity &entity : link_pumps)
        {
            MultiSpeciesSimulationResultLinkPump link_result;
            link_result.id = entity.id;
            link_result.uuid = entity.uuid;
            link_result.species_values = readSpeciesValuesForLink(entity.index, species_by_index);
            result.links_pumps.append(link_result);
        }
        for (const IndexedEntity &entity : link_valves)
        {
            MultiSpeciesSimulationResultLinkValve link_result;
            link_result.id = entity.id;
            link_result.uuid = entity.uuid;
            link_result.species_values = readSpeciesValuesForLink(entity.index, species_by_index);
            result.links_valves.append(link_result);
        }

        timeline.results.append(result);

        if (cancellationRequested(cancellation_requested))
        {
            step_loop_cancelled = true;
            break;
        }
    }

    MSXclose();
    const int en_close_error = MSXENclose();
    if (en_close_error != 0 && first_failure.success)
        first_failure = msxErrorStatus(en_close_error, HydraulicSimulationStatusOperation::CloseMultiSpecies, QStringLiteral("MSXENclose"), network, QStringLiteral("Failed to close the EPANET project after the multi-species run"), true);

    if (step_loop_cancelled)
    {
        cancelled = true;
        timeline.status = msxSuccessStatus();
        timeline.validity = timeline.results.isEmpty()
            ? MultiSpeciesSimulationResultValidity::NotRun
            : MultiSpeciesSimulationResultValidity::Partial;
        return timeline.status;
    }

    if (!first_failure.success)
    {
        failTimeline(timeline, first_failure, MultiSpeciesSimulationResultValidity::Invalid);
        return first_failure;
    }

    timeline.status = first_failure;
    timeline.validity = MultiSpeciesSimulationResultValidity::Valid;
    return timeline.status;
}
