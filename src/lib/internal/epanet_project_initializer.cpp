#include "epanet_project_initializer.h"

#include "epanet_project.h"
#include "epanet_report_collector.h"
#include "epanet_status_helpers.h"

#include <array>
#include <limits>

#include <QByteArray>
#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic.h>

namespace
{
double emitterPressureExponent(const NetworkHydraulic &request)
{
    for (const HydraulicNodeJunction &junction : request.nodes_junctions)
    {
        if (junction.emitter.coefficient > 0.0)
            return junction.emitter.pressure_exponent;
    }

    return 0.5;
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


}

HydraulicSimulationStatus initializeEpanetProject(
    EpanetProject &project,
    const NetworkHydraulic &request,
    EpanetReportCollector &report_collector)
{
    int error = EN_setreportcallbackuserdata(project.handle(), &report_collector);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallback(project.handle(), &EpanetReportCollector::callback);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback"));
        if (!epanet_status.success)
            return epanet_status;
    }

    // Keep the native EPANET project in the canonical units encoded by the AOWIS field names:
    // m3/h for flow, meters for length and head, and millimeters for pipe diameter.
    // Network construction uses a fixed formula so it is independent of the requested
    // hydraulic run. configureEpanetHydraulicRun() applies the real formula afterward.
    error = EN_init(project.handle(), "", "", EN_CMH, EN_HW);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::Initialize, QStringLiteral("EN_init"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project initialization failed"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallbackuserdata(project.handle(), &report_collector);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback data"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setreportcallback(project.handle(), &EpanetReportCollector::callback);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const QByteArray title_line_1 = request.title_line_1.toUtf8();
    const QByteArray title_line_2 = request.title_line_2.toUtf8();
    const QByteArray title_line_3 = request.title_line_3.toUtf8();
    error = EN_settitle(project.handle(), title_line_1.constData(), title_line_2.constData(), title_line_3.constData());
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::SetEntityMetadata, QStringLiteral("EN_settitle"), HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Failed to configure EPANET title lines"));
        if (!epanet_status.success)
            return epanet_status;
    }

    error = EN_setoption(project.handle(), EN_PRESS_UNITS, static_cast<double>(EN_METERS));
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(EN_PRESS_UNITS)"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure EPANET pressure-head units as meters"));
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

        error = EN_settimeparam(project.handle(), time_parameter.parameter, static_cast<long>(time_parameter.value));
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureTime, QStringLiteral("EN_settimeparam(%1)").arg(QString::fromLatin1(time_parameter.name)), HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Failed to configure an EPANET time parameter"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    int backend_report_statistic = 0;
    if (!resolveReportStatistic(request.report_statistic, backend_report_statistic))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Unsupported hydraulic report statistic"));

    error = EN_settimeparam(project.handle(), EN_STATISTIC, backend_report_statistic);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_settimeparam(EN_STATISTIC)"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the report statistic"));
        if (!epanet_status.success)
            return epanet_status;
    }

    const HydraulicSolverOptions &hydraulic = request.options_hydraulic;
    int backend_demand_model = 0;
    if (!resolveDemandModel(hydraulic.demand_model, backend_demand_model))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Unsupported hydraulic demand model"));

    error = EN_setdemandmodel(project.handle(), backend_demand_model, hydraulic.minimum_pressure_head_m, hydraulic.required_pressure_head_m, hydraulic.pressure_exponent);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setdemandmodel"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure the hydraulic demand model"));
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
    const std::array<NumericOption, 16> options = {{
        {EN_TRIALS, static_cast<double>(hydraulic.maximum_trials), "EN_TRIALS"},
        {EN_ACCURACY, hydraulic.accuracy, "EN_ACCURACY"},
        {EN_UNBALANCED, unbalanced_trials, "EN_UNBALANCED"},
        {EN_CHECKFREQ, static_cast<double>(hydraulic.check_frequency), "EN_CHECKFREQ"},
        {EN_MAXCHECK, static_cast<double>(hydraulic.maximum_check), "EN_MAXCHECK"},
        {EN_DAMPLIMIT, hydraulic.damping_limit, "EN_DAMPLIMIT"},
        {EN_HEADERROR, hydraulic.maximum_head_error_m, "EN_HEADERROR"},
        {EN_FLOWCHANGE, hydraulic.maximum_flow_change_m3_per_h, "EN_FLOWCHANGE"},
        {EN_DEMANDMULT, hydraulic.demand_multiplier, "EN_DEMANDMULT"},
        {EN_EMITEXPON, emitterPressureExponent(request), "EN_EMITEXPON"},
        {EN_EMITBACKFLOW, static_cast<double>(hydraulic.emitters_can_backflow ? EN_TRUE : EN_FALSE), "EN_EMITBACKFLOW"},
        {EN_SP_GRAVITY, hydraulic.specific_gravity, "EN_SP_GRAVITY"},
        {EN_SP_VISCOS, hydraulic.relative_viscosity, "EN_SP_VISCOS"},
        {EN_GLOBALEFFIC, request.options_energy.global_pump_efficiency_percent, "EN_GLOBALEFFIC"},
        {EN_GLOBALPRICE, request.options_energy.global_energy_price_per_kw_h, "EN_GLOBALPRICE"},
        {EN_DEMANDCHARGE, request.options_energy.demand_charge_per_kw, "EN_DEMANDCHARGE"}
    }};

    for (const NumericOption &option : options)
    {
        error = EN_setoption(project.handle(), option.option, option.value);
        if (error != 0)
        {
            const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(%1)").arg(QString::fromLatin1(option.name)), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure an EPANET simulation option"));
            if (!epanet_status.success)
                return epanet_status;
        }
    }

    return makeEpanetSuccess();
}

