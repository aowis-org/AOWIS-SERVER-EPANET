#ifndef EPANET_WRAPPER_H
#define EPANET_WRAPPER_H

#include <QObject>
#include <QDebug>

#include <QString>
#include <QStringList>

#include <QMetaType>

#if __has_include(<epanet2_2.h>)
#include <epanet2_2.h>
#elif __has_include(<epanet2.h>)
#include <epanet2.h>
#else
#error "Could not find EPANET header."
#endif

#include "model/simulation_request.h"
#include "model/simulation_result.h"
#include "model/epanet_status.h"

Q_DECLARE_METATYPE(SimulationResult)
Q_DECLARE_METATYPE(EpanetStatus)

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    void run(const SimulationRequest &request);
    EpanetStatus runHydraulics();
    EpanetStatus readResults();
    
    QStringList reportTextList() const;
    QString reportText() const;
    
private:
    EN_Project epanet_project = nullptr;
    QStringList epanet_report;
    QString getEpanetErrorMessage(int error_code) const;
    
    SimulationRequest simulation_request;
    SimulationResult simulation_result;
    
    static void epanetReportCallback(
        void *user_data,
        void *project_handle,
        const char *line
    );
    
    EpanetStatus addEntities(const SimulationRequest &request);
    EpanetStatus addReservoir(const Reservoir &reservoir);
    EpanetStatus addJunction(const Junction &junction);
    EpanetStatus addPipe(const Pipe &pipe);
    
    EpanetStatus readResultsJunctions();
    EpanetStatus readResultsPipes();
    
    void cleanupProject();
    
signals:
    void signalSimulationFinished(SimulationResult result);
    void signalSimulationFailed(EpanetStatus status);
        
};

#endif // EPANET_WRAPPER_H
