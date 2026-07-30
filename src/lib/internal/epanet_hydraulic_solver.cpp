#include "epanet_hydraulic_solver.h"
#include "epanet_project.h"
#include "epanet_result_reader.h"
#include "epanet_status_helpers.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QUuid>

namespace
{
QString reportStatusCommand(HydraulicSimulationReportStatus status)
{
    switch (status)
    {
    case HydraulicSimulationReportStatus::None:
        return QStringLiteral("STATUS NO");
    case HydraulicSimulationReportStatus::Normal:
        return QStringLiteral("STATUS YES");
    case HydraulicSimulationReportStatus::Full:
        return QStringLiteral("STATUS FULL");
    }

    return QStringLiteral("STATUS YES");
}

QHash<QUuid, QString> reportNodeIdsByUuid(const NetworkHydraulic &network)
{
    QHash<QUuid, QString> ids_by_uuid;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        ids_by_uuid.insert(junction.uuid, junction.id);
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        ids_by_uuid.insert(reservoir.uuid, reservoir.id);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        ids_by_uuid.insert(tank.uuid, tank.id);
    return ids_by_uuid;
}

QHash<QUuid, QString> reportLinkIdsByUuid(const NetworkHydraulic &network)
{
    QHash<QUuid, QString> ids_by_uuid;
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
        ids_by_uuid.insert(pipe.uuid, pipe.id);
    for (const HydraulicLinkPump &pump : network.links_pumps)
        ids_by_uuid.insert(pump.uuid, pump.id);
    for (const HydraulicLinkValve &valve : network.links_valves)
        ids_by_uuid.insert(valve.uuid, valve.id);
    return ids_by_uuid;
}

HydraulicSimulationStatus reportSelectionCommand(const QString &entity_name, HydraulicSimulationStatusEntityType entity_type, const HydraulicSimulationReportSelection &selection, const QHash<QUuid, QString> &ids_by_uuid, QString &command)
{
    switch (selection.mode)
    {
    case HydraulicSimulationReportSelectionMode::None:
        command = entity_name + QStringLiteral(" NONE");
        return makeEpanetSuccess();
    case HydraulicSimulationReportSelectionMode::All:
        command = entity_name + QStringLiteral(" ALL");
        return makeEpanetSuccess();
    case HydraulicSimulationReportSelectionMode::Selected:
        break;
    }

    QStringList selected_ids;
    selected_ids.reserve(selection.uuids.length());
    for (const QUuid &uuid : selection.uuids)
    {
        const QString id = ids_by_uuid.value(uuid);
        if (id.isEmpty())
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, QString(), uuid, QStringLiteral("Could not resolve a selected report entity UUID"));
        selected_ids.append(id);
    }

    command = entity_name + QLatin1Char(' ') + selected_ids.join(QLatin1Char(' '));
    return makeEpanetSuccess();
}
}

EpanetHydraulicSolver::EpanetHydraulicSolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetResultReader &result_reader)
    : project(project), network(network), result_reader(result_reader)
{
}

HydraulicSimulationStatus EpanetHydraulicSolver::configureReport() const
{
    const HydraulicSimulationReportOptions &options = this->network.options_report;
    const QHash<QUuid, QString> node_ids_by_uuid = reportNodeIdsByUuid(this->network);
    const QHash<QUuid, QString> link_ids_by_uuid = reportLinkIdsByUuid(this->network);

    QString nodes_command;
    HydraulicSimulationStatus status = reportSelectionCommand(QStringLiteral("NODES"), HydraulicSimulationStatusEntityType::Node, options.selection_nodes, node_ids_by_uuid, nodes_command);
    if (!status.success)
        return status;

    QString links_command;
    status = reportSelectionCommand(QStringLiteral("LINKS"), HydraulicSimulationStatusEntityType::Link, options.selection_links, link_ids_by_uuid, links_command);
    if (!status.success)
        return status;

    QStringList commands;
    commands << reportStatusCommand(options.status)
             << QStringLiteral("SUMMARY %1").arg(options.summary ? QStringLiteral("YES") : QStringLiteral("NO"))
             << QStringLiteral("ENERGY %1").arg(options.energy ? QStringLiteral("YES") : QStringLiteral("NO"))
             << nodes_command
             << links_command;
    commands.append(options.backend_commands);

    for (const QString &command : commands)
    {
        if (command.trimmed().isEmpty())
            continue;

        const QByteArray command_utf8 = command.toUtf8();
        const int error = EN_setreport(this->project.handle(), command_utf8.constData());
        if (error != 0)
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreport"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the EPANET report"));
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetHydraulicSolver::run(HydraulicSimulationResultTimeline &timeline)
{
    HydraulicSimulationStatus status = this->configureReport();
    if (!status.success)
        return status;

    int error = EN_openH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::OpenHydraulics, QStringLiteral("EN_openH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to open EPANET hydraulics"));

    error = EN_initH(this->project.handle(), EN_SAVE_AND_INIT);
    if (error != 0)
    {
        EN_closeH(this->project.handle());
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::InitializeHydraulics, QStringLiteral("EN_initH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to initialize EPANET hydraulics"));
    }

    long current_time_s = 0;
    long next_step_s = 0;
    do
    {
        error = EN_runH(this->project.handle(), &current_time_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, QStringLiteral("EN_runH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to run EPANET hydraulics"));
        }

        if (current_time_s < 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetStatus(HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET returned a negative elapsed simulation time"));
        }

        HydraulicSimulationResult result;
        result.time_elapsed_s = static_cast<quint64>(current_time_s);
        status = this->result_reader.read(result);
        if (!status.success)
        {
            const int close_error = EN_closeH(this->project.handle());
            if (close_error != 0)
                status.details << QStringLiteral("Additionally, EN_closeH failed with error code %1: %2").arg(close_error).arg(this->project.errorMessage(close_error));
            return status;
        }

        timeline.results.append(result);
        error = EN_nextH(this->project.handle(), &next_step_s);
        if (error != 0)
        {
            EN_closeH(this->project.handle());
            return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::AdvanceHydraulics, QStringLiteral("EN_nextH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to advance EPANET hydraulics"));
        }
    }
    while (next_step_s > 0);

    error = EN_closeH(this->project.handle());
    if (error != 0)
        return makeEpanetError(this->project, error, HydraulicSimulationStatusStage::CloseHydraulics, HydraulicSimulationStatusOperation::CloseHydraulics, QStringLiteral("EN_closeH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to close EPANET hydraulics"));

    return makeEpanetSuccess();
}
