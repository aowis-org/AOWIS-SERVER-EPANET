#include "epanet_simulation_manager.h"

#include "epanet_wrapper.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

EpanetSimulationManager::EpanetSimulationManager(QObject *parent)
    : QObject(parent)
{
    int worker_count = QThread::idealThreadCount();
    
    // Keep one logical CPU available for the GUI, HTTP server,
    // operating system, and other application work.
    if (worker_count > 1)
        worker_count--;
    
    if (worker_count < 1)
        worker_count = 1;
    
    this->thread_pool.setMaxThreadCount(worker_count);
    
    qRegisterMetaType<SimulationResultTimeline>(
        "SimulationResultTimeline"
        );
    
    qRegisterMetaType<EpanetStatus>(
        "EpanetStatus"
        );
}

EpanetSimulationManager::~EpanetSimulationManager()
{
    this->shutting_down.store(
        true,
        std::memory_order_release
        );
    
    // Remove requests that have not started yet.
    this->thread_pool.clear();
    
    // Running EPANET calls cannot simply be killed safely.
    this->thread_pool.waitForDone();
}

QUuid EpanetSimulationManager::submit(
    const NetworkHydraulic &request
    )
{
    const QUuid simulation_id =
        QUuid::createUuid();
    
    emit signalSimulationQueued(simulation_id);
    
    this->thread_pool.start(
        [this, simulation_id, request]()
        {
            if (
                this->shutting_down.load(
                    std::memory_order_acquire
                    )
                )
            {
                return;
            }
            
            QMetaObject::invokeMethod(
                this,
                [this, simulation_id]()
                {
                    emit signalSimulationStarted(
                        simulation_id
                        );
                },
                Qt::QueuedConnection
                );
            
            /*
             * This wrapper is local to this worker task.
             * Every simultaneous task therefore has its own
             * EpanetWrapper, EN_Project, request, result timeline,
             * and report callback data.
             */
            EpanetWrapper wrapper;
            
            SimulationResultTimeline timeline =
                wrapper.run(request);
            
            QStringList report =
                wrapper.reportTextList();
            
            if (
                this->shutting_down.load(
                    std::memory_order_acquire
                    )
                )
            {
                return;
            }
            
            QMetaObject::invokeMethod(
                this,
                [
                    this,
                    simulation_id,
                    timeline = std::move(timeline),
                    report = std::move(report)
            ]() mutable
                {
                    if (timeline.status.success)
                    {
                        emit signalSimulationFinished(
                            simulation_id,
                            std::move(timeline),
                            std::move(report)
                            );
                    }
                    else
                    {
                        EpanetStatus status =
                            std::move(timeline.status);
                        
                        emit signalSimulationFailed(
                            simulation_id,
                            std::move(status),
                            std::move(report)
                            );
                    }
                },
                Qt::QueuedConnection
                );
        }
        );
    
    return simulation_id;
}

void EpanetSimulationManager::setMaxWorkerCount(
    int count
    )
{
    if (count < 1)
        count = 1;
    
    this->thread_pool.setMaxThreadCount(count);
}

int EpanetSimulationManager::maxWorkerCount() const
{
    return this->thread_pool.maxThreadCount();
}
