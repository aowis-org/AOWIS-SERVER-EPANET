#include "epanet_inp_report_importer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>
#include <aowis/epanet/epanet_result_import.h>

#include <QFile>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <cmath>

namespace
{
constexpr double meters_per_foot = 0.3048;
constexpr double psi_per_foot = 0.4333;
constexpr double kpa_per_psi = 6.895;
constexpr double bar_per_psi = 0.068948;
constexpr double gallons_per_minute_per_cfs = 448.831;
constexpr double acre_feet_per_day_per_cfs = 1.9837;
constexpr double million_gallons_per_day_per_cfs = 0.64632;
constexpr double imperial_million_gallons_per_day_per_cfs = 0.5382;
constexpr double liters_per_second_per_cfs = 28.317;
constexpr double liters_per_minute_per_cfs = 1699.0;
constexpr double cubic_meters_per_second_per_cfs = 0.028317;
constexpr double cubic_meters_per_hour_per_cfs = 101.94;
constexpr double cubic_meters_per_day_per_cfs = 2446.6;
constexpr double million_liters_per_day_per_cfs = 2.4466;

void appendImportWarning(
    EpanetResultImport &result,
    const QString &message,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::Report,
    const QStringList &details = QStringList())
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Warning;
    diagnostic.stage = HydraulicSimulationStatusStage::ReadInput;
    diagnostic.operation = HydraulicSimulationStatusOperation::ReadInput;
    diagnostic.entity.type = entity_type;
    diagnostic.message = message;
    diagnostic.details = details;
    diagnostic.backend_name = QStringLiteral("EPANET");
    diagnostic.backend_operation = QStringLiteral("INP metadata/report import");
    appendEpanetDiagnosticIfUnique(result.diagnostics, diagnostic);
    result.complete = false;
}

QString stripComment(const QString &line)
{
    bool in_quotes = false;
    for (qsizetype index = 0; index < line.size(); index++)
    {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"'))
            in_quotes = !in_quotes;
        else if (character == QLatin1Char(';') && !in_quotes)
            return line.left(index);
    }
    return line;
}

QStringList tokenize(const QString &line)
{
    QStringList tokens;
    QString token;
    bool in_quotes = false;

    for (qsizetype index = 0; index < line.size(); index++)
    {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"'))
        {
            in_quotes = !in_quotes;
            continue;
        }

        if (character.isSpace() && !in_quotes)
        {
            if (!token.isEmpty())
            {
                tokens.append(token);
                token.clear();
            }
            continue;
        }

        token.append(character);
    }

    if (!token.isEmpty())
        tokens.append(token);
    return tokens;
}

bool tokenStartsWith(const QString &token, const QString &keyword)
{
    return token.startsWith(keyword, Qt::CaseInsensitive);
}

bool parseDouble(const QString &text, double &value)
{
    bool ok = false;
    value = text.toDouble(&ok);
    return ok && std::isfinite(value);
}

bool parseInteger(const QString &text, int &value)
{
    double numeric = 0.0;
    if (!parseDouble(text, numeric))
        return false;
    value = qRound(numeric);
    return true;
}

double flowToCanonicalM3PerH(int flow_units)
{
    double source_per_cfs = cubic_meters_per_hour_per_cfs;
    switch (flow_units)
    {
    case EN_CFS:
        source_per_cfs = 1.0;
        break;
    case EN_GPM:
        source_per_cfs = gallons_per_minute_per_cfs;
        break;
    case EN_MGD:
        source_per_cfs = million_gallons_per_day_per_cfs;
        break;
    case EN_IMGD:
        source_per_cfs = imperial_million_gallons_per_day_per_cfs;
        break;
    case EN_AFD:
        source_per_cfs = acre_feet_per_day_per_cfs;
        break;
    case EN_LPS:
        source_per_cfs = liters_per_second_per_cfs;
        break;
    case EN_LPM:
        source_per_cfs = liters_per_minute_per_cfs;
        break;
    case EN_MLD:
        source_per_cfs = million_liters_per_day_per_cfs;
        break;
    case EN_CMH:
        source_per_cfs = cubic_meters_per_hour_per_cfs;
        break;
    case EN_CMD:
        source_per_cfs = cubic_meters_per_day_per_cfs;
        break;
    case EN_CMS:
        source_per_cfs = cubic_meters_per_second_per_cfs;
        break;
    default:
        break;
    }

    return cubic_meters_per_hour_per_cfs / source_per_cfs;
}

