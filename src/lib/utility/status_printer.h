#ifndef STATUS_PRINTER_H
#define STATUS_PRINTER_H

#include <QString>

#include "model/epanet_status.h"

class StatusPrinter
{
public:
    static QString toString(const EpanetStatus &status);
    static void print(const EpanetStatus &status);
};

#endif // STATUS_PRINTER_H
