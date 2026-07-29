#ifndef AOWIS_EPANET_RESULT_PRINTER_H
#define AOWIS_EPANET_RESULT_PRINTER_H

#include <QString>
#include <aowis/model/hydraulic/epanet_results.h>

class EpanetResultPrinter
{
public:
    static QString toString(const EpanetResult &result);
    static QString toString(const EpanetResultTimeline &timeline);
    static void print(const EpanetResult &result);
    static void print(const EpanetResultTimeline &timeline);
};

#endif