bool flowUnitsAreSi(int flow_units)
{
    return flow_units == EN_LPS
        || flow_units == EN_LPM
        || flow_units == EN_MLD
        || flow_units == EN_CMH
        || flow_units == EN_CMD
        || flow_units == EN_CMS;
}

double sourceLengthToM(int flow_units)
{
    return flowUnitsAreSi(flow_units) ? 1.0 : meters_per_foot;
}

double sourceDiameterToMm(int flow_units)
{
    return flowUnitsAreSi(flow_units) ? 1.0 : 25.4;
}

double sourceVelocityToMPerS(int flow_units)
{
    return flowUnitsAreSi(flow_units) ? 1.0 : meters_per_foot;
}

double sourcePressureToHeadM(const EpanetInpSourceUnits &source_units)
{
    const double specific_gravity = source_units.specific_gravity > 0.0
        ? source_units.specific_gravity : 1.0;

    switch (source_units.pressure_units)
    {
    case EN_METERS:
        return 1.0;
    case EN_FEET:
        return meters_per_foot;
    case EN_PSI:
        return meters_per_foot / (psi_per_foot * specific_gravity);
    case EN_KPA:
        return meters_per_foot / (kpa_per_psi * psi_per_foot * specific_gravity);
    case EN_BAR:
        return meters_per_foot / (bar_per_psi * psi_per_foot * specific_gravity);
    default:
        return 1.0;
    }
}

double qualityToCanonical(const EpanetProject &project)
{
    int quality_type = EN_NONE;
    char chemical_name[EN_MAXID + 1] = {};
    char chemical_units[EN_MAXID + 1] = {};
    int trace_node_index = 0;
    if (EN_getqualinfo(
            project.handle(), &quality_type, chemical_name, chemical_units, &trace_node_index) != 0)
    {
        return 1.0;
    }

    if (quality_type != EN_CHEM)
        return 1.0;

    const QString units = QString::fromUtf8(chemical_units);
    if (units.compare(QStringLiteral("ug/L"), Qt::CaseInsensitive) == 0)
        return 0.001;
    return 1.0;
}

