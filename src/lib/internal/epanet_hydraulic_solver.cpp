#include "epanet_hydraulic_solver.h"
#include "epanet_project.h"
#include "epanet_result_reader.h"
#include "epanet_status_helpers.h"

#include <algorithm>
#include <cmath>

namespace
{
struct PumpEnergyAccumulator
{
    double online_hours = 0.0;
    double efficiency_percent_hours = 0.0;
    double kw_per_flow_hours = 0.0;
    double power_kw_hours = 0.0;
    double peak_power_kw = 0.0;
    double total_cost = 0.0;
};

struct SystemEnergyAccumulator
{
    double peak_power_kw = 0.0;
};

struct FlowBalanceAccumulator
{
    double covered_seconds = 0.0;
    double total_inflow = 0.0;
    double total_outflow = 0.0;
    double consumer_demand = 0.0;
    double demand_deficit = 0.0;
    double emitter_flow = 0.0;
    double leakage_flow = 0.0;
    double storage_flow = 0.0;
};

double patternFactor(const NetworkHydraulic &network, const QUuid &pattern_uuid, quint64 time_s)
{
    if (pattern_uuid.isNull() || network.timestep_pattern_s == 0)
        return 1.0;

    for (const HydraulicPatternTime &pattern : network.patterns_time)
    {
        if (pattern.uuid != pattern_uuid || pattern.multipliers.isEmpty())
            continue;

        const quint64 period = (time_s + network.start_pattern_s) / network.timestep_pattern_s;
        const int factor_index = static_cast<int>(period % static_cast<quint64>(pattern.multipliers.size()));
        return pattern.multipliers.at(factor_index);
    }

    return 1.0;
}

double pumpEnergyCost(const NetworkHydraulic &network, const HydraulicLinkPump &pump, quint64 time_s)
{
    double cost = network.options_energy.global_energy_price_per_kw_h;
    QUuid pattern_uuid = network.options_energy.global_energy_price_pattern_uuid;

    if (pump.energy_price_input_type != HydraulicLinkPumpEnergyPriceInputType::Global)
        cost = pump.energy_price_per_kw_h;
    if (pump.energy_price_input_type == HydraulicLinkPumpEnergyPriceInputType::Pattern)
        pattern_uuid = pump.price_pattern_uuid;

    return cost * patternFactor(network, pattern_uuid, time_s);
}

void accumulatePumpEnergy(const NetworkHydraulic &network, const HydraulicSimulationResult &result, double interval_hours, QList<PumpEnergyAccumulator> &accumulators, SystemEnergyAccumulator &system_accumulator)
{
    double simultaneous_power_kw = 0.0;
    for (int index = 0; index < result.links_pumps.size(); index++)
    {
        const HydraulicSimulationResultLinkPump &pump_result = result.links_pumps.at(index);
        if (pump_result.efficiency_percent <= 0.0)
            continue;

        PumpEnergyAccumulator &accumulator = accumulators[index];
        const HydraulicLinkPump &pump = network.links_pumps.at(index);
        accumulator.online_hours += interval_hours;
        accumulator.efficiency_percent_hours += pump_result.efficiency_percent * interval_hours;
        accumulator.power_kw_hours += pump_result.power_kw * interval_hours;
        constexpr double minimum_energy_flow_m3_per_h = 1.0e-6 * 101.94;
        const double energy_flow_m3_per_h = std::max(minimum_energy_flow_m3_per_h, std::abs(pump_result.flow_m3_per_h));
        accumulator.kw_per_flow_hours += pump_result.power_kw / energy_flow_m3_per_h * interval_hours;
        if (pump_result.power_kw > accumulator.peak_power_kw)
            accumulator.peak_power_kw = pump_result.power_kw;
        accumulator.total_cost += pumpEnergyCost(network, pump, result.time_elapsed_s) * pump_result.power_kw * interval_hours;
        simultaneous_power_kw += pump_result.power_kw;
    }

    if (simultaneous_power_kw > system_accumulator.peak_power_kw)
        system_accumulator.peak_power_kw = simultaneous_power_kw;
}

void storePumpEnergyUsage(const NetworkHydraulic &network, const QList<PumpEnergyAccumulator> &accumulators, const SystemEnergyAccumulator &system_accumulator, HydraulicSimulationResult &result)
{
    const double duration_hours = static_cast<double>(network.duration_s) / 3600.0;
    for (int index = 0; index < network.links_pumps.size(); index++)
    {
        const HydraulicLinkPump &pump = network.links_pumps.at(index);
        const PumpEnergyAccumulator &accumulator = accumulators.at(index);
        HydraulicSimulationResultLinkPumpEnergyUsage usage;
        usage.pump_id = pump.id;
        usage.pump_uuid = pump.uuid;
        if (duration_hours > 0.0)
            usage.time_online_percent = accumulator.online_hours / duration_hours * 100.0;
        else if (accumulator.online_hours > 0.0)
            usage.time_online_percent = 100.0;
        if (accumulator.online_hours > 0.0)
        {
            usage.average_efficiency_percent = accumulator.efficiency_percent_hours / accumulator.online_hours;
            usage.average_kw_per_flow_unit = accumulator.kw_per_flow_hours / accumulator.online_hours;
            usage.average_power_kw = accumulator.power_kw_hours / accumulator.online_hours;
        }
        usage.peak_power_kw = accumulator.peak_power_kw;
        if (duration_hours > 0.0)
            usage.average_cost_per_day = accumulator.total_cost * 24.0 / duration_hours;
        else
            usage.average_cost_per_day = accumulator.total_cost * 24.0;
        result.links_pump_energy_usage.append(usage);
        result.energy_usage.energy_cost_per_day += usage.average_cost_per_day;
    }

    result.energy_usage.peak_power_kw = system_accumulator.peak_power_kw;
    result.energy_usage.demand_charge_per_day = system_accumulator.peak_power_kw * network.options_energy.demand_charge_per_kw;
    result.energy_usage.total_cost_per_day = result.energy_usage.energy_cost_per_day + result.energy_usage.demand_charge_per_day;
}

void accumulateFlowBalance(const HydraulicSimulationResult &result, double interval_seconds, FlowBalanceAccumulator &accumulator)
{
    double total_inflow = 0.0;
    double total_outflow = 0.0;
    double consumer_demand = 0.0;
    double demand_deficit = 0.0;
    double emitter_flow = 0.0;
    double leakage_flow = 0.0;
    double storage_flow = 0.0;

    for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
    {
        if (junction.demand_delivered_m3_per_h < 0.0)
            total_inflow -= junction.demand_delivered_m3_per_h;
        else
        {
            consumer_demand += junction.demand_delivered_m3_per_h;
            total_outflow += junction.demand_delivered_m3_per_h;
        }
        emitter_flow += junction.emitter_flow_m3_per_h;
        total_outflow += junction.emitter_flow_m3_per_h;
        leakage_flow += junction.leakage_flow_m3_per_h;
        total_outflow += junction.leakage_flow_m3_per_h;
        demand_deficit += junction.demand_deficit_m3_per_h;
    }

    for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
    {
        if (reservoir.net_demand_m3_per_h >= 0.0)
            total_outflow += reservoir.net_demand_m3_per_h;
        else
            total_inflow -= reservoir.net_demand_m3_per_h;
    }

    for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
        storage_flow += tank.net_demand_m3_per_h;

    accumulator.covered_seconds += interval_seconds;
    accumulator.total_inflow += total_inflow * interval_seconds;
    accumulator.total_outflow += total_outflow * interval_seconds;
    accumulator.consumer_demand += consumer_demand * interval_seconds;
    accumulator.demand_deficit += demand_deficit * interval_seconds;
    accumulator.emitter_flow += emitter_flow * interval_seconds;
    accumulator.leakage_flow += leakage_flow * interval_seconds;
    accumulator.storage_flow += storage_flow * interval_seconds;
}

void storeFlowBalance(const FlowBalanceAccumulator &accumulator, HydraulicSimulationResult &result)
{
    if (accumulator.covered_seconds <= 0.0)
        return;

    HydraulicSimulationResultFlowBalance &flow_balance = result.flow_balance;
    flow_balance.total_inflow_m3_per_h = accumulator.total_inflow / accumulator.covered_seconds;
    flow_balance.total_outflow_m3_per_h = accumulator.total_outflow / accumulator.covered_seconds;
    flow_balance.consumer_demand_m3_per_h = accumulator.consumer_demand / accumulator.covered_seconds;
    flow_balance.demand_deficit_m3_per_h = accumulator.demand_deficit / accumulator.covered_seconds;
    flow_balance.emitter_flow_m3_per_h = accumulator.emitter_flow / accumulator.covered_seconds;
    flow_balance.leakage_flow_m3_per_h = accumulator.leakage_flow / accumulator.covered_seconds;
    flow_balance.storage_flow_m3_per_h = accumulator.storage_flow / accumulator.covered_seconds;

    double adjusted_inflow = flow_balance.total_inflow_m3_per_h;
    double adjusted_outflow = flow_balance.total_outflow_m3_per_h;
    if (flow_balance.storage_flow_m3_per_h > 0.0)
        adjusted_outflow += flow_balance.storage_flow_m3_per_h;
    else
        adjusted_inflow -= flow_balance.storage_flow_m3_per_h;
    if (adjusted_inflow == adjusted_outflow)
        flow_balance.flow_balance_ratio = 1.0;
    else if (adjusted_inflow > 0.0)
        flow_balance.flow_balance_ratio = adjusted_outflow / adjusted_inflow;
}

HydraulicSimulationDiagnostic diagnosticFromHydraulicStatus(const HydraulicSimulationStatus &status, HydraulicSimulationDiagnosticSeverity severity)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.stage = status.stage;
    diagnostic.operation = status.operation;
    diagnostic.property = status.property;
    diagnostic.entity = status.entity;
    diagnostic.message = status.message;
    diagnostic.details = status.details;
    diagnostic.backend_name = status.backend_name;
    diagnostic.backend_error_code = status.backend_error_code;
    diagnostic.backend_operation = status.backend_operation;
    diagnostic.message_backend = status.message_backend;
    return diagnostic;
}

