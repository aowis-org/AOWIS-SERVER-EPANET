#ifndef SIMULATION_RESULT_H
#define SIMULATION_RESULT_H

#include <QString>
#include <QList>
#include <QDateTime>

#include <QMetaType>

#include "epanet_status.h"

struct JunctionResult
{
    QString id;
    double head_m = 0.0;
    double pressure_m = 0.0;
};

struct PipeResult
{
    QString id;
    double flow_lps = 0.0;
    double velocity_mps = 0.0;
    double headloss = 0.0;
};

struct TankResult
{
    QString id;
    
    double head_m = 0.0;
    double level_m = 0.0;
    double volume_m3 = 0.0;
};

struct SimulationResult
{
    long elapsed_time_s = 0;
    
    EpanetStatus status;
    
    QList<JunctionResult> junctions;
    QList<TankResult> tanks;
    QList<PipeResult> pipes;
};

struct SimulationResultTimeline
{
    EpanetStatus status;
    QDateTime simulation_start_utc;
    QList<SimulationResult> results;
};

Q_DECLARE_METATYPE(SimulationResultTimeline)

#endif // SIMULATION_RESULT_H