void initializeReportDefaults(NetworkHydraulic &network, const EpanetProject &project)
{
    HydraulicSimulationReportOptions &options = network.options_report;
    options.page_size = 0;
    options.status = HydraulicSimulationReportStatus::None;
    options.summary = true;
    options.messages = true;
    options.energy = false;
    options.backend_commands.clear();
    options.selection_nodes.mode = HydraulicSimulationReportSelectionMode::None;
    options.selection_nodes.uuids.clear();
    options.selection_links.mode = HydraulicSimulationReportSelectionMode::None;
    options.selection_links.uuids.clear();

    options.fields_node.elevation.enabled = false;
    options.fields_node.demand.enabled = true;
    options.fields_node.head.enabled = true;
    options.fields_node.pressure.enabled = true;
    options.fields_node.quality.enabled = true;

    options.fields_link.length.enabled = false;
    options.fields_link.diameter.enabled = false;
    options.fields_link.flow.enabled = true;
    options.fields_link.velocity.enabled = true;
    options.fields_link.headloss.enabled = true;
    options.fields_link.position.enabled = false;
    options.fields_link.setting.enabled = false;
    options.fields_link.reaction.enabled = false;
    options.fields_link.friction.enabled = false;

    options.fields_node.elevation.precision = 2;
    options.fields_node.demand.precision = 2;
    options.fields_node.head.precision = 2;
    options.fields_node.pressure.precision = 2;
    options.fields_node.quality.precision = 2;
    options.fields_link.length.precision = 2;
    options.fields_link.diameter.precision = 2;
    options.fields_link.flow.precision = 2;
    options.fields_link.velocity.precision = 2;
    options.fields_link.headloss.precision = 2;
    options.fields_link.position.precision = 2;
    options.fields_link.setting.precision = 2;
    options.fields_link.reaction.precision = 2;
    options.fields_link.friction.precision = 3;

    options.fields_node.elevation.below_m.reset();
    options.fields_node.elevation.above_m.reset();
    options.fields_node.demand.below_m3_per_h.reset();
    options.fields_node.demand.above_m3_per_h.reset();
    options.fields_node.head.below_m.reset();
    options.fields_node.head.above_m.reset();
    options.fields_node.pressure.below_m.reset();
    options.fields_node.pressure.above_m.reset();
    options.fields_link.length.below_m.reset();
    options.fields_link.length.above_m.reset();
    options.fields_link.diameter.below_mm.reset();
    options.fields_link.diameter.above_mm.reset();
    options.fields_link.flow.below_m3_per_h.reset();
    options.fields_link.flow.above_m3_per_h.reset();
    options.fields_link.velocity.below_m_per_s.reset();
    options.fields_link.velocity.above_m_per_s.reset();
    options.fields_link.headloss.below_m_per_km.reset();
    options.fields_link.headloss.above_m_per_km.reset();
    options.fields_link.friction.below_friction_factor.reset();
    options.fields_link.friction.above_friction_factor.reset();

    int quality_type = EN_NONE;
    char chemical_name[EN_MAXID + 1] = {};
    char chemical_units[EN_MAXID + 1] = {};
    int trace_node_index = 0;
    if (EN_getqualinfo(
            project.handle(), &quality_type, chemical_name, chemical_units, &trace_node_index) == 0
        && quality_type == EN_NONE)
    {
        options.fields_node.quality.enabled = false;
    }
}

QHash<QString, QUuid> nodeUuidsById(const NetworkHydraulic &network)
{
    QHash<QString, QUuid> values;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        values.insert(junction.id, junction.uuid);
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        values.insert(reservoir.id, reservoir.uuid);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        values.insert(tank.id, tank.uuid);
    return values;
}

QHash<QString, QUuid> linkUuidsById(const NetworkHydraulic &network)
{
    QHash<QString, QUuid> values;
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
        values.insert(pipe.id, pipe.uuid);
    for (const HydraulicLinkPump &pump : network.links_pumps)
        values.insert(pump.id, pump.uuid);
    for (const HydraulicLinkValve &valve : network.links_valves)
        values.insert(valve.id, valve.uuid);
    return values;
}

bool setReportSelection(
    EpanetResultImport &result,
    HydraulicSimulationReportSelection &selection,
    const QStringList &tokens,
    const QHash<QString, QUuid> &uuids_by_id,
    const QString &source_line,
    const QString &entity_name)
{
    if (tokens.size() < 2)
        return false;

    if (tokenStartsWith(tokens.at(1), QStringLiteral("NONE")))
    {
        selection.mode = HydraulicSimulationReportSelectionMode::None;
        return true;
    }
    if (tokenStartsWith(tokens.at(1), QStringLiteral("ALL")))
    {
        selection.mode = HydraulicSimulationReportSelectionMode::All;
        return true;
    }

    selection.mode = HydraulicSimulationReportSelectionMode::Selected;

    QSet<QUuid> existing;
    for (const QUuid &uuid : selection.uuids)
        existing.insert(uuid);
    for (int index = 1; index < tokens.size(); index++)
    {
        const QUuid uuid = uuids_by_id.value(tokens.at(index));
        if (uuid.isNull())
        {
            appendImportWarning(
                result,
                QStringLiteral("A selected EPANET report %1 could not be resolved and was skipped.").arg(entity_name),
                HydraulicSimulationStatusEntityType::Report,
                {source_line});
            continue;
        }
        if (!existing.contains(uuid))
        {
            selection.uuids.append(uuid);
            existing.insert(uuid);
        }
    }
    return true;
}

