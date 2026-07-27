#ifndef AOWIS_EPANET_INDEX_REGISTRY_H
#define AOWIS_EPANET_INDEX_REGISTRY_H

#include <QHash>
#include <QString>

struct EpanetIndexRegistry
{
    QHash<QString, int> curves;
    QHash<QString, int> reservoirs;
    QHash<QString, int> junctions;
    QHash<QString, int> tanks;
    QHash<QString, int> pipes;
};

#endif
