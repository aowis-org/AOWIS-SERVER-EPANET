#include "conformance_test_framework.h"
#include "net1_conformance_scenarios.h"
#include "result_contract_scenarios.h"
#include "hydraulic_behavior_scenarios.h"
#include "input_mapping_scenarios.h"
#include "pump_scenarios.h"
#include "valve_scenarios.h"
#include "controls_options_operations_scenarios.h"
#include "export_fidelity_scenarios.h"
#include "quality_execution_scenarios.h"
#include "negative_validation_scenarios.h"
#include "deterministic_stress_scenarios.h"

#include <iostream>

int main(int argc, char *argv[])
{
    AowisEpanetTests::ScenarioRegistry registry;
    AowisEpanetTests::registerResultContractScenarios(registry);
    AowisEpanetTests::registerNet1ConformanceScenarios(registry);
    AowisEpanetTests::registerHydraulicBehaviorScenarios(registry);
    AowisEpanetTests::registerInputMappingScenarios(registry);
    AowisEpanetTests::registerPumpScenarios(registry);
    AowisEpanetTests::registerValveScenarios(registry);
    AowisEpanetTests::registerControlsOptionsOperationsScenarios(registry);
    AowisEpanetTests::registerExportFidelityScenarios(registry);
    AowisEpanetTests::registerQualityExecutionScenarios(registry);
    AowisEpanetTests::registerNegativeValidationScenarios(registry);
    AowisEpanetTests::registerDeterministicStressScenarios(registry);
    return AowisEpanetTests::runTestProgram(argc, argv, registry, std::cout, std::cerr);
}
