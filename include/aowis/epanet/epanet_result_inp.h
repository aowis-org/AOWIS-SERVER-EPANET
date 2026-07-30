#ifndef AOWIS_EPANET_RESULT_INP_H
#define AOWIS_EPANET_RESULT_INP_H

#include <QString>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

struct EpanetResultInp
{
    QString inp_text;
    HydraulicSimulationStatus status;
    QStringList report_lines;
};

#endif // AOWIS_EPANET_RESULT_INP_H
