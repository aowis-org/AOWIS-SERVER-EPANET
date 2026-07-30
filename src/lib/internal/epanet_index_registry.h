#ifndef AOWIS_EPANET_INDEX_REGISTRY_H
#define AOWIS_EPANET_INDEX_REGISTRY_H

#include <QHash>
#include <QUuid>

struct EpanetIndexRegistry
{
    QHash<QUuid, int> curves_tank_volume;
    QHash<QUuid, int> nodes_reservoirs;
    QHash<QUuid, int> nodes_junctions;
    QHash<QUuid, int> nodes_tanks;
    QHash<QUuid, int> links_pipes;
};

#endif
