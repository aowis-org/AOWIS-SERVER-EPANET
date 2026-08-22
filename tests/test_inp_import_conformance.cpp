#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/inp_import_scenarios.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"

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
constexpr double epanet_cmh_per_cfs = 101.94;
constexpr double epanet_gpm_per_cfs = 448.831;
constexpr double epanet_gpm_to_cmh = epanet_cmh_per_cfs / epanet_gpm_per_cfs;

ComparisonContext comparison(std::string field)
{
    ComparisonContext value;
    value.field = std::move(field);
    return value;
}

const HydraulicNodeJunction *junctionById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const HydraulicNodeReservoir *reservoirById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

const HydraulicNodeTank *tankById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const HydraulicLinkPipe *pipeById(const NetworkHydraulic &network, const QString &id)
{
    for (const HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

QString nodeIdForUuid(const NetworkHydraulic &network, const QUuid &uuid)
{
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid == uuid)
            return junction.id;
    }
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.uuid == uuid)
            return reservoir.id;
    }
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.uuid == uuid)
            return tank.id;
    }
    return QString();
}

void scenarioImportNet1ProjectGlobals(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));

    context.expect(result.status.success, "Net1 INP import must open successfully");
    context.expect(!result.complete, "Net1 import must report source families that remain deferred");

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

    context.expectEqual(static_cast<std::int64_t>(network.nodes_junctions.size()), std::int64_t{9}, comparison("junction_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_reservoirs.size()), std::int64_t{1}, comparison("reservoir_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_tanks.size()), std::int64_t{1}, comparison("tank_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pipes.size()), std::int64_t{12}, comparison("pipe_count"));
    context.expect(result.request.quality_runs.isEmpty(), "quality-run import is outside the current INP-import coverage");
    context.expect(!result.diagnostics.isEmpty(), "partial import must explain deferred source data through structured diagnostics");
}

void scenarioImportCanonicalGlobalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_GLOBAL_OPTIONS_US_INP));

    context.expect(result.status.success, "custom global-options INP import must open successfully");
    context.expect(!result.complete, "global-options fixture still omits report directives and coordinate metadata");
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
    context.expectNear(hydraulic.maximum_flow_change_m3_per_h, 10.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("maximum_flow_change_m3_per_h"));
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

