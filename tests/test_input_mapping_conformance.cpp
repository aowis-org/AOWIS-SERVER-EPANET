#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/input_mapping_scenarios.h"

#include <QUuid>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#ifndef AOWIS_EPANET_TEST_NET1_INP
#error "AOWIS_EPANET_TEST_NET1_INP must identify the vendored Net1 fixture"
#endif

namespace
{
using AowisEpanetTests::ComparisonContext;
using AowisEpanetTests::NativeHydraulicResult;
using AowisEpanetTests::NativeHydraulicTimeline;
using AowisEpanetTests::NativeJunctionResult;
using AowisEpanetTests::NativePipeResult;
using AowisEpanetTests::NativeReferenceConfiguration;
using AowisEpanetTests::NativeReferenceVariant;
using AowisEpanetTests::NativeReservoirResult;
using AowisEpanetTests::NativeTankResult;
using AowisEpanetTests::Net1Fixture;
using AowisEpanetTests::NumericTolerance;
using AowisEpanetTests::ScenarioDefinition;
using AowisEpanetTests::ScenarioRegistry;
using AowisEpanetTests::TestContext;

ComparisonContext comparison(std::string field, std::int64_t time_s = -1, std::string entity_type = {}, std::string entity_id = {})
{
    ComparisonContext value;
    value.time_s = time_s;
    value.entity_type = std::move(entity_type);
    value.entity_id = std::move(entity_id);
    value.field = std::move(field);
    return value;
}

NativeReferenceConfiguration nativeConfiguration(NativeReferenceVariant variant, const QHash<int, QString> &control_ids)
{
    NativeReferenceConfiguration configuration;
    configuration.input_file = QString::fromUtf8(AOWIS_EPANET_TEST_NET1_INP);
    configuration.control_ids_by_index = control_ids;
    configuration.variant = variant;
    return configuration;
}

HydraulicNodeJunction *findModelJunction(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

HydraulicNodeReservoir *findModelReservoir(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

HydraulicNodeTank *findModelTank(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

HydraulicLinkPipe *findModelPipe(NetworkHydraulic &network, const QString &id)
{
    for (HydraulicLinkPipe &pipe : network.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

const NativeHydraulicResult *findResult(const NativeHydraulicTimeline &timeline, std::int64_t time_s)
{
    for (const NativeHydraulicResult &result : timeline.results)
    {
        if (result.time_elapsed_s == time_s)
            return &result;
    }
    return nullptr;
}

const NativeJunctionResult *findJunction(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeJunctionResult &junction : result.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

const NativeReservoirResult *findReservoir(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeReservoirResult &reservoir : result.nodes_reservoirs)
    {
        if (reservoir.id == id)
            return &reservoir;
    }
    return nullptr;
}

const NativeTankResult *findTank(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativeTankResult &tank : result.nodes_tanks)
    {
        if (tank.id == id)
            return &tank;
    }
    return nullptr;
}

const NativePipeResult *findPipe(const NativeHydraulicResult &result, const QString &id)
{
    for (const NativePipeResult &pipe : result.links_pipes)
    {
        if (pipe.id == id)
            return &pipe;
    }
    return nullptr;
}

NativeHydraulicTimeline runNative(const Net1Fixture &fixture, NativeReferenceVariant variant, TestContext &context)
{
    const NativeHydraulicTimeline timeline = AowisEpanetTests::runNativeEpanetReference(
        nativeConfiguration(variant, fixture.native_control_ids_by_index));
    context.expect(timeline.success, timeline.error.toStdString());
    context.expect(!timeline.results.isEmpty(), "native input-mapping timeline must contain at least one hydraulic result");
    return timeline;
}

void compareWithWrapper(const Net1Fixture &fixture, const NativeHydraulicTimeline &native_timeline, TestContext &context)
{
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;
    const EpanetResultRun wrapper_run = EpanetRunner().run(fixture.network);
    AowisEpanetTests::compareHydraulicTimelines(native_timeline, wrapper_run, fixture.network, context);
}

void testJunctionReservoirInputs(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 7200;

    HydraulicNodeJunction *junction = findModelJunction(fixture.network, QStringLiteral("11"));
    HydraulicNodeReservoir *reservoir = findModelReservoir(fixture.network, QStringLiteral("9"));
    context.expect(junction != nullptr, "junction input-mapping fixture must contain junction 11");
    context.expect(reservoir != nullptr, "reservoir input-mapping fixture must contain reservoir 9");
    if (junction == nullptr || reservoir == nullptr)
        return;

    junction->elevation_input_type = HydraulicNodeElevationInputType::TerrainElevationAndOffset;
    junction->terrain_elevation_m = 210.0;
    junction->elevation_offset_m = 5.0;
    junction->elevation_m = 0.0;
    junction->emitter_coefficient_m3_per_h_per_m_exponent = 1.75;
    context.expect(!junction->demands.isEmpty(), "junction 11 must retain its primary demand");
    if (junction->demands.isEmpty())
        return;
    junction->demands[0].base_demand_m3_per_h = 34.0;
    junction->demands[0].pattern_mode = HydraulicTimePatternMode::TimePattern;
    junction->demands[0].pattern_uuid = fixture.network.patterns_time.first().uuid;

    HydraulicPatternTime reservoir_pattern;
    reservoir_pattern.id = QStringLiteral("RESERVOIR_HEAD_PATTERN");
    reservoir_pattern.uuid = QUuid::createUuid();
    reservoir_pattern.factors = {1.0, 1.05};
    fixture.network.patterns_time.append(reservoir_pattern);

    reservoir->head_input_type = HydraulicNodeElevationInputType::TerrainElevationAndOffset;
    reservoir->terrain_elevation_m = 240.0;
    reservoir->head_offset_m = 10.0;
    reservoir->head_m = 0.0;
    reservoir->head_pattern_mode = HydraulicTimePatternMode::TimePattern;
    reservoir->head_pattern_uuid = reservoir_pattern.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::JunctionReservoirInputs, context);
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    const NativeHydraulicResult *result_0 = findResult(native_timeline, 0);
    const NativeHydraulicResult *result_7200 = findResult(native_timeline, 7200);
    context.expect(result_0 != nullptr, "junction/reservoir input scenario must contain time 0");
    context.expect(result_7200 != nullptr, "junction/reservoir input scenario must contain time 7200");
    if (result_0 != nullptr)
    {
        const NativeReservoirResult *native_reservoir = findReservoir(*result_0, QStringLiteral("9"));
        const NativeJunctionResult *native_junction = findJunction(*result_0, QStringLiteral("11"));
        context.expect(native_reservoir != nullptr, "native result must contain reservoir 9");
        context.expect(native_junction != nullptr, "native result must contain junction 11");
        if (native_reservoir != nullptr)
        {
            context.expectNear(native_reservoir->head_m, 250.0, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.head_m", 0, "Reservoir", "9"));
        }
        if (native_junction != nullptr)
        {
            context.expect(native_junction->emitter_flow_m3_per_h > 0.0,
                "non-zero junction emitter coefficient must produce emitter flow");
        }
    }
    if (result_7200 != nullptr)
    {
        const NativeReservoirResult *native_reservoir = findReservoir(*result_7200, QStringLiteral("9"));
        context.expect(native_reservoir != nullptr, "native result at 7200 s must contain reservoir 9");
        if (native_reservoir != nullptr)
        {
            context.expectNear(native_reservoir->head_m, 262.5, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.patterned_head_m", 7200, "Reservoir", "9"));
        }
    }

    compareWithWrapper(fixture, native_timeline, context);
}

void testDemandCategories(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 7200;

    HydraulicPatternTime primary_pattern;
    primary_pattern.id = QStringLiteral("PRIMARY_DEMAND");
    primary_pattern.uuid = QUuid::createUuid();
    primary_pattern.factors = {1.0, 2.0};
    fixture.network.patterns_time.append(primary_pattern);

    HydraulicPatternTime secondary_pattern;
    secondary_pattern.id = QStringLiteral("SECONDARY_DEMAND_PATTERN");
    secondary_pattern.uuid = QUuid::createUuid();
    secondary_pattern.factors = {0.5, 1.5};
    fixture.network.patterns_time.append(secondary_pattern);

    HydraulicNodeJunction *junction = findModelJunction(fixture.network, QStringLiteral("12"));
    context.expect(junction != nullptr, "demand-category fixture must contain junction 12");
    if (junction == nullptr)
        return;
    junction->demands.clear();

    HydraulicNodeJunctionDemand primary_demand;
    primary_demand.category_name = QStringLiteral("PrimaryDemand");
    primary_demand.base_demand_m3_per_h = 20.0;
    primary_demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
    primary_demand.pattern_uuid = primary_pattern.uuid;
    junction->demands.append(primary_demand);

    HydraulicNodeJunctionDemand constant_demand;
    constant_demand.category_name = QStringLiteral("SecondaryDemand");
    constant_demand.base_demand_m3_per_h = 7.0;
    constant_demand.pattern_mode = HydraulicTimePatternMode::Constant;
    junction->demands.append(constant_demand);

    HydraulicNodeJunctionDemand secondary_demand;
    secondary_demand.category_name = QStringLiteral("TertiaryDemand");
    secondary_demand.base_demand_m3_per_h = 5.0;
    secondary_demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
    secondary_demand.pattern_uuid = secondary_pattern.uuid;
    junction->demands.append(secondary_demand);

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::DemandCategories, context);
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;

    const NativeHydraulicResult *result_0 = findResult(native_timeline, 0);
    const NativeHydraulicResult *result_7200 = findResult(native_timeline, 7200);
    context.expect(result_0 != nullptr, "demand-category scenario must contain time 0");
    context.expect(result_7200 != nullptr, "demand-category scenario must contain time 7200");
    if (result_0 != nullptr)
    {
        const NativeJunctionResult *native_junction = findJunction(*result_0, QStringLiteral("12"));
        context.expect(native_junction != nullptr, "native result at time 0 must contain junction 12");
        if (native_junction != nullptr)
        {
            context.expectNear(native_junction->demand_requested_m3_per_h, 29.5, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.multicategory_demand_m3_per_h", 0, "Junction", "12"),
                "20*1.0 + 7*1.0 + 5*0.5 must equal 29.5 m3/h");
        }
    }
    if (result_7200 != nullptr)
    {
        const NativeJunctionResult *native_junction = findJunction(*result_7200, QStringLiteral("12"));
        context.expect(native_junction != nullptr, "native result at 7200 s must contain junction 12");
        if (native_junction != nullptr)
        {
            context.expectNear(native_junction->demand_requested_m3_per_h, 54.5, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.multicategory_demand_m3_per_h", 7200, "Junction", "12"),
                "constant demand category must remain at 7 m3/h while patterned categories advance");
        }
    }

    compareWithWrapper(fixture, native_timeline, context);
}

void configureTankBase(Net1Fixture &fixture, HydraulicNodeTankGeometryInputType geometry_type, TestContext &context)
{
    fixture.network.duration_s = 0;
    HydraulicNodeTank *tank = findModelTank(fixture.network, QStringLiteral("2"));
    context.expect(tank != nullptr, "tank input-mapping fixture must contain tank 2");
    if (tank == nullptr)
        return;

    tank->elevation_input_type = HydraulicNodeTankElevationInputType::TerrainElevationAndOffset;
    tank->terrain_elevation_m = 250.0;
    tank->bottom_offset_m = 5.0;
    tank->bottom_elevation_m = 0.0;
    tank->water_level_initial_m = 40.0;
    tank->water_level_minimum_m = 30.0;
    tank->water_level_maximum_m = 50.0;
    tank->geometry_input_type = geometry_type;
    tank->minimum_volume_m3 = 40.0;
}

void assertTankGolden(const NativeHydraulicTimeline &native_timeline, double expected_volume_m3, TestContext &context)
{
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;
    const NativeTankResult *tank = findTank(native_timeline.results.first(), QStringLiteral("2"));
    context.expect(tank != nullptr, "native tank input-mapping result must contain tank 2");
    if (tank == nullptr)
        return;
    context.expectNear(tank->head_m, 295.0, NumericTolerance{1.0e-9, 0.0},
        comparison("upstream_golden.initial_head_m", native_timeline.results.first().time_elapsed_s, "Tank", "2"));
    context.expectNear(tank->volume_m3, expected_volume_m3, NumericTolerance{1.0e-6, 0.0},
        comparison("upstream_golden.initial_volume_m3", native_timeline.results.first().time_elapsed_s, "Tank", "2"));
}

void testTankUniformArea(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    configureTankBase(fixture, HydraulicNodeTankGeometryInputType::UniformArea, context);
    HydraulicNodeTank *tank = findModelTank(fixture.network, QStringLiteral("2"));
    if (tank == nullptr)
        return;
    tank->cross_section_area_m2 = 200.0;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::TankUniformArea, context);
    assertTankGolden(native_timeline, 2040.0, context);
    compareWithWrapper(fixture, native_timeline, context);
}

void testTankVolumeAtMaximum(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    configureTankBase(fixture, HydraulicNodeTankGeometryInputType::VolumeAtMaximumLevel, context);
    HydraulicNodeTank *tank = findModelTank(fixture.network, QStringLiteral("2"));
    if (tank == nullptr)
        return;
    tank->volume_at_maximum_level_m3 = 4040.0;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::TankVolumeAtMaximum, context);
    assertTankGolden(native_timeline, 2040.0, context);
    compareWithWrapper(fixture, native_timeline, context);
}

void testTankVolumeCurve(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    configureTankBase(fixture, HydraulicNodeTankGeometryInputType::VolumeCurve, context);
    HydraulicNodeTank *tank = findModelTank(fixture.network, QStringLiteral("2"));
    if (tank == nullptr)
        return;

    HydraulicCurveTankVolume curve;
    curve.id = QStringLiteral("TANK_VOLUME_CURVE");
    curve.uuid = QUuid::createUuid();

    const double levels[] = {30.0, 35.0, 40.0, 45.0, 50.0};
    const double volumes[] = {40.0, 600.0, 1500.0, 2700.0, 4200.0};
    for (int index = 0; index < 5; index++)
    {
        HydraulicCurveTankVolumePoint point;
        point.water_level_m = levels[index];
        point.volume_m3 = volumes[index];
        curve.points.append(point);
    }
    fixture.network.curves_tank_volume.append(curve);
    tank->volume_curve_uuid = curve.uuid;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::TankVolumeCurve, context);
    assertTankGolden(native_timeline, 1500.0, context);
    compareWithWrapper(fixture, native_timeline, context);
}

void testPipeInputs(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    HydraulicLinkPipe *pipe_111 = findModelPipe(fixture.network, QStringLiteral("111"));
    HydraulicLinkPipe *pipe_113 = findModelPipe(fixture.network, QStringLiteral("113"));
    HydraulicLinkPipe *pipe_122 = findModelPipe(fixture.network, QStringLiteral("122"));
    context.expect(pipe_111 != nullptr, "pipe input-mapping fixture must contain pipe 111");
    context.expect(pipe_113 != nullptr, "pipe input-mapping fixture must contain pipe 113");
    context.expect(pipe_122 != nullptr, "pipe input-mapping fixture must contain pipe 122");
    if (pipe_111 == nullptr || pipe_113 == nullptr || pipe_122 == nullptr)
        return;

    pipe_111->length_measured_m = 1234.0;
    pipe_111->diameter_mm = 275.0;
    pipe_111->roughness_hw = 127.0;
    pipe_111->minor_loss = 0.65;

    const QUuid original_from = pipe_113->node_uuid_from;
    pipe_113->node_uuid_from = pipe_113->node_uuid_to;
    pipe_113->node_uuid_to = original_from;
    pipe_113->initial_status = HydraulicLinkPipeInitialStatus::CheckValve;

    pipe_122->initial_status = HydraulicLinkPipeInitialStatus::Closed;

    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::PipeInputs, context);
    if (native_timeline.success && !native_timeline.results.isEmpty())
    {
        const NativeHydraulicResult &result = native_timeline.results.first();
        const NativePipeResult *native_pipe_111 = findPipe(result, QStringLiteral("111"));
        const NativePipeResult *native_pipe_113 = findPipe(result, QStringLiteral("113"));
        const NativePipeResult *native_pipe_122 = findPipe(result, QStringLiteral("122"));
        context.expect(native_pipe_111 != nullptr, "native result must contain pipe 111");
        context.expect(native_pipe_113 != nullptr, "native result must contain pipe 113");
        context.expect(native_pipe_122 != nullptr, "native result must contain pipe 122");
        if (native_pipe_111 != nullptr)
        {
            context.expectNear(native_pipe_111->roughness, 127.0, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.roughness_hw", result.time_elapsed_s, "Pipe", "111"));
        }
        if (native_pipe_113 != nullptr)
        {
            context.expectNear(native_pipe_113->flow_m3_per_h, 0.0, NumericTolerance{1.0e-6, 0.0},
                comparison("upstream_golden.reverse_check_valve_flow_m3_per_h", result.time_elapsed_s, "Pipe", "113"));
            context.expect(!native_pipe_113->open, "reversed check-valve pipe 113 must close");
        }
        if (native_pipe_122 != nullptr)
        {
            context.expectNear(native_pipe_122->flow_m3_per_h, 0.0, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.closed_pipe_flow_m3_per_h", result.time_elapsed_s, "Pipe", "122"));
            context.expect(!native_pipe_122->open, "initially closed pipe 122 must remain closed at time 0");
        }
    }

    compareWithWrapper(fixture, native_timeline, context);
}

void configureFormulaFixture(Net1Fixture &fixture, HydraulicHeadlossFormula formula, double default_roughness, double pipe_10_roughness)
{
    fixture.network.duration_s = 0;
    fixture.network.options_hydraulic.headloss_formula = formula;
    for (HydraulicLinkPipe &pipe : fixture.network.links_pipes)
    {
        if (formula == HydraulicHeadlossFormula::DarcyWeisbach)
            pipe.roughness_dw_mm = pipe.id == QStringLiteral("10") ? pipe_10_roughness : default_roughness;
        else if (formula == HydraulicHeadlossFormula::ChezyManning)
            pipe.roughness_cm = pipe.id == QStringLiteral("10") ? pipe_10_roughness : default_roughness;
    }
}

void assertFormulaGolden(const NativeHydraulicTimeline &native_timeline, double expected_roughness, TestContext &context)
{
    if (!native_timeline.success || native_timeline.results.isEmpty())
        return;
    const NativeHydraulicResult &result = native_timeline.results.first();
    const NativePipeResult *pipe = findPipe(result, QStringLiteral("10"));
    context.expect(pipe != nullptr, "formula-specific native result must contain pipe 10");
    if (pipe == nullptr)
        return;
    context.expectNear(pipe->roughness, expected_roughness, NumericTolerance{1.0e-9, 0.0},
        comparison("upstream_golden.formula_specific_roughness", result.time_elapsed_s, "Pipe", "10"));
    context.expect(pipe->head_loss_m > 0.0, "formula-specific pipe 10 must carry non-zero head loss");
}

void testDarcyWeisbach(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    configureFormulaFixture(fixture, HydraulicHeadlossFormula::DarcyWeisbach, 0.25, 0.35);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::DarcyWeisbach, context);
    assertFormulaGolden(native_timeline, 0.35, context);
    compareWithWrapper(fixture, native_timeline, context);
}

void testChezyManning(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    configureFormulaFixture(fixture, HydraulicHeadlossFormula::ChezyManning, 0.013, 0.017);
    const NativeHydraulicTimeline native_timeline = runNative(fixture, NativeReferenceVariant::ChezyManning, context);
    assertFormulaGolden(native_timeline, 0.017, context);
    compareWithWrapper(fixture, native_timeline, context);
}
}

namespace AowisEpanetTests
{
void registerInputMappingScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "conformance-upstream-junction-reservoir-inputs",
        "Exercises non-default junction elevation/emitter input and patterned reservoir head through native EPANET and the AOWIS model path.",
        {"conformance", "hydraulic", "upstream", "junction", "reservoir"},
        &testJunctionReservoirInputs});
    registry.add(ScenarioDefinition{
        "conformance-upstream-demand-categories",
        "Ports multiple junction demand categories with independent patterned and constant modes and compares the complete hydraulic timeline.",
        {"conformance", "hydraulic", "upstream", "junction", "demand"},
        &testDemandCategories});
    registry.add(ScenarioDefinition{
        "conformance-upstream-tank-uniform-area",
        "Exercises the AOWIS uniform-cross-section tank geometry resolver against an equivalent native EPANET tank.",
        {"conformance", "hydraulic", "upstream", "tank"},
        &testTankUniformArea});
    registry.add(ScenarioDefinition{
        "conformance-upstream-tank-volume-at-max",
        "Exercises volume-at-maximum-level tank geometry and its derived equivalent diameter against native EPANET.",
        {"conformance", "hydraulic", "upstream", "tank"},
        &testTankVolumeAtMaximum});
    registry.add(ScenarioDefinition{
        "conformance-upstream-tank-volume-curve",
        "Exercises a non-uniform tank volume curve with a non-default minimum volume and compares native and wrapper tank results.",
        {"conformance", "hydraulic", "upstream", "tank", "curve"},
        &testTankVolumeCurve});
    registry.add(ScenarioDefinition{
        "conformance-upstream-pipe-inputs",
        "Exercises measured length, diameter, Hazen-Williams roughness, minor loss, reversed check valve, and closed pipe mappings.",
        {"conformance", "hydraulic", "upstream", "pipe"},
        &testPipeInputs});
    registry.add(ScenarioDefinition{
        "conformance-upstream-pipe-darcy-weisbach",
        "Exercises Darcy-Weisbach headloss selection and millimetre roughness mapping through both native and wrapper paths.",
        {"conformance", "hydraulic", "upstream", "pipe", "darcy-weisbach"},
        &testDarcyWeisbach});
    registry.add(ScenarioDefinition{
        "conformance-upstream-pipe-chezy-manning",
        "Exercises Chezy-Manning headloss selection and Manning roughness mapping through both native and wrapper paths.",
        {"conformance", "hydraulic", "upstream", "pipe", "chezy-manning"},
        &testChezyManning});
}
}
