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

#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

#include "epanet_resolvers.h"

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    SimulationResultTimeline run(const NetworkHydraulic &request);
    
    EpanetStatus runHydraulics();
    
    QStringList reportTextList() const;
    QString reportText() const;
    
private:
    EN_Project epanet_project = nullptr;
    QStringList epanet_report;
    QString getEpanetErrorMessage(int error_code) const;
    
    NetworkHydraulic simulation_request;
    SimulationResultTimeline simulation_result_timeline;
    
    static void epanetReportCallback(
        void *user_data,
        void *project_handle,
        const char *line
    );
    
    EpanetStatus addEntities(const NetworkHydraulic &request);
    
    EpanetStatus addTankVolumeCurve(const TankVolumeCurve &curve);
    
    EpanetStatus addReservoir(const Reservoir &reservoir);
    EpanetStatus addJunction(const Junction &junction);
    EpanetStatus addTank(const Tank &tank);
    EpanetStatus addPipe(const Pipe &pipe);
    
    EpanetStatus readResults(SimulationResult &result);
    
    EpanetStatus readResultsJunctions(SimulationResult &result);
    EpanetStatus readResultsTanks(SimulationResult &result);
    EpanetStatus readResultsPipes(SimulationResult &result);
    
    void cleanupProject();
    
signals:
    void signalSimulationFinished(SimulationResultTimeline result);
    void signalSimulationFailed(EpanetStatus status);
        
};

#endif // EPANET_WRAPPER_H
