#include <aowis/epanet/epanet_wrapper.h>
#include <aowis/epanet/epanet_runner.h>

#include <QMetaType>
#include <QMutexLocker>

EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SimulationResultTimeline>();
    qRegisterMetaType<EpanetStatus>();
}

SimulationResultTimeline EpanetWrapper::run(const NetworkHydraulic &request)
{
    QMutexLocker locker(&this->run_mutex);

    EpanetRunner runner;
    EpanetRunResult run_result = runner.run(request);
    this->report = run_result.report;
    this->latest_status = run_result.timeline.status;
    this->has_run = true;

    locker.unlock();

    if (run_result.timeline.status.success)
        emit signalSimulationFinished(run_result.timeline);
    else
        emit signalSimulationFailed(run_result.timeline.status);

    return run_result.timeline;
}

QStringList EpanetWrapper::reportTextList() const
{
    QMutexLocker locker(&this->run_mutex);
    return this->report;
}

QString EpanetWrapper::reportText() const
{
    QMutexLocker locker(&this->run_mutex);
    return this->report.join('\n');
}
