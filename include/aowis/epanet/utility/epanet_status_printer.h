#ifndef AOWIS_EPANET_STATUS_PRINTER_H
#define AOWIS_EPANET_STATUS_PRINTER_H

#include <QString>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetStatusPrinter
{
public:
    static QString toString(const EpanetStatus &status);
    static void print(const EpanetStatus &status);
};

#endif