enum class ReportField
{
    Elevation,
    Demand,
    Head,
    Pressure,
    Quality,
    Length,
    Diameter,
    Flow,
    Velocity,
    Headloss,
    State,
    Setting,
    Reaction,
    Friction,
    Unknown
};

ReportField reportField(const QString &token)
{
    if (tokenStartsWith(token, QStringLiteral("HEADLOSS")))
        return ReportField::Headloss;
    if (tokenStartsWith(token, QStringLiteral("ELEVATION")))
        return ReportField::Elevation;
    if (tokenStartsWith(token, QStringLiteral("DEMAND")))
        return ReportField::Demand;
    if (tokenStartsWith(token, QStringLiteral("HEAD")))
        return ReportField::Head;
    if (tokenStartsWith(token, QStringLiteral("PRESSURE")))
        return ReportField::Pressure;
    if (tokenStartsWith(token, QStringLiteral("QUALITY")))
        return ReportField::Quality;
    if (tokenStartsWith(token, QStringLiteral("LENGTH")))
        return ReportField::Length;
    if (tokenStartsWith(token, QStringLiteral("DIAMETER")))
        return ReportField::Diameter;
    if (tokenStartsWith(token, QStringLiteral("FLOW")))
        return ReportField::Flow;
    if (tokenStartsWith(token, QStringLiteral("VELOCITY")))
        return ReportField::Velocity;
    if (tokenStartsWith(token, QStringLiteral("STATE")))
        return ReportField::State;
    if (tokenStartsWith(token, QStringLiteral("SETTING")))
        return ReportField::Setting;
    if (tokenStartsWith(token, QStringLiteral("REACTION")))
        return ReportField::Reaction;
    if (tokenStartsWith(token, QStringLiteral("F-FACTOR")))
        return ReportField::Friction;
    return ReportField::Unknown;
}

void setFieldEnabled(NetworkHydraulic &network, ReportField field, bool enabled)
{
    switch (field)
    {
    case ReportField::Elevation: network.options_report.fields_node.elevation.enabled = enabled; break;
    case ReportField::Demand: network.options_report.fields_node.demand.enabled = enabled; break;
    case ReportField::Head: network.options_report.fields_node.head.enabled = enabled; break;
    case ReportField::Pressure: network.options_report.fields_node.pressure.enabled = enabled; break;
    case ReportField::Quality: network.options_report.fields_node.quality.enabled = enabled; break;
    case ReportField::Length: network.options_report.fields_link.length.enabled = enabled; break;
    case ReportField::Diameter: network.options_report.fields_link.diameter.enabled = enabled; break;
    case ReportField::Flow: network.options_report.fields_link.flow.enabled = enabled; break;
    case ReportField::Velocity: network.options_report.fields_link.velocity.enabled = enabled; break;
    case ReportField::Headloss: network.options_report.fields_link.headloss.enabled = enabled; break;
    case ReportField::State: network.options_report.fields_link.position.enabled = enabled; break;
    case ReportField::Setting: network.options_report.fields_link.setting.enabled = enabled; break;
    case ReportField::Reaction: network.options_report.fields_link.reaction.enabled = enabled; break;
    case ReportField::Friction: network.options_report.fields_link.friction.enabled = enabled; break;
    case ReportField::Unknown: break;
    }
}

void setFieldPrecision(NetworkHydraulic &network, ReportField field, int precision)
{
    switch (field)
    {
    case ReportField::Elevation: network.options_report.fields_node.elevation.precision = precision; break;
    case ReportField::Demand: network.options_report.fields_node.demand.precision = precision; break;
    case ReportField::Head: network.options_report.fields_node.head.precision = precision; break;
    case ReportField::Pressure: network.options_report.fields_node.pressure.precision = precision; break;
    case ReportField::Quality: network.options_report.fields_node.quality.precision = precision; break;
    case ReportField::Length: network.options_report.fields_link.length.precision = precision; break;
    case ReportField::Diameter: network.options_report.fields_link.diameter.precision = precision; break;
    case ReportField::Flow: network.options_report.fields_link.flow.precision = precision; break;
    case ReportField::Velocity: network.options_report.fields_link.velocity.precision = precision; break;
    case ReportField::Headloss: network.options_report.fields_link.headloss.precision = precision; break;
    case ReportField::State: network.options_report.fields_link.position.precision = precision; break;
    case ReportField::Setting: network.options_report.fields_link.setting.precision = precision; break;
    case ReportField::Reaction: network.options_report.fields_link.reaction.precision = precision; break;
    case ReportField::Friction: network.options_report.fields_link.friction.precision = precision; break;
    case ReportField::Unknown: break;
    }
}

