#include "epanet_inp_importer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_inp_geometry_importer.h"
#include "epanet_inp_metadata_importer.h"
#include "epanet_inp_report_importer.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double kw_per_hp = 0.7457;
constexpr double meters_per_foot = 0.3048;
constexpr int epanet_active_valve_status = 2;
constexpr int epanet_no_quality_source_error = 240;

struct ImportReferences
{
    QHash<int, QUuid> pattern_uuids_by_index;
    QHash<int, QUuid> curve_uuids_by_index;
    QHash<int, int> curve_types_by_index;
    QHash<int, QUuid> node_uuids_by_index;
    QHash<int, QUuid> link_uuids_by_index;
    QHash<int, int> link_types_by_index;
};

HydraulicSimulationStatus readFailure(
    const EpanetProject &project,
    int error,
    const QString &backend_operation,
    const QString &message,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::Network)
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

HydraulicSimulationStatus readSimpleControlActionTokens(
    const QString &input_file_path, QList<QString> &action_tokens)
{
    QFile file(input_file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Control,
            QString(),
            QStringLiteral("Could not read the source INP [CONTROLS] section: %1")
                .arg(file.errorString()));
    }

    bool in_controls = false;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (QString line : lines)
    {
        const qsizetype comment_index = line.indexOf(QLatin1Char(';'));
        if (comment_index >= 0)
            line.truncate(comment_index);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('[')))
        {
            in_controls = line.compare(QStringLiteral("[CONTROLS]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_controls)
            continue;

        const QStringList tokens = line.simplified().split(QLatin1Char(' '));
        if (tokens.size() < 3
            || tokens.at(0).compare(QStringLiteral("LINK"), Qt::CaseInsensitive) != 0)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Control,
                QString(),
                QStringLiteral("Could not parse a source INP simple-control statement"));
        }
        action_tokens.append(tokens.at(2));
    }

    return makeEpanetSuccess();
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

HydraulicSimulationStatus normalizeProjectToCanonicalUnits(
    EpanetProject &project, int source_flow_units)
{
    int error = EN_setflowunits(project.handle(), EN_CMH);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_setflowunits(EN_CMH)"),
            QStringLiteral("Failed to normalize EPANET flow and geometric units for INP import"));
    }

    error = EN_setoption(project.handle(), EN_PRESS_UNITS, EN_METERS);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_setoption(EN_PRESS_UNITS, EN_METERS)"),
            QStringLiteral("Failed to normalize EPANET pressure units for INP import"),
            HydraulicSimulationStatusEntityType::HydraulicSolver);
    }

    // EN_setflowunits() converts EPANET's typed curves and rule thresholds, but
    // constant-power pump Link.Km values intentionally remain numerically
    // unchanged even though their meaning switches from HP in US projects to
    // kW in SI projects. Canonicalize that native exception explicitly.
    if (!flowUnitsAreSi(source_flow_units))
    {
        int link_count = 0;
        error = EN_getcount(project.handle(), EN_LINKCOUNT, &link_count);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getcount(EN_LINKCOUNT)"),
                QStringLiteral("Failed to inspect pumps while normalizing EPANET units"),
                HydraulicSimulationStatusEntityType::Pump);
        }

        for (int link_index = 1; link_index <= link_count; link_index++)
        {
            int link_type = EN_PIPE;
            error = EN_getlinktype(project.handle(), link_index, &link_type);
            if (error != 0)
            {
                return readFailure(
                    project, error, QStringLiteral("EN_getlinktype"),
                    QStringLiteral("Failed to inspect pump type while normalizing EPANET units"),
                    HydraulicSimulationStatusEntityType::Pump);
            }
            if (link_type != EN_PUMP)
                continue;

            double power = 0.0;
            error = EN_getlinkvalue(project.handle(), link_index, EN_PUMP_POWER, &power);
            if (error != 0)
            {
                return readFailure(
                    project, error, QStringLiteral("EN_getlinkvalue(EN_PUMP_POWER)"),
                    QStringLiteral("Failed to read pump power while normalizing EPANET units"),
                    HydraulicSimulationStatusEntityType::Pump);
            }
            if (power <= 0.0)
                continue;

            error = EN_setlinkvalue(
                project.handle(), link_index, EN_PUMP_POWER, power * kw_per_hp);
            if (error != 0)
            {
                return readFailure(
                    project, error, QStringLiteral("EN_setlinkvalue(EN_PUMP_POWER)"),
                    QStringLiteral("Failed to normalize constant-power pump HP to kW"),
                    HydraulicSimulationStatusEntityType::Pump);
            }
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus readOption(
    const EpanetProject &project,
    int option,
    double &value,
    const QString &name,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::HydraulicSolver)
{
    const int error = EN_getoption(project.handle(), option, &value);
    if (error == 0)
        return makeEpanetSuccess();

    return readFailure(
        project,
        error,
        QStringLiteral("EN_getoption(%1)").arg(name),
        QStringLiteral("Failed to read an EPANET input option"),
        entity_type);
}

HydraulicSimulationStatus readTimeParameter(
    const EpanetProject &project,
    int parameter,
    quint64 &value,
    const QString &name,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::Network)
{
    long backend_value = 0;
    const int error = EN_gettimeparam(project.handle(), parameter, &backend_value);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_gettimeparam(%1)").arg(name),
            QStringLiteral("Failed to read an EPANET time parameter"),
            entity_type);
    }
    if (backend_value < 0)
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            entity_type,
            QString(),
            QStringLiteral("EPANET returned a negative time value for %1").arg(name));
    }

    value = static_cast<quint64>(backend_value);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus readTimeParameterInt(
    const EpanetProject &project,
    int parameter,
    int &value,
    const QString &name,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::Report)
{
    long backend_value = 0;
    const int error = EN_gettimeparam(project.handle(), parameter, &backend_value);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_gettimeparam(%1)").arg(name),
            QStringLiteral("Failed to read an EPANET report parameter"),
            entity_type);
    }

    value = static_cast<int>(backend_value);
    return makeEpanetSuccess();
}

bool resolveHeadlossFormula(int backend_formula, HydraulicHeadlossFormula &formula)
{
    switch (backend_formula)
    {
    case EN_HW:
        formula = HydraulicHeadlossFormula::HazenWilliams;
        return true;
    case EN_DW:
        formula = HydraulicHeadlossFormula::DarcyWeisbach;
        return true;
    case EN_CM:
        formula = HydraulicHeadlossFormula::ChezyManning;
        return true;
    default:
        return false;
    }
}

bool resolveDemandModel(int backend_model, HydraulicDemandModel &model)
{
    switch (backend_model)
    {
    case EN_DDA:
        model = HydraulicDemandModel::DemandDriven;
        return true;
    case EN_PDA:
        model = HydraulicDemandModel::PressureDriven;
        return true;
    default:
        return false;
    }
}

bool resolveReportStatistic(int backend_statistic, HydraulicSimulationReportStatistic &statistic)
{
    switch (backend_statistic)
    {
    case EN_SERIES:
        statistic = HydraulicSimulationReportStatistic::Series;
        return true;
    case EN_AVERAGE:
        statistic = HydraulicSimulationReportStatistic::Average;
        return true;
    case EN_MINIMUM:
        statistic = HydraulicSimulationReportStatistic::Minimum;
        return true;
    case EN_MAXIMUM:
        statistic = HydraulicSimulationReportStatistic::Maximum;
        return true;
    case EN_RANGE:
        statistic = HydraulicSimulationReportStatistic::Range;
        return true;
    default:
        return false;
    }
}

bool resolveReportStatus(int backend_status, HydraulicSimulationReportStatus &status)
{
    switch (backend_status)
    {
    case EN_NO_REPORT:
        status = HydraulicSimulationReportStatus::None;
        return true;
    case EN_NORMAL_REPORT:
        status = HydraulicSimulationReportStatus::Normal;
        return true;
    case EN_FULL_REPORT:
        status = HydraulicSimulationReportStatus::Full;
        return true;
    default:
        return false;
    }
}

bool resolveValveType(int backend_type, HydraulicLinkValveType &type)
{
    switch (backend_type)
    {
    case EN_PRV:
        type = HydraulicLinkValveType::PRV;
        return true;
    case EN_PSV:
        type = HydraulicLinkValveType::PSV;
        return true;
    case EN_FCV:
        type = HydraulicLinkValveType::FCV;
        return true;
    case EN_PBV:
        type = HydraulicLinkValveType::PBV;
        return true;
    case EN_TCV:
        type = HydraulicLinkValveType::TCV;
        return true;
    case EN_GPV:
        type = HydraulicLinkValveType::GPV;
        return true;
    case EN_PCV:
        type = HydraulicLinkValveType::PCV;
        return true;
    default:
        return false;
    }
}

constexpr int epanet_rule_if = 1;
constexpr int epanet_rule_and = 2;
constexpr int epanet_rule_or = 3;

bool resolveSimpleControlType(int backend_type, HydraulicControlSimpleType &type)
{
    switch (backend_type)
    {
    case EN_LOWLEVEL:
        type = HydraulicControlSimpleType::LowLevel;
        return true;
    case EN_HILEVEL:
        type = HydraulicControlSimpleType::HighLevel;
        return true;
    case EN_TIMER:
        type = HydraulicControlSimpleType::Timer;
        return true;
    case EN_TIMEOFDAY:
        type = HydraulicControlSimpleType::TimeOfDay;
        return true;
    default:
        return false;
    }
}

bool resolveRuleLogicalOperator(int backend_operator, HydraulicControlRuleLogicalOperator &logical_operator)
{
    switch (backend_operator)
    {
    case epanet_rule_if:
        logical_operator = HydraulicControlRuleLogicalOperator::If;
        return true;
    case epanet_rule_and:
        logical_operator = HydraulicControlRuleLogicalOperator::And;
        return true;
    case epanet_rule_or:
        logical_operator = HydraulicControlRuleLogicalOperator::Or;
        return true;
    default:
        return false;
    }
}

bool resolveRuleObject(int backend_object, HydraulicControlRuleObject &object)
{
    switch (backend_object)
    {
    case EN_R_NODE:
        object = HydraulicControlRuleObject::Node;
        return true;
    case EN_R_LINK:
        object = HydraulicControlRuleObject::Link;
        return true;
    case EN_R_SYSTEM:
        object = HydraulicControlRuleObject::System;
        return true;
    default:
        return false;
    }
}

bool resolveRuleVariable(int backend_variable, HydraulicControlRuleVariable &variable)
{
    switch (backend_variable)
    {
    case EN_R_DEMAND:
        variable = HydraulicControlRuleVariable::Demand;
        return true;
    case EN_R_HEAD:
        variable = HydraulicControlRuleVariable::Head;
        return true;
    case EN_R_GRADE:
        variable = HydraulicControlRuleVariable::Grade;
        return true;
    case EN_R_LEVEL:
        variable = HydraulicControlRuleVariable::Level;
        return true;
    case EN_R_PRESSURE:
        variable = HydraulicControlRuleVariable::Pressure;
        return true;
    case EN_R_FLOW:
        variable = HydraulicControlRuleVariable::Flow;
        return true;
    case EN_R_STATUS:
        variable = HydraulicControlRuleVariable::Status;
        return true;
    case EN_R_SETTING:
        variable = HydraulicControlRuleVariable::Setting;
        return true;
    case EN_R_POWER:
        variable = HydraulicControlRuleVariable::Power;
        return true;
    case EN_R_TIME:
        variable = HydraulicControlRuleVariable::Time;
        return true;
    case EN_R_CLOCKTIME:
        variable = HydraulicControlRuleVariable::ClockTime;
        return true;
    case EN_R_FILLTIME:
        variable = HydraulicControlRuleVariable::FillTime;
        return true;
    case EN_R_DRAINTIME:
        variable = HydraulicControlRuleVariable::DrainTime;
        return true;
    default:
        return false;
    }
}

bool resolveRuleOperator(int backend_operator, HydraulicControlRuleOperator &comparison)
{
    switch (backend_operator)
    {
    case EN_R_EQ:
        comparison = HydraulicControlRuleOperator::Equal;
        return true;
    case EN_R_NE:
        comparison = HydraulicControlRuleOperator::NotEqual;
        return true;
    case EN_R_LE:
        comparison = HydraulicControlRuleOperator::LessOrEqual;
        return true;
    case EN_R_GE:
        comparison = HydraulicControlRuleOperator::GreaterOrEqual;
        return true;
    case EN_R_LT:
        comparison = HydraulicControlRuleOperator::Less;
        return true;
    case EN_R_GT:
        comparison = HydraulicControlRuleOperator::Greater;
        return true;
    case EN_R_IS:
        comparison = HydraulicControlRuleOperator::Is;
        return true;
    case EN_R_NOT:
        comparison = HydraulicControlRuleOperator::IsNot;
        return true;
    case EN_R_BELOW:
        comparison = HydraulicControlRuleOperator::Below;
        return true;
    case EN_R_ABOVE:
        comparison = HydraulicControlRuleOperator::Above;
        return true;
    default:
        return false;
    }
}

