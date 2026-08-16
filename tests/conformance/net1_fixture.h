#ifndef AOWIS_EPANET_NET1_FIXTURE_H
#define AOWIS_EPANET_NET1_FIXTURE_H

#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QHash>
#include <QString>

namespace AowisEpanetTests
{
struct Net1Fixture
{
    NetworkHydraulic network;
    QHash<int, QString> native_control_ids_by_index;
};

Net1Fixture makeNet1Fixture();
}

#endif // AOWIS_EPANET_NET1_FIXTURE_H
