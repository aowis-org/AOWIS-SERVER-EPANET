#include "epanet_project.h"
#include "epanet_report_collector.h"
#include "epanet_status_helpers.h"

#include <array>
#include <limits>

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
bool resolveHeadlossFormula(HydraulicHeadlossFormula formula, int &backend_formula)
{
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        backend_formula = EN_HW;
        return true;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        backend_formula = EN_DW;
        return true;
    case HydraulicHeadlossFormula::ChezyManning:
        backend_formula = EN_CM;
        return true;
    }

    return false;
}

bool resolveDemandModel(HydraulicDemandModel model, int &backend_model)
{
    switch (model)
    {
    case HydraulicDemandModel::DemandDriven:
        backend_model = EN_DDA;
        return true;
    case HydraulicDemandModel::PressureDriven:
        backend_model = EN_PDA;
        return true;
    }

    return false;
}

bool resolveReportStatistic(HydraulicSimulationReportStatistic statistic, int &backend_statistic)
{
    switch (statistic)
    {
    case HydraulicSimulationReportStatistic::Series:
        backend_statistic = EN_SERIES;
        return true;
    case HydraulicSimulationReportStatistic::Average:
        backend_statistic = EN_AVERAGE;
        return true;
    case HydraulicSimulationReportStatistic::Minimum:
        backend_statistic = EN_MINIMUM;
        return true;
    case HydraulicSimulationReportStatistic::Maximum:
        backend_statistic = EN_MAXIMUM;
        return true;
    case HydraulicSimulationReportStatistic::Range:
        backend_statistic = EN_RANGE;
        return true;
    }

    return false;
}

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

void appendReportFieldCommands(QStringList &commands, const QString &field_name, const HydraulicSimulationReportField &field)
{
    commands.append(field_name + (field.enabled ? QStringLiteral(" YES") : QStringLiteral(" NO")));
    if (!field.enabled)
        return;

    if (field.precision.has_value())
        commands.append(field_name + QStringLiteral(" PRECISION %1").arg(field.precision.value()));
    if (field.below.has_value())
        commands.append(field_name + QStringLiteral(" BELOW %1").arg(QString::number(field.below.value(), 'g', 17)));
    if (field.above.has_value())
        commands.append(field_name + QStringLiteral(" ABOVE %1").arg(QString::number(field.above.value(), 'g', 17)));
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

HydraulicSimulationReportField effectiveFrictionReportField(const HydraulicSimulationReportOptions &options)
{
    HydraulicSimulationReportField field = options.fields_link.friction;
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
            field.below = value;
        else if (tokens.at(1).compare(QStringLiteral("ABOVE"), Qt::CaseInsensitive) == 0)
            field.above = value;
    }

    return field;
}

