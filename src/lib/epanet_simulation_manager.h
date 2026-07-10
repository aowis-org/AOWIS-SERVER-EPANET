#ifndef EPANET_SIMULATION_MANAGER_H
#define EPANET_SIMULATION_MANAGER_H

#include <QObject>
#include <QThreadPool>
#include <QUuid>

#include <atomic>

#include "model/simulation_request.h"
#include "model/simulation_result.h"
#include "model/epanet_status.h"

class EpanetSimulationManager : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetSimulationManager(QObject *parent = nullptr);
    ~EpanetSimulationManager() override;
    
    QUuid submit(const SimulationRequest &request);
    
    void setMaxWorkerCount(int count);
    int maxWorkerCount() const;
    
signals:
    void signalSimulationQueued(QUuid simulation_id);
    void signalSimulationStarted(QUuid simulation_id);
    
    void signalSimulationFinished(
        QUuid simulation_id,
        SimulationResult result,
        QStringList report
        );
    
    void signalSimulationFailed(
        QUuid simulation_id,
        EpanetStatus status,
        QStringList report
        );
    
private:
    QThreadPool thread_pool;
    std::atomic_bool shutting_down = false;
};

#endif // EPANET_SIMULATION_MANAGER_H
