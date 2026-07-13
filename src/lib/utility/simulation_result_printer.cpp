#include "simulation_result_printer.h"

#include <QTextStream>

#include <cstdio>

QString SimulationResultPrinter::toString(
    const SimulationResult &result
    )
{
    QString output;
    QTextStream stream(&output);
    
    stream << "--------------------------------------------------\n";
    stream << "SIMULATION RESULT\n";
    stream << "Time: "
           << result.elapsed_time_s
           << " s\n";
    
    stream << "Junctions:\n";
    
    for (const JunctionResult &junction : result.junctions)
    {
        stream << "  "
               << junction.id
               << ": head="
               << junction.head_m
               << " m, pressure="
               << junction.pressure_m
               << " m\n";
    }
    
    stream << "Tanks:\n";
    
    for (const TankResult &tank : result.tanks)
    {
        stream << "  "
               << tank.id
               << ": head="
               << tank.head_m
               << " m, level="
               << tank.level_m
               << " m, volume="
               << tank.volume_m3
               << " m3\n";
    }
    
    stream << "Pipes:\n";
    
    for (const PipeResult &pipe : result.pipes)
    {
        stream << "  "
               << pipe.id
               << ": flow="
               << pipe.flow_lps
               << " L/s, velocity="
               << pipe.velocity_mps
               << " m/s, headloss="
               << pipe.headloss
               << '\n';
    }
    
    stream << "--------------------------------------------------\n";
    
    return output;
}

QString SimulationResultPrinter::toString(
    const SimulationResultTimeline &timeline
    )
{
    QString output;
    QTextStream stream(&output);
    
    stream << "==================================================\n";
    stream << "SIMULATION RESULT TIMELINE\n";
    
    if (timeline.simulation_start_utc.isValid())
    {
        stream << "Start UTC: "
               << timeline.simulation_start_utc.toString(
                      Qt::ISODate
                      )
               << '\n';
    }
    
    stream << "Results: "
           << timeline.results.size()
           << '\n';
    
    stream << "==================================================\n";
    
    for (const SimulationResult &result : timeline.results)
    {
        if (timeline.simulation_start_utc.isValid())
        {
            stream << "Timestamp UTC: "
                   << timeline.simulation_start_utc
                          .addSecs(result.elapsed_time_s)
                          .toString(Qt::ISODate)
                   << '\n';
        }
        
        stream << toString(result);
    }
    
    return output;
}

void SimulationResultPrinter::print(
    const SimulationResult &result
    )
{
    QTextStream stream(stdout);
    stream << toString(result);
    stream.flush();
}

void SimulationResultPrinter::print(
    const SimulationResultTimeline &timeline
    )
{
    QTextStream stream(stdout);
    stream << toString(timeline);
    stream.flush();
}