bool resolveRuleStatus(int backend_status, HydraulicControlRuleStatus &status)
{
    switch (backend_status)
    {
    case EN_R_IS_OPEN:
        status = HydraulicControlRuleStatus::Open;
        return true;
    case EN_R_IS_CLOSED:
        status = HydraulicControlRuleStatus::Closed;
        return true;
    case EN_R_IS_ACTIVE:
        status = HydraulicControlRuleStatus::Active;
        return true;
    default:
        return false;
    }
}

bool assignControlLinkSetting(
    int backend_link_type, double value, HydraulicControlLinkSetting &setting)
{
    switch (backend_link_type)
    {
    case EN_PUMP:
        setting.pump_speed_ratio = value;
        return true;
    case EN_PRV:
    case EN_PSV:
    case EN_PBV:
        setting.valve_pressure_head_m = value;
        return true;
    case EN_FCV:
        setting.valve_flow_m3_per_h = value;
        return true;
    case EN_TCV:
        setting.valve_loss_coefficient = value;
        return true;
    case EN_PCV:
        setting.valve_position_percent = value;
        return true;
    default:
        return false;
    }
}

void appendImportWarning(
    EpanetResultImport &result,
    const QString &message,
    HydraulicSimulationStatusEntityType entity_type = HydraulicSimulationStatusEntityType::Network)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Warning;
    diagnostic.stage = HydraulicSimulationStatusStage::ReadInput;
    diagnostic.operation = HydraulicSimulationStatusOperation::ReadInput;
    diagnostic.entity.type = entity_type;
    diagnostic.message = message;
    diagnostic.backend_name = QStringLiteral("EPANET");
    diagnostic.backend_operation = QStringLiteral("INP import");
    appendEpanetDiagnosticIfUnique(result.diagnostics, diagnostic);
    result.complete = false;
}

EpanetResultImport finishImport(
    EpanetResultImport result,
    const HydraulicSimulationStatus &status,
    const EpanetProject &project)
{
    result.status = status;
    if (!status.success)
        result.complete = false;
    appendEpanetDiagnostics(result.diagnostics, project.diagnostics());
    if (!status.success)
        appendEpanetDiagnosticIfUnique(result.diagnostics, epanetDiagnosticFromStatus(status));
    return result;
}

HydraulicSimulationStatus importTitles(EpanetProject &project, NetworkHydraulic &network)
{
    std::array<char, 256> line_1{};
    std::array<char, 256> line_2{};
    std::array<char, 256> line_3{};
    const int error = EN_gettitle(project.handle(), line_1.data(), line_2.data(), line_3.data());
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_gettitle"),
            QStringLiteral("Failed to read EPANET title lines"),
            HydraulicSimulationStatusEntityType::Network);
    }

    network.title_line_1 = QString::fromUtf8(line_1.data());
    network.title_line_2 = QString::fromUtf8(line_2.data());
    network.title_line_3 = QString::fromUtf8(line_3.data());
    return makeEpanetSuccess();
}

