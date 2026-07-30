#include <aowis/epanet/utility/hydraulic_simulation_result_printer.h>

#include <cstdio>
#include <limits>

#include <QTextStream>

QString HydraulicSimulationResultPrinter::toString(const HydraulicSimulationResult &result)
{
    QString output;
    QTextStream stream(&output);
    stream << "--------------------------------------------------\n";
    stream << "SIMULATION RESULT\n";
    stream << "Time: " << result.time_elapsed_s << " s\n";
    stream << "Junctions:\n";
    for (const HydraulicSimulationResultNodeJunction &junction : result.nodes_junctions)
        stream << "  " << junction.id << ": head=" << junction.head_m << " m, pressure=" << junction.pressure_head_m << " m\n";
    stream << "Reservoirs:\n";
    for (const HydraulicSimulationResultNodeReservoir &reservoir : result.nodes_reservoirs)
        stream << "  " << reservoir.id << ": head=" << reservoir.head_m << " m, pressure=" << reservoir.pressure_head_m << " m\n";
    stream << "Tanks:\n";
    for (const HydraulicSimulationResultNodeTank &tank : result.nodes_tanks)
        stream << "  " << tank.id << ": head=" << tank.head_m << " m, level=" << tank.water_level_m << " m, volume=" << tank.volume_m3 << " m3\n";
    stream << "Pipes:\n";
    for (const HydraulicSimulationResultLinkPipe &pipe : result.links_pipes)
        stream << "  " << pipe.id << ": flow=" << pipe.flow_m3_per_h << " m3/h, velocity=" << pipe.velocity_m_per_s << " m/s, head_loss=" << pipe.head_loss_m << " m" << '\n';
    stream << "Pumps:\n";
    for (const HydraulicSimulationResultLinkPump &pump : result.links_pumps)
        stream << "  " << pump.id << ": flow=" << pump.flow_m3_per_h << " m3/h, head_gain=" << pump.head_gain_m << " m, power=" << pump.power_kw << " kW, efficiency=" << pump.efficiency_percent << "%\n";
    stream << "Valves:\n";
    for (const HydraulicSimulationResultLinkValve &valve : result.links_valves)
        stream << "  " << valve.id << ": flow=" << valve.flow_m3_per_h << " m3/h, head_loss=" << valve.head_loss_m << " m, setting=" << valve.setting << '\n';
    stream << "--------------------------------------------------\n";
    return output;
}

QString HydraulicSimulationResultPrinter::toString(const HydraulicSimulationResultTimeline &timeline)
{
    QString output;
    QTextStream stream(&output);
    stream << "==================================================\n";
    stream << "SIMULATION RESULT TIMELINE\n";
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
