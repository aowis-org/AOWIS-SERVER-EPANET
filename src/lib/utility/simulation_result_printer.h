#ifndef SIMULATION_RESULT_PRINTER_H
#define SIMULATION_RESULT_PRINTER_H

#include <QString>

#include "model/simulation_result.h"

class SimulationResultPrinter
{
public:
    static QString toString(
        const SimulationResult &result
        );
    
    static void print(
        const SimulationResult &result
        );
};

#endif // SIMULATION_RESULT_PRINTER_H
