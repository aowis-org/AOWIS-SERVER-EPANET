#include "epanet_inp_exporter.h"

#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <optional>

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic.h>

namespace
{
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

void appendFrictionReportFieldCommands(
    QStringList &commands,
    const QString &field_name,
    const HydraulicSimulationReportFieldFrictionFactor &field)
{
    commands.append(field_name + (field.enabled ? QStringLiteral(" YES") : QStringLiteral(" NO")));
    if (!field.enabled)
        return;

    if (field.precision.has_value())
        commands.append(field_name + QStringLiteral(" PRECISION %1").arg(field.precision.value()));
    if (field.below_friction_factor.has_value())
        commands.append(field_name + QStringLiteral(" BELOW %1").arg(QString::number(field.below_friction_factor.value(), 'g', 17)));
    if (field.above_friction_factor.has_value())
        commands.append(field_name + QStringLiteral(" ABOVE %1").arg(QString::number(field.above_friction_factor.value(), 'g', 17)));
}

HydraulicSimulationReportFieldFrictionFactor effectiveFrictionReportField(const HydraulicSimulationReportOptions &options)
{
    HydraulicSimulationReportFieldFrictionFactor field = options.fields_link.friction;
    for (const QString &backend_command : options.backend_commands)
    {
        const QStringList tokens = backend_command.simplified().split(QChar(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty() || tokens.first().compare(QStringLiteral("F-FACTOR"), Qt::CaseInsensitive) != 0)
            continue;

        if (tokens.size() == 1 || tokens.at(1).compare(QStringLiteral("YES"), Qt::CaseInsensitive) == 0)
        {
            field.enabled = true;
            continue;
        }
        if (tokens.at(1).compare(QStringLiteral("NO"), Qt::CaseInsensitive) == 0)
        {
            field.enabled = false;
            continue;
        }
        if (tokens.size() < 3)
            continue;

        bool value_ok = false;
        const double value = tokens.at(2).toDouble(&value_ok);
        if (!value_ok)
            continue;

        if (tokens.at(1).compare(QStringLiteral("PRECISION"), Qt::CaseInsensitive) == 0)
        {
            field.enabled = true;
            field.precision = qRound(value);
        }
        else if (tokens.at(1).compare(QStringLiteral("BELOW"), Qt::CaseInsensitive) == 0)
            field.below_friction_factor = value;
        else if (tokens.at(1).compare(QStringLiteral("ABOVE"), Qt::CaseInsensitive) == 0)
            field.above_friction_factor = value;
    }

    return field;
}

QString mapNumber(double value)
{
    return QString::number(value, 'g', 15);
}

QString preserveMapLayoutSections(QString inp_text, const NetworkHydraulic &network)
{
    QStringList lines = inp_text.split(QChar('\n'));
    QStringList retained_lines;
    retained_lines.reserve(lines.size());

    bool skip_section = false;
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('[')))
        {
            const bool map_section = trimmed.compare(QStringLiteral("[LABELS]"), Qt::CaseInsensitive) == 0
                || trimmed.compare(QStringLiteral("[BACKDROP]"), Qt::CaseInsensitive) == 0;
            skip_section = map_section;
            if (map_section)
                continue;
        }
        if (!skip_section)
            retained_lines.append(line);
    }

    QStringList layout_lines;
    const QHash<QUuid, QString> node_ids_by_uuid = reportNodeIdsByUuid(network);

    if (!network.map_labels.isEmpty())
    {
        layout_lines.append(QStringLiteral("[LABELS]"));
        for (const HydraulicMapLabel &label : network.map_labels)
        {
            QString row = QStringLiteral("%1 %2 \"%3\"")
                .arg(mapNumber(label.coordinate_wgs84.longitude_deg),
                    mapNumber(label.coordinate_wgs84.latitude_deg),
                    label.text);
            if (!label.anchor_node_uuid.isNull())
            {
                const QString anchor_id = node_ids_by_uuid.value(label.anchor_node_uuid);
                if (!anchor_id.isEmpty())
                    row += QStringLiteral(" %1").arg(anchor_id);
            }
            layout_lines.append(row);
        }
        layout_lines.append(QString());
    }

    if (network.map_backdrop.enabled)
    {
        layout_lines.append(QStringLiteral("[BACKDROP]"));
        layout_lines.append(QStringLiteral("DIMENSIONS %1 %2 %3 %4")
            .arg(mapNumber(network.map_backdrop.lower_left_wgs84.longitude_deg),
                mapNumber(network.map_backdrop.lower_left_wgs84.latitude_deg),
                mapNumber(network.map_backdrop.upper_right_wgs84.longitude_deg),
                mapNumber(network.map_backdrop.upper_right_wgs84.latitude_deg)));
        layout_lines.append(QStringLiteral("UNITS DEGREES"));
        layout_lines.append(QStringLiteral("FILE %1").arg(network.map_backdrop.file));
        layout_lines.append(QStringLiteral("OFFSET %1 %2")
            .arg(mapNumber(network.map_backdrop.offset_longitude_deg),
                mapNumber(network.map_backdrop.offset_latitude_deg)));
        layout_lines.append(QString());
    }

    if (layout_lines.isEmpty())
        return retained_lines.join(QChar('\n'));

    int end_index = retained_lines.size();
    for (int index = 0; index < retained_lines.size(); index++)
    {
        if (retained_lines.at(index).trimmed().compare(QStringLiteral("[END]"), Qt::CaseInsensitive) == 0)
        {
            end_index = index;
            break;
        }
    }

    for (int index = 0; index < layout_lines.size(); index++)
        retained_lines.insert(end_index + index, layout_lines.at(index));

    return retained_lines.join(QChar('\n'));
}