void collectHydraulicFailure(EpanetProject &project, HydraulicSimulationStatus status, HydraulicSimulationStatus &first_failure, HydraulicSimulationDiagnosticSeverity severity, long simulation_time_s = -1)
{
    if (status.success)
        return;

    if (simulation_time_s >= 0)
        status.details.append(QStringLiteral("Simulation time: %1 s").arg(simulation_time_s));

    project.appendDiagnostic(diagnosticFromHydraulicStatus(status, severity));
    if (first_failure.success)
        first_failure = status;
}

bool canAdvanceAfterRunError(int error_code)
{
    // EPANET error 110 is produced by hydsolve() for an ill-conditioned
    // hydraulic matrix. The solver remains open and EN_nextH() can still
    // advance the project to a later hydraulic event. Other fatal run
    // errors can represent unusable solver state or allocation failures.
    return error_code == 110;
}

bool cancellationRequested(const std::function<bool()> &cancellation_requested)
{
    return cancellation_requested && cancellation_requested();
}
}

EpanetHydraulicSolver::EpanetHydraulicSolver(EpanetProject &project, const NetworkHydraulic &network, const EpanetResultReader &result_reader)
    : project(project), network(network), result_reader(result_reader)
{
}

HydraulicSimulationStatus EpanetHydraulicSolver::configureReport() const
{
    return this->project.configureReport(this->network);
}

