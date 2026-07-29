#include <aowis/epanet/utility/epanet_result_printer.h>

#include <QTextStream>
#include <cstdio>

QString EpanetResultPrinter::toString(const EpanetResult &result)
{
    QString output;
    QTextStream stream(&output);
    stream << "--------------------------------------------------\n";
    stream << "SIMULATION RESULT\n";
    stream << "Time: " << result.time_elapsed_s << " s\n";
    stream << "Junctions:\n";
    for (const EpanetResultNodeJunction &junction : result.nodes_junctions)
        stream << "  " << junction.id << ": head=" << junction.head_m << " m, pressure=" << junction.pressure_head_m << " m\n";
    stream << "Tanks:\n";
    for (const EpanetResultNodeTank &tank : result.nodes_tanks)
        stream << "  " << tank.id << ": head=" << tank.head_m << " m, level=" << tank.water_level_m << " m, volume=" << tank.volume_m3 << " m3\n";
    stream << "Pipes:\n";
    for (const EpanetResultLinkPipe &pipe : result.links_pipes)
        stream << "  " << pipe.id << ": flow=" << pipe.flow_m3_per_h << " m3/h, velocity=" << pipe.velocity_m_per_s << " m/s, headloss=" << pipe.headloss << '\n';
    stream << "--------------------------------------------------\n";
    return output;
}

QString EpanetResultPrinter::toString(const EpanetResultTimeline &timeline)
{
    QString output;
    QTextStream stream(&output);
    stream << "==================================================\n";
    stream << "SIMULATION RESULT TIMELINE\n";
    if (timeline.simulation_start_utc.isValid())
        stream << "Start UTC: " << timeline.simulation_start_utc.toString(Qt::ISODate) << '\n';
    stream << "Results: " << timeline.results.size() << '\n';
    stream << "==================================================\n";
    for (const EpanetResult &result : timeline.results)
    {
        if (timeline.simulation_start_utc.isValid())
            stream << "Timestamp UTC: " << timeline.simulation_start_utc.addSecs(result.time_elapsed_s).toString(Qt::ISODate) << '\n';
        stream << toString(result);
    }
    return output;
}

void EpanetResultPrinter::print(const EpanetResult &result)
{
    QTextStream stream(stdout);
    stream << toString(result);
    stream.flush();
}

void EpanetResultPrinter::print(const EpanetResultTimeline &timeline)
{
    QTextStream stream(stdout);
    stream << toString(timeline);
    stream.flush();
}
