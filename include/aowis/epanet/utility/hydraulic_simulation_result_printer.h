#ifndef AOWIS_EPANET_HYDRAULIC_SIMULATION_RESULT_PRINTER_H
#define AOWIS_EPANET_HYDRAULIC_SIMULATION_RESULT_PRINTER_H

#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>

class HydraulicSimulationResultPrinter
{
public:
    static QString toString(const HydraulicSimulationResult &result);
    static QString toString(const HydraulicSimulationResultTimeline &timeline);
    static void print(const HydraulicSimulationResult &result);
    static void print(const HydraulicSimulationResultTimeline &timeline);
};

#endif // AOWIS_EPANET_HYDRAULIC_SIMULATION_RESULT_PRINTER_H
