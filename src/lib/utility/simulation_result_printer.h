#ifndef SIMULATION_RESULT_PRINTER_H
#define SIMULATION_RESULT_PRINTER_H

#include <QString>

#include "model/simulation_result.h"

class SimulationResultPrinter
{
public:
    static QString toString(const SimulationResult &result);
    static QString toString(const SimulationResultTimeline &timeline);
    
    static void print(const SimulationResult &result);
    static void print(const SimulationResultTimeline &timeline);
};

#endif // SIMULATION_RESULT_PRINTER_H
