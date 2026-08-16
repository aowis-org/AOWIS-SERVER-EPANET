#ifndef AOWIS_EPANET_NATIVE_REFERENCE_RUNNER_H
#define AOWIS_EPANET_NATIVE_REFERENCE_RUNNER_H

#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>

namespace AowisEpanetTests
{
enum class NativePumpState
{
    CannotSupplyHead,
    Closed,
    Open,
    CannotSupplyFlow
};

enum class NativeTimestepEventType
{
    ReportStep,
    HydraulicStep,
    QualityStep,
    TankEvent,
    ControlEvent
};

struct NativeJunctionResult
{
    QString id;
    double demand_requested_m3_per_h = 0.0;
    double demand_delivered_m3_per_h = 0.0;
    double demand_deficit_m3_per_h = 0.0;
    double total_demand_m3_per_h = 0.0;
    double emitter_flow_m3_per_h = 0.0;
    double leakage_flow_m3_per_h = 0.0;
    double head_m = 0.0;
    double pressure_head_m = 0.0;
    bool appears_in_control = false;
};

struct NativeReservoirResult
{
    QString id;
    double net_demand_m3_per_h = 0.0;
    double head_m = 0.0;
    double pressure_head_m = 0.0;
    bool appears_in_control = false;
};

struct NativeTankResult
{
    QString id;
    double net_demand_m3_per_h = 0.0;
    double head_m = 0.0;
    double pressure_head_m = 0.0;
    double water_level_m = 0.0;
    double volume_m3 = 0.0;
    double mixing_zone_volume_m3 = 0.0;
    bool appears_in_control = false;
};

struct NativePipeResult
{
    QString id;
    double flow_m3_per_h = 0.0;
    double leakage_flow_m3_per_h = 0.0;
    double velocity_m_per_s = 0.0;
    double head_loss_m = 0.0;
    double unit_head_loss_m_per_km = 0.0;
    double friction_factor = 0.0;
    bool open = true;
    double roughness = 0.0;
    bool appears_in_control = false;
};

struct NativePumpResult
{
    QString id;
    double flow_m3_per_h = 0.0;
    double velocity_m_per_s = 0.0;
    double head_gain_m = 0.0;
    bool open = true;
    NativePumpState state = NativePumpState::Closed;
    double speed = 0.0;
    double efficiency_percent = 0.0;
    double power_kw = 0.0;
    bool appears_in_control = false;
};

struct NativeValveResult
{
    QString id;
    double flow_m3_per_h = 0.0;
    double velocity_m_per_s = 0.0;
    double head_loss_m = 0.0;
    bool open = true;
    bool active = false;
    double setting = 0.0;
    bool appears_in_control = false;
};

struct NativePumpEnergyUsage
{
    QString pump_id;
    double time_online_percent = 0.0;
    double average_efficiency_percent = 0.0;
    double average_kw_per_flow_unit = 0.0;
    double average_power_kw = 0.0;
    double peak_power_kw = 0.0;
    double average_cost_per_day = 0.0;
};

struct NativeFlowBalance
{
    double total_inflow_m3_per_h = 0.0;
    double total_outflow_m3_per_h = 0.0;
    double consumer_demand_m3_per_h = 0.0;
    double demand_deficit_m3_per_h = 0.0;
    double emitter_flow_m3_per_h = 0.0;
    double leakage_flow_m3_per_h = 0.0;
    double storage_flow_m3_per_h = 0.0;
    double flow_balance_ratio = 0.0;
};

struct NativeEnergyUsage
{
    double peak_power_kw = 0.0;
    double energy_cost_per_day = 0.0;
    double demand_charge_per_day = 0.0;
    double total_cost_per_day = 0.0;
};

struct NativeHydraulicStatistics
{
    std::int64_t hydraulic_iterations = 0;
    double relative_error = 0.0;
    double maximum_head_error_m = 0.0;
    double maximum_flow_change_m3_per_h = 0.0;
    std::int64_t deficient_nodes = 0;
    double demand_reduction_percent = 0.0;
    double leakage_loss_percent = 0.0;
};

struct NativeTimestepEvent
{
    NativeTimestepEventType type = NativeTimestepEventType::HydraulicStep;
    std::int64_t time_until_event_s = 0;
    QString tank_id;
    QString control_id;
};

struct NativeHydraulicResult
{
    std::int64_t time_elapsed_s = 0;
    QList<NativeJunctionResult> nodes_junctions;
    QList<NativeReservoirResult> nodes_reservoirs;
    QList<NativeTankResult> nodes_tanks;
    QList<NativePipeResult> links_pipes;
    QList<NativePumpResult> links_pumps;
    QList<NativeValveResult> links_valves;
    QList<NativePumpEnergyUsage> links_pump_energy_usage;
    NativeFlowBalance flow_balance;
    NativeEnergyUsage energy_usage;
    NativeHydraulicStatistics statistics;
    NativeTimestepEvent event_next;
};

struct NativeHydraulicTimeline
{
    bool success = false;
    QString error;
    QList<NativeHydraulicResult> results;
};

struct NativeReferenceConfiguration
{
    QString input_file;
    QHash<int, QString> control_ids_by_index;
};

NativeHydraulicTimeline runNativeEpanetReference(const NativeReferenceConfiguration &configuration);
}

#endif // AOWIS_EPANET_NATIVE_REFERENCE_RUNNER_H
