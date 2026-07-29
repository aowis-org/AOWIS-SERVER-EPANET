#ifndef AOWIS_EPANET_SIMULATION_MANAGER_H
#define AOWIS_EPANET_SIMULATION_MANAGER_H

#include <QObject>
#include <QThreadPool>
#include <QUuid>
#include <atomic>
#include <aowis/model/hydraulic/network_hydraulic.h>
#include <aowis/model/hydraulic/epanet_results.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetSimulationManager : public QObject
{
    Q_OBJECT

public:
    explicit EpanetSimulationManager(QObject *parent = nullptr);
    ~EpanetSimulationManager() override;

    QUuid submit(const NetworkHydraulic &request);
    void setMaxWorkerCount(int count);
    int maxWorkerCount() const;

signals:
    void signalSimulationQueued(QUuid simulation_id);
    void signalSimulationStarted(QUuid simulation_id);
    void signalSimulationFinished(QUuid simulation_id, EpanetResultTimeline result_timeline, QStringList report_lines);
    void signalSimulationFailed(QUuid simulation_id, EpanetStatus status, QStringList report_lines);

private:
    QThreadPool thread_pool;
    std::atomic_bool shutting_down = false;
};

#endif
