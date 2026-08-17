#include "conformance_test_framework.h"
#include "net1_conformance_scenarios.h"
#include "result_contract_scenarios.h"
#include "upstream_step3_scenarios.h"
#include "upstream_step4_scenarios.h"
#include "upstream_step5_scenarios.h"
#include "upstream_step6_scenarios.h"
#include "upstream_step7_scenarios.h"
#include "upstream_step8a_scenarios.h"
#include "upstream_step8b_scenarios.h"

#include <iostream>

int main(int argc, char *argv[])
{
    AowisEpanetTests::ScenarioRegistry registry;
    AowisEpanetTests::registerResultContractScenarios(registry);
    AowisEpanetTests::registerNet1ConformanceScenarios(registry);
    AowisEpanetTests::registerUpstreamStep3Scenarios(registry);
    AowisEpanetTests::registerUpstreamStep4Scenarios(registry);
    AowisEpanetTests::registerUpstreamStep5Scenarios(registry);
    AowisEpanetTests::registerUpstreamStep6Scenarios(registry);
    AowisEpanetTests::registerUpstreamStep7Scenarios(registry);
    AowisEpanetTests::registerUpstreamStep8aScenarios(registry);
    // Keep Step 8B registration explicit so the executable registry matches CTest.
    AowisEpanetTests::registerUpstreamStep8bScenarios(registry);
    return AowisEpanetTests::runTestProgram(argc, argv, registry, std::cout, std::cerr);
}
