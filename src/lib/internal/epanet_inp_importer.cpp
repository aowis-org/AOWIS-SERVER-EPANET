#include "epanet_inp_importer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>

#include <QFileInfo>
#include <QHash>

#include <array>
#include <cmath>
#include <utility>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double kw_per_hp = 0.7457;
constexpr int epanet_active_valve_status = 2;

struct ImportReferences
{
    QHash<int, QUuid> pattern_uuids_by_index;
    QHash<int, QUuid> curve_uuids_by_index;
    QHash<int, int> curve_types_by_index;
    QHash<int, QUuid> node_uuids_by_index;
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

    comment = QString::fromUtf8(value);
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

HydraulicSimulationStatus collectDeferredImportDiagnostics(
    EpanetProject &project,
    EpanetResultImport &result)
{
    struct CountCheck
    {
        int object_type;
        const char *description;
        HydraulicSimulationStatusEntityType entity_type;
    };

    const std::array<CountCheck, 2> checks = {{
        {EN_CONTROLCOUNT, "Simple controls are present but control import is not available.", HydraulicSimulationStatusEntityType::Control},
        {EN_RULECOUNT, "Rules are present but rule import is not available.", HydraulicSimulationStatusEntityType::Rule}
    }};

    for (const CountCheck &check : checks)
    {
        int count = 0;
        const int error = EN_getcount(project.handle(), check.object_type, &count);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getcount"),
                QStringLiteral("Failed to inspect EPANET input content"));
        }
        if (count > 0)
            appendImportWarning(result, QString::fromLatin1(check.description), check.entity_type);
    }

    int quality_type = EN_NONE;
    char chemical_name[EN_MAXID + 1] = {};
    char chemical_units[EN_MAXID + 1] = {};
    int trace_node = 0;
    const int quality_error = EN_getqualinfo(
        project.handle(), &quality_type, chemical_name, chemical_units, &trace_node);
    if (quality_error != 0)
    {
        return readFailure(
            project,
            quality_error,
            QStringLiteral("EN_getqualinfo"),
            QStringLiteral("Failed to inspect EPANET water-quality input"),
            HydraulicSimulationStatusEntityType::QualitySolver);
    }
    if (quality_type != EN_NONE)
    {
        appendImportWarning(
            result,
            QStringLiteral("Water-quality analysis configuration is present but quality-run import is not available."),
            HydraulicSimulationStatusEntityType::QualitySolver);
    }

    appendImportWarning(
        result,
        QStringLiteral("Report directives beyond status level and statistic are not imported."),
        HydraulicSimulationStatusEntityType::Report);
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

    status = importReportStatus(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = collectDeferredImportDiagnostics(project, result);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    return finishImport(std::move(result), makeEpanetSuccess(), project);
}
