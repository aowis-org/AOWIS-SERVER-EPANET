#ifndef EPANET_WRAPPER_H
#define EPANET_WRAPPER_H

#include <QObject>
#include <QDebug>

#include <QString>
#include <QStringList>

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

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    void run(const SimulationRequest &request);
    bool runHydraulics();
    bool readResults();
    
    QStringList reportTextList() const;
    QString reportText() const;
    
private:
    EN_Project epanet_project = nullptr;
    QStringList epanet_report;
    
    SimulationRequest simulation_request;
    SimulationResult simulation_result;
    
    static void epanetReportCallback(
        void *user_data,
        void *project_handle,
        const char *line
    );
    
    EpanetStatus addReservoir(const Reservoir &reservoir);
    EpanetStatus addJunction(const Junction &junction);
    EpanetStatus addPipe(const Pipe &pipe);
    
    bool readResultsJunctions();
    bool readResultsPipes();
    
    void cleanupProject();
    
signals:
    void signalSimulationFinished(SimulationResult result);
    void signalSimulationFailed(EpanetStatus status);
        
};

#endif // EPANET_WRAPPER_H
