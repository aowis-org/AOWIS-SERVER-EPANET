#include <aowis/epanet/epanet_runner.h>

#include "conformance/conformance_test_framework.h"
#include "conformance/epanet_test_requests.h"
#include "conformance/hydraulic_result_comparator.h"
#include "conformance/native_epanet_reference_runner.h"
#include "conformance/net1_fixture.h"
#include "conformance/input_mapping_scenarios.h"

#include <QDate>
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
using AowisEpanetTests::HydraulicQuantity;
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

HydraulicNodeJunction *findModelJunctionByUuid(NetworkHydraulic &network, const QUuid &uuid)
{
    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        if (junction.uuid == uuid)
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

std::optional<double> pipeRoughnessFromInp(const QString &inp_text, const QString &pipe_id)
{
    bool in_pipes = false;
    const QStringList lines = inp_text.split(QChar('\n'));
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QChar('[')))
        {
            in_pipes = trimmed.compare(QStringLiteral("[PIPES]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!in_pipes || trimmed.isEmpty() || trimmed.startsWith(QChar(';')))
            continue;

        const QStringList fields = trimmed.simplified().split(QChar(' '));
        if (fields.size() < 6 || fields.at(0) != pipe_id)
            continue;

        bool ok = false;
        const double roughness = fields.at(5).toDouble(&ok);
        if (ok)
            return roughness;
        return std::nullopt;
    }

    return std::nullopt;
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
    const EpanetResultRun wrapper_run = EpanetRunner().run(AowisEpanetTests::makeRunRequest(fixture.network));
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
    junction->emitter.coefficient = 1.75;
    junction->emitter.pressure_exponent = 0.73;
    context.expect(!junction->demands.isEmpty(), "junction 11 must retain its primary demand");
    if (junction->demands.isEmpty())
        return;
    junction->demands[0].base_demand_m3_per_h = 34.0;
    junction->demands[0].pattern_mode = HydraulicTimePatternMode::TimePattern;
    junction->demands[0].pattern_uuid = fixture.network.patterns_time.first().uuid;

    HydraulicPatternTime reservoir_pattern;
    reservoir_pattern.id = QStringLiteral("RESERVOIR_HEAD_PATTERN");
    reservoir_pattern.uuid = QUuid::createUuid();
    reservoir_pattern.multipliers = {1.0, 1.05};
    fixture.network.patterns_time.append(reservoir_pattern);

    reservoir->head_input_type = HydraulicNodeElevationInputType::TerrainElevationAndOffset;
    reservoir->terrain_elevation_m = 240.0;
    reservoir->hydraulic_head_offset_m = 10.0;
    reservoir->hydraulic_head_m = 0.0;
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
            context.expectNear(native_reservoir->hydraulic_head_m, 250.0, NumericTolerance{1.0e-9, 0.0},
                comparison("upstream_golden.hydraulic_head_m", 0, "Reservoir", "9"));
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
            context.expectNear(native_reservoir->hydraulic_head_m, 262.5, NumericTolerance{1.0e-9, 0.0},
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
    primary_pattern.multipliers = {1.0, 2.0};
    fixture.network.patterns_time.append(primary_pattern);

    HydraulicPatternTime secondary_pattern;
    secondary_pattern.id = QStringLiteral("SECONDARY_DEMAND_PATTERN");
    secondary_pattern.uuid = QUuid::createUuid();
    secondary_pattern.multipliers = {0.5, 1.5};
    fixture.network.patterns_time.append(secondary_pattern);

    HydraulicNodeJunction *junction = findModelJunction(fixture.network, QStringLiteral("12"));
    context.expect(junction != nullptr, "demand-category fixture must contain junction 12");
    if (junction == nullptr)
        return;
    junction->demands.clear();

    HydraulicDemand primary_demand;
    primary_demand.category_name = QStringLiteral("PrimaryDemand");
    primary_demand.base_demand_m3_per_h = 20.0;
    primary_demand.pattern_mode = HydraulicTimePatternMode::TimePattern;
    primary_demand.pattern_uuid = primary_pattern.uuid;
    junction->demands.append(primary_demand);

    HydraulicDemand constant_demand;
    constant_demand.category_name = QStringLiteral("SecondaryDemand");
    constant_demand.base_demand_m3_per_h = 7.0;
    constant_demand.pattern_mode = HydraulicTimePatternMode::Constant;
    junction->demands.append(constant_demand);

    HydraulicDemand secondary_demand;
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
    context.expectNear(tank->hydraulic_head_m, 295.0, NumericTolerance{1.0e-9, 0.0},
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
    pipe_111->roughness_hazen_williams = 127.0;
    pipe_111->minor_loss_coefficient = 0.65;

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
                comparison("upstream_golden.roughness_hazen_williams", result.time_elapsed_s, "Pipe", "111"));
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


void testPipeMaterialRoughnessResolution(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    HydraulicLinkPipe *pipe = findModelPipe(fixture.network, QStringLiteral("111"));
    context.expect(pipe != nullptr, "material-roughness fixture must contain pipe 111");
    if (pipe == nullptr)
        return;

    HydraulicPipeMaterial material;
    material.id = QStringLiteral("Imported Ductile Iron");
    material.uuid = QUuid::createUuid();

    HydraulicPipeMaterialRoughnessAtAge age_0;
    age_0.age_years = 0;
    age_0.roughness_hazen_williams = 140.0;
    age_0.roughness_darcy_weisbach_mm = 0.26;
    age_0.roughness_chezy_manning = 0.013;
    material.roughness_by_age.append(age_0);

    HydraulicPipeMaterialRoughnessAtAge age_20;
    age_20.age_years = 20;
    age_20.roughness_hazen_williams = 127.0;
    age_20.roughness_darcy_weisbach_mm = 0.18;
    material.roughness_by_age.append(age_20);

    HydraulicPipeMaterialRoughnessAtAge age_40;
    age_40.age_years = 40;
    age_40.roughness_hazen_williams = 110.0;
    age_40.roughness_chezy_manning = 0.016;
    material.roughness_by_age.append(age_40);

    fixture.network.pipe_materials.append(material);
    pipe->material_uuid = material.uuid;
    pipe->roughness_mode = HydraulicPipeRoughnessMode::MaterialLibrary;
    pipe->metadata.date_installed = QDate(QDate::currentDate().year() - 25, 1, 1);
    pipe->roughness_hazen_williams = 5.0;
    pipe->roughness_darcy_weisbach_mm = 5.0;
    pipe->roughness_chezy_manning = 0.05;

    fixture.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    EpanetResultInp inp = EpanetRunner().retrieveInp(AowisEpanetTests::makeRunRequest(fixture.network));
    context.expect(inp.status.success, "Hazen-Williams material-library roughness must resolve");
    std::optional<double> roughness = pipeRoughnessFromInp(inp.inp_text, pipe->id);
    context.expect(roughness.has_value(), "generated Hazen-Williams INP must contain material-backed pipe roughness");
    if (roughness.has_value())
        context.expectNear(roughness.value(), 127.0, NumericTolerance{1.0e-9, 0.0}, comparison("material_library.hazen_williams", 0, "Pipe", "111"));

    fixture.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;
    inp = EpanetRunner().retrieveInp(AowisEpanetTests::makeRunRequest(fixture.network));
    context.expect(inp.status.success, "Darcy-Weisbach material-library roughness must resolve");
    roughness = pipeRoughnessFromInp(inp.inp_text, pipe->id);
    context.expect(roughness.has_value(), "generated Darcy-Weisbach INP must contain material-backed pipe roughness");
    if (roughness.has_value())
        context.expectNear(roughness.value(), 0.18, NumericTolerance{1.0e-9, 0.0}, comparison("material_library.darcy_weisbach_mm", 0, "Pipe", "111"));

    fixture.network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::ChezyManning;
    inp = EpanetRunner().retrieveInp(AowisEpanetTests::makeRunRequest(fixture.network));
    context.expect(inp.status.success, "Chezy-Manning material-library roughness must resolve");
    roughness = pipeRoughnessFromInp(inp.inp_text, pipe->id);
    context.expect(roughness.has_value(), "generated Chezy-Manning INP must contain material-backed pipe roughness");
    if (roughness.has_value())
        context.expectNear(roughness.value(), 0.013, NumericTolerance{1.0e-9, 0.0}, comparison("material_library.chezy_manning", 0, "Pipe", "111"));

    NetworkHydraulic missing_material = fixture.network;
    HydraulicLinkPipe *missing_material_pipe = findModelPipe(missing_material, QStringLiteral("111"));
    missing_material_pipe->material_uuid = QUuid();
    const EpanetResultInp missing_material_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(missing_material));
    context.expect(!missing_material_inp.status.success,
        "material-library roughness must reject a missing material reference");

    NetworkHydraulic unknown_material = fixture.network;
    HydraulicLinkPipe *unknown_material_pipe = findModelPipe(unknown_material, QStringLiteral("111"));
    unknown_material_pipe->material_uuid = QUuid::createUuid();
    const EpanetResultInp unknown_material_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(unknown_material));
    context.expect(!unknown_material_inp.status.success,
        "material-library roughness must reject an unknown material reference");

    NetworkHydraulic missing_date = fixture.network;
    HydraulicLinkPipe *missing_date_pipe = findModelPipe(missing_date, QStringLiteral("111"));
    missing_date_pipe->metadata.date_installed.reset();
    const EpanetResultInp missing_date_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(missing_date));
    context.expect(!missing_date_inp.status.success,
        "material-library roughness must reject a missing installation date");

    NetworkHydraulic future_date = fixture.network;
    HydraulicLinkPipe *future_date_pipe = findModelPipe(future_date, QStringLiteral("111"));
    future_date_pipe->metadata.date_installed = QDate(QDate::currentDate().year() + 1, 1, 1);
    const EpanetResultInp future_date_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(future_date));
    context.expect(!future_date_inp.status.success,
        "material-library roughness must reject a future installation year");

    NetworkHydraulic missing_formula = fixture.network;
    missing_formula.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::DarcyWeisbach;
    for (HydraulicPipeMaterialRoughnessAtAge &entry : missing_formula.pipe_materials[0].roughness_by_age)
        entry.roughness_darcy_weisbach_mm.reset();
    const EpanetResultInp missing_formula_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(missing_formula));
    context.expect(!missing_formula_inp.status.success,
        "material-library roughness must reject missing data for the active headloss formula");

    NetworkHydraulic explicit_network = fixture.network;
    explicit_network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::HazenWilliams;
    HydraulicLinkPipe *explicit_pipe = findModelPipe(explicit_network, QStringLiteral("111"));
    explicit_pipe->roughness_mode = HydraulicPipeRoughnessMode::Explicit;
    explicit_pipe->material_uuid = QUuid();
    explicit_pipe->metadata.date_installed.reset();
    explicit_pipe->roughness_hazen_williams = 133.0;
    const EpanetResultInp explicit_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(explicit_network));
    context.expect(explicit_inp.status.success,
        "explicit roughness mode must not require material-library metadata");
    roughness = pipeRoughnessFromInp(explicit_inp.inp_text, explicit_pipe->id);
    if (roughness.has_value())
        context.expectNear(roughness.value(), 133.0, NumericTolerance{1.0e-9, 0.0}, comparison("material_library.explicit_bypass", 0, "Pipe", "111"));
}