QString preserveFrictionReportField(QString inp_text, const HydraulicSimulationReportOptions &options)
{
    QStringList commands;
    appendFrictionReportFieldCommands(commands, QStringLiteral("F-FACTOR"), effectiveFrictionReportField(options));

    QStringList lines = inp_text.split(QChar('\n'));
    int report_section_index = -1;
    int next_section_index = lines.size();
    for (int index = 0; index < lines.size(); index++)
    {
        const QString trimmed = lines.at(index).trimmed();
        if (trimmed.compare(QStringLiteral("[REPORT]"), Qt::CaseInsensitive) == 0)
        {
            report_section_index = index;
            continue;
        }
        if (report_section_index >= 0 && index > report_section_index && trimmed.startsWith(QChar('[')))
        {
            next_section_index = index;
            break;
        }
    }

    if (report_section_index < 0)
        return inp_text;

    // EPANET 2.3 omits F-Factor from EN_saveinpfile(). Remove any existing
    // F-Factor rows as well so this remains correct if the native writer is
    // fixed in a later vendored EPANET release.
    for (int index = next_section_index - 1; index > report_section_index; index--)
    {
        const QString trimmed = lines.at(index).trimmed();
        const QStringList tokens = trimmed.simplified().split(QChar(' '), Qt::SkipEmptyParts);
        if (!tokens.isEmpty() && tokens.first().compare(QStringLiteral("F-FACTOR"), Qt::CaseInsensitive) == 0)
        {
            lines.removeAt(index);
            next_section_index--;
        }
    }

    QStringList formatted_commands;
    formatted_commands.reserve(commands.size());
    for (const QString &command : commands)
        formatted_commands.append(QLatin1Char(' ') + command);

    for (int index = formatted_commands.size() - 1; index >= 0; index--)
        lines.insert(next_section_index, formatted_commands.at(index));

    return lines.join(QChar('\n'));
}

QString normalizeSavedRuleFillDrainTimes(QString inp_text)
{
    QStringList lines = inp_text.split(QChar('\n'));
    bool in_rules = false;

    for (QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('[')))
        {
            in_rules = trimmed.compare(QStringLiteral("[RULES]"), Qt::CaseInsensitive) == 0;
            continue;
        }

        if (!in_rules
            || (!line.contains(QStringLiteral(" FILLTIME "), Qt::CaseInsensitive)
                && !line.contains(QStringLiteral(" DRAINTIME "), Qt::CaseInsensitive)))
        {
            continue;
        }

        int value_end = line.size() - 1;
        while (value_end >= 0 && line.at(value_end).isSpace())
            value_end--;
        if (value_end < 0)
            continue;

        int value_start = value_end;
        while (value_start >= 0 && !line.at(value_start).isSpace())
            value_start--;
        value_start++;

        const QString value_text = line.mid(value_start, value_end - value_start + 1);
        if (!value_text.contains(QChar(':')))
            continue;

        const QStringList parts = value_text.split(QChar(':'));
        if (parts.size() != 2 && parts.size() != 3)
            continue;

        bool hours_ok = false;
        bool minutes_ok = false;
        bool seconds_ok = true;
        const double hours = parts.at(0).toDouble(&hours_ok);
        const double minutes = parts.at(1).toDouble(&minutes_ok);
        double seconds = 0.0;
        if (parts.size() == 3)
            seconds = parts.at(2).toDouble(&seconds_ok);

        if (!hours_ok || !minutes_ok || !seconds_ok)
            continue;

        const double decimal_hours = hours + minutes / 60.0 + seconds / 3600.0;
        line.replace(value_start, value_end - value_start + 1, QString::number(decimal_hours, 'g', 17));
    }

    return lines.join(QChar('\n'));
}

