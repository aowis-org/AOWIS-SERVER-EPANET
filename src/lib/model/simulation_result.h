#ifndef SIMULATION_RESULT_H
#define SIMULATION_RESULT_H

#include <QString>
#include <QList>

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

struct SimulationResult
{
    QList<JunctionResult> junctions;
    QList<PipeResult> pipes;
};

#endif // SIMULATION_RESULT_H