void testDemandPointNodeProjection(TestContext &context)
{
    Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.duration_s = 0;

    HydraulicNodeJunction *junction_11 = findModelJunction(fixture.network, QStringLiteral("11"));
    HydraulicNodeJunction *junction_12 = findModelJunction(fixture.network, QStringLiteral("12"));
    context.expect(junction_11 != nullptr, "demand-point projection fixture must contain junction 11");
    context.expect(junction_12 != nullptr, "demand-point projection fixture must contain junction 12");
    if (junction_11 == nullptr || junction_12 == nullptr)
        return;

    HydraulicLinkPipe *pipe_11 = nullptr;
    for (HydraulicLinkPipe &pipe : fixture.network.links_pipes)
    {
        if (pipe.id == QStringLiteral("11"))
        {
            pipe_11 = &pipe;
            break;
        }
    }
    context.expect(pipe_11 != nullptr, "demand-point projection fixture must contain pipe 11");
    if (pipe_11 == nullptr)
        return;

    HydraulicDemand demand;
    demand.category_name = QStringLiteral("Demand point attachment");
    demand.base_demand_m3_per_h = 10.0;
    demand.pattern_mode = HydraulicTimePatternMode::Constant;
    demand.source_method = HydraulicDemandSourceMethod::MeterData;
    demand.note = QStringLiteral("Projection provenance must survive scaling");

    HydraulicDemandPoint pipe_demand_point;
    pipe_demand_point.id = QStringLiteral("DP_PIPE");
    pipe_demand_point.uuid = QUuid::createUuid();
    pipe_demand_point.coordinate_wgs84.longitude_deg = 18.0;
    pipe_demand_point.coordinate_wgs84.latitude_deg = 12.0;
    pipe_demand_point.demands.append(demand);
    pipe_demand_point.attachment.type = HydraulicDemandPointAttachmentType::Pipe;
    pipe_demand_point.attachment.pipe_uuid = pipe_11->uuid;
    pipe_demand_point.attachment.pipe_position = 0.25;

    HydraulicDemandPoint assigned_pipe_demand_point;
    assigned_pipe_demand_point.id = QStringLiteral("DP_PIPE_ASSIGNED");
    assigned_pipe_demand_point.uuid = QUuid::createUuid();
    assigned_pipe_demand_point.coordinate_wgs84.longitude_deg = 18.05;
    assigned_pipe_demand_point.coordinate_wgs84.latitude_deg = 12.05;
    assigned_pipe_demand_point.demands.append(demand);
    assigned_pipe_demand_point.attachment.type = HydraulicDemandPointAttachmentType::Pipe;
    assigned_pipe_demand_point.attachment.pipe_uuid = pipe_11->uuid;
    assigned_pipe_demand_point.attachment.pipe_position = 0.80;
    assigned_pipe_demand_point.attachment.pipe_allocation_mode =
        HydraulicDemandPointPipeAllocationMode::AssignedJunction;
    assigned_pipe_demand_point.attachment.pipe_assigned_junction_uuid =
        pipe_11->node_uuid_to;

    HydraulicDemandPoint junction_demand_point;
    junction_demand_point.id = QStringLiteral("DP_JUNCTION");
    junction_demand_point.uuid = QUuid::createUuid();
    junction_demand_point.coordinate_wgs84.longitude_deg = 18.1;
    junction_demand_point.coordinate_wgs84.latitude_deg = 12.1;
    junction_demand_point.demands.append(demand);
    junction_demand_point.attachment.type = HydraulicDemandPointAttachmentType::Junction;
    junction_demand_point.attachment.junction_uuid = junction_12->uuid;

    NetworkHydraulic equivalent_direct_network = fixture.network;
    HydraulicNodeJunction *equivalent_junction_11 =
        findModelJunction(equivalent_direct_network, QStringLiteral("11"));
    HydraulicNodeJunction *equivalent_junction_12 =
        findModelJunction(equivalent_direct_network, QStringLiteral("12"));
    context.expect(equivalent_junction_11 != nullptr,
        "equivalent direct-demand fixture must contain junction 11");
    context.expect(equivalent_junction_12 != nullptr,
        "equivalent direct-demand fixture must contain junction 12");
    if (equivalent_junction_11 == nullptr || equivalent_junction_12 == nullptr)
        return;

    HydraulicDemand pipe_demand_from = demand;
    pipe_demand_from.base_demand_m3_per_h *= 0.75;
    equivalent_junction_11->demands.append(pipe_demand_from);

    HydraulicNodeJunction *equivalent_assigned_junction =
        findModelJunctionByUuid(equivalent_direct_network, pipe_11->node_uuid_to);
    context.expect(equivalent_assigned_junction != nullptr,
        "equivalent direct-demand fixture must contain the assigned pipe endpoint junction");
    if (equivalent_assigned_junction == nullptr)
        return;

    HydraulicDemand aggregated_junction_12_demand = demand;
    aggregated_junction_12_demand.base_demand_m3_per_h = 22.5;
    equivalent_junction_12->demands.append(aggregated_junction_12_demand);

    fixture.network.demand_points.append(pipe_demand_point);
    fixture.network.demand_points.append(assigned_pipe_demand_point);
    fixture.network.demand_points.append(junction_demand_point);

    const EpanetResultInp projected_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(fixture.network));
    const EpanetResultInp direct_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(equivalent_direct_network));

    context.expect(projected_inp.status.success,
        "demand-point attachment projection must produce a valid EPANET input model");
    context.expect(direct_inp.status.success,
        "equivalent direct junction demands must produce a valid EPANET input model");
    if (projected_inp.status.success && direct_inp.status.success)
    {
        context.expect(
            projected_inp.inp_text == direct_inp.inp_text,
            "positional pipe, assigned-junction pipe, and direct junction demand-point attachments must resolve to the same EPANET model as equivalent direct junction demands");
        context.expect(!projected_inp.inp_text.contains(pipe_demand_point.id),
            "position-interpolated pipe demand-point identity must not leak into the solver-facing EPANET model");
        context.expect(!projected_inp.inp_text.contains(assigned_pipe_demand_point.id),
            "assigned-junction pipe demand-point identity must not leak into the solver-facing EPANET model");
        context.expect(!projected_inp.inp_text.contains(junction_demand_point.id),
            "junction-attached demand-point identity must not leak into the solver-facing EPANET model");
        context.expect(
            projected_inp.inp_text.count(QStringLiteral("Demand point attachment")) == 2,
            "solver projection must aggregate equivalent projected demand categories per junction");
    }

    NetworkHydraulic disabled_network = fixture.network;
    for (HydraulicDemandPoint &disabled_demand_point : disabled_network.demand_points)
        disabled_demand_point.metadata.enabled = false;
    const EpanetResultInp disabled_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(disabled_network));
    Net1Fixture baseline_fixture = AowisEpanetTests::makeNet1Fixture();
    baseline_fixture.network.duration_s = 0;
    const EpanetResultInp baseline_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(baseline_fixture.network));
    context.expect(disabled_inp.status.success,
        "disabled demand points must be ignored by solver projection");
    context.expect(baseline_inp.status.success,
        "baseline demand-point comparison input must be valid");
    if (disabled_inp.status.success && baseline_inp.status.success)
    {
        context.expect(
            disabled_inp.inp_text == baseline_inp.inp_text,
            "disabled demand points must not alter the solver-facing model");
    }

    NetworkHydraulic unattached_network = fixture.network;
    unattached_network.demand_points.first().attachment =
        HydraulicDemandPointAttachment();
    const EpanetResultInp unattached_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(unattached_network));
    context.expect(!unattached_inp.status.success,
        "enabled demand point with hydraulic demand but without attachment must be rejected");
    context.expect(
        unattached_inp.status.entity.type == HydraulicSimulationStatusEntityType::DemandPoint,
        "invalid demand-point projection must identify the demand point as the failing entity type");

    NetworkHydraulic invalid_position_network = fixture.network;
    invalid_position_network.demand_points.first().attachment.pipe_position = 1.5;
    const EpanetResultInp invalid_position_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(invalid_position_network));
    context.expect(!invalid_position_inp.status.success,
        "demand-point pipe positions outside the normalized range must be rejected");

    NetworkHydraulic missing_assigned_junction_network = fixture.network;
    missing_assigned_junction_network.demand_points[1].attachment.pipe_assigned_junction_uuid = QUuid();
    const EpanetResultInp missing_assigned_junction_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(missing_assigned_junction_network));
    context.expect(!missing_assigned_junction_inp.status.success,
        "assigned-junction pipe demand point without an assigned junction must be rejected");
    context.expect(
        missing_assigned_junction_inp.status.entity.type == HydraulicSimulationStatusEntityType::DemandPoint,
        "missing assigned junction must identify the demand point as the failing entity type");

    NetworkHydraulic non_endpoint_assigned_junction_network = fixture.network;
    const QUuid non_endpoint_junction_uuid = junction_11->uuid == pipe_11->node_uuid_from
            || junction_11->uuid == pipe_11->node_uuid_to
        ? QUuid::createUuid()
        : junction_11->uuid;
    non_endpoint_assigned_junction_network.demand_points[1]
        .attachment.pipe_assigned_junction_uuid = non_endpoint_junction_uuid;
    const EpanetResultInp non_endpoint_assigned_junction_inp = EpanetRunner().retrieveInp(
        AowisEpanetTests::makeRunRequest(non_endpoint_assigned_junction_network));
    context.expect(!non_endpoint_assigned_junction_inp.status.success,
        "assigned-junction pipe demand point must reject a junction that is not a pipe endpoint");
    context.expect(
        non_endpoint_assigned_junction_inp.status.entity.type == HydraulicSimulationStatusEntityType::DemandPoint,
        "non-endpoint assigned junction must identify the demand point as the failing entity type");
}

