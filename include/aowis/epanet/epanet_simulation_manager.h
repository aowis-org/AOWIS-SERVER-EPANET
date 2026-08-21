#ifndef AOWIS_EPANET_SIMULATION_MANAGER_H
#define AOWIS_EPANET_SIMULATION_MANAGER_H

#include <atomic>
#include <memory>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QUuid>

#include <aowis/epanet/epanet_batch_request.h>
#include <aowis/epanet/epanet_result_batch.h>

class EpanetSimulationManager : public QObject
{
    Q_OBJECT

public:
    explicit EpanetSimulationManager(QObject *parent = nullptr);
    ~EpanetSimulationManager() override;

    QUuid submit(const EpanetBatchRequest &request);
    bool cancel(const QUuid &simulation_id);
    void cancelAll();

    void setMaxWorkerCount(int count);
    int maxWorkerCount() const;
    int activeSimulationCount() const;

signals:
    void signalSimulationQueued(QUuid simulation_id);
    void signalSimulationStarted(QUuid simulation_id);
    void signalSimulationCompleted(QUuid simulation_id, EpanetResultBatch result);

private:
    void removeSimulation(const QUuid &simulation_id);

    mutable QMutex simulations_mutex;
    QHash<QUuid, std::shared_ptr<std::atomic_bool>> cancellation_flags;
    QThreadPool thread_pool;
    std::atomic_bool shutting_down = false;
};

#endif // AOWIS_EPANET_SIMULATION_MANAGER_H
