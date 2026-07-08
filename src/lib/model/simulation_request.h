#ifndef SIMULATION_REQUEST_H
#define SIMULATION_REQUEST_H

#include <QString>
#include <QList>

struct Reservoir
{
    QString id;
    double head_m;
};

struct Junction
{
    QString id;
    double elevation_m;
    double demand_lps;
};

struct Pipe
{
    QString id;
    QString node_id_from;
    QString node_id_to;
    double length_m;
    double diameter_mm;
    double roughness_hw;
    double minor_loss;
    bool open;
};

struct SimulationRequest
{
    QList<Reservoir> reservoirs;
    QList<Junction> junctions;
    QList<Pipe> pipes;
};

#endif // SIMULATION_REQUEST_H
