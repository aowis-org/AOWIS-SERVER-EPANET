#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/inp_import_scenarios.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

constexpr NumericTolerance numeric_tolerance{1.0e-10, 1.0e-10};

ComparisonContext comparison(std::string field)
{
    ComparisonContext value;
    value.field = std::move(field);
    return value;
}

void scenarioImportNet1ProjectGlobals(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));

    context.expect(result.status.success, "Net1 INP import must open successfully");
    context.expect(!result.complete, "project/global-only import must report that Net1 still contains deferred network data");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(network.id.toStdString(), std::string("Net1"), comparison("network.id"));
    context.expect(!network.uuid.isNull(), "imported network must receive a stable in-memory UUID");
    context.expectEqual(network.title_line_1.toStdString(), std::string(" EPANET Example Network 1"), comparison("title_line_1"));
    context.expectEqual(network.title_line_2.toStdString(), std::string("A simple example of modeling chlorine decay. Both bulk and"), comparison("title_line_2"));
    context.expectEqual(network.title_line_3.toStdString(), std::string("wall reactions are included. "), comparison("title_line_3"));

    context.expectEqual(static_cast<std::int64_t>(network.duration_s), std::int64_t{86400}, comparison("duration_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_hydraulic_s), std::int64_t{3600}, comparison("timestep_hydraulic_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_quality_s), std::int64_t{300}, comparison("timestep_quality_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_pattern_s), std::int64_t{7200}, comparison("timestep_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_report_s), std::int64_t{3600}, comparison("timestep_report_s"));
    context.expectEqual(
        static_cast<std::int64_t>(network.report_statistic),
        static_cast<std::int64_t>(HydraulicSimulationReportStatistic::Series),
        comparison("report_statistic"));

    context.expectEqual(
        static_cast<std::int64_t>(network.options_hydraulic.headloss_formula),
        static_cast<std::int64_t>(HydraulicHeadlossFormula::HazenWilliams),
        comparison("headloss_formula"));
    context.expectEqual(static_cast<std::int64_t>(network.options_hydraulic.maximum_trials), std::int64_t{40}, comparison("maximum_trials"));
    context.expectNear(network.options_hydraulic.accuracy, 0.001, numeric_tolerance, comparison("accuracy"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_hydraulic.unbalanced_action),
        static_cast<std::int64_t>(HydraulicUnbalancedAction::Continue),
        comparison("unbalanced_action"));
    context.expectEqual(static_cast<std::int64_t>(network.options_hydraulic.unbalanced_extra_trials), std::int64_t{10}, comparison("unbalanced_extra_trials"));
    context.expectNear(network.options_energy.global_pump_efficiency_percent, 75.0, numeric_tolerance, comparison("global_pump_efficiency_percent"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_report.status),
        static_cast<std::int64_t>(HydraulicSimulationReportStatus::Normal),
        comparison("report_status"));

    context.expect(network.nodes_junctions.isEmpty(), "topology is outside the current INP-import coverage");
    context.expect(result.request.quality_runs.isEmpty(), "quality-run import is outside the current INP-import coverage");
    context.expect(!result.diagnostics.isEmpty(), "partial import must explain deferred source data through structured diagnostics");
}

void scenarioImportCanonicalGlobalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GLOBAL_OPTIONS_US_INP));

    context.expect(result.status.success, "custom global-options INP import must open successfully");
    context.expect(!result.complete, "project/global-only import must report deferred topology from the fixture");
    context.expect(result.request.quality_runs.isEmpty(), "quality-run import is outside the current INP-import coverage");
    const NetworkHydraulic &network = result.request.network;

    context.expectEqual(network.title_line_1.toStdString(), std::string("Import Global Options Fixture"), comparison("title_line_1"));
    context.expectEqual(static_cast<std::int64_t>(network.duration_s), std::int64_t{45000}, comparison("duration_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_hydraulic_s), std::int64_t{1800}, comparison("timestep_hydraulic_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_quality_s), std::int64_t{600}, comparison("timestep_quality_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_pattern_s), std::int64_t{5400}, comparison("timestep_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_pattern_s), std::int64_t{900}, comparison("start_pattern_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_report_s), std::int64_t{2700}, comparison("timestep_report_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_report_s), std::int64_t{1800}, comparison("start_report_s"));
    context.expectEqual(static_cast<std::int64_t>(network.timestep_rule_s), std::int64_t{300}, comparison("timestep_rule_s"));
    context.expectEqual(static_cast<std::int64_t>(network.start_time_of_day_s), std::int64_t{54900}, comparison("start_time_of_day_s"));
    context.expectEqual(
        static_cast<std::int64_t>(network.report_statistic),
        static_cast<std::int64_t>(HydraulicSimulationReportStatistic::Average),
        comparison("report_statistic"));

    const HydraulicSolverOptions &hydraulic = network.options_hydraulic;
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.headloss_formula),
        static_cast<std::int64_t>(HydraulicHeadlossFormula::DarcyWeisbach),
        comparison("headloss_formula"));
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.demand_model),
        static_cast<std::int64_t>(HydraulicDemandModel::PressureDriven),
        comparison("demand_model"));
    context.expectNear(hydraulic.specific_gravity, 1.2, numeric_tolerance, comparison("specific_gravity"));

    const double expected_minimum_pressure_head_m = 20.0 / (0.4333 * 1.2) * 0.3048;
    const double expected_required_pressure_head_m = 50.0 / (0.4333 * 1.2) * 0.3048;
    context.expectNear(hydraulic.minimum_pressure_head_m, expected_minimum_pressure_head_m, numeric_tolerance, comparison("minimum_pressure_head_m"));
    context.expectNear(hydraulic.required_pressure_head_m, expected_required_pressure_head_m, numeric_tolerance, comparison("required_pressure_head_m"));
    context.expectNear(hydraulic.pressure_exponent, 0.7, numeric_tolerance, comparison("pressure_exponent"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.maximum_trials), std::int64_t{123}, comparison("maximum_trials"));
    context.expectNear(hydraulic.accuracy, 0.0005, numeric_tolerance, comparison("accuracy"));
    context.expectEqual(
        static_cast<std::int64_t>(hydraulic.unbalanced_action),
        static_cast<std::int64_t>(HydraulicUnbalancedAction::Continue),
        comparison("unbalanced_action"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.unbalanced_extra_trials), std::int64_t{7}, comparison("unbalanced_extra_trials"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.check_frequency), std::int64_t{4}, comparison("check_frequency"));
    context.expectEqual(static_cast<std::int64_t>(hydraulic.maximum_check), std::int64_t{8}, comparison("maximum_check"));
    context.expectNear(hydraulic.damping_limit, 0.01, numeric_tolerance, comparison("damping_limit"));
    context.expectNear(hydraulic.maximum_head_error_m, 2.0 * 0.3048, numeric_tolerance, comparison("maximum_head_error_m"));
    context.expectNear(hydraulic.maximum_flow_change_m3_per_h, 10.0 * 0.22712470704, numeric_tolerance, comparison("maximum_flow_change_m3_per_h"));
    context.expectNear(hydraulic.demand_multiplier, 1.25, numeric_tolerance, comparison("demand_multiplier"));
    context.expect(hydraulic.emitters_can_backflow, "emitter backflow flag must import from EPANET options");
    context.expectNear(hydraulic.relative_viscosity, 1.3, numeric_tolerance, comparison("relative_viscosity"));

    context.expectNear(network.options_energy.global_pump_efficiency_percent, 82.0, numeric_tolerance, comparison("global_pump_efficiency_percent"));
    context.expectNear(network.options_energy.global_energy_price_per_kw_h, 0.15, numeric_tolerance, comparison("global_energy_price_per_kw_h"));
    context.expectNear(network.options_energy.demand_charge_per_kw, 5.0, numeric_tolerance, comparison("demand_charge_per_kw"));
    context.expectEqual(
        static_cast<std::int64_t>(network.options_report.status),
        static_cast<std::int64_t>(HydraulicSimulationReportStatus::Full),
        comparison("report_status"));
}

void scenarioImportOpenErrorDiagnostic(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral("/this/path/does/not/exist/aowis-import-missing.inp"));

    context.expect(!result.status.success, "missing INP must fail import");
    context.expect(!result.complete, "failed import cannot be complete");
    context.expect(result.status.stage == HydraulicSimulationStatusStage::OpenInput, "missing INP must identify the input-open stage");
    context.expect(result.status.operation == HydraulicSimulationStatusOperation::OpenInput, "missing INP must identify the input-open operation");
    context.expectEqual(result.status.backend_operation.toStdString(), std::string("EN_open"), comparison("backend_operation"));
    context.expect(result.status.backend_error_code != 0, "missing INP must retain the native EPANET error code");
    context.expect(!result.diagnostics.isEmpty(), "missing INP must retain a structured import diagnostic");
}
}

namespace AowisEpanetTests
{
void registerInpImportScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-import-project-globals-net1",
        "Opens the upstream Net1 INP and reconstructs project titles, timing, hydraulic globals, energy globals, and report status.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportNet1ProjectGlobals});
    registry.add(ScenarioDefinition{
        "conformance-import-project-globals-canonical-units",
        "Imports non-canonical EPANET source units and verifies canonical AOWIS pressure-head, head-error, and flow-change values.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCanonicalGlobalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-open-error-diagnostic",
        "Rejects an unavailable INP path with native EPANET error details and a structured import diagnostic.",
        {"conformance", "import", "negative"},
        &scenarioImportOpenErrorDiagnostic});
}
}
