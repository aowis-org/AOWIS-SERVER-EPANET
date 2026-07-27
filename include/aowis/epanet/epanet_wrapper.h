#ifndef AOWIS_EPANET_WRAPPER_H
#define AOWIS_EPANET_WRAPPER_H

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <aowis/model/hydraulic/network.h>
#include <aowis/model/hydraulic/simulation_result.h>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetWrapper : public QObject
{
    Q_OBJECT

public:
    explicit EpanetWrapper(QObject *parent = nullptr);

    SimulationResultTimeline run(const NetworkHydraulic &request);
    QStringList reportTextList() const;
    QString reportText() const;

signals:
    void signalSimulationFinished(SimulationResultTimeline result);
    void signalSimulationFailed(EpanetStatus status);

private:
    mutable QMutex run_mutex;
    QStringList report;
    EpanetStatus latest_status;
    bool has_run = false;
};

#endif
