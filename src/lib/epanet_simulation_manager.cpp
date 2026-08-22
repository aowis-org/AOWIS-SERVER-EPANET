#include <aowis/epanet/epanet_simulation_manager.h>
#include <aowis/epanet/epanet_runner.h>

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QMetaType>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>

EpanetSimulationManager::EpanetSimulationManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<EpanetResultRun>();
    qRegisterMetaType<QUuid>();
    this->thread_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
}

EpanetSimulationManager::~EpanetSimulationManager()
{
    this->shutting_down.store(true);
    cancelAll();
    this->thread_pool.clear();
    this->thread_pool.waitForDone();

    QMutexLocker<QMutex> locker(&this->simulations_mutex);
    this->cancellation_flags.clear();
}

QUuid EpanetSimulationManager::submit(const EpanetRunRequest &request)
{
    const QUuid simulation_id = QUuid::createUuid();
    const std::shared_ptr<std::atomic_bool> cancellation_flag = std::make_shared<std::atomic_bool>(false);

    {
        QMutexLocker<QMutex> locker(&this->simulations_mutex);
        this->cancellation_flags.insert(simulation_id, cancellation_flag);
    }

    emit signalSimulationQueued(simulation_id);

    QRunnable *task = QRunnable::create([this, simulation_id, request, cancellation_flag]()
    {
        if (this->shutting_down.load())
        {
            removeSimulation(simulation_id);
            return;
        }

        QMetaObject::invokeMethod(this, [this, simulation_id]()
        {
            if (!this->shutting_down.load())
                emit signalSimulationStarted(simulation_id);
        }, Qt::QueuedConnection);

        EpanetRunner runner;
        EpanetResultRun result = runner.run(request, [this, cancellation_flag]()
        {
            return this->shutting_down.load() || cancellation_flag->load();
        });

        removeSimulation(simulation_id);

        QMetaObject::invokeMethod(this, [this, simulation_id, result = std::move(result)]() mutable
        {
            if (!this->shutting_down.load())
                emit signalSimulationCompleted(simulation_id, std::move(result));
        }, Qt::QueuedConnection);
    });

    this->thread_pool.start(task);
    return simulation_id;
}

bool EpanetSimulationManager::cancel(const QUuid &simulation_id)
{
    QMutexLocker<QMutex> locker(&this->simulations_mutex);
    const QHash<QUuid, std::shared_ptr<std::atomic_bool>>::const_iterator it = this->cancellation_flags.constFind(simulation_id);
    if (it == this->cancellation_flags.constEnd())
        return false;

    it.value()->store(true);
    return true;
}

void EpanetSimulationManager::cancelAll()
{
    QMutexLocker<QMutex> locker(&this->simulations_mutex);
    QHash<QUuid, std::shared_ptr<std::atomic_bool>>::const_iterator it = this->cancellation_flags.constBegin();
    while (it != this->cancellation_flags.constEnd())
    {
        it.value()->store(true);
        ++it;
    }
}

void EpanetSimulationManager::setMaxWorkerCount(int count)
{
    this->thread_pool.setMaxThreadCount(std::max(1, count));
}

int EpanetSimulationManager::maxWorkerCount() const
{
    return this->thread_pool.maxThreadCount();
}

int EpanetSimulationManager::activeSimulationCount() const
{
    QMutexLocker<QMutex> locker(&this->simulations_mutex);
    return this->cancellation_flags.size();
}

void EpanetSimulationManager::removeSimulation(const QUuid &simulation_id)
{
    QMutexLocker<QMutex> locker(&this->simulations_mutex);
    this->cancellation_flags.remove(simulation_id);
}