HydraulicSimulationStatus readObjectComment(
    const EpanetProject &project,
    int object_type,
    int object_index,
    QString &comment,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &object_description)
{
    char value[EN_MAXMSG + 1] = {};
    const int error = EN_getcomment(project.handle(), object_type, object_index, value);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcomment"),
            QStringLiteral("Failed to read EPANET %1 comment").arg(object_description),
            entity_type);
    }

    comment = QString::fromUtf8(value).trimmed();
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importPatterns(
    EpanetProject &project,
    EpanetResultImport &result,
    ImportReferences &references)
{
    int pattern_count = 0;
    int error = EN_getcount(project.handle(), EN_PATCOUNT, &pattern_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_PATCOUNT)"),
            QStringLiteral("Failed to read EPANET pattern count"),
            HydraulicSimulationStatusEntityType::Pattern);
    }

    for (int pattern_index = 1; pattern_index <= pattern_count; pattern_index++)
    {
        char pattern_id_value[EN_MAXID + 1] = {};
        error = EN_getpatternid(project.handle(), pattern_index, pattern_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getpatternid"),
                QStringLiteral("Failed to read EPANET pattern ID"),
                HydraulicSimulationStatusEntityType::Pattern);
        }

        int pattern_length = 0;
        error = EN_getpatternlen(project.handle(), pattern_index, &pattern_length);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getpatternlen"),
                QStringLiteral("Failed to read EPANET pattern length"),
                HydraulicSimulationStatusEntityType::Pattern);
        }
        if (pattern_length <= 0)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Pattern,
                QString::fromUtf8(pattern_id_value),
                QStringLiteral("EPANET returned an empty time pattern"));
        }

        HydraulicPatternTime pattern;
        pattern.id = QString::fromUtf8(pattern_id_value);
        pattern.uuid = QUuid::createUuid();
        for (int period = 1; period <= pattern_length; period++)
        {
            double multiplier = 0.0;
            error = EN_getpatternvalue(project.handle(), pattern_index, period, &multiplier);
            if (error != 0)
            {
                return readFailure(
                    project,
                    error,
                    QStringLiteral("EN_getpatternvalue"),
                    QStringLiteral("Failed to read EPANET pattern multiplier"),
                    HydraulicSimulationStatusEntityType::Pattern);
            }
            pattern.multipliers.append(multiplier);
        }

        HydraulicSimulationStatus status = readObjectComment(
            project,
            EN_TIMEPAT,
            pattern_index,
            pattern.comment,
            HydraulicSimulationStatusEntityType::Pattern,
            QStringLiteral("time pattern"));
        if (!status.success)
            return status;

        references.pattern_uuids_by_index.insert(pattern_index, pattern.uuid);
        result.request.network.patterns_time.append(pattern);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importCurves(
    EpanetProject &project,
    EpanetResultImport &result,
    ImportReferences &references)
{
    int curve_count = 0;
    int error = EN_getcount(project.handle(), EN_CURVECOUNT, &curve_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_CURVECOUNT)"),
            QStringLiteral("Failed to read EPANET curve count"),
            HydraulicSimulationStatusEntityType::Curve);
    }

    for (int curve_index = 1; curve_index <= curve_count; curve_index++)
    {
        char curve_id_value[EN_MAXID + 1] = {};
        error = EN_getcurveid(project.handle(), curve_index, curve_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcurveid"),
                QStringLiteral("Failed to read EPANET curve ID"),
                HydraulicSimulationStatusEntityType::Curve);
        }
        const QString curve_id = QString::fromUtf8(curve_id_value);

        int curve_type = EN_GENERIC_CURVE;
        error = EN_getcurvetype(project.handle(), curve_index, &curve_type);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcurvetype"),
                QStringLiteral("Failed to read EPANET curve type"),
                HydraulicSimulationStatusEntityType::Curve);
        }

        int point_count = 0;
        error = EN_getcurvelen(project.handle(), curve_index, &point_count);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcurvelen"),
                QStringLiteral("Failed to read EPANET curve length"),
                HydraulicSimulationStatusEntityType::Curve);
        }
        if (point_count <= 0)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Curve,
                curve_id,
                QStringLiteral("EPANET returned an empty curve"));
        }

        QString comment;
        HydraulicSimulationStatus status = readObjectComment(
            project,
            EN_CURVE,
            curve_index,
            comment,
            HydraulicSimulationStatusEntityType::Curve,
            QStringLiteral("curve"));
        if (!status.success)
            return status;

        const QUuid curve_uuid = QUuid::createUuid();
        references.curve_uuids_by_index.insert(curve_index, curve_uuid);
        references.curve_types_by_index.insert(curve_index, curve_type);

        if (curve_type == EN_VOLUME_CURVE)
        {
            HydraulicCurveTankVolume curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurveTankVolumePoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index,
                    &point.water_level_m, &point.volume_m3);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET tank-volume curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_tank_volume.append(curve);
        }
        else if (curve_type == EN_PUMP_CURVE)
        {
            HydraulicCurvePumpHead curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurvePumpHeadPoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index,
                    &point.flow_m3_per_h, &point.head_gain_m);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET pump-head curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_pump_head.append(curve);
        }
        else if (curve_type == EN_EFFIC_CURVE)
        {
            HydraulicCurvePumpEfficiency curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurvePumpEfficiencyPoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index,
                    &point.flow_m3_per_h, &point.efficiency_percent);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET pump-efficiency curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_pump_efficiency.append(curve);
        }
        else if (curve_type == EN_HLOSS_CURVE)
        {
            HydraulicCurveValveHeadloss curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurveValveHeadlossPoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index,
                    &point.flow_m3_per_h, &point.head_loss_m);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET valve head-loss curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_valve_headloss.append(curve);
        }
        else if (curve_type == EN_VALVE_CURVE)
        {
            HydraulicCurveValveCharacteristic curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurveValveCharacteristicPoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index,
                    &point.position_percent, &point.relative_flow_percent);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET valve-characteristic curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_valve_characteristic.append(curve);
        }
        else if (curve_type == EN_GENERIC_CURVE)
        {
            HydraulicCurveGeneric curve;
            curve.id = curve_id;
            curve.uuid = curve_uuid;
            curve.comment = comment;
            for (int point_index = 1; point_index <= point_count; point_index++)
            {
                HydraulicCurveGenericPoint point;
                error = EN_getcurvevalue(
                    project.handle(), curve_index, point_index, &point.x, &point.y);
                if (error != 0)
                {
                    return readFailure(
                        project, error, QStringLiteral("EN_getcurvevalue"),
                        QStringLiteral("Failed to read EPANET generic curve point"),
                        HydraulicSimulationStatusEntityType::Curve);
                }
                curve.points.append(point);
            }
            result.request.network.curves_generic.append(curve);
        }
        else
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Curve,
                curve_id,
                curve_uuid,
                QStringLiteral("EPANET returned an unsupported curve type"));
        }
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importTimes(EpanetProject &project, NetworkHydraulic &network)
{
    struct TimeField
    {
        int parameter;
        quint64 NetworkHydraulic::*member;
        const char *name;
    };

    const std::array<TimeField, 9> fields = {{
        {EN_DURATION, &NetworkHydraulic::duration_s, "EN_DURATION"},
        {EN_HYDSTEP, &NetworkHydraulic::timestep_hydraulic_s, "EN_HYDSTEP"},
        {EN_QUALSTEP, &NetworkHydraulic::timestep_quality_s, "EN_QUALSTEP"},
        {EN_PATTERNSTEP, &NetworkHydraulic::timestep_pattern_s, "EN_PATTERNSTEP"},
        {EN_PATTERNSTART, &NetworkHydraulic::start_pattern_s, "EN_PATTERNSTART"},
        {EN_REPORTSTEP, &NetworkHydraulic::timestep_report_s, "EN_REPORTSTEP"},
        {EN_REPORTSTART, &NetworkHydraulic::start_report_s, "EN_REPORTSTART"},
        {EN_RULESTEP, &NetworkHydraulic::timestep_rule_s, "EN_RULESTEP"},
        {EN_STARTTIME, &NetworkHydraulic::start_time_of_day_s, "EN_STARTTIME"}
    }};

    for (const TimeField &field : fields)
    {
        quint64 value = 0;
        const HydraulicSimulationStatus status = readTimeParameter(
            project, field.parameter, value, QString::fromLatin1(field.name));
        if (!status.success)
            return status;
        network.*(field.member) = value;
    }

    int backend_statistic = 0;
    HydraulicSimulationStatus status = readTimeParameterInt(
        project, EN_STATISTIC, backend_statistic, QStringLiteral("EN_STATISTIC"));
    if (!status.success)
        return status;
    if (!resolveReportStatistic(backend_statistic, network.report_statistic))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Report,
            QString(),
            QStringLiteral("EPANET returned an unsupported report statistic"));
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importHydraulicOptions(
    EpanetProject &project,
    NetworkHydraulic &network,
    const ImportReferences &references)
{
    double value = 0.0;
    HydraulicSimulationStatus status = readOption(
        project, EN_SP_GRAVITY, value, QStringLiteral("EN_SP_GRAVITY"));
    if (!status.success)
        return status;
    network.options_hydraulic.specific_gravity = value;

    int demand_model = 0;
    double minimum_pressure = 0.0;
    double required_pressure = 0.0;
    double pressure_exponent = 0.0;
    const int error = EN_getdemandmodel(
        project.handle(),
        &demand_model,
        &minimum_pressure,
        &required_pressure,
        &pressure_exponent);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getdemandmodel"),
            QStringLiteral("Failed to read EPANET demand model"));
    }
    if (!resolveDemandModel(demand_model, network.options_hydraulic.demand_model))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("EPANET returned an unsupported demand model"));
    }
    network.options_hydraulic.minimum_pressure_head_m = minimum_pressure;
    network.options_hydraulic.required_pressure_head_m = required_pressure;
    network.options_hydraulic.pressure_exponent = pressure_exponent;

    struct ScalarOption
    {
        int option;
        const char *name;
    };
    const std::array<ScalarOption, 13> options = {{
        {EN_HEADLOSSFORM, "EN_HEADLOSSFORM"},
        {EN_TRIALS, "EN_TRIALS"},
        {EN_ACCURACY, "EN_ACCURACY"},
        {EN_UNBALANCED, "EN_UNBALANCED"},
        {EN_CHECKFREQ, "EN_CHECKFREQ"},
        {EN_MAXCHECK, "EN_MAXCHECK"},
        {EN_DAMPLIMIT, "EN_DAMPLIMIT"},
        {EN_HEADERROR, "EN_HEADERROR"},
        {EN_FLOWCHANGE, "EN_FLOWCHANGE"},
        {EN_DEMANDMULT, "EN_DEMANDMULT"},
        {EN_EMITBACKFLOW, "EN_EMITBACKFLOW"},
        {EN_SP_VISCOS, "EN_SP_VISCOS"},
        {EN_DEMANDPATTERN, "EN_DEMANDPATTERN"}
    }};

    std::array<double, 13> values{};
    for (std::size_t index = 0; index < options.size(); index++)
    {
        status = readOption(
            project,
            options.at(index).option,
            values.at(index),
            QString::fromLatin1(options.at(index).name));
        if (!status.success)
            return status;
    }

    const int headloss_formula = static_cast<int>(std::llround(values.at(0)));
    if (!resolveHeadlossFormula(headloss_formula, network.options_hydraulic.headloss_formula))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("EPANET returned an unsupported headloss formula"));
    }

    network.options_hydraulic.maximum_trials = static_cast<int>(std::llround(values.at(1)));
    network.options_hydraulic.accuracy = values.at(2);
    const int extra_trials = static_cast<int>(std::llround(values.at(3)));
    if (extra_trials < 0)
    {
        network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Stop;
        network.options_hydraulic.unbalanced_extra_trials = 0;
    }
    else
    {
        network.options_hydraulic.unbalanced_action = HydraulicUnbalancedAction::Continue;
        network.options_hydraulic.unbalanced_extra_trials = extra_trials;
    }
    network.options_hydraulic.check_frequency = static_cast<int>(std::llround(values.at(4)));
    network.options_hydraulic.maximum_check = static_cast<int>(std::llround(values.at(5)));
    network.options_hydraulic.damping_limit = values.at(6);
    network.options_hydraulic.maximum_head_error_m = values.at(7);
    network.options_hydraulic.maximum_flow_change_m3_per_h = values.at(8);
    network.options_hydraulic.demand_multiplier = values.at(9);
    network.options_hydraulic.emitters_can_backflow = static_cast<int>(std::llround(values.at(10))) == EN_TRUE;
    network.options_hydraulic.relative_viscosity = values.at(11);

    const int default_pattern_index = static_cast<int>(std::llround(values.at(12)));
    if (default_pattern_index > 0)
    {
        if (!references.pattern_uuids_by_index.contains(default_pattern_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pattern,
                QString(),
                QStringLiteral("Could not resolve EPANET default demand pattern"));
        }
        network.options_hydraulic.default_demand_pattern_uuid =
            references.pattern_uuids_by_index.value(default_pattern_index);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importEnergyOptions(
    EpanetProject &project,
    NetworkHydraulic &network,
    const ImportReferences &references)
{
    struct EnergyOption
    {
        int option;
        double PumpEnergyOptions::*member;
        const char *name;
    };

    const std::array<EnergyOption, 3> options = {{
        {EN_GLOBALEFFIC, &PumpEnergyOptions::global_pump_efficiency_percent, "EN_GLOBALEFFIC"},
        {EN_GLOBALPRICE, &PumpEnergyOptions::global_energy_price_per_kw_h, "EN_GLOBALPRICE"},
        {EN_DEMANDCHARGE, &PumpEnergyOptions::demand_charge_per_kw, "EN_DEMANDCHARGE"}
    }};

    for (const EnergyOption &option : options)
    {
        double value = 0.0;
        const HydraulicSimulationStatus status = readOption(
            project,
            option.option,
            value,
            QString::fromLatin1(option.name),
            HydraulicSimulationStatusEntityType::HydraulicSolver);
        if (!status.success)
            return status;
        network.options_energy.*(option.member) = value;
    }

    double global_pattern_value = 0.0;
    const HydraulicSimulationStatus pattern_status = readOption(
        project,
        EN_GLOBALPATTERN,
        global_pattern_value,
        QStringLiteral("EN_GLOBALPATTERN"),
        HydraulicSimulationStatusEntityType::Pattern);
    if (!pattern_status.success)
        return pattern_status;

    const int global_pattern_index = static_cast<int>(std::llround(global_pattern_value));
    if (global_pattern_index > 0)
    {
        if (!references.pattern_uuids_by_index.contains(global_pattern_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pattern,
                QString(),
                QStringLiteral("Could not resolve EPANET global energy-price pattern"));
        }
        network.options_energy.global_energy_price_pattern_uuid =
            references.pattern_uuids_by_index.value(global_pattern_index);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importReportStatus(EpanetProject &project, NetworkHydraulic &network)
{
    double backend_status_value = 0.0;
    const HydraulicSimulationStatus status = readOption(
        project,
        EN_STATUS_REPORT,
        backend_status_value,
        QStringLiteral("EN_STATUS_REPORT"),
        HydraulicSimulationStatusEntityType::Report);
    if (!status.success)
        return status;

    const int backend_status = static_cast<int>(std::llround(backend_status_value));
    if (!resolveReportStatus(backend_status, network.options_report.status))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Report,
            QString(),
            QStringLiteral("EPANET returned an unsupported report status level"));
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus readNodeValue(
    const EpanetProject &project,
    int node_index,
    int property,
    double &value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &property_name)
{
    const int error = EN_getnodevalue(project.handle(), node_index, property, &value);
    if (error == 0)
        return makeEpanetSuccess();
    return readFailure(
        project,
        error,
        QStringLiteral("EN_getnodevalue(%1)").arg(property_name),
        QStringLiteral("Failed to read EPANET node input"),
        entity_type);
}

HydraulicSimulationStatus readLinkValue(
    const EpanetProject &project,
    int link_index,
    int property,
    double &value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &property_name)
{
    const int error = EN_getlinkvalue(project.handle(), link_index, property, &value);
    if (error == 0)
        return makeEpanetSuccess();
    return readFailure(
        project,
        error,
        QStringLiteral("EN_getlinkvalue(%1)").arg(property_name),
        QStringLiteral("Failed to read EPANET link input"),
        entity_type);
}

HydraulicSimulationStatus importJunction(
    EpanetProject &project,
    EpanetResultImport &result,
    int node_index,
    const QString &node_id,
    const QUuid &node_uuid,
    double emitter_exponent,
    const ImportReferences &references)
{
    HydraulicNodeJunction junction;
    junction.id = node_id;
    junction.uuid = node_uuid;
    junction.elevation_input_type = HydraulicNodeElevationInputType::TotalElevation;

    double value = 0.0;
    HydraulicSimulationStatus status = readNodeValue(
        project, node_index, EN_ELEVATION, value,
        HydraulicSimulationStatusEntityType::Junction, QStringLiteral("EN_ELEVATION"));
    if (!status.success)
        return status;
    junction.elevation_m = value;

    int demand_count = 0;
    int error = EN_getnumdemands(project.handle(), node_index, &demand_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getnumdemands"),
            QStringLiteral("Failed to read EPANET junction demand categories"),
            HydraulicSimulationStatusEntityType::Junction);
    }

    for (int demand_index = 1; demand_index <= demand_count; demand_index++)
    {
        HydraulicNodeJunctionDemand demand;
        double base_demand = 0.0;
        error = EN_getbasedemand(project.handle(), node_index, demand_index, &base_demand);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getbasedemand"),
                QStringLiteral("Failed to read EPANET junction base demand"),
                HydraulicSimulationStatusEntityType::Junction);
        }
        demand.base_demand_m3_per_h = base_demand;

        char demand_name[EN_MAXID + 1] = {};
        error = EN_getdemandname(project.handle(), node_index, demand_index, demand_name);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getdemandname"),
                QStringLiteral("Failed to read EPANET junction demand category name"),
                HydraulicSimulationStatusEntityType::Junction);
        }
        demand.category_name = QString::fromUtf8(demand_name);

        int pattern_index = 0;
        error = EN_getdemandpattern(project.handle(), node_index, demand_index, &pattern_index);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getdemandpattern"),
                QStringLiteral("Failed to read EPANET junction demand pattern reference"),
                HydraulicSimulationStatusEntityType::Junction);
        }
        demand.pattern_mode = HydraulicTimePatternMode::Constant;
        if (pattern_index > 0)
        {
            if (!references.pattern_uuids_by_index.contains(pattern_index))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    HydraulicSimulationStatusEntityType::Pattern,
                    junction.id,
                    junction.uuid,
                    QStringLiteral("Could not resolve imported junction demand pattern"));
            }
            demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
            demand.pattern_uuid = references.pattern_uuids_by_index.value(pattern_index);
        }
        else if (!result.request.network.options_hydraulic.default_demand_pattern_uuid.isNull())
        {
            // EPANET applies the project default pattern to demand categories whose
            // native pattern index is zero. Make that effective relationship
            // explicit in the AOWIS demand so rebuilding the network preserves it.
            demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
            demand.pattern_uuid = result.request.network.options_hydraulic.default_demand_pattern_uuid;
        }

        junction.demands.append(demand);
    }

    status = readNodeValue(
        project, node_index, EN_EMITTER, value,
        HydraulicSimulationStatusEntityType::Junction, QStringLiteral("EN_EMITTER"));
    if (!status.success)
        return status;
    junction.emitter.pressure_exponent = emitter_exponent;
    junction.emitter.coefficient = value;

    result.request.network.nodes_junctions.append(junction);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importReservoir(
    EpanetProject &project,
    EpanetResultImport &result,
    int node_index,
    const QString &node_id,
    const QUuid &node_uuid,
    const ImportReferences &references)
{
    HydraulicNodeReservoir reservoir;
    reservoir.id = node_id;
    reservoir.uuid = node_uuid;
    reservoir.head_input_type = HydraulicNodeElevationInputType::TotalHead;

    double value = 0.0;
    HydraulicSimulationStatus status = readNodeValue(
        project, node_index, EN_ELEVATION, value,
        HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("EN_ELEVATION"));
    if (!status.success)
        return status;
    reservoir.hydraulic_head_m = value;

    status = readNodeValue(
        project, node_index, EN_PATTERN, value,
        HydraulicSimulationStatusEntityType::Reservoir, QStringLiteral("EN_PATTERN"));
    if (!status.success)
        return status;
    reservoir.head_pattern_mode = HydraulicTimePatternMode::Constant;
    const int pattern_index = static_cast<int>(std::llround(value));
    if (pattern_index > 0)
    {
        if (!references.pattern_uuids_by_index.contains(pattern_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pattern,
                reservoir.id,
                reservoir.uuid,
                QStringLiteral("Could not resolve imported reservoir head pattern"));
        }
        reservoir.head_pattern_mode = HydraulicTimePatternMode::TimePattern;
        reservoir.head_pattern_uuid = references.pattern_uuids_by_index.value(pattern_index);
    }

    result.request.network.nodes_reservoirs.append(reservoir);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importTank(
    EpanetProject &project,
    EpanetResultImport &result,
    int node_index,
    const QString &node_id,
    const QUuid &node_uuid,
    const ImportReferences &references)
{
    HydraulicNodeTank tank;
    tank.id = node_id;
    tank.uuid = node_uuid;
    tank.elevation_input_type = HydraulicNodeTankElevationInputType::BottomElevation;
    tank.geometry_input_type = HydraulicNodeTankGeometryInputType::Cylindrical;

    struct NodeValueField
    {
        int property;
        const char *name;
        double *target;
    };

    double elevation = 0.0;
    double initial_level = 0.0;
    double minimum_level = 0.0;
    double maximum_level = 0.0;
    double diameter = 0.0;
    double minimum_volume = 0.0;
    double maximum_volume = 0.0;
    double volume_curve_index = 0.0;
    double can_overflow = 0.0;
    const std::array<NodeValueField, 9> fields = {{
        {EN_ELEVATION, "EN_ELEVATION", &elevation},
        {EN_TANKLEVEL, "EN_TANKLEVEL", &initial_level},
        {EN_MINLEVEL, "EN_MINLEVEL", &minimum_level},
        {EN_MAXLEVEL, "EN_MAXLEVEL", &maximum_level},
        {EN_TANKDIAM, "EN_TANKDIAM", &diameter},
        {EN_MINVOLUME, "EN_MINVOLUME", &minimum_volume},
        {EN_MAXVOLUME, "EN_MAXVOLUME", &maximum_volume},
        {EN_VOLCURVE, "EN_VOLCURVE", &volume_curve_index},
        {EN_CANOVERFLOW, "EN_CANOVERFLOW", &can_overflow}
    }};

    for (const NodeValueField &field : fields)
    {
        HydraulicSimulationStatus status = readNodeValue(
            project,
            node_index,
            field.property,
            *field.target,
            HydraulicSimulationStatusEntityType::Tank,
            QString::fromLatin1(field.name));
        if (!status.success)
            return status;
    }

    tank.bottom_elevation_m = elevation;
    tank.water_level_initial_m = initial_level;
    tank.water_level_minimum_m = minimum_level;
    tank.water_level_maximum_m = maximum_level;
    tank.diameter_m = diameter;
    tank.cross_section_area_m2 = pi * tank.diameter_m * tank.diameter_m / 4.0;
    // Toolkit readback is intentionally semantic: EPANET may normalize source
    // tokens such as a zero tank minimum volume into the geometric volume it
    // actually simulates. Preserve that native semantic value in the model.
    tank.minimum_volume_m3 = minimum_volume;
    tank.volume_at_maximum_level_m3 = maximum_volume;
    tank.can_overflow = static_cast<int>(std::llround(can_overflow)) == EN_TRUE;

    const int volume_curve_backend_index = static_cast<int>(std::llround(volume_curve_index));
    if (volume_curve_backend_index > 0)
    {
        if (!references.curve_uuids_by_index.contains(volume_curve_backend_index)
            || references.curve_types_by_index.value(volume_curve_backend_index, -1) != EN_VOLUME_CURVE)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Curve,
                tank.id,
                tank.uuid,
                QStringLiteral("Could not resolve imported tank volume curve"));
        }
        tank.geometry_input_type = HydraulicNodeTankGeometryInputType::VolumeCurve;
        tank.volume_curve_uuid = references.curve_uuids_by_index.value(volume_curve_backend_index);
    }

    result.request.network.nodes_tanks.append(tank);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importPipe(
    EpanetProject &project,
    EpanetResultImport &result,
    int link_index,
    int link_type,
    const QString &link_id,
    const QUuid &link_uuid,
    const QHash<int, QUuid> &node_uuids_by_index)
{
    int node_from_index = 0;
    int node_to_index = 0;
    int error = EN_getlinknodes(
        project.handle(), link_index, &node_from_index, &node_to_index);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getlinknodes"),
            QStringLiteral("Failed to read EPANET pipe endpoints"),
            HydraulicSimulationStatusEntityType::Pipe);
    }

    if (!node_uuids_by_index.contains(node_from_index)
        || !node_uuids_by_index.contains(node_to_index))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ResolveEntity,
            HydraulicSimulationStatusEntityType::Pipe,
            link_id,
            link_uuid,
            QStringLiteral("Could not resolve imported EPANET pipe endpoint"));
    }

    HydraulicLinkPipe pipe;
    pipe.id = link_id;
    pipe.uuid = link_uuid;
    pipe.node_uuid_from = node_uuids_by_index.value(node_from_index);
    pipe.node_uuid_to = node_uuids_by_index.value(node_to_index);

    struct LinkValueField
    {
        int property;
        const char *name;
        double *target;
    };

    double length = 0.0;
    double diameter = 0.0;
    double roughness = 0.0;
    double minor_loss = 0.0;
    double initial_status = 0.0;
    double leak_area = 0.0;
    double leak_expansion = 0.0;
    const std::array<LinkValueField, 7> fields = {{
        {EN_LENGTH, "EN_LENGTH", &length},
        {EN_DIAMETER, "EN_DIAMETER", &diameter},
        {EN_ROUGHNESS, "EN_ROUGHNESS", &roughness},
        {EN_MINORLOSS, "EN_MINORLOSS", &minor_loss},
        {EN_INITSTATUS, "EN_INITSTATUS", &initial_status},
        {EN_LEAK_AREA, "EN_LEAK_AREA", &leak_area},
        {EN_LEAK_EXPAN, "EN_LEAK_EXPAN", &leak_expansion}
    }};

    for (const LinkValueField &field : fields)
    {
        HydraulicSimulationStatus status = readLinkValue(
            project,
            link_index,
            field.property,
            *field.target,
            HydraulicSimulationStatusEntityType::Pipe,
            QString::fromLatin1(field.name));
        if (!status.success)
            return status;
    }

    pipe.length_measured_m = length;
    pipe.diameter_mm = diameter;
    pipe.minor_loss_coefficient = minor_loss;
    pipe.leak_area_mm2_per_100m = leak_area;
    pipe.leak_area_expansion_per_pressure_head_mm2_per_m = leak_expansion;

    switch (result.request.network.options_hydraulic.headloss_formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        pipe.roughness_hazen_williams = roughness;
        break;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        pipe.roughness_darcy_weisbach_mm = roughness;
        break;
    case HydraulicHeadlossFormula::ChezyManning:
        pipe.roughness_chezy_manning = roughness;
        break;
    }

    if (link_type == EN_CVPIPE)
    {
        pipe.initial_status = HydraulicLinkPipeInitialStatus::CheckValve;
    }
    else
    {
        const int backend_status = static_cast<int>(std::llround(initial_status));
        if (backend_status == EN_OPEN)
            pipe.initial_status = HydraulicLinkPipeInitialStatus::Open;
        else if (backend_status == EN_CLOSED)
            pipe.initial_status = HydraulicLinkPipeInitialStatus::Closed;
        else
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Pipe,
                link_id,
                link_uuid,
                QStringLiteral("EPANET returned an unsupported initial pipe status"));
        }
    }

    result.request.network.links_pipes.append(pipe);
    return makeEpanetSuccess();
}

