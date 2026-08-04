#ifndef RANDOM_HYDRAULIC_NETWORK_GENERATOR_H
#define RANDOM_HYDRAULIC_NETWORK_GENERATOR_H

#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic.h>

class RandomHydraulicNetworkGenerator
{
public:
    static NetworkHydraulic generate();
    static NetworkHydraulic generate(quint64 seed);

    static NetworkHydraulic generateFractal();
    static NetworkHydraulic generateFractal(quint64 seed);
};

#endif // RANDOM_HYDRAULIC_NETWORK_GENERATOR_H

