#ifndef AOWIS_EPANET_INDEX_REGISTRY_H
#define AOWIS_EPANET_INDEX_REGISTRY_H

#include <QHash>
#include <QUuid>

struct EpanetIndexRegistry
{
    QHash<QUuid, int> patterns_time;
    QHash<QUuid, int> curves_tank_volume;
    QHash<QUuid, int> curves_pump_head;
    QHash<QUuid, int> curves_pump_efficiency;
    QHash<QUuid, int> curves_valve_headloss;
    QHash<QUuid, int> curves_valve_characteristic;
    QHash<QUuid, int> curves_generic;
    QHash<QUuid, int> nodes_reservoirs;
    QHash<QUuid, int> nodes_junctions;
    QHash<QUuid, int> nodes_tanks;
    QHash<QUuid, int> links_pipes;
    QHash<QUuid, int> links_pumps;
    QHash<QUuid, int> links_valves;
    QHash<QUuid, int> controls_simple;
    QHash<QUuid, int> controls_rules;
};

#endif