HydraulicSimulationStatus EpanetHydraulicSolver::run(
    HydraulicSimulationResultTimeline &timeline,
    const std::function<bool()> &cancellation_requested,
    bool &cancelled)
{
    cancelled = false;

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        return makeEpanetSuccess();
    }

    HydraulicSimulationStatus status = configureReport();
    if (!status.success)
        return status;

    if (cancellationRequested(cancellation_requested))
    {
        cancelled = true;
        return makeEpanetSuccess();
    }

    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    int error = EN_openH(this->project.handle());
    if (error != 0)
    {
        status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::OpenHydraulics, QStringLiteral("EN_openH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to open EPANET hydraulics"));
        if (!status.success)
        {
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            return first_failure;
        }
    }

    error = EN_initH(this->project.handle(), EN_SAVE_AND_INIT);
    if (error != 0)
    {
        status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::InitializeHydraulics, QStringLiteral("EN_initH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to initialize EPANET hydraulics"));
        if (!status.success)
        {
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);

            const int close_error = EN_closeH(this->project.handle());
            if (close_error != 0)
            {
                const HydraulicSimulationStatus close_status = processEpanetReturnCode(this->project, close_error, HydraulicSimulationStatusStage::CloseHydraulics, HydraulicSimulationStatusOperation::CloseHydraulics, QStringLiteral("EN_closeH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to close EPANET hydraulics after initialization failure"));
                if (!close_status.success)
                    collectHydraulicFailure(this->project, close_status, first_failure, HydraulicSimulationDiagnosticSeverity::Error);
            }
            return first_failure;
        }
    }

    long current_time_s = 0;
    long previous_time_s = -1;
    long next_step_s = 0;

    if (cancellationRequested(cancellation_requested))
        cancelled = true;
    QList<PumpEnergyAccumulator> pump_energy_accumulators;
    pump_energy_accumulators.resize(this->network.links_pumps.size());
    SystemEnergyAccumulator system_energy_accumulator;
    FlowBalanceAccumulator flow_balance_accumulator;

    while (!cancelled)
    {
        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        bool result_available = true;

        error = EN_runH(this->project.handle(), &current_time_s);

        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }
        if (error != 0)
        {
            status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, QStringLiteral("EN_runH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET hydraulic analysis returned a diagnostic"));
            if (!status.success)
            {
                const bool can_advance = canAdvanceAfterRunError(error);
                collectHydraulicFailure(this->project, status, first_failure,
                    can_advance ? HydraulicSimulationDiagnosticSeverity::Error : HydraulicSimulationDiagnosticSeverity::Fatal,
                    current_time_s);
                result_available = false;
                if (!can_advance)
                {
                    break;
                }
            }
        }

        if (current_time_s < 0)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET returned a negative elapsed simulation time"));
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }

        if (previous_time_s >= 0 && current_time_s <= previous_time_s)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::RunHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET hydraulic simulation time did not advance"));
            status.details.append(QStringLiteral("Previous simulation time: %1 s").arg(previous_time_s));
            status.details.append(QStringLiteral("Current simulation time: %1 s").arg(current_time_s));
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal);
            break;
        }
        previous_time_s = current_time_s;

        HydraulicSimulationResult result;
        if (result_available)
        {
            result.time_elapsed_s = static_cast<quint64>(current_time_s);
            status = this->result_reader.read(result);
            if (!status.success)
            {
                collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Error, current_time_s);
                result_available = false;
            }
            else
            {
                timeline.results.append(result);
            }
        }

        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        error = EN_nextH(this->project.handle(), &next_step_s);
        if (error != 0)
        {
            status = processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::AdvanceHydraulics, QStringLiteral("EN_nextH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET hydraulic timestep advance returned a diagnostic"));
            if (!status.success)
            {
                collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal, current_time_s);
                break;
            }
        }

        if (next_step_s < 0)
        {
            status = makeEpanetStatus(HydraulicSimulationStatusStage::RunHydraulics, HydraulicSimulationStatusOperation::AdvanceHydraulics, HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("EPANET returned a negative hydraulic timestep"));
            status.details.append(QStringLiteral("Hydraulic timestep: %1 s").arg(next_step_s));
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Fatal, current_time_s);
            break;
        }

        if (result_available && !timeline.results.isEmpty())
        {
            const quint64 actual_step_s = static_cast<quint64>(next_step_s);
            if (actual_step_s != result.event_next.time_until_event_s)
            {
                result.event_next.type = HydraulicSimulationTimestepEventType::HydraulicStep;
                result.event_next.tank_id.clear();
                result.event_next.tank_uuid = QUuid();
                result.event_next.control_id.clear();
                result.event_next.control_uuid = QUuid();
            }
            result.event_next.time_until_event_s = actual_step_s;
            timeline.results.last().event_next = result.event_next;
        }

        if (result_available && next_step_s > 0)
        {
            accumulatePumpEnergy(this->network, result, static_cast<double>(next_step_s) / 3600.0, pump_energy_accumulators, system_energy_accumulator);
            accumulateFlowBalance(result, static_cast<double>(next_step_s), flow_balance_accumulator);
        }
        else if (result_available && this->network.duration_s == 0)
        {
            accumulatePumpEnergy(this->network, result, 1.0, pump_energy_accumulators, system_energy_accumulator);
            accumulateFlowBalance(result, 1.0, flow_balance_accumulator);
        }

        if (cancellationRequested(cancellation_requested))
        {
            cancelled = true;
            break;
        }

        if (next_step_s <= 0)
            break;
    }

    const int close_error = EN_closeH(this->project.handle());
    if (close_error != 0)
    {
        status = processEpanetReturnCode(this->project, close_error, HydraulicSimulationStatusStage::CloseHydraulics, HydraulicSimulationStatusOperation::CloseHydraulics, QStringLiteral("EN_closeH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to close EPANET hydraulics"));
        if (!status.success)
            collectHydraulicFailure(this->project, status, first_failure, HydraulicSimulationDiagnosticSeverity::Error, current_time_s);
    }

    if (!cancelled && !timeline.results.isEmpty())
    {
        HydraulicSimulationResult &final_result = timeline.results.last();
        storePumpEnergyUsage(this->network, pump_energy_accumulators, system_energy_accumulator, final_result);
        storeFlowBalance(flow_balance_accumulator, final_result);
    }

    if (cancelled)
        return makeEpanetSuccess();

    if (!first_failure.success)
        return first_failure;

    return makeEpanetSuccess();
}
