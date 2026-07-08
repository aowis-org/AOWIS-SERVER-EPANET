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

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    void run(SimulationRequest request);
    void runHydraulics();
    SimulationResult readResults();
    
    QStringList reportTextList() const;
    QString reportText() const;
    
private:
    EN_Project epanet_project = nullptr;
    QStringList epanet_report;
    
    SimulationRequest simulation_request;
    
    static void epanetReportCallback(
        void *user_data,
        void *project_handle,
        const char *line
    );
    
    void addReservoir(Reservoir reservoir);
    void addJunction(Junction junction);
    void addPipe(Pipe Pipe);
    
    SimulationResult readResultsJunctions(SimulationResult result);
    SimulationResult readResultsPipes(SimulationResult result);
    
signals:
        
};

#endif // EPANET_WRAPPER_H