QString preserveNoDefaultDemandPattern(QString inp_text, const QString &unused_pattern_id)
{
    QStringList lines = inp_text.split(QChar('\n'));
    for (int index = 0; index < lines.size(); index++)
    {
        if (lines.at(index).trimmed().compare(QStringLiteral("[OPTIONS]"), Qt::CaseInsensitive) != 0)
            continue;

        lines.insert(index + 1, QStringLiteral(" PATTERN             %1").arg(unused_pattern_id));
        return lines.join(QChar('\n'));
    }

    return inp_text;
}


}

HydraulicSimulationStatus retrieveEpanetInpText(
    EpanetProject &project,
    const NetworkHydraulic &request,
    QString &inp_text)
{
    inp_text.clear();

    double default_demand_pattern_index = 0.0;
    const int demand_pattern_error = EN_getoption(project.handle(), EN_DEMANDPATTERN, &default_demand_pattern_index);
    if (demand_pattern_error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, demand_pattern_error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_getoption(EN_DEMANDPATTERN)"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to inspect the default demand pattern before EPANET INP export"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QTemporaryDir temporary_directory;
    if (!temporary_directory.isValid())
        return makeEpanetStatus(HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to create a temporary directory for the EPANET INP export"));

    const QString inp_path = temporary_directory.filePath(QStringLiteral("network.inp"));
    const QByteArray inp_path_native = QFile::encodeName(inp_path);
    const int error = EN_saveinpfile(project.handle(), inp_path_native.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_saveinpfile"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to serialize the EPANET project as an INP file"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QFile inp_file(inp_path);
    if (!inp_file.open(QIODevice::ReadOnly))
        return makeEpanetStatus(HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to read the EPANET INP export: %1").arg(inp_file.errorString()));

    const QByteArray inp_data = inp_file.readAll();
    if (inp_file.error() != QFileDevice::NoError)
        return makeEpanetStatus(HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed while reading the EPANET INP export: %1").arg(inp_file.errorString()));

    inp_text = normalizeSavedRuleFillDrainTimes(QString::fromUtf8(inp_data));
    if (default_demand_pattern_index == 0.0)
    {
        QString unused_pattern_id = QStringLiteral("__AOWIS_NO_DEFAULT");
        int suffix = 1;
        while (true)
        {
            const QByteArray pattern_id_utf8 = unused_pattern_id.toUtf8();
            int pattern_index = 0;
            const int pattern_error = EN_getpatternindex(project.handle(), pattern_id_utf8.constData(), &pattern_index);
            if (pattern_error == 205)
                break;
            if (pattern_error != 0)
            {
                const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, pattern_error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to choose an unused default-demand-pattern sentinel for EPANET INP export"));
                if (!epanet_status.success)
                    return epanet_status;
            }

            unused_pattern_id = QStringLiteral("__AOWIS_NO_DEFAULT_%1").arg(suffix++);
        }

        inp_text = preserveNoDefaultDemandPattern(inp_text, unused_pattern_id);
    }

    // EPANET 2.3's native INP writer currently omits the final F-Factor report
    // field from [REPORT]. Reinsert the effective configured value so reopening
    // the generated INP preserves the complete typed report configuration.
    inp_text = preserveFrictionReportField(inp_text, request.options_report);

    // AOWIS hydraulic geometry uses canonical WGS84 longitude/latitude. EPANET
    // [COORDINATES], [VERTICES], [LABELS], and [BACKDROP] therefore share that
    // same degree-based map space in generated INP files.
    inp_text = preserveMapLayoutSections(inp_text, request);

    return makeEpanetSuccess();
}

