#ifndef AOWIS_EPANET_SIMULATION_MANAGER_H
#define AOWIS_EPANET_SIMULATION_MANAGER_H

#include <atomic>

#include <QObject>
#include <QThreadPool>
#include <QUuid>

#include <aowis/model/hydraulic/hydraulic_simulation_results.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

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
    void signalSimulationFinished(QUuid simulation_id, HydraulicSimulationResultTimeline result_timeline, QStringList report_lines);
    void signalSimulationFailed(QUuid simulation_id, HydraulicSimulationStatus status, QStringList report_lines);

private:
    QThreadPool thread_pool;
    std::atomic_bool shutting_down = false;
};

#endif // AOWIS_EPANET_SIMULATION_MANAGER_H