int pumpHeadCurvePointCount(const NetworkHydraulic &network, const QUuid &curve_uuid)
{
    for (const HydraulicCurvePumpHead &curve : network.curves_pump_head)
    {
        if (curve.uuid == curve_uuid)
            return curve.points.size();
    }
    return 0;
}

HydraulicSimulationStatus importPump(
    EpanetProject &project,
    EpanetResultImport &result,
    int link_index,
    const QString &link_id,
    const QUuid &link_uuid,
    const ImportReferences &references)
{
    int node_from_index = 0;
    int node_to_index = 0;
    int error = EN_getlinknodes(
        project.handle(), link_index, &node_from_index, &node_to_index);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getlinknodes"),
            QStringLiteral("Failed to read EPANET pump endpoints"),
            HydraulicSimulationStatusEntityType::Pump);
    }
    if (!references.node_uuids_by_index.contains(node_from_index)
        || !references.node_uuids_by_index.contains(node_to_index))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ResolveEntity,
            HydraulicSimulationStatusEntityType::Pump,
            link_id,
            link_uuid,
            QStringLiteral("Could not resolve imported EPANET pump endpoint"));
    }

    HydraulicLinkPump pump;
    pump.id = link_id;
    pump.uuid = link_uuid;
    pump.node_uuid_from = references.node_uuids_by_index.value(node_from_index);
    pump.node_uuid_to = references.node_uuids_by_index.value(node_to_index);

    double power = 0.0;
    double head_curve_index_value = 0.0;
    double initial_speed = 0.0;
    double initial_status = 0.0;
    double speed_pattern_index_value = 0.0;
    double efficiency_curve_index_value = 0.0;
    double energy_price = 0.0;
    double energy_pattern_index_value = 0.0;

    struct PumpValueField
    {
        int property;
        const char *name;
        double *target;
    };
    const std::array<PumpValueField, 8> fields = {{
        {EN_PUMP_POWER, "EN_PUMP_POWER", &power},
        {EN_PUMP_HCURVE, "EN_PUMP_HCURVE", &head_curve_index_value},
        {EN_INITSETTING, "EN_INITSETTING", &initial_speed},
        {EN_INITSTATUS, "EN_INITSTATUS", &initial_status},
        {EN_LINKPATTERN, "EN_LINKPATTERN", &speed_pattern_index_value},
        {EN_PUMP_ECURVE, "EN_PUMP_ECURVE", &efficiency_curve_index_value},
        {EN_PUMP_ECOST, "EN_PUMP_ECOST", &energy_price},
        {EN_PUMP_EPAT, "EN_PUMP_EPAT", &energy_pattern_index_value}
    }};

    for (const PumpValueField &field : fields)
    {
        HydraulicSimulationStatus status = readLinkValue(
            project,
            link_index,
            field.property,
            *field.target,
            HydraulicSimulationStatusEntityType::Pump,
            QString::fromLatin1(field.name));
        if (!status.success)
            return status;
    }

    if (power > 0.0)
    {
        pump.definition_type = HydraulicLinkPumpDefinitionType::ConstantPower;
        pump.constant_power_kw = power;
    }
    else
    {
        const int head_curve_index = static_cast<int>(std::llround(head_curve_index_value));
        if (head_curve_index <= 0
            || !references.curve_uuids_by_index.contains(head_curve_index)
            || references.curve_types_by_index.value(head_curve_index, -1) != EN_PUMP_CURVE)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Curve,
                pump.id,
                pump.uuid,
                QStringLiteral("Could not resolve imported pump head curve"));
        }

        pump.head_curve_uuid = references.curve_uuids_by_index.value(head_curve_index);
        const int point_count = pumpHeadCurvePointCount(result.request.network, pump.head_curve_uuid);
        if (point_count == 1)
            pump.definition_type = HydraulicLinkPumpDefinitionType::OnePointCurve;
        else if (point_count == 3)
            pump.definition_type = HydraulicLinkPumpDefinitionType::ThreePointCurve;
        else if (point_count >= 2)
            pump.definition_type = HydraulicLinkPumpDefinitionType::MultiPointCurve;
        else
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Pump,
                pump.id,
                pump.uuid,
                QStringLiteral("Imported pump head curve has an unsupported point count"));
        }
    }

    pump.initial_speed_ratio = initial_speed;
    const int backend_status = static_cast<int>(std::llround(initial_status));
    if (backend_status == EN_OPEN)
        pump.initial_status = HydraulicLinkPumpInitialStatus::On;
    else if (backend_status == EN_CLOSED)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;
    else
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Pump,
            pump.id,
            pump.uuid,
            QStringLiteral("EPANET returned an unsupported initial pump status"));
    }

    const int speed_pattern_index = static_cast<int>(std::llround(speed_pattern_index_value));
    if (speed_pattern_index > 0)
    {
        if (!references.pattern_uuids_by_index.contains(speed_pattern_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pattern,
                pump.id,
                pump.uuid,
                QStringLiteral("Could not resolve imported pump speed pattern"));
        }
        pump.speed_pattern_uuid = references.pattern_uuids_by_index.value(speed_pattern_index);
    }

    const int efficiency_curve_index = static_cast<int>(std::llround(efficiency_curve_index_value));
    if (efficiency_curve_index > 0)
    {
        if (!references.curve_uuids_by_index.contains(efficiency_curve_index)
            || references.curve_types_by_index.value(efficiency_curve_index, -1) != EN_EFFIC_CURVE)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Curve,
                pump.id,
                pump.uuid,
                QStringLiteral("Could not resolve imported pump efficiency curve"));
        }
        pump.efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Curve;
        pump.efficiency_curve_uuid = references.curve_uuids_by_index.value(efficiency_curve_index);
    }
    else
    {
        pump.efficiency_input_type = HydraulicLinkPumpEfficiencyInputType::Global;
    }

    const int energy_pattern_index = static_cast<int>(std::llround(energy_pattern_index_value));
    if (energy_pattern_index > 0)
    {
        if (!references.pattern_uuids_by_index.contains(energy_pattern_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pattern,
                pump.id,
                pump.uuid,
                QStringLiteral("Could not resolve imported pump energy-price pattern"));
        }

        double base_price = energy_price;
        if (base_price <= 0.0)
            base_price = result.request.network.options_energy.global_energy_price_per_kw_h;
        if (base_price > 0.0)
        {
            pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Pattern;
            pump.energy_price_per_kw_h = base_price;
            pump.price_pattern_uuid = references.pattern_uuids_by_index.value(energy_pattern_index);
        }
        else
        {
            appendImportWarning(
                result,
                QStringLiteral("Pump %1 has an energy-price pattern with zero effective base price; the no-effect pattern was omitted.").arg(pump.id),
                HydraulicSimulationStatusEntityType::Pump);
            pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Global;
        }
    }
    else if (energy_price > 0.0)
    {
        pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Constant;
        pump.energy_price_per_kw_h = energy_price;
    }
    else
    {
        pump.energy_price_input_type = HydraulicLinkPumpEnergyPriceInputType::Global;
    }

    result.request.network.links_pumps.append(pump);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importValve(
    EpanetProject &project,
    EpanetResultImport &result,
    int link_index,
    int link_type,
    const QString &link_id,
    const QUuid &link_uuid,
    const ImportReferences &references)
{
    int node_from_index = 0;
    int node_to_index = 0;
    int error = EN_getlinknodes(
        project.handle(), link_index, &node_from_index, &node_to_index);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getlinknodes"),
            QStringLiteral("Failed to read EPANET valve endpoints"),
            HydraulicSimulationStatusEntityType::Valve);
    }
    if (!references.node_uuids_by_index.contains(node_from_index)
        || !references.node_uuids_by_index.contains(node_to_index))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ResolveEntity,
            HydraulicSimulationStatusEntityType::Valve,
            link_id,
            link_uuid,
            QStringLiteral("Could not resolve imported EPANET valve endpoint"));
    }

    HydraulicLinkValve valve;
    valve.id = link_id;
    valve.uuid = link_uuid;
    valve.node_uuid_from = references.node_uuids_by_index.value(node_from_index);
    valve.node_uuid_to = references.node_uuids_by_index.value(node_to_index);
    if (!resolveValveType(link_type, valve.type))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Valve,
            link_id,
            link_uuid,
            QStringLiteral("EPANET returned an unsupported valve type"));
    }

    double diameter = 0.0;
    double minor_loss = 0.0;
    double initial_setting = 0.0;
    double initial_status = 0.0;
    struct ValveValueField
    {
        int property;
        const char *name;
        double *target;
    };
    const std::array<ValveValueField, 4> fields = {{
        {EN_DIAMETER, "EN_DIAMETER", &diameter},
        {EN_MINORLOSS, "EN_MINORLOSS", &minor_loss},
        {EN_INITSETTING, "EN_INITSETTING", &initial_setting},
        {EN_INITSTATUS, "EN_INITSTATUS", &initial_status}
    }};
    for (const ValveValueField &field : fields)
    {
        HydraulicSimulationStatus status = readLinkValue(
            project,
            link_index,
            field.property,
            *field.target,
            HydraulicSimulationStatusEntityType::Valve,
            QString::fromLatin1(field.name));
        if (!status.success)
            return status;
    }

    valve.diameter_mm = diameter;
    valve.minor_loss_coefficient = minor_loss;
    switch (valve.type)
    {
    case HydraulicLinkValveType::PRV:
    case HydraulicLinkValveType::PSV:
    case HydraulicLinkValveType::PBV:
        valve.setting_pressure_head_m = initial_setting;
        break;
    case HydraulicLinkValveType::FCV:
        valve.setting_flow_m3_per_h = initial_setting;
        break;
    case HydraulicLinkValveType::TCV:
        valve.setting_loss_coefficient = initial_setting;
        break;
    case HydraulicLinkValveType::PCV:
        valve.setting_position_percent = initial_setting;
        break;
    case HydraulicLinkValveType::GPV:
        break;
    }

    const int backend_status = static_cast<int>(std::llround(initial_status));
    if (backend_status == epanet_active_valve_status)
        valve.initial_status = HydraulicLinkValveInitialStatus::Active;
    else if (backend_status == EN_OPEN)
        valve.initial_status = HydraulicLinkValveInitialStatus::Open;
    else if (backend_status == EN_CLOSED)
        valve.initial_status = HydraulicLinkValveInitialStatus::Closed;
    else
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Valve,
            valve.id,
            valve.uuid,
            QStringLiteral("EPANET returned an unsupported initial valve status"));
    }

    if (valve.type == HydraulicLinkValveType::GPV)
    {
        double curve_index_value = 0.0;
        HydraulicSimulationStatus status = readLinkValue(
            project,
            link_index,
            EN_GPV_CURVE,
            curve_index_value,
            HydraulicSimulationStatusEntityType::Valve,
            QStringLiteral("EN_GPV_CURVE"));
        if (!status.success)
            return status;
        const int curve_index = static_cast<int>(std::llround(curve_index_value));
        if (curve_index <= 0
            || !references.curve_uuids_by_index.contains(curve_index)
            || references.curve_types_by_index.value(curve_index, -1) != EN_HLOSS_CURVE)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Curve,
                valve.id,
                valve.uuid,
                QStringLiteral("Could not resolve imported GPV head-loss curve"));
        }
        valve.head_loss_curve_uuid = references.curve_uuids_by_index.value(curve_index);
    }
    else if (valve.type == HydraulicLinkValveType::PCV)
    {
        double curve_index_value = 0.0;
        HydraulicSimulationStatus status = readLinkValue(
            project,
            link_index,
            EN_PCV_CURVE,
            curve_index_value,
            HydraulicSimulationStatusEntityType::Valve,
            QStringLiteral("EN_PCV_CURVE"));
        if (!status.success)
            return status;
        const int curve_index = static_cast<int>(std::llround(curve_index_value));
        if (curve_index > 0)
        {
            if (!references.curve_uuids_by_index.contains(curve_index)
                || references.curve_types_by_index.value(curve_index, -1) != EN_VALVE_CURVE)
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    HydraulicSimulationStatusEntityType::Curve,
                    valve.id,
                    valve.uuid,
                    QStringLiteral("Could not resolve imported PCV characteristic curve"));
            }
            valve.characteristic_curve_uuid = references.curve_uuids_by_index.value(curve_index);
        }
    }

    result.request.network.links_valves.append(valve);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importCoreTopology(
    EpanetProject &project,
    EpanetResultImport &result,
    ImportReferences &references)
{
    double emitter_exponent = 0.0;
    HydraulicSimulationStatus status = readOption(
        project,
        EN_EMITEXPON,
        emitter_exponent,
        QStringLiteral("EN_EMITEXPON"),
        HydraulicSimulationStatusEntityType::HydraulicSolver);
    if (!status.success)
        return status;

    int node_count = 0;
    int error = EN_getcount(project.handle(), EN_NODECOUNT, &node_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_NODECOUNT)"),
            QStringLiteral("Failed to read EPANET node count"));
    }

    for (int node_index = 1; node_index <= node_count; node_index++)
    {
        char node_id_value[EN_MAXID + 1] = {};
        error = EN_getnodeid(project.handle(), node_index, node_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getnodeid"),
                QStringLiteral("Failed to read EPANET node ID"),
                HydraulicSimulationStatusEntityType::Node);
        }
        const QString node_id = QString::fromUtf8(node_id_value);
        const QUuid node_uuid = QUuid::createUuid();
        references.node_uuids_by_index.insert(node_index, node_uuid);

        int node_type = EN_JUNCTION;
        error = EN_getnodetype(project.handle(), node_index, &node_type);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getnodetype"),
                QStringLiteral("Failed to read EPANET node type"),
                HydraulicSimulationStatusEntityType::Node);
        }

        if (node_type == EN_JUNCTION)
        {
            status = importJunction(
                project,
                result,
                node_index,
                node_id,
                node_uuid,
                emitter_exponent,
                references);
        }
        else if (node_type == EN_RESERVOIR)
        {
            status = importReservoir(
                project,
                result,
                node_index,
                node_id,
                node_uuid,
                references);
        }
        else if (node_type == EN_TANK)
        {
            status = importTank(
                project,
                result,
                node_index,
                node_id,
                node_uuid,
                references);
        }
        else
        {
            status = makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Node,
                node_id,
                node_uuid,
                QStringLiteral("EPANET returned an unsupported node type"));
        }
        if (!status.success)
            return status;
    }

    int link_count = 0;
    error = EN_getcount(project.handle(), EN_LINKCOUNT, &link_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_LINKCOUNT)"),
            QStringLiteral("Failed to read EPANET link count"));
    }

    for (int link_index = 1; link_index <= link_count; link_index++)
    {
        int link_type = EN_PIPE;
        error = EN_getlinktype(project.handle(), link_index, &link_type);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getlinktype"),
                QStringLiteral("Failed to read EPANET link type"),
                HydraulicSimulationStatusEntityType::Link);
        }

        char link_id_value[EN_MAXID + 1] = {};
        error = EN_getlinkid(project.handle(), link_index, link_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getlinkid"),
                QStringLiteral("Failed to read EPANET link ID"),
                HydraulicSimulationStatusEntityType::Link);
        }
        const QString link_id = QString::fromUtf8(link_id_value);
        const QUuid link_uuid = QUuid::createUuid();
        references.link_uuids_by_index.insert(link_index, link_uuid);
        references.link_types_by_index.insert(link_index, link_type);

        if (link_type == EN_PIPE || link_type == EN_CVPIPE)
        {
            status = importPipe(
                project,
                result,
                link_index,
                link_type,
                link_id,
                link_uuid,
                references.node_uuids_by_index);
        }
        else if (link_type == EN_PUMP)
        {
            status = importPump(
                project,
                result,
                link_index,
                link_id,
                link_uuid,
                references);
        }
        else if (link_type >= EN_PRV && link_type <= EN_PCV)
        {
            status = importValve(
                project,
                result,
                link_index,
                link_type,
                link_id,
                link_uuid,
                references);
        }
        else
        {
            status = makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Link,
                link_id,
                link_uuid,
                QStringLiteral("EPANET returned an unsupported link type"));
        }

        if (!status.success)
            return status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importSimpleControls(
    EpanetProject &project,
    NetworkHydraulic &network,
    const ImportReferences &references,
    const QString &input_file_path)
{
    int control_count = 0;
    int error = EN_getcount(project.handle(), EN_CONTROLCOUNT, &control_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_CONTROLCOUNT)"),
            QStringLiteral("Failed to read EPANET simple-control count"),
            HydraulicSimulationStatusEntityType::Control);
    }

    QList<QString> source_action_tokens;
    HydraulicSimulationStatus source_status = readSimpleControlActionTokens(
        input_file_path, source_action_tokens);
    if (!source_status.success)
        return source_status;
    if (source_action_tokens.size() != control_count)
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Control,
            QString(),
            QStringLiteral("Source INP control count does not match native EPANET control count"));
    }

    for (int control_index = 1; control_index <= control_count; control_index++)
    {
        int backend_type = EN_LOWLEVEL;
        int link_index = 0;
        int trigger_node_index = 0;
        double backend_setting = 0.0;
        double backend_level = 0.0;
        error = EN_getcontrol(
            project.handle(),
            control_index,
            &backend_type,
            &link_index,
            &backend_setting,
            &trigger_node_index,
            &backend_level);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcontrol"),
                QStringLiteral("Failed to read EPANET simple control"),
                HydraulicSimulationStatusEntityType::Control);
        }

        if (!references.link_uuids_by_index.contains(link_index)
            || !references.link_types_by_index.contains(link_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Control,
                QStringLiteral("CONTROL_%1").arg(control_index),
                QStringLiteral("Could not resolve the imported simple-control link"));
        }

        const int backend_link_type = references.link_types_by_index.value(link_index);
        const QString source_action = source_action_tokens.at(control_index - 1);

        HydraulicControlSimple control;
        control.id = QStringLiteral("CONTROL_%1").arg(control_index);
        control.uuid = QUuid::createUuid();
        control.link_uuid = references.link_uuids_by_index.value(link_index);
        if (!resolveSimpleControlType(backend_type, control.type))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Control,
                control.id,
                control.uuid,
                QStringLiteral("EPANET returned an unsupported simple-control type"));
        }

        if (source_action.compare(QStringLiteral("OPEN"), Qt::CaseInsensitive) == 0)
        {
            control.action = HydraulicControlActionType::Open;
        }
        else if (source_action.compare(QStringLiteral("CLOSED"), Qt::CaseInsensitive) == 0)
        {
            control.action = HydraulicControlActionType::Close;
        }
        else
        {
            control.action = HydraulicControlActionType::Setting;
            if (!assignControlLinkSetting(backend_link_type, backend_setting, control.setting))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Control,
                    control.id,
                    control.uuid,
                    QStringLiteral("EPANET returned a simple-control setting that cannot be represented by the controlled link type"));
            }
        }

        if (control.type == HydraulicControlSimpleType::LowLevel
            || control.type == HydraulicControlSimpleType::HighLevel)
        {
            if (!references.node_uuids_by_index.contains(trigger_node_index))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    HydraulicSimulationStatusEntityType::Control,
                    control.id,
                    control.uuid,
                    QStringLiteral("Could not resolve the imported simple-control trigger node"));
            }

            int trigger_node_type = EN_JUNCTION;
            error = EN_getnodetype(project.handle(), trigger_node_index, &trigger_node_type);
            if (error != 0)
            {
                return readFailure(
                    project,
                    error,
                    QStringLiteral("EN_getnodetype"),
                    QStringLiteral("Failed to read the simple-control trigger node type"),
                    HydraulicSimulationStatusEntityType::Control);
            }
            control.trigger_node_uuid = references.node_uuids_by_index.value(trigger_node_index);
            if (trigger_node_type == EN_JUNCTION)
                control.trigger_pressure_head_m = backend_level;
            else if (trigger_node_type == EN_RESERVOIR || trigger_node_type == EN_TANK)
                control.trigger_water_level_m = backend_level;
            else
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Control,
                    control.id,
                    control.uuid,
                    QStringLiteral("EPANET returned an unsupported simple-control trigger node type"));
            }
        }
        else
        {
            if (backend_level < 0.0
                || backend_level > static_cast<double>(std::numeric_limits<quint64>::max()))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Control,
                    control.id,
                    control.uuid,
                    QStringLiteral("EPANET returned an invalid simple-control time value"));
            }
            const quint64 time_s = static_cast<quint64>(std::llround(backend_level));
            if (control.type == HydraulicControlSimpleType::Timer)
                control.trigger_elapsed_time_s = time_s;
            else
                control.trigger_time_of_day_s = time_s;
        }

        int enabled = EN_TRUE;
        error = EN_getcontrolenabled(project.handle(), control_index, &enabled);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcontrolenabled"),
                QStringLiteral("Failed to read EPANET simple-control enabled state"),
                HydraulicSimulationStatusEntityType::Control);
        }
        control.enabled = enabled == EN_TRUE;
        network.controls_simple.append(control);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus assignRulePremiseValue(
    EpanetResultImport &result,
    HydraulicControlRulePremise &premise,
    int backend_variable,
    int backend_status,
    double backend_value,
    int backend_object_index,
    const ImportReferences &references,
    const QString &rule_id,
    bool &representable)
{
    representable = true;
    switch (backend_variable)
    {
    case EN_R_DEMAND:
        premise.demand_m3_per_h = backend_value;
        return makeEpanetSuccess();
    case EN_R_HEAD:
    case EN_R_GRADE:
        premise.hydraulic_head_m = backend_value;
        return makeEpanetSuccess();
    case EN_R_LEVEL:
        premise.water_level_m = backend_value;
        return makeEpanetSuccess();
    case EN_R_PRESSURE:
        premise.pressure_head_m = backend_value;
        return makeEpanetSuccess();
    case EN_R_FLOW:
        premise.flow_m3_per_h = backend_value;
        return makeEpanetSuccess();
    case EN_R_STATUS:
    {
        HydraulicControlRuleStatus status;
        if (!resolveRuleStatus(backend_status, status))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an unsupported rule status premise"));
        }
        premise.status = status;
        return makeEpanetSuccess();
    }
    case EN_R_SETTING:
        if (!references.link_types_by_index.contains(backend_object_index)
            || !assignControlLinkSetting(
                references.link_types_by_index.value(backend_object_index),
                backend_value,
                premise.link_setting))
        {
            appendImportWarning(
                result,
                QStringLiteral("Rule %1 contains a SETTING premise for a link type the current AOWIS control-setting model cannot represent; the rule was not imported.").arg(rule_id),
                HydraulicSimulationStatusEntityType::Rule);
            representable = false;
            return makeEpanetSuccess();
        }
        return makeEpanetSuccess();
    case EN_R_POWER:
        appendImportWarning(
            result,
            QStringLiteral("Rule %1 contains a POWER premise that the bundled EPANET 2.3 rule engine cannot execute through the AOWIS rule builder; the rule was not imported.").arg(rule_id),
            HydraulicSimulationStatusEntityType::Rule);
        representable = false;
        return makeEpanetSuccess();
    case EN_R_TIME:
        if (backend_value < 0.0
            || backend_value > static_cast<double>(std::numeric_limits<quint64>::max()))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an invalid rule time value"));
        }
        premise.elapsed_time_s = static_cast<quint64>(std::llround(backend_value));
        return makeEpanetSuccess();
    case EN_R_CLOCKTIME:
        if (backend_value < 0.0
            || backend_value > static_cast<double>(std::numeric_limits<quint64>::max()))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an invalid rule time value"));
        }
        premise.time_of_day_s = static_cast<quint64>(std::llround(backend_value));
        return makeEpanetSuccess();
    case EN_R_FILLTIME:
        if (backend_value < 0.0
            || backend_value > static_cast<double>(std::numeric_limits<quint64>::max()))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an invalid rule time value"));
        }
        premise.fill_time_s = static_cast<quint64>(std::llround(backend_value));
        return makeEpanetSuccess();
    case EN_R_DRAINTIME:
        if (backend_value < 0.0
            || backend_value > static_cast<double>(std::numeric_limits<quint64>::max()))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an invalid rule time value"));
        }
        premise.drain_time_s = static_cast<quint64>(std::llround(backend_value));
        return makeEpanetSuccess();
    default:
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::Rule,
            rule_id,
            QStringLiteral("EPANET returned an unsupported rule premise variable"));
    }
}