QString canonicalBackendThresholdCommand(
    const QString &field_name,
    const QString &qualifier,
    double value)
{
    return QStringLiteral("%1 %2 %3")
        .arg(field_name, qualifier, QString::number(value, 'g', 17));
}

void setPhysicalThreshold(
    NetworkHydraulic &network,
    ReportField field,
    bool below,
    double value,
    const EpanetInpSourceUnits &source_units)
{
    switch (field)
    {
    case ReportField::Elevation:
    {
        const double canonical = value * sourceLengthToM(source_units.flow_units);
        (below ? network.options_report.fields_node.elevation.below_m
               : network.options_report.fields_node.elevation.above_m) = canonical;
        break;
    }
    case ReportField::Demand:
    {
        const double canonical = value * flowToCanonicalM3PerH(source_units.flow_units);
        (below ? network.options_report.fields_node.demand.below_m3_per_h
               : network.options_report.fields_node.demand.above_m3_per_h) = canonical;
        break;
    }
    case ReportField::Head:
    {
        const double canonical = value * sourceLengthToM(source_units.flow_units);
        (below ? network.options_report.fields_node.head.below_m
               : network.options_report.fields_node.head.above_m) = canonical;
        break;
    }
    case ReportField::Pressure:
    {
        const double canonical = value * sourcePressureToHeadM(source_units);
        (below ? network.options_report.fields_node.pressure.below_m
               : network.options_report.fields_node.pressure.above_m) = canonical;
        break;
    }
    case ReportField::Length:
    {
        const double canonical = value * sourceLengthToM(source_units.flow_units);
        (below ? network.options_report.fields_link.length.below_m
               : network.options_report.fields_link.length.above_m) = canonical;
        break;
    }
    case ReportField::Diameter:
    {
        const double canonical = value * sourceDiameterToMm(source_units.flow_units);
        (below ? network.options_report.fields_link.diameter.below_mm
               : network.options_report.fields_link.diameter.above_mm) = canonical;
        break;
    }
    case ReportField::Flow:
    {
        const double canonical = value * flowToCanonicalM3PerH(source_units.flow_units);
        (below ? network.options_report.fields_link.flow.below_m3_per_h
               : network.options_report.fields_link.flow.above_m3_per_h) = canonical;
        break;
    }
    case ReportField::Velocity:
    {
        const double canonical = value * sourceVelocityToMPerS(source_units.flow_units);
        (below ? network.options_report.fields_link.velocity.below_m_per_s
               : network.options_report.fields_link.velocity.above_m_per_s) = canonical;
        break;
    }
    case ReportField::Headloss:
        (below ? network.options_report.fields_link.headloss.below_m_per_km
               : network.options_report.fields_link.headloss.above_m_per_km) = value;
        break;
    case ReportField::Friction:
        (below ? network.options_report.fields_link.friction.below_friction_factor
               : network.options_report.fields_link.friction.above_friction_factor) = value;
        break;
    default:
        break;
    }
}

bool isPhysicalThresholdField(ReportField field)
{
    return field == ReportField::Elevation
        || field == ReportField::Demand
        || field == ReportField::Head
        || field == ReportField::Pressure
        || field == ReportField::Length
        || field == ReportField::Diameter
        || field == ReportField::Flow
        || field == ReportField::Velocity
        || field == ReportField::Headloss
        || field == ReportField::Friction;
}

