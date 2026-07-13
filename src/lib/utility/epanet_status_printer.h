#ifndef EPANET_STATUS_PRINTER_H
#define EPANET_STATUS_PRINTER_H

#include <QString>

#include "model/epanet_status.h"

class EpanetStatusPrinter
{
public:
    static QString toString(const EpanetStatus &status);
    static void print(const EpanetStatus &status);
};

#endif // EPANET_STATUS_PRINTER_H
