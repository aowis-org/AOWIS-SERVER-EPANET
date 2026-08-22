#include "epanet_inp_importer.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/epanet/epanet_api.h>

#include <QFileInfo>

#include <array>
#include <cmath>
#include <utility>

namespace
{
constexpr double meters_per_foot = 0.3048;
constexpr double psi_per_foot = 0.4333;
constexpr double kpa_per_psi = 6.895;
constexpr double bar_per_psi = 0.068948;

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

bool isUsFlowUnit(int flow_unit)
{
    return flow_unit >= EN_CFS && flow_unit <= EN_AFD;
}

double flowToM3PerH(double value, int flow_unit)
{
    switch (flow_unit)
    {
    case EN_CFS:
        return value * 101.9406477312;
    case EN_GPM:
        return value * 0.22712470704;
    case EN_MGD:
        return value * 157.725491;
    case EN_IMGD:
        return value * 189.42041666666667;
    case EN_AFD:
        return value * 51.39507656448;
    case EN_LPS:
        return value * 3.6;
    case EN_LPM:
        return value * 0.06;
    case EN_MLD:
        return value * (1000.0 / 24.0);
    case EN_CMH:
        return value;
    case EN_CMD:
        return value / 24.0;
    case EN_CMS:
        return value * 3600.0;
    default:
        return value;
    }
}

double headToMeters(double value, int flow_unit)
{
    return isUsFlowUnit(flow_unit) ? value * meters_per_foot : value;
}

double pressureToHeadMeters(double value, int pressure_unit, double specific_gravity)
{
    switch (pressure_unit)
    {
    case EN_PSI:
        return value / (psi_per_foot * specific_gravity) * meters_per_foot;
    case EN_KPA:
        return value / (kpa_per_psi * psi_per_foot * specific_gravity) * meters_per_foot;
    case EN_METERS:
        return value;
    case EN_BAR:
        return value / (bar_per_psi * psi_per_foot * specific_gravity) * meters_per_foot;
    case EN_FEET:
        return value * meters_per_foot;
    default:
        return value;
    }
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
    int flow_unit = 0;
    int error = EN_getflowunits(project.handle(), &flow_unit);
    if (error != 0)
    {
        return readFailure(
            project,
            error,
            QStringLiteral("EN_getflowunits"),
            QStringLiteral("Failed to read EPANET flow units"));
    }
    if (flow_unit < EN_CFS || flow_unit > EN_CMS)
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("EPANET returned unsupported flow units"));
    }

    double value = 0.0;
    HydraulicSimulationStatus status = readOption(
        project, EN_PRESS_UNITS, value, QStringLiteral("EN_PRESS_UNITS"));
    if (!status.success)
        return status;
    const int pressure_unit = static_cast<int>(std::llround(value));
    if (pressure_unit < EN_PSI || pressure_unit > EN_FEET)
    {
        return makeEpanetStatus(
            HydraulicSimulationStatusStage::ReadInput,
            HydraulicSimulationStatusOperation::ReadInput,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("EPANET returned unsupported pressure units"));
    }

    status = readOption(project, EN_SP_GRAVITY, value, QStringLiteral("EN_SP_GRAVITY"));
    if (!status.success)
        return status;
    network.options_hydraulic.specific_gravity = value;

    int demand_model = 0;
    double minimum_pressure = 0.0;
    double required_pressure = 0.0;
    double pressure_exponent = 0.0;
    error = EN_getdemandmodel(
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
    network.options_hydraulic.minimum_pressure_head_m = pressureToHeadMeters(
        minimum_pressure, pressure_unit, network.options_hydraulic.specific_gravity);
    network.options_hydraulic.required_pressure_head_m = pressureToHeadMeters(
        required_pressure, pressure_unit, network.options_hydraulic.specific_gravity);
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
    network.options_hydraulic.maximum_head_error_m = headToMeters(values.at(7), flow_unit);
    network.options_hydraulic.maximum_flow_change_m3_per_h = flowToM3PerH(values.at(8), flow_unit);
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

    const std::array<CountCheck, 6> checks = {{
        {EN_NODECOUNT, "Network nodes are present but node import is not available.", HydraulicSimulationStatusEntityType::Node},
        {EN_LINKCOUNT, "Network links are present but link import is not available.", HydraulicSimulationStatusEntityType::Link},
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