void configureFormulaFixture(Net1Fixture &fixture, HydraulicHeadlossFormula formula, double default_roughness, double pipe_10_roughness)
{
    fixture.network.duration_s = 0;
    fixture.network.options_hydraulic.headloss_formula = formula;
    for (HydraulicLinkPipe &pipe : fixture.network.links_pipes)
    {
        if (formula == HydraulicHeadlossFormula::DarcyWeisbach)
            pipe.roughness_darcy_weisbach_mm = pipe.id == QStringLiteral("10") ? pipe_10_roughness : default_roughness;
        else if (formula == HydraulicHeadlossFormula::ChezyManning)
            pipe.roughness_chezy_manning = pipe.id == QStringLiteral("10") ? pipe_10_roughness : default_roughness;
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
        {"conformance", "hydraulic", "upstream", "junction", "reservoir", "emitter"},
        &testJunctionReservoirInputs});
    registry.add(ScenarioDefinition{
        "conformance-upstream-demand-categories",
        "Ports multiple junction demand categories with independent patterned and constant modes and compares the complete hydraulic timeline.",
        {"conformance", "hydraulic", "upstream", "junction", "demand"},
        &testDemandCategories});
    registry.add(ScenarioDefinition{
        "conformance-demand-point-node-projection",
        "Projects positional and assigned-junction Demand Point pipe attachments plus direct junction attachments into transient junction demands before the EPANET-specific build and rejects invalid attachments.",
        {"conformance", "hydraulic", "demand-point", "projection"},
        &testDemandPointNodeProjection});
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
        "conformance-pipe-material-roughness-resolution",
        "Resolves age-dependent pipe material roughness for all EPANET headloss formulas and rejects incomplete material-backed pipe configuration.",
        {"conformance", "hydraulic", "pipe", "material", "roughness"},
        &testPipeMaterialRoughnessResolution});
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
