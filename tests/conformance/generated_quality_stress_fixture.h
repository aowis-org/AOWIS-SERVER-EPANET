#ifndef AOWIS_EPANET_GENERATED_QUALITY_STRESS_FIXTURE_H
#define AOWIS_EPANET_GENERATED_QUALITY_STRESS_FIXTURE_H

#include "generated_network_stress_fixture.h"

namespace AowisEpanetTests
{
struct GeneratedQualityStressCase
{
    const char *scenario_name = nullptr;
    GeneratedStressTopology topology = GeneratedStressTopology::Chain;
    int junction_count = 0;
    int grid_rows = 0;
    int grid_columns = 0;
    std::uint64_t seed = 0;
    HydraulicHeadlossFormula headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    WaterQualityAnalysisType analysis = WaterQualityAnalysisType::Chemical;
    int duration_s = 21600;
    int hydraulic_timestep_s = 3600;
    int quality_timestep_s = 300;
    bool patterned_sources = false;
    bool reactions = false;
};

struct GeneratedQualityStressFixture
{
    NetworkHydraulic network;
    WaterQualitySolverOptions quality_options;
    QString native_inp_text;
    int expected_quality_sample_count = 0;
};

GeneratedQualityStressFixture makeGeneratedQualityStressFixture(const GeneratedQualityStressCase &definition);
}

#endif // AOWIS_EPANET_GENERATED_QUALITY_STRESS_FIXTURE_H
