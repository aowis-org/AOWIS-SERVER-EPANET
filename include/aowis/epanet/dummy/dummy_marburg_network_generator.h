#ifndef DUMMY_MARBURG_NETWORK_GENERATOR_H
#define DUMMY_MARBURG_NETWORK_GENERATOR_H

#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic.h>

class DummyMarburgNetworkGenerator
{
public:
    static NetworkHydraulic generate();
    static NetworkHydraulic generate(quint64 seed);
};

#endif // DUMMY_MARBURG_NETWORK_GENERATOR_H
