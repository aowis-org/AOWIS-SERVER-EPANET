#ifndef RANDOM_HYDRAULIC_NETWORK_GENERATOR_H
#define RANDOM_HYDRAULIC_NETWORK_GENERATOR_H

#include <QtGlobal>

#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

class RandomHydraulicNetworkGenerator
{
public:
    static NetworkHydraulicDistribution generate();
    static NetworkHydraulicDistribution generate(quint64 seed);

    static NetworkHydraulicDistribution generateFractal();
    static NetworkHydraulicDistribution generateFractal(quint64 seed);
};

#endif // RANDOM_HYDRAULIC_NETWORK_GENERATOR_H

