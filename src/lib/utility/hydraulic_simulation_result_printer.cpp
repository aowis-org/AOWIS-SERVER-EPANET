#include <aowis/epanet/utility/hydraulic_simulation_result_printer.h>

#include <cstdio>
#include <limits>

#include <QTextStream>

namespace
{
QString timestepEventText(HydraulicSimulationTimestepEventType type)
{
    switch (type)
    {
    case HydraulicSimulationTimestepEventType::ReportStep:
        return QStringLiteral("report");
    case HydraulicSimulationTimestepEventType::HydraulicStep:
        return QStringLiteral("hydraulic");
    case HydraulicSimulationTimestepEventType::QualityStep:
        return QStringLiteral("quality");
    case HydraulicSimulationTimestepEventType::TankEvent:
        return QStringLiteral("tank");
    case HydraulicSimulationTimestepEventType::ControlEvent:
        return QStringLiteral("control");
    }

    return QStringLiteral("unknown");
}
}

QString HydraulicSimulationResultPrinter::toString(const HydraulicSimulationResult &result)
{
    QString output;
    QTextStream stream(&output);
    stream << "--------------------------------------------------\n";
    stream << "SIMULATION RESULT\n";
    stream << "Time: " << result.time_elapsed_s << " s\n";
    stream << "Junctions:\n";
    for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
        stream << "  " << junction.id << ": requested=" << junction.demand_requested_m3_per_h << " m3/h, delivered=" << junction.demand_delivered_m3_per_h << " m3/h, deficit=" << junction.demand_deficit_m3_per_h << " m3/h, total=" << junction.total_demand_m3_per_h << " m3/h, emitter=" << junction.emitter_flow_m3_per_h << " m3/h, leakage=" << junction.leakage_flow_m3_per_h << " m3/h, head=" << junction.head_m << " m, pressure=" << junction.pressure_head_m << " m\n";
    stream << "Reservoirs:\n";
    for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
        stream << "  " << reservoir.id << ": head=" << reservoir.head_m << " m, pressure=" << reservoir.pressure_head_m << " m\n";
    stream << "Tanks:\n";
    for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
        stream << "  " << tank.id << ": head=" << tank.head_m << " m, level=" << tank.water_level_m << " m, volume=" << tank.volume_m3 << " m3\n";
    stream << "Pipes:\n";
    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
        stream << "  " << pipe.id << ": flow=" << pipe.flow_m3_per_h << " m3/h, leakage=" << pipe.leakage_flow_m3_per_h << " m3/h, velocity=" << pipe.velocity_m_per_s << " m/s, head_loss=" << pipe.head_loss_m << " m, unit_head_loss=" << pipe.unit_head_loss_m_per_km << " m/km, friction_factor=" << pipe.friction_factor << '\n';
    stream << "Pumps:\n";
    for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
        stream << "  " << pump.id << ": flow=" << pump.flow_m3_per_h << " m3/h, head_gain=" << pump.head_gain_m << " m, power=" << pump.power_kw << " kW, efficiency=" << pump.efficiency_percent << "%\n";
    stream << "Valves:\n";
    for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
        stream << "  " << valve.id << ": flow=" << valve.flow_m3_per_h << " m3/h, head_loss=" << valve.head_loss_m << " m, setting=" << valve.setting << '\n';
    stream << "Next event: " << timestepEventText(result.event_next.type) << " in " << result.event_next.time_until_event_s << " s";
    if (!result.event_next.tank_id.isEmpty())
        stream << ", tank=" << result.event_next.tank_id;
    if (!result.event_next.control_id.isEmpty())
        stream << ", control=" << result.event_next.control_id;
    stream << '\n';
    if (result.flow_balance.flow_balance_ratio != 0.0)
    {
        stream << "Flow balance: inflow=" << result.flow_balance.total_inflow_m3_per_h
               << " m3/h, outflow=" << result.flow_balance.total_outflow_m3_per_h
               << " m3/h, consumer=" << result.flow_balance.consumer_demand_m3_per_h
               << " m3/h, deficit=" << result.flow_balance.demand_deficit_m3_per_h
               << " m3/h, emitter=" << result.flow_balance.emitter_flow_m3_per_h
               << " m3/h, leakage=" << result.flow_balance.leakage_flow_m3_per_h
               << " m3/h, storage=" << result.flow_balance.storage_flow_m3_per_h
               << " m3/h, ratio=" << result.flow_balance.flow_balance_ratio << '\n';
    }
    if (!result.links_pump_energy_usage.isEmpty())
    {
        stream << "System energy: peak=" << result.energy_usage.peak_power_kw
               << " kW, energy_cost/day=" << result.energy_usage.energy_cost_per_day
               << ", demand_charge/day=" << result.energy_usage.demand_charge_per_day
               << ", total_cost/day=" << result.energy_usage.total_cost_per_day << '\n';
        for (const HydraulicSimulationResultLinkPumpEnergyUsage &usage : result.links_pump_energy_usage)
            stream << "  " << usage.pump_id << ": online=" << usage.time_online_percent << "%, efficiency=" << usage.average_efficiency_percent << "%, average_power=" << usage.average_power_kw << " kW, peak_power=" << usage.peak_power_kw << " kW, cost/day=" << usage.average_cost_per_day << '\n';
    }
    stream << "--------------------------------------------------\n";
    return output;
}

namespace
{
QString resultValidityText(HydraulicSimulationResultValidity validity)
{
    switch (validity)
    {
    case HydraulicSimulationResultValidity::Valid:
        return QStringLiteral("Valid");
    case HydraulicSimulationResultValidity::Partial:
        return QStringLiteral("Partial");
    case HydraulicSimulationResultValidity::Invalid:
        return QStringLiteral("Invalid");
    }

    return QStringLiteral("Invalid");
}
}

QString HydraulicSimulationResultPrinter::toString(const HydraulicSimulationResultTimeline &timeline)
{
    QString output;
    QTextStream stream(&output);
    stream << "==================================================\n";
    stream << "SIMULATION RESULT TIMELINE\n";
    stream << "Validity: " << resultValidityText(timeline.validity) << '\n';
    if (timeline.simulation_start_utc.isValid())
        stream << "Start UTC: " << timeline.simulation_start_utc.toString(Qt::ISODate) << '\n';
    stream << "Results: " << timeline.results.size() << '\n';
    stream << "==================================================\n";
    for (const HydraulicSimulationResult &result : timeline.results)
    {
        if (timeline.simulation_start_utc.isValid())
        {
            if (result.time_elapsed_s <= static_cast<quint64>(std::numeric_limits<qint64>::max()))
                stream << "Timestamp UTC: " << timeline.simulation_start_utc.addSecs(static_cast<qint64>(result.time_elapsed_s)).toString(Qt::ISODate) << '\n';
            else
                stream << "Timestamp UTC: unavailable (elapsed time exceeds QDateTime range)\n";
        }
        stream << toString(result);
    }
    return output;
}

void HydraulicSimulationResultPrinter::print(const HydraulicSimulationResult &result)
{
    QTextStream stream(stdout);
    stream << toString(result);
    stream.flush();
}

void HydraulicSimulationResultPrinter::print(const HydraulicSimulationResultTimeline &timeline)
{
    QTextStream stream(stdout);
    stream << toString(timeline);
    stream.flush();
}