QString preserveFrictionReportField(QString inp_text, const HydraulicSimulationReportOptions &options)
{
    QStringList commands;
    appendReportFieldCommands(commands, QStringLiteral("F-FACTOR"), effectiveFrictionReportField(options));

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

EpanetProject::~EpanetProject()
{
    if (this->project != nullptr)
        EN_deleteproject(this->project);
}

HydraulicSimulationStatus EpanetProject::create()
{
    if (this->project != nullptr)
        return makeEpanetStatus(HydraulicSimulationStatusStage::CreateBackendContext, HydraulicSimulationStatusOperation::CreateBackendContext, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project already exists"));

    const int error = EN_createproject(&this->project);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::CreateBackendContext, HydraulicSimulationStatusOperation::CreateBackendContext, QStringLiteral("EN_createproject"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project creation failed"));
        if (!epanet_status.success)
            return epanet_status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetProject::initialize(const NetworkHydraulic &request, EpanetReportCollector &report_collector)
{
    int error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback"));
        if (!epanet_status.success)
            return epanet_status;
    }

    int backend_headloss_formula = 0;
    if (!resolveHeadlossFormula(request.options_hydraulic.headloss_formula, backend_headloss_formula))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Unsupported hydraulic headloss formula"));

    // Keep the native EPANET project in the canonical units encoded by the AOWIS field names:
    // m3/h for flow, meters for length and head, and millimeters for pipe diameter.
    error = EN_init(this->project, "", "", EN_CMH, backend_headloss_formula);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::Initialize, QStringLiteral("EN_init"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project initialization failed"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const QByteArray title_line_1 = request.title_line_1.toUtf8();
    const QByteArray title_line_2 = request.title_line_2.toUtf8();
    const QByteArray title_line_3 = request.title_line_3.toUtf8();
    error = EN_settitle(this->project, title_line_1.constData(), title_line_2.constData(), title_line_3.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_settitle"), HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Failed to configure EPANET title lines"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setoption(this->project, EN_PRESS_UNITS, static_cast<double>(EN_METERS));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_PRESS_UNITS)"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure EPANET pressure-head units as meters"));
        if (!epanet_status.success)
            return epanet_status;
    }

    struct TimeParameter
    {
        int parameter;
        quint64 value;
        const char *name;
    };

    const std::array<TimeParameter, 9> time_parameters = {{
        {EN_DURATION, request.duration_s, "EN_DURATION"},
        {EN_HYDSTEP, request.timestep_hydraulic_s, "EN_HYDSTEP"},
        {EN_QUALSTEP, request.timestep_quality_s, "EN_QUALSTEP"},
        {EN_PATTERNSTEP, request.timestep_pattern_s, "EN_PATTERNSTEP"},
        {EN_PATTERNSTART, request.start_pattern_s, "EN_PATTERNSTART"},
        {EN_REPORTSTEP, request.timestep_report_s, "EN_REPORTSTEP"},
        {EN_REPORTSTART, request.start_report_s, "EN_REPORTSTART"},
        {EN_RULESTEP, request.timestep_rule_s, "EN_RULESTEP"},
        {EN_STARTTIME, request.start_time_of_day_s, "EN_STARTTIME"}
    }};

    for (const TimeParameter &time_parameter : time_parameters)
    {
        if (time_parameter.value > static_cast<quint64>(std::numeric_limits<long>::max()))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureTime, HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("A simulation time value exceeds the range supported by EPANET on this platform: %1").arg(QString::fromLatin1(time_parameter.name)));

        error = EN_settimeparam(this->project, time_parameter.parameter, static_cast<long>(time_parameter.value));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureTime, QStringLiteral("EN_settimeparam(%1)").arg(QString::fromLatin1(time_parameter.name)), HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Failed to configure an EPANET time parameter"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    int backend_report_statistic = 0;
    if (!resolveReportStatistic(request.report_statistic, backend_report_statistic))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Unsupported hydraulic report statistic"));

    error = EN_settimeparam(this->project, EN_STATISTIC, backend_report_statistic);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_settimeparam(EN_STATISTIC)"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the report statistic"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const HydraulicSolverOptions &hydraulic = request.options_hydraulic;
    int backend_demand_model = 0;
    if (!resolveDemandModel(hydraulic.demand_model, backend_demand_model))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Unsupported hydraulic demand model"));

    error = EN_setdemandmodel(this->project, backend_demand_model, hydraulic.minimum_pressure_head_m, hydraulic.required_pressure_head_m, hydraulic.pressure_exponent);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setdemandmodel"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure the hydraulic demand model"));
        if (!epanet_status.success)
            return epanet_status;
    }

    struct NumericOption
    {
        int option;
        double value;
        const char *name;
    };

    double unbalanced_trials = 0.0;
    switch (hydraulic.unbalanced_action)
    {
    case HydraulicUnbalancedAction::Stop:
        unbalanced_trials = -1.0;
        break;
    case HydraulicUnbalancedAction::Continue:
        if (hydraulic.unbalanced_extra_trials < 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Unbalanced continuation trials cannot be negative"));
        unbalanced_trials = static_cast<double>(hydraulic.unbalanced_extra_trials);
        break;
    default:
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Unsupported unbalanced-action value"));
    }
    const std::array<NumericOption, 18> options = {{
        {EN_TRIALS, static_cast<double>(hydraulic.maximum_trials), "EN_TRIALS"},
        {EN_ACCURACY, hydraulic.accuracy, "EN_ACCURACY"},
        {EN_UNBALANCED, unbalanced_trials, "EN_UNBALANCED"},
        {EN_CHECKFREQ, static_cast<double>(hydraulic.check_frequency), "EN_CHECKFREQ"},
        {EN_MAXCHECK, static_cast<double>(hydraulic.maximum_check), "EN_MAXCHECK"},
        {EN_DAMPLIMIT, hydraulic.damping_limit, "EN_DAMPLIMIT"},
        {EN_HEADERROR, hydraulic.maximum_head_error_m, "EN_HEADERROR"},
        {EN_FLOWCHANGE, hydraulic.maximum_flow_change_m3_per_h, "EN_FLOWCHANGE"},
        {EN_DEMANDMULT, hydraulic.demand_multiplier, "EN_DEMANDMULT"},
        {EN_EMITEXPON, hydraulic.emitter_exponent, "EN_EMITEXPON"},
        {EN_EMITBACKFLOW, static_cast<double>(hydraulic.emitters_can_backflow ? EN_TRUE : EN_FALSE), "EN_EMITBACKFLOW"},
        {EN_SP_GRAVITY, hydraulic.specific_gravity, "EN_SP_GRAVITY"},
        {EN_SP_VISCOS, hydraulic.relative_viscosity, "EN_SP_VISCOS"},
        {EN_TOLERANCE, request.options_quality.tolerance, "EN_TOLERANCE"},
        {EN_SP_DIFFUS, request.options_quality.relative_diffusivity, "EN_SP_DIFFUS"},
        {EN_GLOBALEFFIC, request.options_energy.global_pump_efficiency_percent, "EN_GLOBALEFFIC"},
        {EN_GLOBALPRICE, request.options_energy.global_energy_price_per_kw_h, "EN_GLOBALPRICE"},
        {EN_DEMANDCHARGE, request.options_energy.demand_charge_per_kw, "EN_DEMANDCHARGE"}
    }};

    for (const NumericOption &option : options)
    {
        error = EN_setoption(this->project, option.option, option.value);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(%1)").arg(QString::fromLatin1(option.name)), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure an EPANET simulation option"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetProject::configureReport(const NetworkHydraulic &request) const
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
        const int error = EN_setreport(this->project, command_utf8.constData());
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreport"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the EPANET report"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetProject::retrieveInpText(const NetworkHydraulic &request, QString &inp_text) const
{
    inp_text.clear();

    double default_demand_pattern_index = 0.0;
    const int demand_pattern_error = EN_getoption(this->project, EN_DEMANDPATTERN, &default_demand_pattern_index);
    if (demand_pattern_error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, demand_pattern_error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_getoption(EN_DEMANDPATTERN)"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to inspect the default demand pattern before EPANET INP export"));
        if (!epanet_status.success)
            return epanet_status;
    }

    QTemporaryDir temporary_directory;
    if (!temporary_directory.isValid())
        return makeEpanetStatus(HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to create a temporary directory for the EPANET INP export"));

    const QString inp_path = temporary_directory.filePath(QStringLiteral("network.inp"));
    const QByteArray inp_path_native = QFile::encodeName(inp_path);
    const int error = EN_saveinpfile(this->project, inp_path_native.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_saveinpfile"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to serialize the EPANET project as an INP file"));
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
            const int pattern_error = EN_getpatternindex(this->project, pattern_id_utf8.constData(), &pattern_index);
            if (pattern_error == 205)
                break;
            if (pattern_error != 0)
            {
                const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, pattern_error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_getpatternindex"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("Failed to choose an unused default-demand-pattern sentinel for EPANET INP export"));
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

    return makeEpanetSuccess();
}

EN_Project EpanetProject::handle() const
{
    return this->project;
}

QString EpanetProject::errorMessage(int error_code) const
{
    if (error_code == 0)
        return QString();

    char message[256] = "";
    const int result = EN_geterror(error_code, message, sizeof(message));
    if (result != 0)
        return QStringLiteral("Unknown EPANET error code %1").arg(error_code);

    return QString::fromUtf8(message);
}

const QList<HydraulicSimulationDiagnostic> &EpanetProject::diagnostics() const
{
    return this->diagnostics_collected;
}

void EpanetProject::appendDiagnostic(const HydraulicSimulationDiagnostic &diagnostic) const
{
    this->diagnostics_collected.append(diagnostic);
}
