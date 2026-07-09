#ifndef EPANET_STATUS_H
#define EPANET_STATUS_H

#include <QString>
#include <QStringList>

struct EpanetStatus
{
    bool success = true;
    
    int epanet_error_code = 0;
    
    QString stage;        // "build-network", "run-hydraulics", "read-results"
    QString operation;    // "EN_addlink", "EN_getnodevalue", ...
    QString object_type;  // "pipe", "junction", "reservoir"
    QString object_id;    // "P1", "J42", ...
    
    QString message;        // AOWIS/user-facing message
    QString epanet_message; // decoded EPANET message if available
    QStringList details;    // optional extra context
};

#endif // EPANET_STATUS_H