QString reportFieldName(ReportField field)
{
    switch (field)
    {
    case ReportField::Quality: return QStringLiteral("QUALITY");
    case ReportField::State: return QStringLiteral("STATE");
    case ReportField::Setting: return QStringLiteral("SETTING");
    case ReportField::Reaction: return QStringLiteral("REACTION");
    default: return QString();
    }
}

bool sourceSettingThresholdsAlreadyCanonical(const EpanetInpSourceUnits &source_units)
{
    return source_units.flow_units == EN_CMH
        && source_units.pressure_units == EN_METERS;
}

HydraulicSimulationStatus importReport(
    const EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result,
    const EpanetInpSourceUnits &source_units)
{
    QFile file(input_file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Report,
            QString(),
            QStringLiteral("Could not read the source INP [REPORT] section: %1")
                .arg(file.errorString()));
    }

    NetworkHydraulic &network = result.request.network;
    initializeReportDefaults(network, project);
    const QHash<QString, QUuid> node_uuids_by_id = nodeUuidsById(network);
    const QHash<QString, QUuid> link_uuids_by_id = linkUuidsById(network);
    const double quality_scale = qualityToCanonical(project);

    bool in_report = false;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines)
    {
        line = stripComment(line).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('[')))
        {
            in_report = line.compare(QStringLiteral("[REPORT]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_report)
            continue;

        const QStringList tokens = tokenize(line);
        if (tokens.isEmpty())
            continue;

        const QString command = tokens.first();
        if (tokenStartsWith(command, QStringLiteral("PAGE")) && tokens.size() >= 2)
        {
            double page_size = 0.0;
            if (!parseDouble(tokens.constLast(), page_size))
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Report,
                    QString(),
                    QStringLiteral("Could not parse EPANET report page size"));
            network.options_report.page_size = static_cast<int>(page_size);
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("STATUS")) && tokens.size() >= 2)
        {
            const QString value = tokens.constLast();
            if (tokenStartsWith(value, QStringLiteral("NO")))
                network.options_report.status = HydraulicSimulationReportStatus::None;
            else if (tokenStartsWith(value, QStringLiteral("YES")))
                network.options_report.status = HydraulicSimulationReportStatus::Normal;
            else if (tokenStartsWith(value, QStringLiteral("FULL")))
                network.options_report.status = HydraulicSimulationReportStatus::Full;
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("SUMM")) && tokens.size() >= 2)
        {
            const QString value = tokens.constLast();
            if (tokenStartsWith(value, QStringLiteral("NO")))
                network.options_report.summary = false;
            else if (tokenStartsWith(value, QStringLiteral("YES")))
                network.options_report.summary = true;
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("MESS")) && tokens.size() >= 2)
        {
            const QString value = tokens.constLast();
            if (tokenStartsWith(value, QStringLiteral("NO")))
                network.options_report.messages = false;
            else if (tokenStartsWith(value, QStringLiteral("YES")))
                network.options_report.messages = true;
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("ENER")) && tokens.size() >= 2)
        {
            const QString value = tokens.constLast();
            if (tokenStartsWith(value, QStringLiteral("NO")))
                network.options_report.energy = false;
            else if (tokenStartsWith(value, QStringLiteral("YES")))
                network.options_report.energy = true;
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("NODE")))
        {
            if (!setReportSelection(
                    result, network.options_report.selection_nodes, tokens,
                    node_uuids_by_id, line, QStringLiteral("node")))
            {
                appendImportWarning(
                    result,
                    QStringLiteral("An EPANET NODES report directive could not be parsed."),
                    HydraulicSimulationStatusEntityType::Report,
                    {line});
            }
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("LINK")))
        {
            if (!setReportSelection(
                    result, network.options_report.selection_links, tokens,
                    link_uuids_by_id, line, QStringLiteral("link")))
            {
                appendImportWarning(
                    result,
                    QStringLiteral("An EPANET LINKS report directive could not be parsed."),
                    HydraulicSimulationStatusEntityType::Report,
                    {line});
            }
            continue;
        }
        if (tokenStartsWith(command, QStringLiteral("FILE")))
        {
            // FILE is a backend output destination rather than hydraulic-model state.
            // Preserve it through the report backend-command escape hatch.
            network.options_report.backend_commands.append(line);
            continue;
        }

        const ReportField field = reportField(command);
        if (field == ReportField::Unknown)
        {
            appendImportWarning(
                result,
                QStringLiteral("An EPANET report directive is not understood by the AOWIS importer and was preserved as a backend command."),
                HydraulicSimulationStatusEntityType::Report,
                {line});
            network.options_report.backend_commands.append(line);
            continue;
        }

        if (tokens.size() == 1 || tokenStartsWith(tokens.at(1), QStringLiteral("YES")))
        {
            setFieldEnabled(network, field, true);
            continue;
        }
        if (tokenStartsWith(tokens.at(1), QStringLiteral("NO")))
        {
            setFieldEnabled(network, field, false);
            continue;
        }
        if (tokens.size() < 3)
        {
            appendImportWarning(
                result,
                QStringLiteral("An EPANET report-field directive could not be parsed."),
                HydraulicSimulationStatusEntityType::Report,
                {line});
            continue;
        }

        if (tokenStartsWith(tokens.at(1), QStringLiteral("PREC")))
        {
            int precision = 0;
            if (!parseInteger(tokens.at(2), precision))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Report,
                    QString(),
                    QStringLiteral("Could not parse EPANET report precision"));
            }
            setFieldEnabled(network, field, true);
            setFieldPrecision(network, field, precision);
            continue;
        }

        const bool below = tokenStartsWith(tokens.at(1), QStringLiteral("BELOW"));
        const bool above = tokenStartsWith(tokens.at(1), QStringLiteral("ABOVE"));
        if (!below && !above)
        {
            appendImportWarning(
                result,
                QStringLiteral("An EPANET report-field qualifier is not supported by the importer."),
                HydraulicSimulationStatusEntityType::Report,
                {line});
            continue;
        }

        double value = 0.0;
        if (!parseDouble(tokens.at(2), value))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Report,
                QString(),
                QStringLiteral("Could not parse an EPANET report threshold"));
        }

        if (isPhysicalThresholdField(field))
        {
            setPhysicalThreshold(network, field, below, value, source_units);
            continue;
        }

        const QString qualifier = below ? QStringLiteral("BELOW") : QStringLiteral("ABOVE");
        if (field == ReportField::Quality || field == ReportField::Reaction)
        {
            network.options_report.backend_commands.append(
                canonicalBackendThresholdCommand(
                    reportFieldName(field), qualifier, value * quality_scale));
            continue;
        }
        if (field == ReportField::State)
        {
            network.options_report.backend_commands.append(
                canonicalBackendThresholdCommand(
                    reportFieldName(field), qualifier, value));
            continue;
        }
        if (field == ReportField::Setting)
        {
            if (sourceSettingThresholdsAlreadyCanonical(source_units))
            {
                network.options_report.backend_commands.append(
                    canonicalBackendThresholdCommand(
                        reportFieldName(field), qualifier, value));
            }
            else
            {
                appendImportWarning(
                    result,
                    QStringLiteral("An EPANET SETTING report threshold cannot be converted losslessly after unit normalization because link settings use different units by link type; the threshold was not imported."),
                    HydraulicSimulationStatusEntityType::Report,
                    {line});
            }
            continue;
        }
    }

    int quality_type = EN_NONE;
    char chemical_name[EN_MAXID + 1] = {};
    char chemical_units[EN_MAXID + 1] = {};
    int trace_node_index = 0;
    if (EN_getqualinfo(
            project.handle(), &quality_type, chemical_name, chemical_units, &trace_node_index) == 0
        && quality_type == EN_NONE)
    {
        network.options_report.fields_node.quality.enabled = false;
    }

    return makeEpanetSuccess();
}
}

HydraulicSimulationStatus importEpanetInpReport(
    const EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result,
    const EpanetInpSourceUnits &source_units)
{
    return importReport(project, input_file_path, result, source_units);
}