HydraulicSimulationStatus importRuleAction(
    EpanetProject &project,
    EpanetResultImport &result,
    const ImportReferences &references,
    const QString &rule_id,
    int rule_index,
    int action_index,
    bool else_action,
    HydraulicControlRuleAction &action,
    bool &representable)
{
    representable = true;
    int link_index = 0;
    int backend_status = 0;
    double backend_setting = EN_MISSING;
    const int error = else_action
        ? EN_getelseaction(
            project.handle(), rule_index, action_index,
            &link_index, &backend_status, &backend_setting)
        : EN_getthenaction(
            project.handle(), rule_index, action_index,
            &link_index, &backend_status, &backend_setting);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            else_action ? QStringLiteral("EN_getelseaction") : QStringLiteral("EN_getthenaction"),
            QStringLiteral("Failed to read EPANET rule action"),
            HydraulicSimulationStatusEntityType::Rule);
    }

    if (!references.link_uuids_by_index.contains(link_index)
        || !references.link_types_by_index.contains(link_index))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ResolveEntity,
            HydraulicSimulationStatusEntityType::Rule,
            rule_id,
            QStringLiteral("Could not resolve a link referenced by an imported rule action"));
    }
    action.link_uuid = references.link_uuids_by_index.value(link_index);

    if (backend_setting == EN_MISSING)
    {
        HydraulicControlRuleStatus status;
        if (!resolveRuleStatus(backend_status, status))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Rule,
                rule_id,
                QStringLiteral("EPANET returned an unsupported rule action status"));
        }
        action.status = status;
        return makeEpanetSuccess();
    }

    if (!assignControlLinkSetting(
            references.link_types_by_index.value(link_index),
            backend_setting,
            action.setting))
    {
        appendImportWarning(
            result,
            QStringLiteral("Rule %1 contains a numeric action setting for a link type the current AOWIS control-setting model cannot represent; the rule was not imported.").arg(rule_id),
            HydraulicSimulationStatusEntityType::Rule);
        representable = false;
        return makeEpanetSuccess();
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importRules(
    EpanetProject &project,
    EpanetResultImport &result,
    const ImportReferences &references)
{
    int rule_count = 0;
    int error = EN_getcount(project.handle(), EN_RULECOUNT, &rule_count);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getcount(EN_RULECOUNT)"),
            QStringLiteral("Failed to read EPANET rule count"),
            HydraulicSimulationStatusEntityType::Rule);
    }

    for (int rule_index = 1; rule_index <= rule_count; rule_index++)
    {
        char rule_id_value[EN_MAXID + 1] = {};
        error = EN_getruleID(project.handle(), rule_index, rule_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getruleID"),
                QStringLiteral("Failed to read EPANET rule ID"),
                HydraulicSimulationStatusEntityType::Rule);
        }

        HydraulicControlRule rule;
        rule.id = QString::fromUtf8(rule_id_value);
        rule.uuid = QUuid::createUuid();

        int premise_count = 0;
        int then_action_count = 0;
        int else_action_count = 0;
        error = EN_getrule(
            project.handle(), rule_index,
            &premise_count, &then_action_count, &else_action_count, &rule.priority);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getrule"),
                QStringLiteral("Failed to read EPANET rule summary"),
                HydraulicSimulationStatusEntityType::Rule);
        }

        bool skip_rule = false;
        for (int premise_index = 1; premise_index <= premise_count; premise_index++)
        {
            int backend_logical_operator = 0;
            int backend_object = 0;
            int backend_object_index = 0;
            int backend_variable = 0;
            int backend_operator = 0;
            int backend_status = 0;
            double backend_value = EN_MISSING;
            error = EN_getpremise(
                project.handle(), rule_index, premise_index,
                &backend_logical_operator, &backend_object, &backend_object_index,
                &backend_variable, &backend_operator, &backend_status, &backend_value);
            if (error != 0)
            {
                return readFailure(
                    project,
                    error,
                    QStringLiteral("EN_getpremise"),
                    QStringLiteral("Failed to read EPANET rule premise"),
                    HydraulicSimulationStatusEntityType::Rule);
            }

            HydraulicControlRulePremise premise;
            bool logical_operator_supported = true;
            if (premise_index == 1)
            {
                // EPANET stores the leading IF premise internally with the same
                // logical code it uses for AND. Premise position is therefore
                // the authoritative way to reconstruct the leading IF.
                premise.logical_operator = HydraulicControlRuleLogicalOperator::If;
            }
            else
            {
                logical_operator_supported = resolveRuleLogicalOperator(
                    backend_logical_operator, premise.logical_operator);
            }

            if (!logical_operator_supported
                || !resolveRuleObject(backend_object, premise.object)
                || !resolveRuleVariable(backend_variable, premise.variable)
                || !resolveRuleOperator(backend_operator, premise.comparison))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ReadInput,
                    HydraulicSimulationStatusEntityType::Rule,
                    rule.id,
                    rule.uuid,
                    QStringLiteral("EPANET returned an unsupported rule premise enum value"));
            }

            if (premise.object == HydraulicControlRuleObject::Node)
            {
                if (!references.node_uuids_by_index.contains(backend_object_index))
                {
                    return makeEpanetStatus(
                        HydraulicSimulationStatusStage::ReadInput,
                        HydraulicSimulationStatusOperation::ResolveEntity,
                        HydraulicSimulationStatusEntityType::Rule,
                        rule.id,
                        rule.uuid,
                        QStringLiteral("Could not resolve a node referenced by an imported rule premise"));
                }
                premise.object_uuid = references.node_uuids_by_index.value(backend_object_index);
            }
            else if (premise.object == HydraulicControlRuleObject::Link)
            {
                if (!references.link_uuids_by_index.contains(backend_object_index))
                {
                    return makeEpanetStatus(
                        HydraulicSimulationStatusStage::ReadInput,
                        HydraulicSimulationStatusOperation::ResolveEntity,
                        HydraulicSimulationStatusEntityType::Rule,
                        rule.id,
                        rule.uuid,
                        QStringLiteral("Could not resolve a link referenced by an imported rule premise"));
                }
                premise.object_uuid = references.link_uuids_by_index.value(backend_object_index);
            }

            bool premise_representable = true;
            const HydraulicSimulationStatus premise_status = assignRulePremiseValue(
                result,
                premise,
                backend_variable,
                backend_status,
                backend_value,
                backend_object_index,
                references,
                rule.id,
                premise_representable);
            if (!premise_status.success)
                return premise_status;
            if (!premise_representable)
            {
                skip_rule = true;
                break;
            }
            rule.premises.append(premise);
        }

        if (skip_rule)
            continue;

        for (int action_index = 1; action_index <= then_action_count; action_index++)
        {
            HydraulicControlRuleAction action;
            bool action_representable = true;
            const HydraulicSimulationStatus action_status = importRuleAction(
                project, result, references, rule.id,
                rule_index, action_index, false, action, action_representable);
            if (!action_status.success)
                return action_status;
            if (!action_representable)
            {
                skip_rule = true;
                break;
            }
            rule.actions_then.append(action);
        }
        if (skip_rule)
            continue;

        for (int action_index = 1; action_index <= else_action_count; action_index++)
        {
            HydraulicControlRuleAction action;
            bool action_representable = true;
            const HydraulicSimulationStatus action_status = importRuleAction(
                project, result, references, rule.id,
                rule_index, action_index, true, action, action_representable);
            if (!action_status.success)
                return action_status;
            if (!action_representable)
            {
                skip_rule = true;
                break;
            }
            rule.actions_else.append(action);
        }
        if (skip_rule)
            continue;

        int enabled = EN_TRUE;
        error = EN_getruleenabled(project.handle(), rule_index, &enabled);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getruleenabled"),
                QStringLiteral("Failed to read EPANET rule enabled state"),
                HydraulicSimulationStatusEntityType::Rule);
        }
        rule.enabled = enabled == EN_TRUE;
        result.request.network.controls_rules.append(rule);
    }

    return makeEpanetSuccess();
}


