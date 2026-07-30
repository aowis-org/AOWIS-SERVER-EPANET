#include "epanet_project.h"
#include "epanet_report_collector.h"
#include "epanet_status_helpers.h"

#include <array>
#include <limits>

#include <aowis/model/hydraulic/network_hydraulic.h>

namespace
{
int headlossFormula(HydraulicHeadlossFormula formula)
{
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        return EN_HW;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        return EN_DW;
    case HydraulicHeadlossFormula::ChezyManning:
        return EN_CM;
    }

    return EN_HW;
}

int demandModel(HydraulicDemandModel model)
{
    switch (model)
    {
    case HydraulicDemandModel::DemandDriven:
        return EN_DDA;
    case HydraulicDemandModel::PressureDriven:
        return EN_PDA;
    }

    return EN_DDA;
}

int reportStatistic(HydraulicSimulationReportStatistic statistic)
{
    switch (statistic)
    {
    case HydraulicSimulationReportStatistic::Series:
        return EN_SERIES;
    case HydraulicSimulationReportStatistic::Average:
        return EN_AVERAGE;
    case HydraulicSimulationReportStatistic::Minimum:
        return EN_MINIMUM;
    case HydraulicSimulationReportStatistic::Maximum:
        return EN_MAXIMUM;
    case HydraulicSimulationReportStatistic::Range:
        return EN_RANGE;
    }

    return EN_SERIES;
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
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::CreateBackendContext, HydraulicSimulationStatusOperation::CreateBackendContext, QStringLiteral("EN_createproject"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project creation failed"));

    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetProject::initialize(const NetworkHydraulic &request, EpanetReportCollector &report_collector)
{
    int error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback data"));

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to set EPANET report callback"));

    // The adapter uses L/s and meters internally so all model values cross the backend
    // boundary through one fixed SI-oriented unit system.
    error = EN_init(this->project, "", "", EN_LPS, headlossFormula(request.options_hydraulic.headloss_formula));
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::Initialize, QStringLiteral("EN_init"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project initialization failed"));

    error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallbackuserdata"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback data"));

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::InitializeSimulation, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_setreportcallback"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to restore EPANET report callback"));

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
            return makeEpanetError(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureTime, QStringLiteral("EN_settimeparam(%1)").arg(QString::fromLatin1(time_parameter.name)), HydraulicSimulationStatusEntityType::Network, request.id, request.uuid, QStringLiteral("Failed to configure an EPANET time parameter"));
    }

    error = EN_settimeparam(this->project, EN_STATISTIC, reportStatistic(request.report_statistic));
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureReport, QStringLiteral("EN_settimeparam(EN_STATISTIC)"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to configure the report statistic"));

    const HydraulicSolverOptions &hydraulic = request.options_hydraulic;
    error = EN_setdemandmodel(this->project, demandModel(hydraulic.demand_model), hydraulic.minimum_pressure_head_m, hydraulic.required_pressure_head_m, hydraulic.pressure_exponent);
    if (error != 0)
        return makeEpanetError(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setdemandmodel"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure the hydraulic demand model"));

    struct NumericOption
    {
        int option;
        double value;
        const char *name;
    };

    const double unbalanced_trials = hydraulic.unbalanced_action == HydraulicUnbalancedAction::Continue ? hydraulic.unbalanced_extra_trials : 0.0;
    const std::array<NumericOption, 18> options = {{
        {EN_TRIALS, static_cast<double>(hydraulic.maximum_trials), "EN_TRIALS"},
        {EN_ACCURACY, hydraulic.accuracy, "EN_ACCURACY"},
        {EN_UNBALANCED, unbalanced_trials, "EN_UNBALANCED"},
        {EN_CHECKFREQ, static_cast<double>(hydraulic.check_frequency), "EN_CHECKFREQ"},
        {EN_MAXCHECK, static_cast<double>(hydraulic.maximum_check), "EN_MAXCHECK"},
        {EN_DAMPLIMIT, hydraulic.damping_limit, "EN_DAMPLIMIT"},
        {EN_HEADERROR, hydraulic.maximum_head_error_m, "EN_HEADERROR"},
        {EN_FLOWCHANGE, hydraulic.maximum_flow_change_m3_per_h / 3.6, "EN_FLOWCHANGE"},
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
            return makeEpanetError(*this, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureHydraulics, QStringLiteral("EN_setoption(%1)").arg(QString::fromLatin1(option.name)), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to configure an EPANET simulation option"));
    }

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
