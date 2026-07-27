#include <aowis/epanet/epanet_simulation_manager.h>
#include <aowis/epanet/epanet_runner.h>

#include <QMetaObject>
#include <QMetaType>
#include <QRunnable>
#include <QThread>
#include <algorithm>
#include <utility>

EpanetSimulationManager::EpanetSimulationManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SimulationResultTimeline>();
    qRegisterMetaType<EpanetStatus>();
    qRegisterMetaType<QStringList>();
    qRegisterMetaType<QUuid>();
    this->thread_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
}

EpanetSimulationManager::~EpanetSimulationManager()
{
    this->shutting_down.store(true);
    this->thread_pool.clear();
    this->thread_pool.waitForDone();
}

QUuid EpanetSimulationManager::submit(const NetworkHydraulic &request)
{
    const QUuid simulation_id = QUuid::createUuid();
    emit signalSimulationQueued(simulation_id);

    QRunnable *task = QRunnable::create([this, simulation_id, request]()
    {
        if (this->shutting_down.load())
            return;

        QMetaObject::invokeMethod(this, [this, simulation_id]()
        {
            if (!this->shutting_down.load())
                emit signalSimulationStarted(simulation_id);
        }, Qt::QueuedConnection);

        EpanetRunner runner;
        EpanetRunResult run_result = runner.run(request);

        QMetaObject::invokeMethod(this, [this, simulation_id, run_result = std::move(run_result)]() mutable
        {
            if (this->shutting_down.load())
                return;

            if (run_result.timeline.status.success)
                emit signalSimulationFinished(simulation_id, std::move(run_result.timeline), std::move(run_result.report));
            else
                emit signalSimulationFailed(simulation_id, std::move(run_result.timeline.status), std::move(run_result.report));
        }, Qt::QueuedConnection);
    });

    this->thread_pool.start(task);
    return simulation_id;
}

void EpanetSimulationManager::setMaxWorkerCount(int count)
{
    this->thread_pool.setMaxThreadCount(std::max(1, count));
}

int EpanetSimulationManager::maxWorkerCount() const
{
    return this->thread_pool.maxThreadCount();
}
