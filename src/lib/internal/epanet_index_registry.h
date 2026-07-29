#ifndef AOWIS_EPANET_INDEX_REGISTRY_H
#define AOWIS_EPANET_INDEX_REGISTRY_H

#include <QHash>
#include <QString>

struct EpanetIndexRegistry
{
    QHash<QString, int> curves_tank_volume;
    QHash<QString, int> nodes_reservoirs;
    QHash<QString, int> nodes_junctions;
    QHash<QString, int> nodes_tanks;
    QHash<QString, int> links_pipes;
};

#endif
