#include "simulation_result_printer.h"

#include <QTextStream>


namespace
{
    QString formatNumber(double value)
    {
        return QString::number(value, 'f', 3);
    }
}

QString SimulationResultPrinter::toString(
    const SimulationResult &result
)
{
    QString output;
    QTextStream stream(&output);
    
    stream << "--------------------------------------------------\n";
    stream << "EPANET SIMULATION RESULT\n";
    
    stream << "Junctions:     "
           << result.junctions.size()
           << '\n';
    
    for (const JunctionResult &junction : result.junctions)
    {
        stream << "\n";
        stream << "Junction:      "
               << junction.id
               << '\n';
        
        stream << "  Head:        "
               << formatNumber(junction.head_m)
               << " m\n";
        
        stream << "  Pressure:    "
               << formatNumber(junction.pressure_m)
               << " m\n";
    }
    
    stream << "\n";
    stream << "Tanks:         "
           << result.tanks.size()
           << '\n';
    
    for (const TankResult &tank : result.tanks)
    {
        stream << "\n";
        stream << "Tank:          "
               << tank.id
               << '\n';
        
        stream << "  Head:        "
               << formatNumber(tank.head_m)
               << " m\n";
        
        stream << "  Level:       "
               << formatNumber(tank.level_m)
               << " m\n";
        
        stream << "  Volume:      "
               << formatNumber(tank.volume_m3)
               << " m³\n";
    }
    
    stream << "\n";
    stream << "Pipes:         "
           << result.pipes.size()
           << '\n';
    
    for (const PipeResult &pipe : result.pipes)
    {
        stream << "\n";
        stream << "Pipe:          "
               << pipe.id
               << '\n';
        
        stream << "  Flow:        "
               << formatNumber(pipe.flow_lps)
               << " L/s\n";
        
        stream << "  Velocity:    "
               << formatNumber(pipe.velocity_mps)
               << " m/s\n";
        
        stream << "  Headloss:    "
               << formatNumber(pipe.headloss)
               << '\n';
    }
    
    stream << "--------------------------------------------------\n";
    
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
