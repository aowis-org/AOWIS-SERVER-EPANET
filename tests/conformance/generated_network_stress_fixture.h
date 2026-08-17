#ifndef AOWIS_EPANET_GENERATED_NETWORK_STRESS_FIXTURE_H
#define AOWIS_EPANET_GENERATED_NETWORK_STRESS_FIXTURE_H

#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QString>

#include <cstdint>

namespace AowisEpanetTests
{
enum class GeneratedStressTopology
{
    Chain,
    Branch,
    Ring,
    Grid,
    DualSourceGrid
};

struct GeneratedStressCase
{
    const char *scenario_name = nullptr;
    GeneratedStressTopology topology = GeneratedStressTopology::Chain;
    int junction_count = 0;
    int grid_rows = 0;
    int grid_columns = 0;
    std::uint64_t seed = 0;
    HydraulicHeadlossFormula headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
};

struct GeneratedStressFixture
{
    NetworkHydraulic network;
    QString native_inp_text;
    int expected_junction_count = 0;
    int expected_reservoir_count = 0;
    int expected_pipe_count = 0;
};

GeneratedStressFixture makeGeneratedStressFixture(const GeneratedStressCase &definition);
}

#endif // AOWIS_EPANET_GENERATED_NETWORK_STRESS_FIXTURE_H