double qualityConcentrationScaleToCanonicalMgPerL(const QString &units, bool &supported)
{
    if (units.compare(QStringLiteral("mg/L"), Qt::CaseInsensitive) == 0)
    {
        supported = true;
        return 1.0;
    }
    if (units.compare(QStringLiteral("ug/L"), Qt::CaseInsensitive) == 0)
    {
        supported = true;
        return 0.001;
    }

    supported = false;
    return 1.0;
}

struct ReactionSourceMetadata
{
    double global_bulk_coefficient = 0.0;
    double global_wall_coefficient = 0.0;
    double roughness_reaction_factor = 0.0;
    QSet<QString> explicit_bulk_pipe_ids;
    QSet<QString> explicit_wall_pipe_ids;
    QSet<QString> explicit_tank_ids;
};

long epanetAtoLong(const QString &text)
{
    const QByteArray bytes = text.toLatin1();
    return std::atol(bytes.constData());
}

bool reactionRangeContainsId(
    const QString &id,
    const QString &first_id,
    const QString &last_id)
{
    const long first_number = epanetAtoLong(first_id);
    const long last_number = epanetAtoLong(last_id);
    if (first_number > 0 && last_number > 0)
    {
        const long id_number = epanetAtoLong(id);
        return id_number >= first_number && id_number <= last_number;
    }

    return QString::compare(first_id, id, Qt::CaseSensitive) <= 0
        && QString::compare(last_id, id, Qt::CaseSensitive) >= 0;
}

