#ifndef SIMULATION_STATUS_PRINTER_H
#define SIMULATION_STATUS_PRINTER_H

#include <QString>

#include <aowis/model/hydraulic/epanet_status.h>

class SimulationStatusPrinter
{
public:
    static QString toString(const EpanetStatus &status);
    static void print(const EpanetStatus &status);
};

#endif // SIMULATION_STATUS_PRINTER_H
