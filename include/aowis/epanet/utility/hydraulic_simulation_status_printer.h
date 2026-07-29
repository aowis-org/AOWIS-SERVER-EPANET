#ifndef AOWIS_EPANET_HYDRAULIC_SIMULATION_STATUS_PRINTER_H
#define AOWIS_EPANET_HYDRAULIC_SIMULATION_STATUS_PRINTER_H

#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class HydraulicSimulationStatusPrinter
{
public:
    static QString toString(const HydraulicSimulationStatus &status);
    static void print(const HydraulicSimulationStatus &status);
};

#endif // AOWIS_EPANET_HYDRAULIC_SIMULATION_STATUS_PRINTER_H