void collectPipeReactionOverrideIds(
    const NetworkHydraulic &network,
    const QStringList &tokens,
    QSet<QString> &ids)
{
    if (tokens.size() == 3)
    {
        const QString &id = tokens.at(1);
        for (const HydraulicLinkPipe &pipe : network.links_pipes)
        {
            if (pipe.id == id)
            {
                ids.insert(pipe.id);
                return;
            }
        }
        return;
    }

    if (tokens.size() < 4)
        return;

    const QString &first_id = tokens.at(1);
    const QString &last_id = tokens.at(2);
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (reactionRangeContainsId(pipe.id, first_id, last_id))
            ids.insert(pipe.id);
    }
}

void collectTankReactionOverrideIds(
    const NetworkHydraulic &network,
    const QStringList &tokens,
    QSet<QString> &ids)
{
    if (tokens.size() == 3)
    {
        const QString &id = tokens.at(1);
        for (const HydraulicNodeTank &tank : network.nodes_tanks)
        {
            if (tank.id == id)
            {
                ids.insert(tank.id);
                return;
            }
        }
        return;
    }

    if (tokens.size() < 4)
        return;

    const QString &first_id = tokens.at(1);
    const QString &last_id = tokens.at(2);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (reactionRangeContainsId(tank.id, first_id, last_id))
            ids.insert(tank.id);
    }
}

HydraulicSimulationStatus readReactionSourceMetadata(
    const QString &input_file_path,
    const NetworkHydraulic &network,
    ReactionSourceMetadata &metadata)
{
    QFile file(input_file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::QualitySolver,
            QString(),
            QStringLiteral("Could not read the source INP [REACTIONS] section: %1")
                .arg(file.errorString()));
    }

    bool in_reactions = false;
    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (QString line : lines)
    {
        const qsizetype comment_index = line.indexOf(QLatin1Char(';'));
        if (comment_index >= 0)
            line.truncate(comment_index);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('[')))
        {
            in_reactions = line.compare(QStringLiteral("[REACTIONS]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_reactions)
            continue;

        const QStringList tokens = line.simplified().split(QLatin1Char(' '));
        if (tokens.size() < 3)
            continue;

        bool value_ok = false;
        const double value = tokens.constLast().toDouble(&value_ok);
        if (!value_ok)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::QualitySolver,
                QString(),
                QStringLiteral("Could not parse a source INP reaction coefficient"));
        }

        const QString &keyword = tokens.at(0);
        if (keyword.compare(QStringLiteral("GLOBAL"), Qt::CaseInsensitive) == 0)
        {
            if (tokens.at(1).compare(QStringLiteral("BULK"), Qt::CaseInsensitive) == 0)
                metadata.global_bulk_coefficient = value;
            else if (tokens.at(1).compare(QStringLiteral("WALL"), Qt::CaseInsensitive) == 0)
                metadata.global_wall_coefficient = value;
            continue;
        }
        if (keyword.compare(QStringLiteral("ROUGHNESS"), Qt::CaseInsensitive) == 0)
        {
            metadata.roughness_reaction_factor = value;
            continue;
        }
        if (keyword.compare(QStringLiteral("BULK"), Qt::CaseInsensitive) == 0)
        {
            collectPipeReactionOverrideIds(network, tokens, metadata.explicit_bulk_pipe_ids);
            continue;
        }
        if (keyword.compare(QStringLiteral("WALL"), Qt::CaseInsensitive) == 0)
        {
            collectPipeReactionOverrideIds(network, tokens, metadata.explicit_wall_pipe_ids);
            continue;
        }
        if (keyword.compare(QStringLiteral("TANK"), Qt::CaseInsensitive) == 0)
            collectTankReactionOverrideIds(network, tokens, metadata.explicit_tank_ids);
    }

    return makeEpanetSuccess();
}

double reactionCoefficientScaleToCanonicalMg(
    double chemical_scale_to_canonical_mg,
    double reaction_order)
{
    const double dimensional_order = reaction_order < 0.0 ? 0.0 : reaction_order;
    return std::pow(chemical_scale_to_canonical_mg, 1.0 - dimensional_order);
}

double wallReactionSourceCoefficientScaleToCanonical(
    double chemical_scale_to_canonical_mg,
    double wall_order,
    int source_flow_units)
{
    const double source_length_to_m = flowUnitsAreSi(source_flow_units) ? 1.0 : meters_per_foot;
    if (wall_order == 0.0)
    {
        return chemical_scale_to_canonical_mg
            / (source_length_to_m * source_length_to_m);
    }

    return source_length_to_m;
}

double wallReactionLiveProjectCoefficientScaleToCanonical(
    double chemical_scale_to_canonical_mg,
    double wall_order)
{
    if (wall_order == 0.0)
        return chemical_scale_to_canonical_mg;

    return 1.0;
}

bool importTankMixingModel(int backend_model, HydraulicNodeTankMixingModel &mixing_model)
{
    switch (backend_model)
    {
    case EN_MIX1:
        mixing_model = HydraulicNodeTankMixingModel::CompleteMix;
        return true;
    case EN_MIX2:
        mixing_model = HydraulicNodeTankMixingModel::TwoCompartment;
        return true;
    case EN_FIFO:
        mixing_model = HydraulicNodeTankMixingModel::FirstInFirstOut;
        return true;
    case EN_LIFO:
        mixing_model = HydraulicNodeTankMixingModel::LastInFirstOut;
        return true;
    default:
        return false;
    }
}

bool importQualitySourceType(int backend_type, HydraulicNodeQualitySourceType &source_type)
{
    switch (backend_type)
    {
    case EN_CONCEN:
        source_type = HydraulicNodeQualitySourceType::Concentration;
        return true;
    case EN_MASS:
        source_type = HydraulicNodeQualitySourceType::MassBooster;
        return true;
    case EN_FLOWPACED:
        source_type = HydraulicNodeQualitySourceType::FlowPacedBooster;
        return true;
    case EN_SETPOINT:
        source_type = HydraulicNodeQualitySourceType::SetpointBooster;
        return true;
    default:
        return false;
    }
}

template<typename NodeType>
HydraulicSimulationStatus importQualitySourcesForNodes(
    EpanetProject &project,
    QList<NodeType> &nodes,
    double chemical_scale_to_canonical_mg,
    HydraulicSimulationStatusEntityType entity_type,
    const ImportReferences &references)
{
    for (NodeType &node : nodes)
    {
        const QByteArray node_id_utf8 = node.id.toUtf8();
        int node_index = 0;
        int error = EN_getnodeindex(project.handle(), node_id_utf8.constData(), &node_index);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getnodeindex"),
                QStringLiteral("Failed to resolve node while importing water-quality source"),
                entity_type);
        }

        double backend_source_type_value = 0.0;
        error = EN_getnodevalue(project.handle(), node_index, EN_SOURCETYPE, &backend_source_type_value);
        if (error == epanet_no_quality_source_error)
            continue;
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getnodevalue(EN_SOURCETYPE)"),
                QStringLiteral("Failed to read node water-quality source type"),
                entity_type);
        }

        HydraulicNodeQualitySource source;
        const int backend_source_type = static_cast<int>(std::llround(backend_source_type_value));
        if (!importQualitySourceType(backend_source_type, source.type))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                entity_type,
                node.id,
                node.uuid,
                QStringLiteral("EPANET returned an unsupported node water-quality source type"));
        }

        double source_strength = 0.0;
        HydraulicSimulationStatus status = readNodeValue(
            project, node_index, EN_SOURCEQUAL, source_strength, entity_type,
            QStringLiteral("EN_SOURCEQUAL"));
        if (!status.success)
            return status;

        if (source.type == HydraulicNodeQualitySourceType::MassBooster)
            source.chemical_mass_flow_mg_per_min = source_strength * chemical_scale_to_canonical_mg;
        else
            source.chemical_concentration_mg_per_l = source_strength * chemical_scale_to_canonical_mg;

        double pattern_index_value = 0.0;
        status = readNodeValue(
            project, node_index, EN_SOURCEPAT, pattern_index_value, entity_type,
            QStringLiteral("EN_SOURCEPAT"));
        if (!status.success)
            return status;

        const int pattern_index = static_cast<int>(std::llround(pattern_index_value));
        if (pattern_index > 0)
        {
            if (!references.pattern_uuids_by_index.contains(pattern_index))
            {
                return makeEpanetStatus(
                    HydraulicSimulationStatusStage::ReadInput,
                    HydraulicSimulationStatusOperation::ResolveEntity,
                    entity_type,
                    node.id,
                    node.uuid,
                    QStringLiteral("Could not resolve EPANET water-quality source pattern"));
            }
            source.pattern_uuid = references.pattern_uuids_by_index.value(pattern_index);
        }

        node.quality_source = source;
    }

    return makeEpanetSuccess();
}

