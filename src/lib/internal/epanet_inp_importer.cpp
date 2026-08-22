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

HydraulicSimulationStatus normalizeProjectToCanonicalUnits(EpanetProject &project)
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

HydraulicSimulationStatus importHydraulicOptions(EpanetProject &project, NetworkHydraulic &network)
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

    return makeEpanetSuccess();
}

HydraulicSimulationStatus importEnergyOptions(EpanetProject &project, NetworkHydraulic &network)
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
    bool &pattern_reference_present)
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
            pattern_reference_present = true;

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
    bool &pattern_reference_present)
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
    if (static_cast<int>(std::llround(value)) > 0)
        pattern_reference_present = true;

    result.request.network.nodes_reservoirs.append(reservoir);
    return makeEpanetSuccess();
}

HydraulicSimulationStatus importTank(
    EpanetProject &project,
    EpanetResultImport &result,
    int node_index,
    const QString &node_id,
    const QUuid &node_uuid,
    bool &volume_curve_reference_present)
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

    if (static_cast<int>(std::llround(volume_curve_index)) > 0)
        volume_curve_reference_present = true;

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

HydraulicSimulationStatus importCoreTopology(
    EpanetProject &project,
    EpanetResultImport &result)
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

    QHash<int, QUuid> node_uuids_by_index;
    bool pattern_reference_present = false;
    bool volume_curve_reference_present = false;
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
        node_uuids_by_index.insert(node_index, node_uuid);

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
                pattern_reference_present);
        }
        else if (node_type == EN_RESERVOIR)
        {
            status = importReservoir(
                project,
                result,
                node_index,
                node_id,
                node_uuid,
                pattern_reference_present);
        }
        else if (node_type == EN_TANK)
        {
            status = importTank(
                project,
                result,
                node_index,
                node_id,
                node_uuid,
                volume_curve_reference_present);
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

    int skipped_pump_count = 0;
    int skipped_valve_count = 0;
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

        if (link_type == EN_PUMP)
        {
            skipped_pump_count++;
            continue;
        }
        if (link_type >= EN_PRV && link_type <= EN_PCV)
        {
            skipped_valve_count++;
            continue;
        }
        if (link_type != EN_PIPE && link_type != EN_CVPIPE)
        {
            return makeEpanetStatus(
                HydraulicSimulationStatusStage::ReadInput,
                HydraulicSimulationStatusOperation::ReadInput,
                HydraulicSimulationStatusEntityType::Link,
                QString(),
                QStringLiteral("EPANET returned an unsupported link type"));
        }

        char link_id_value[EN_MAXID + 1] = {};
        error = EN_getlinkid(project.handle(), link_index, link_id_value);
        if (error != 0)
        {
            return readFailure(
                project,
                error,
                QStringLiteral("EN_getlinkid"),
                QStringLiteral("Failed to read EPANET pipe ID"),
                HydraulicSimulationStatusEntityType::Pipe);
        }

        status = importPipe(
            project,
            result,
            link_index,
            link_type,
            QString::fromUtf8(link_id_value),
            QUuid::createUuid(),
            node_uuids_by_index);
        if (!status.success)
            return status;
    }

    if (pattern_reference_present)
    {
        appendImportWarning(
            result,
            QStringLiteral("Node demand or reservoir head pattern references are present; base values were imported but pattern assignment is deferred until pattern import is available."),
            HydraulicSimulationStatusEntityType::Pattern);
    }
    if (volume_curve_reference_present)
    {
        appendImportWarning(
            result,
            QStringLiteral("Tank volume-curve references are present; tank scalar geometry was imported but the volume curve assignment is deferred until curve import is available."),
            HydraulicSimulationStatusEntityType::Curve);
    }
    if (skipped_pump_count > 0)
    {
        appendImportWarning(
            result,
            QStringLiteral("%1 pump%2 %3 present but pump import is not available.")
                .arg(skipped_pump_count)
                .arg(skipped_pump_count == 1 ? QString() : QStringLiteral("s"))
                .arg(skipped_pump_count == 1 ? QStringLiteral("is") : QStringLiteral("are")),
            HydraulicSimulationStatusEntityType::Pump);
    }
    if (skipped_valve_count > 0)
    {
        appendImportWarning(
            result,
            QStringLiteral("%1 valve%2 %3 present but valve import is not available.")
                .arg(skipped_valve_count)
                .arg(skipped_valve_count == 1 ? QString() : QStringLiteral("s"))
                .arg(skipped_valve_count == 1 ? QStringLiteral("is") : QStringLiteral("are")),
            HydraulicSimulationStatusEntityType::Valve);
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

    const std::array<CountCheck, 4> checks = {{
        {EN_PATCOUNT, "Time patterns are present but pattern import is not available.", HydraulicSimulationStatusEntityType::Pattern},
        {EN_CURVECOUNT, "Curves are present but curve import is not available.", HydraulicSimulationStatusEntityType::Curve},
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

    double pattern_index = 0.0;
    HydraulicSimulationStatus status = readOption(
        project,
        EN_DEMANDPATTERN,
        pattern_index,
        QStringLiteral("EN_DEMANDPATTERN"),
        HydraulicSimulationStatusEntityType::Pattern);
    if (!status.success)
        return status;
    if (static_cast<int>(std::llround(pattern_index)) > 0)
    {
        appendImportWarning(
            result,
            QStringLiteral("The default demand-pattern reference is present but cannot be represented without imported patterns."),
            HydraulicSimulationStatusEntityType::Pattern);
    }

    status = readOption(
        project,
        EN_GLOBALPATTERN,
        pattern_index,
        QStringLiteral("EN_GLOBALPATTERN"),
        HydraulicSimulationStatusEntityType::Pattern);
    if (!status.success)
        return status;
    if (static_cast<int>(std::llround(pattern_index)) > 0)
    {
        appendImportWarning(
            result,
            QStringLiteral("The global energy-price pattern reference is present but cannot be represented without imported patterns."),
            HydraulicSimulationStatusEntityType::Pattern);
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

    status = normalizeProjectToCanonicalUnits(project);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    NetworkHydraulic &network = result.request.network;
    status = importTitles(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importTimes(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importHydraulicOptions(project, network);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importCoreTopology(project, result);
    if (!status.success)
        return finishImport(std::move(result), status, project);

    status = importEnergyOptions(project, network);
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
