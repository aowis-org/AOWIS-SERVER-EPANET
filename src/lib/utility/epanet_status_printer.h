#ifndef EPANET_STATUS_PRINTER_H
#define EPANET_STATUS_PRINTER_H

#include <QString>

#include "model/epanet_status.h"

class EpanetStatusPrinter
{
public:
    static QString toString(const EpanetStatus &status);
    static void print(const EpanetStatus &status);

private:
    static QString stageToString(EpanetStage stage);
    static QString operationToString(EpanetOperation operation);
    static QString propertyToString(EpanetProperty property);
    static QString entityTypeToString(EpanetEntityType type);
};

#endif // EPANET_STATUS_PRINTER_H
