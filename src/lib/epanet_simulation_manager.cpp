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
    
    qRegisterMetaType<SimulationResult>("SimulationResult");
    qRegisterMetaType<EpanetStatus>("EpanetStatus");
}

EpanetSimulationManager::~EpanetSimulationManager()
{
    this->shutting_down.store(true, std::memory_order_release);
    
    // Remove requests that have not started yet.
    this->thread_pool.clear();
    
    // Running EPANET calls cannot simply be killed safely.
    this->thread_pool.waitForDone();
}

QUuid EpanetSimulationManager::submit(
    const SimulationRequest &request
    )
{
    const QUuid simulation_id = QUuid::createUuid();
    
    emit signalSimulationQueued(simulation_id);
    
    this->thread_pool.start(
        [this, simulation_id, request]()
        {
            if (this->shutting_down.load(std::memory_order_acquire))
                return;
            
            QMetaObject::invokeMethod(
                this,
                [this, simulation_id]()
                {
                    emit signalSimulationStarted(simulation_id);
                },
                Qt::QueuedConnection
                );
            
            /*
             * Important:
             *
             * This wrapper is local to this worker task.
             * Every simultaneous task therefore has:
             *
             * - its own EpanetWrapper
             * - its own EN_Project
             * - its own SimulationRequest
             * - its own SimulationResult
             * - its own report callback data
             */
            EpanetWrapper wrapper;
            
            bool simulation_succeeded = false;
            
            SimulationResult simulation_result;
            
            EpanetStatus simulation_status;
            simulation_status.success = false;
            simulation_status.message =
                "Simulation ended without reporting a result";
            
            QObject::connect(
                &wrapper,
                &EpanetWrapper::signalSimulationFinished,
                &wrapper,
                [&simulation_succeeded, &simulation_result](
                    SimulationResult result
                    )
                {
                    simulation_succeeded = true;
                    simulation_result = std::move(result);
                },
                Qt::DirectConnection
                );
            
            QObject::connect(
                &wrapper,
                &EpanetWrapper::signalSimulationFailed,
                &wrapper,
                [&simulation_succeeded, &simulation_status](
                    EpanetStatus status
                    )
                {
                    simulation_succeeded = false;
                    simulation_status = std::move(status);
                },
                Qt::DirectConnection
                );
            
            wrapper.run(request);
            
            QStringList report = wrapper.reportTextList();
            
            if (this->shutting_down.load(std::memory_order_acquire))
                return;
            
            QMetaObject::invokeMethod(
                this,
                [
                    this,
                    simulation_id,
                    simulation_succeeded,
                    simulation_result = std::move(simulation_result),
                    simulation_status = std::move(simulation_status),
                    report = std::move(report)
            ]() mutable
                {
                    if (simulation_succeeded)
                    {
                        emit signalSimulationFinished(
                            simulation_id,
                            std::move(simulation_result),
                            std::move(report)
                            );
                    }
                    else
                    {
                        emit signalSimulationFailed(
                            simulation_id,
                            std::move(simulation_status),
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

void EpanetSimulationManager::setMaxWorkerCount(int count)
{
    if (count < 1)
        count = 1;
    
    this->thread_pool.setMaxThreadCount(count);
}

int EpanetSimulationManager::maxWorkerCount() const
{
    return this->thread_pool.maxThreadCount();
}
