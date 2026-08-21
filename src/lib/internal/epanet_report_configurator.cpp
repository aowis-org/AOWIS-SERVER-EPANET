#include "epanet_report_configurator.h"

#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <optional>

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QUuid>

#include <aowis/model/hydraulic/network_hydraulic.h>

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

void appendReportFieldCommands(
    QStringList &commands,
    const QString &field_name,
    bool enabled,
    const std::optional<int> &precision,
    const std::optional<double> &below,
    const std::optional<double> &above)
{
    commands.append(field_name + (enabled ? QStringLiteral(" YES") : QStringLiteral(" NO")));
    if (!enabled)
        return;

    if (precision.has_value())
        commands.append(field_name + QStringLiteral(" PRECISION %1").arg(precision.value()));
    if (below.has_value())
        commands.append(field_name + QStringLiteral(" BELOW %1").arg(QString::number(below.value(), 'g', 17)));
    if (above.has_value())
        commands.append(field_name + QStringLiteral(" ABOVE %1").arg(QString::number(above.value(), 'g', 17)));
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportField &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, std::nullopt, std::nullopt);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldM &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_m, field.above_m);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldM3PerH &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_m3_per_h, field.above_m3_per_h);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldMm &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_mm, field.above_mm);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldMPerS &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_m_per_s, field.above_m_per_s);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldMPerKm &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_m_per_km, field.above_m_per_km);
}

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportFieldFrictionFactor &field)
{
    appendReportFieldCommands(commands, field_name, field.enabled, field.precision, field.below_friction_factor, field.above_friction_factor);
}

void appendTypedReportFieldCommands(QStringList &commands, const HydraulicSimulationReportOptions &options)
{
    appendReportFieldCommands(commands, QStringLiteral("ELEVATION"), options.fields_node.elevation);
    appendReportFieldCommands(commands, QStringLiteral("DEMAND"), options.fields_node.demand);
    appendReportFieldCommands(commands, QStringLiteral("HEAD"), options.fields_node.head);
    appendReportFieldCommands(commands, QStringLiteral("PRESSURE"), options.fields_node.pressure);
    appendReportFieldCommands(commands, QStringLiteral("QUALITY"), options.fields_node.quality);

    appendReportFieldCommands(commands, QStringLiteral("LENGTH"), options.fields_link.length);
    appendReportFieldCommands(commands, QStringLiteral("DIAMETER"), options.fields_link.diameter);
    appendReportFieldCommands(commands, QStringLiteral("FLOW"), options.fields_link.flow);
    appendReportFieldCommands(commands, QStringLiteral("VELOCITY"), options.fields_link.velocity);
    appendReportFieldCommands(commands, QStringLiteral("HEADLOSS"), options.fields_link.headloss);
    // The model's link "position" report field maps to EPANET's link State column.
    appendReportFieldCommands(commands, QStringLiteral("STATE"), options.fields_link.position);
    appendReportFieldCommands(commands, QStringLiteral("SETTING"), options.fields_link.setting);
    appendReportFieldCommands(commands, QStringLiteral("REACTION"), options.fields_link.reaction);
    appendReportFieldCommands(commands, QStringLiteral("F-FACTOR"), options.fields_link.friction);
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

HydraulicSimulationStatus configureEpanetReport(
    EpanetProject &project,
    const NetworkHydraulic &request)
{
    const HydraulicSimulationReportOptions &options = request.options_report;
    const QHash<QUuid, QString> node_ids_by_uuid = reportNodeIdsByUuid(request);
    const QHash<QUuid, QString> link_ids_by_uuid = reportLinkIdsByUuid(request);

    QString nodes_command;
    HydraulicSimulationStatus status = reportSelectionCommand(QStringLiteral("NODES"), HydraulicSimulationStatusEntityType::Node, options.selection_nodes, node_ids_by_uuid, nodes_command);
    if (!status.success)
        return status;

    QString links_command;
    status = reportSelectionCommand(QStringLiteral("LINKS"), HydraulicSimulationStatusEntityType::Link, options.selection_links, link_ids_by_uuid, links_command);
    if (!status.success)
        return status;

    QStringList commands;
    commands << QStringLiteral("PAGESIZE %1").arg(options.page_size)
             << reportStatusCommand(options.status)
             << QStringLiteral("SUMMARY %1").arg(options.summary ? QStringLiteral("YES") : QStringLiteral("NO"))
             << QStringLiteral("MESSAGES %1").arg(options.messages ? QStringLiteral("YES") : QStringLiteral("NO"))
             << QStringLiteral("ENERGY %1").arg(options.energy ? QStringLiteral("YES") : QStringLiteral("NO"))
             << nodes_command
             << links_command;
    appendTypedReportFieldCommands(commands, options);
    commands.append(options.backend_commands);

    for (const QString &command : commands)
    {
        if (command.trimmed().isEmpty())
            continue;

        const QByteArray command_utf8 = command.toUtf8();
        const int error = EN_setreport(project.handle(), command_utf8.constData());
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreport"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the EPANET report"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    return makeEpanetSuccess();
}