void scenarioImportCoreTopologyNet1(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_NET1_INP));
    context.expect(result.status.success, "Net1 core topology import must succeed");
    context.expect(!result.complete, "Net1 still contains deferred pumps, patterns, curves, controls, quality, and geometry metadata");

    const NetworkHydraulic &network = result.request.network;
    const HydraulicNodeJunction *junction_11 = junctionById(network, QStringLiteral("11"));
    context.expect(junction_11 != nullptr, "Net1 junction 11 must be imported");
    if (junction_11 != nullptr)
    {
        context.expectNear(junction_11->elevation_m, 710.0 * 0.3048, numeric_tolerance, comparison("junction_11.elevation_m"));
        context.expectEqual(static_cast<std::int64_t>(junction_11->demands.size()), std::int64_t{1}, comparison("junction_11.demands.size"));
        if (!junction_11->demands.isEmpty())
        {
            context.expectNear(
                junction_11->demands.first().base_demand_m3_per_h,
                150.0 * epanet_gpm_to_cmh,
                numeric_tolerance,
                comparison("junction_11.base_demand_m3_per_h"));
        }
    }

    const HydraulicNodeReservoir *reservoir_9 = reservoirById(network, QStringLiteral("9"));
    context.expect(reservoir_9 != nullptr, "Net1 reservoir 9 must be imported");
    if (reservoir_9 != nullptr)
        context.expectNear(reservoir_9->hydraulic_head_m, 800.0 * 0.3048, numeric_tolerance, comparison("reservoir_9.hydraulic_head_m"));

    const HydraulicNodeTank *tank_2 = tankById(network, QStringLiteral("2"));
    context.expect(tank_2 != nullptr, "Net1 tank 2 must be imported");
    if (tank_2 != nullptr)
    {
        context.expectNear(tank_2->bottom_elevation_m, 850.0 * 0.3048, numeric_tolerance, comparison("tank_2.bottom_elevation_m"));
        context.expectNear(tank_2->water_level_initial_m, 120.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_initial_m"));
        context.expectNear(tank_2->water_level_minimum_m, 100.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_minimum_m"));
        context.expectNear(tank_2->water_level_maximum_m, 150.0 * 0.3048, numeric_tolerance, comparison("tank_2.water_level_maximum_m"));
        context.expectNear(tank_2->diameter_m, 50.5 * 0.3048, numeric_tolerance, comparison("tank_2.diameter_m"));
        const double expected_minimum_volume_ft3 =
            std::acos(-1.0) * 50.5 * 50.5 / 4.0 * 100.0;
        context.expectNear(
            tank_2->minimum_volume_m3,
            expected_minimum_volume_ft3 * 0.028316846592,
            numeric_tolerance,
            comparison("tank_2.minimum_volume_m3"));
    }

    const HydraulicLinkPipe *pipe_10 = pipeById(network, QStringLiteral("10"));
    context.expect(pipe_10 != nullptr, "Net1 pipe 10 must be imported");
    if (pipe_10 != nullptr)
    {
        context.expectEqual(nodeIdForUuid(network, pipe_10->node_uuid_from).toStdString(), std::string("10"), comparison("pipe_10.node_from"));
        context.expectEqual(nodeIdForUuid(network, pipe_10->node_uuid_to).toStdString(), std::string("11"), comparison("pipe_10.node_to"));
        context.expect(pipe_10->length_measured_m.has_value(), "imported explicit pipe length must be retained as measured length");
        if (pipe_10->length_measured_m.has_value())
            context.expectNear(pipe_10->length_measured_m.value(), 10530.0 * 0.3048, numeric_tolerance, comparison("pipe_10.length_measured_m"));
        context.expectNear(pipe_10->diameter_mm, 18.0 * 25.4, numeric_tolerance, comparison("pipe_10.diameter_mm"));
        context.expectNear(pipe_10->roughness_hazen_williams, 100.0, numeric_tolerance, comparison("pipe_10.roughness_hazen_williams"));
        context.expectEqual(
            static_cast<std::int64_t>(pipe_10->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::Open),
            comparison("pipe_10.initial_status"));
    }
}

void scenarioImportCoreTopologyCanonicalUnits(TestContext &context)
{
    const EpanetResultImport result = EpanetRunner().importInp(
        QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CORE_TOPOLOGY_US_INP));
    context.expect(result.status.success, "US customary core-topology fixture must import successfully");

    const NetworkHydraulic &network = result.request.network;
    context.expectEqual(static_cast<std::int64_t>(network.nodes_junctions.size()), std::int64_t{2}, comparison("junction_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_reservoirs.size()), std::int64_t{1}, comparison("reservoir_count"));
    context.expectEqual(static_cast<std::int64_t>(network.nodes_tanks.size()), std::int64_t{1}, comparison("tank_count"));
    context.expectEqual(static_cast<std::int64_t>(network.links_pipes.size()), std::int64_t{3}, comparison("pipe_count"));

    const HydraulicNodeJunction *junction_1 = junctionById(network, QStringLiteral("J1"));
    context.expect(junction_1 != nullptr, "J1 must be imported");
    if (junction_1 != nullptr)
    {
        context.expectNear(junction_1->elevation_m, 100.0 * 0.3048, numeric_tolerance, comparison("J1.elevation_m"));
        context.expectEqual(static_cast<std::int64_t>(junction_1->demands.size()), std::int64_t{2}, comparison("J1.demands.size"));
        if (junction_1->demands.size() == 2)
        {
            context.expectNear(junction_1->demands.at(0).base_demand_m3_per_h, 10.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("J1.demands[0]"));
            context.expectNear(junction_1->demands.at(1).base_demand_m3_per_h, 5.0 * epanet_gpm_to_cmh, numeric_tolerance, comparison("J1.demands[1]"));
        }
    }

    const HydraulicNodeJunction *junction_2 = junctionById(network, QStringLiteral("J2"));
    context.expect(junction_2 != nullptr, "J2 must be imported");
    if (junction_2 != nullptr)
    {
        const double pressure_head_m_per_psi = 0.3048 / 0.4333;
        const double expected_emitter = 2.5 * epanet_gpm_to_cmh / std::pow(pressure_head_m_per_psi, 0.6);
        context.expectNear(junction_2->emitter.pressure_exponent, 0.6, numeric_tolerance, comparison("J2.emitter.pressure_exponent"));
        context.expectNear(junction_2->emitter.coefficient, expected_emitter, numeric_tolerance, comparison("J2.emitter.coefficient"));
    }

    const HydraulicNodeReservoir *reservoir = reservoirById(network, QStringLiteral("R1"));
    context.expect(reservoir != nullptr, "R1 must be imported");
    if (reservoir != nullptr)
        context.expectNear(reservoir->hydraulic_head_m, 250.0 * 0.3048, numeric_tolerance, comparison("R1.hydraulic_head_m"));

    const HydraulicNodeTank *tank = tankById(network, QStringLiteral("T1"));
    context.expect(tank != nullptr, "T1 must be imported");
    if (tank != nullptr)
    {
        context.expectNear(tank->bottom_elevation_m, 150.0 * 0.3048, numeric_tolerance, comparison("T1.bottom_elevation_m"));
        context.expectNear(tank->water_level_initial_m, 10.0 * 0.3048, numeric_tolerance, comparison("T1.water_level_initial_m"));
        context.expectNear(tank->water_level_maximum_m, 20.0 * 0.3048, numeric_tolerance, comparison("T1.water_level_maximum_m"));
        context.expectNear(tank->diameter_m, 30.0 * 0.3048, numeric_tolerance, comparison("T1.diameter_m"));
        context.expectNear(tank->minimum_volume_m3, 100.0 * 0.028316846592, numeric_tolerance, comparison("T1.minimum_volume_m3"));
    }

    const HydraulicLinkPipe *pipe_1 = pipeById(network, QStringLiteral("P1"));
    context.expect(pipe_1 != nullptr, "P1 must be imported");
    if (pipe_1 != nullptr)
    {
        context.expectEqual(nodeIdForUuid(network, pipe_1->node_uuid_from).toStdString(), std::string("R1"), comparison("P1.node_from"));
        context.expectEqual(nodeIdForUuid(network, pipe_1->node_uuid_to).toStdString(), std::string("J1"), comparison("P1.node_to"));
        context.expectNear(pipe_1->length_measured_m.value_or(0.0), 1000.0 * 0.3048, numeric_tolerance, comparison("P1.length_measured_m"));
        context.expectNear(pipe_1->diameter_mm, 12.0 * 25.4, numeric_tolerance, comparison("P1.diameter_mm"));
        context.expectNear(pipe_1->roughness_darcy_weisbach_mm, 0.5 * 0.3048, numeric_tolerance, comparison("P1.roughness_darcy_weisbach_mm"));
        context.expectNear(pipe_1->minor_loss_coefficient, 0.2, numeric_tolerance, comparison("P1.minor_loss_coefficient"));
        context.expectNear(pipe_1->leak_area_mm2_per_100m, 2.0 / 0.3048, numeric_tolerance, comparison("P1.leak_area_mm2_per_100m"));
        context.expectNear(pipe_1->leak_area_expansion_per_pressure_head_mm2_per_m, 0.1 / 0.3048, numeric_tolerance, comparison("P1.leak_expansion"));
    }

    const HydraulicLinkPipe *pipe_2 = pipeById(network, QStringLiteral("P2"));
    context.expect(pipe_2 != nullptr, "P2 must be imported");
    if (pipe_2 != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(pipe_2->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::Closed),
            comparison("P2.initial_status"));
    }

    const HydraulicLinkPipe *pipe_3 = pipeById(network, QStringLiteral("P3"));
    context.expect(pipe_3 != nullptr, "P3 must be imported");
    if (pipe_3 != nullptr)
    {
        context.expectEqual(
            static_cast<std::int64_t>(pipe_3->initial_status),
            static_cast<std::int64_t>(HydraulicLinkPipeInitialStatus::CheckValve),
            comparison("P3.initial_status"));
    }

    AowisEpanetTests::NativeReferenceConfiguration native_configuration;
    native_configuration.input_file = QStringLiteral(AOWIS_EPANET_TEST_IMPORT_CORE_TOPOLOGY_US_INP);
    const AowisEpanetTests::NativeHydraulicTimeline native_timeline =
        AowisEpanetTests::runNativeEpanetReference(native_configuration);
    context.expect(native_timeline.success, "native topology fixture must solve successfully");

    const EpanetResultRun wrapper_run = EpanetRunner().run(result.request);
    context.expect(wrapper_run.status.success, "imported topology fixture must solve successfully through the wrapper");
    if (native_timeline.success && wrapper_run.status.success)
        AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, network, context);
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
        "conformance-import-core-topology-net1",
        "Reconstructs Net1 junctions, reservoir, tank, and pipes with endpoint references and canonical US-unit conversion.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCoreTopologyNet1});
    registry.add(ScenarioDefinition{
        "conformance-import-core-topology-canonical-units",
        "Imports demands, emitter data, tank geometry, pipe status, Darcy-Weisbach roughness, and leakage from US customary source units into canonical AOWIS fields.",
        {"conformance", "import", "hydraulic"},
        &scenarioImportCoreTopologyCanonicalUnits});
    registry.add(ScenarioDefinition{
        "conformance-import-open-error-diagnostic",
        "Rejects an unavailable INP path with native EPANET error details and a structured import diagnostic.",
        {"conformance", "import", "negative"},
        &scenarioImportOpenErrorDiagnostic});
}
}