template<typename NodeType>
HydraulicSimulationStatus importInitialQualityForNodes(
    EpanetProject &project,
    QList<NodeType> &nodes,
    WaterQualityAnalysisType analysis,
    double chemical_concentration_scale,
    HydraulicSimulationStatusEntityType entity_type)
{
    for (NodeType &node : nodes)
    {
        const QByteArray node_id_utf8 = node.id.toUtf8();
        int node_index = 0;
        int error = EN_getnodeindex(project.handle(), node_id_utf8.constData(), &node_index);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getnodeindex"),
                QStringLiteral("Failed to resolve node while importing initial water quality"),
                entity_type);
        }

        double initial_quality = 0.0;
        HydraulicSimulationStatus status = readNodeValue(
            project,
            node_index,
            EN_INITQUAL,
            initial_quality,
            entity_type,
            QStringLiteral("EN_INITQUAL"));
        if (!status.success)
            return status;

        if (analysis == WaterQualityAnalysisType::Chemical)
            node.initial_chemical_concentration_mg_per_l = initial_quality * chemical_concentration_scale;
        else if (analysis == WaterQualityAnalysisType::WaterAge)
            node.initial_water_age_h = initial_quality;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importTankMixing(
    EpanetProject &project,
    NetworkHydraulic &network)
{
    for (HydraulicNodeTank &tank : network.nodes_tanks)
    {
        const QByteArray tank_id_utf8 = tank.id.toUtf8();
        int node_index = 0;
        int error = EN_getnodeindex(project.handle(), tank_id_utf8.constData(), &node_index);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getnodeindex"),
                QStringLiteral("Failed to resolve tank while importing water-quality mixing"),
                HydraulicSimulationStatusEntityType::Tank);
        }

        double mixing_model_value = 0.0;
        HydraulicSimulationStatus status = readNodeValue(
            project, node_index, EN_MIXMODEL, mixing_model_value,
            HydraulicSimulationStatusEntityType::Tank, QStringLiteral("EN_MIXMODEL"));
        if (!status.success)
            return status;

        const int backend_mixing_model = static_cast<int>(std::llround(mixing_model_value));
        if (!importTankMixingModel(backend_mixing_model, tank.mixing_model))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Tank,
                tank.id,
                tank.uuid,
                QStringLiteral("EPANET returned an unsupported tank mixing model"));
        }

        status = readNodeValue(
            project, node_index, EN_MIXFRACTION, tank.mixing_fraction,
            HydraulicSimulationStatusEntityType::Tank, QStringLiteral("EN_MIXFRACTION"));
        if (!status.success)
            return status;
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importQualityReactions(
    EpanetProject &project,
    const QString &input_file_path,
    NetworkHydraulic &network,
    double chemical_scale_to_canonical_mg,
    int source_flow_units)
{
    ReactionSourceMetadata metadata;
    HydraulicSimulationStatus status = readReactionSourceMetadata(
        input_file_path, network, metadata);
    if (!status.success)
        return status;

    double pipe_bulk_order = 0.0;
    double pipe_wall_order = 0.0;
    double tank_bulk_order = 0.0;
    double limiting_concentration = 0.0;

    status = readOption(
        project, EN_BULKORDER, pipe_bulk_order, QStringLiteral("EN_BULKORDER"),
        HydraulicSimulationStatusEntityType::QualitySolver);
    if (!status.success)
        return status;
    status = readOption(
        project, EN_WALLORDER, pipe_wall_order, QStringLiteral("EN_WALLORDER"),
        HydraulicSimulationStatusEntityType::QualitySolver);
    if (!status.success)
        return status;
    status = readOption(
        project, EN_TANKORDER, tank_bulk_order, QStringLiteral("EN_TANKORDER"),
        HydraulicSimulationStatusEntityType::QualitySolver);
    if (!status.success)
        return status;
    status = readOption(
        project, EN_CONCENLIMIT, limiting_concentration, QStringLiteral("EN_CONCENLIMIT"),
        HydraulicSimulationStatusEntityType::QualitySolver);
    if (!status.success)
        return status;

    const double pipe_bulk_scale = reactionCoefficientScaleToCanonicalMg(
        chemical_scale_to_canonical_mg, pipe_bulk_order);
    const double pipe_wall_source_scale = wallReactionSourceCoefficientScaleToCanonical(
        chemical_scale_to_canonical_mg, pipe_wall_order, source_flow_units);
    const double pipe_wall_live_scale = wallReactionLiveProjectCoefficientScaleToCanonical(
        chemical_scale_to_canonical_mg, pipe_wall_order);
    const double tank_bulk_scale = reactionCoefficientScaleToCanonicalMg(
        chemical_scale_to_canonical_mg, tank_bulk_order);

    network.options_reaction.global_pipe_bulk_reaction.order = pipe_bulk_order;
    network.options_reaction.global_pipe_bulk_reaction.coefficient =
        metadata.global_bulk_coefficient * pipe_bulk_scale;
    network.options_reaction.global_pipe_wall_reaction.order = pipe_wall_order;
    network.options_reaction.global_pipe_wall_reaction.coefficient =
        metadata.global_wall_coefficient * pipe_wall_source_scale;
    network.options_reaction.global_tank_bulk_reaction.order = tank_bulk_order;
    network.options_reaction.global_tank_bulk_reaction.coefficient =
        metadata.global_bulk_coefficient * tank_bulk_scale;
    network.options_reaction.limiting_concentration_mg_per_l =
        limiting_concentration * chemical_scale_to_canonical_mg;
    network.options_reaction.roughness_reaction_factor =
        metadata.roughness_reaction_factor * pipe_wall_source_scale;

    for (HydraulicLinkPipe &pipe : network.links_pipes)
    {
        const QByteArray pipe_id_utf8 = pipe.id.toUtf8();
        int link_index = 0;
        const int index_error = EN_getlinkindex(
            project.handle(), pipe_id_utf8.constData(), &link_index);
        if (index_error != 0)
        {
            return readFailure(
                project, index_error, QStringLiteral("EN_getlinkindex"),
                QStringLiteral("Failed to resolve pipe while importing water-quality reactions"),
                HydraulicSimulationStatusEntityType::Pipe);
        }

        double bulk_coefficient = 0.0;
        int error = EN_getlinkvalue(
            project.handle(), link_index, EN_KBULK, &bulk_coefficient);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getlinkvalue(EN_KBULK)"),
                QStringLiteral("Failed to read pipe bulk reaction coefficient"),
                HydraulicSimulationStatusEntityType::Pipe);
        }
        pipe.bulk_reaction.order = pipe_bulk_order;
        pipe.bulk_reaction.coefficient = bulk_coefficient * pipe_bulk_scale;
        pipe.override_bulk_reaction = metadata.explicit_bulk_pipe_ids.contains(pipe.id);

        double wall_coefficient = 0.0;
        error = EN_getlinkvalue(
            project.handle(), link_index, EN_KWALL, &wall_coefficient);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getlinkvalue(EN_KWALL)"),
                QStringLiteral("Failed to read pipe wall reaction coefficient"),
                HydraulicSimulationStatusEntityType::Pipe);
        }
        pipe.wall_reaction.order = pipe_wall_order;
        pipe.wall_reaction.coefficient = wall_coefficient * pipe_wall_live_scale;
        pipe.override_wall_reaction = metadata.explicit_wall_pipe_ids.contains(pipe.id);
    }

    for (HydraulicNodeTank &tank : network.nodes_tanks)
    {
        const QByteArray tank_id_utf8 = tank.id.toUtf8();
        int node_index = 0;
        const int index_error = EN_getnodeindex(
            project.handle(), tank_id_utf8.constData(), &node_index);
        if (index_error != 0)
        {
            return readFailure(
                project, index_error, QStringLiteral("EN_getnodeindex"),
                QStringLiteral("Failed to resolve tank while importing water-quality reactions"),
                HydraulicSimulationStatusEntityType::Tank);
        }

        double bulk_coefficient = 0.0;
        const int error = EN_getnodevalue(
            project.handle(), node_index, EN_TANK_KBULK, &bulk_coefficient);
        if (error != 0)
        {
            return readFailure(
                project, error, QStringLiteral("EN_getnodevalue(EN_TANK_KBULK)"),
                QStringLiteral("Failed to read tank bulk reaction coefficient"),
                HydraulicSimulationStatusEntityType::Tank);
        }
        tank.bulk_reaction.order = tank_bulk_order;
        tank.bulk_reaction.coefficient = bulk_coefficient * tank_bulk_scale;
        tank.override_bulk_reaction = metadata.explicit_tank_ids.contains(tank.id);
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importWaterQualityConfiguration(
    EpanetProject &project,
    const QString &input_file_path,
    EpanetResultImport &result,
    const ImportReferences &references,
    int source_flow_units)
{
    int quality_type = EN_NONE;
    char chemical_name[EN_MAXID + 1] = {};
    char chemical_units[EN_MAXID + 1] = {};
    int trace_node_index = 0;
    const int quality_error = EN_getqualinfo(
        project.handle(), &quality_type, chemical_name, chemical_units, &trace_node_index);
    if (quality_error != 0)
    {
        return readFailure(
            project,
            quality_error,
            QStringLiteral("EN_getqualinfo"),
            QStringLiteral("Failed to read EPANET water-quality analysis configuration"),
            HydraulicSimulationStatusEntityType::QualitySolver);
    }

    if (quality_type == EN_NONE)
        return makeEpanetSuccess();

    WaterQualitySolverOptions options;
    double chemical_concentration_scale = 1.0;
    switch (quality_type)
    {
    case EN_CHEM:
    {
        options.analysis = WaterQualityAnalysisType::Chemical;
        options.chemical_name = QString::fromUtf8(chemical_name);
        bool units_supported = false;
        const QString units = QString::fromUtf8(chemical_units);
        chemical_concentration_scale = qualityConcentrationScaleToCanonicalMgPerL(
            units, units_supported);
        if (!units_supported)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::QualitySolver,
                QString(),
                QStringLiteral("Unsupported EPANET chemical concentration units: %1").arg(units));
        }
        break;
    }
    case EN_AGE:
        options.analysis = WaterQualityAnalysisType::WaterAge;
        break;
    case EN_TRACE:
        options.analysis = WaterQualityAnalysisType::SourceTrace;
        if (!references.node_uuids_by_index.contains(trace_node_index))
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::QualitySolver,
                QString(),
                QStringLiteral("Could not resolve EPANET source-trace node"));
        }
        options.trace_node_uuid = references.node_uuids_by_index.value(trace_node_index);
        break;
    default:
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::QualitySolver,
            QString(),
            QStringLiteral("EPANET returned an unsupported water-quality analysis type"));
    }

    double tolerance = 0.0;
    HydraulicSimulationStatus status = readOption(
        project,
        EN_TOLERANCE,
        tolerance,
        QStringLiteral("EN_TOLERANCE"),
        HydraulicSimulationStatusEntityType::QualitySolver);
    if (!status.success)
        return status;

    switch (options.analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        options.chemical_tolerance_mg_per_l = tolerance * chemical_concentration_scale;
        break;
    case WaterQualityAnalysisType::WaterAge:
        options.water_age_tolerance_h = tolerance;
        break;
    case WaterQualityAnalysisType::SourceTrace:
        options.source_trace_tolerance_percent = tolerance;
        break;
    case WaterQualityAnalysisType::None:
        break;
    }

    if (options.analysis == WaterQualityAnalysisType::Chemical)
    {
        status = readOption(
            project,
            EN_SP_DIFFUS,
            options.relative_diffusivity,
            QStringLiteral("EN_SP_DIFFUS"),
            HydraulicSimulationStatusEntityType::QualitySolver);
        if (!status.success)
            return status;
    }

    if (options.analysis == WaterQualityAnalysisType::Chemical
        || options.analysis == WaterQualityAnalysisType::WaterAge)
    {
        NetworkHydraulic &network = result.request.network;
        status = importInitialQualityForNodes(
            project,
            network.nodes_junctions,
            options.analysis,
            chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Junction);
        if (!status.success)
            return status;
        status = importInitialQualityForNodes(
            project,
            network.nodes_reservoirs,
            options.analysis,
            chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Reservoir);
        if (!status.success)
            return status;
        status = importInitialQualityForNodes(
            project,
            network.nodes_tanks,
            options.analysis,
            chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Tank);
        if (!status.success)
            return status;
    }

    if (options.analysis == WaterQualityAnalysisType::Chemical)
    {
        NetworkHydraulic &network = result.request.network;
        status = importQualitySourcesForNodes(
            project, network.nodes_junctions, chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Junction, references);
        if (!status.success)
            return status;
        status = importQualitySourcesForNodes(
            project, network.nodes_reservoirs, chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Reservoir, references);
        if (!status.success)
            return status;
        status = importQualitySourcesForNodes(
            project, network.nodes_tanks, chemical_concentration_scale,
            HydraulicSimulationStatusEntityType::Tank, references);
        if (!status.success)
            return status;
    }

    NetworkHydraulic &network = result.request.network;
    status = importTankMixing(project, network);
    if (!status.success)
        return status;

    if (options.analysis == WaterQualityAnalysisType::Chemical)
    {
        status = importQualityReactions(
            project, input_file_path, network, chemical_concentration_scale, source_flow_units);
        if (!status.success)
            return status;
    }

    result.request.quality_runs.append(options);
    return makeEpanetSuccess();
}

}

EpanetResultImport importEpanetInp(const QString &input_file_path)
{
    EpanetResultImport result;
    result.complete = true;
    result.request.network.id = QFileInfo(input_file_path).completeBaseName();
    result.request.network.uuid = QUuid::createUuid();

    EpanetProject project;
    HydraulicSimulationStatus status = project.openInput(input_file_path);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    int source_flow_units = EN_CMH;
    const int flow_units_error = EN_getflowunits(project.handle(), &source_flow_units);
    if (flow_units_error != 0)
    {
        status = readFailure(
            project, flow_units_error, QStringLiteral("EN_getflowunits"),
            QStringLiteral("Failed to read source EPANET flow units"),
            HydraulicSimulationStatusEntityType::HydraulicSolver);
        return finishImport(std::move(result), status, project);
    }

    double source_pressure_units_value = EN_METERS;
    status = readOption(
        project, EN_PRESS_UNITS, source_pressure_units_value,
        QStringLiteral("EN_PRESS_UNITS"),
        HydraulicSimulationStatusEntityType::HydraulicSolver);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    double source_specific_gravity = 1.0;
    status = readOption(
        project, EN_SP_GRAVITY, source_specific_gravity,
        QStringLiteral("EN_SP_GRAVITY"),
        HydraulicSimulationStatusEntityType::HydraulicSolver);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    EpanetInpSourceUnits source_units;
    source_units.flow_units = source_flow_units;
    source_units.pressure_units = static_cast<int>(std::llround(source_pressure_units_value));
    source_units.specific_gravity = source_specific_gravity;

    status = normalizeProjectToCanonicalUnits(project, source_flow_units);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    NetworkHydraulic &network = result.request.network;
    ImportReferences references;

    status = importTitles(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importTimes(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importPatterns(project, result, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importCurves(project, result, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importHydraulicOptions(project, network, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importEnergyOptions(project, network, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importCoreTopology(project, result, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importEpanetInpGeometry(project, input_file_path, result);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importWaterQualityConfiguration(
        project, input_file_path, result, references, source_flow_units);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importSimpleControls(project, network, references, input_file_path);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importRules(project, result, references);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importEpanetInpEntityMetadata(project, result);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importEpanetInpReport(project, input_file_path, result, source_units);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importReportStatus(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    return finishImport(std::move(result), makeEpanetSuccess(), project);
}
