#include "conformance/msx_vendor_smoke_scenarios.h"
#include "conformance/net1_fixture.h"

#include <aowis/epanet/epanet_runner.h>

#include "../src/lib/internal/epanet_msx_project.h"
#include "../src/lib/internal/epanet_multi_quality_run_executor.h"
#include "../src/lib/internal/epanet_prepared_project.h"

#include <epanetmsx.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace
{
std::string msxErrorMessage(int code)
{
    std::string buffer(256, '\0');
    MSXgeterror(code, buffer.data(), static_cast<int>(buffer.size() - 1));
    return buffer.c_str();
}

bool writeUtf8TextFile(const QString &path, const QString &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    const QByteArray bytes = contents.toUtf8();
    return file.write(bytes) == static_cast<qint64>(bytes.size());
}

QString msxNonPipeLinkInpFixture()
{
    return QString::fromLatin1(R"INP([TITLE]
MSX non-pipe link semantics fixture

[JUNCTIONS]
;ID   Elevation   Demand
 J1   0           0
 J2   0           0
 J3   0           10

[RESERVOIRS]
;ID   Head
 R1   100

[PIPES]
;ID   Node1 Node2 Length Diameter Roughness MinorLoss Status
 P1   J2    J3    1000   100      100       0         OPEN

[PUMPS]
;ID   Node1 Node2 Parameters
 PU1  R1    J1    POWER 10

[VALVES]
;ID   Node1 Node2 Diameter Type Setting MinorLoss
 V1   J1    J2    100      TCV  1       0

[OPTIONS]
 UNITS LPS
 HEADLOSS H-W

[TIMES]
 DURATION 1:00
 HYDRAULIC TIMESTEP 0:05
 QUALITY TIMESTEP 0:01
 REPORT TIMESTEP 0:05

[END]
)INP");
}

QString msxNonPipeLinkModelFixture()
{
    return QString::fromLatin1(R"MSX([TITLE]
MSX non-pipe link semantics fixture

[OPTIONS]
 AREA_UNITS M2
 RATE_UNITS HR
 SOLVER RK5
 TIMESTEP 60

[SPECIES]
 BULK S MG

[COEFFICIENTS]
 PARAM K 1.0

[PIPES]
 RATE S -K*S

[TANKS]
 RATE S 0

[QUALITY]
 NODE R1  S 1
 NODE J1  S 3
 NODE J2  S 5
 NODE J3  S 7
 LINK PU1 S 9
 LINK V1  S 11

[PARAMETERS]
 PIPE PU1 K 4
 PIPE V1  K 6
)MSX");
}

// Mirrors the identically-named helper in other test files (e.g.
// test_export_fidelity_conformance.cpp): each file defines its own, since
// it is a small anonymous-namespace convenience, not a shared fixture.
NetworkHydraulic cleanNet1()
{
    AowisEpanetTests::Net1Fixture fixture = AowisEpanetTests::makeNet1Fixture();
    fixture.network.controls_simple.clear();
    fixture.network.controls_rules.clear();
    return fixture.network;
}

void addSimpleChlorineMsxModel(NetworkHydraulic &network)
{
    const HydraulicNodeReservoir &source_node = network.nodes_reservoirs.first();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    chlorine.type = MultiSpeciesSpeciesType::Bulk;
    chlorine.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(chlorine);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = chlorine.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = chlorine.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesNodeInitialQuality initial_quality;
    initial_quality.node_uuid = source_node.uuid;
    initial_quality.species_uuid = chlorine.uuid;
    initial_quality.value = 1.0;
    network.multi_species.initial_quality_nodes.append(initial_quality);
}

std::pair<QUuid, QUuid> addCoupledTwoSpeciesMsxModel(NetworkHydraulic &network)
{
    MultiSpeciesSpecies species_one;
    species_one.id = QStringLiteral("S1");
    species_one.uuid = QUuid::createUuid();
    species_one.type = MultiSpeciesSpeciesType::Bulk;
    species_one.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(species_one);

    MultiSpeciesSpecies species_two;
    species_two.id = QStringLiteral("S2");
    species_two.uuid = QUuid::createUuid();
    species_two.type = MultiSpeciesSpeciesType::Bulk;
    species_two.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(species_two);

    MultiSpeciesConstant rate_constant;
    rate_constant.id = QStringLiteral("K");
    rate_constant.uuid = QUuid::createUuid();
    rate_constant.value = 0.05;
    network.multi_species.constants.append(rate_constant);

    for (const MultiSpeciesReactionLocation location : {MultiSpeciesReactionLocation::Pipe, MultiSpeciesReactionLocation::Tank})
    {
        MultiSpeciesReaction species_one_reaction;
        species_one_reaction.uuid = QUuid::createUuid();
        species_one_reaction.species_uuid = species_one.uuid;
        species_one_reaction.location = location;
        species_one_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
        species_one_reaction.expression = QStringLiteral("-K*S1*S2");
        network.multi_species.reactions.append(species_one_reaction);

        MultiSpeciesReaction species_two_reaction;
        species_two_reaction.uuid = QUuid::createUuid();
        species_two_reaction.species_uuid = species_two.uuid;
        species_two_reaction.location = location;
        species_two_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
        species_two_reaction.expression = QStringLiteral("0");
        network.multi_species.reactions.append(species_two_reaction);
    }

    MultiSpeciesGlobalInitialQuality species_one_initial;
    species_one_initial.species_uuid = species_one.uuid;
    species_one_initial.value = 1.0;
    network.multi_species.initial_quality_global.append(species_one_initial);

    MultiSpeciesGlobalInitialQuality species_two_initial;
    species_two_initial.species_uuid = species_two.uuid;
    species_two_initial.value = 2.0;
    network.multi_species.initial_quality_global.append(species_two_initial);

    return std::make_pair(species_one.uuid, species_two.uuid);
}

bool timelineHasSpeciesValue(const MultiSpeciesSimulationResultTimeline &timeline)
{
    for (const MultiSpeciesSimulationResult &step : timeline.results)
    {
        for (const MultiSpeciesSimulationResultNodeJunction &junction_result : step.nodes_junctions)
        {
            if (!junction_result.species_values.isEmpty())
                return true;
        }
    }
    return false;
}

bool findMsxSpeciesConcentration(
    const QList<MultiSpeciesResultValue> &values,
    const QUuid &species_uuid,
    double &concentration);

// Exercises the raw vendored MSXENopen/MSXopen/MSXsolveH/MSXinit/MSXstep/
// MSXgetqual/MSXclose/MSXENclose sequence directly, with none of AOWIS's own
// adapter code involved. This is deliberately not a conformance or contract
// scenario: it proves the EPANET-MSX submodule is present, builds, links
// against this adapter's own vendored EPANET rather than MSX's bundled
// EPANET2.2 copy, and can run a real reaction model to a real result -- the
// foundation the AOWIS MSX adapter is built on, not a claim about AOWIS's own
// translation of that foundation.
void scenarioMsxVendorOpenInitStepClose(AowisEpanetTests::TestContext &context)
{
    QTemporaryDir scratch_dir;
    context.expect(scratch_dir.isValid(), "must be able to create a scratch directory for MSX report/output files");
    if (!scratch_dir.isValid())
        return;

    const std::string inp_path = AOWIS_EPANET_MSX_EXAMPLE_INP;
    const std::string rpt_path = (scratch_dir.path() + QStringLiteral("/example.rpt")).toStdString();
    const std::string out_path = (scratch_dir.path() + QStringLiteral("/example.out")).toStdString();
    std::string msx_path = AOWIS_EPANET_MSX_EXAMPLE_MSX;
    std::string as3_species_name = "AS3";
    std::string node_d_name = "D";

    int errcode = MSXENopen(inp_path.c_str(), rpt_path.c_str(), out_path.c_str());
    context.expect(errcode == 0, "MSXENopen must open the vendored EPANET-MSX example network without error: " + msxErrorMessage(errcode));
    if (errcode != 0)
        return;

    errcode = MSXopen(msx_path.data());
    context.expect(errcode == 0, "MSXopen must open the vendored EPANET-MSX example reaction model without error: " + msxErrorMessage(errcode));

    if (errcode == 0)
    {
        errcode = MSXsolveH();
        context.expect(errcode == 0, "MSXsolveH must solve hydraulics for the example network without error: " + msxErrorMessage(errcode));
    }

    int species_index = -1;
    int node_index = -1;
    if (errcode == 0)
    {
        const int species_errcode = MSXgetindex(MSX_SPECIES, as3_species_name.data(), &species_index);
        context.expect(species_errcode == 0 && species_index > 0, "MSXgetindex must resolve the AS3 species declared in the example reaction model");

        // MSXgetindex only resolves MSX's own object types (species,
        // constants, parameters, patterns) -- its switch has no MSX_NODE
        // case, so it would return ERR_INVALID_OBJECT_TYPE for a node
        // lookup. Nodes belong to the underlying EPANET project, so they are
        // resolved through the legacy EN toolkit MSX itself is built on.
        const int node_errcode = ENgetnodeindex(node_d_name.c_str(), &node_index);
        context.expect(node_errcode == 0 && node_index > 0, "ENgetnodeindex must resolve junction D declared in the example network");
    }

    if (errcode == 0)
    {
        errcode = MSXinit(0);
        context.expect(errcode == 0, "MSXinit must initialize water-quality state without error: " + msxErrorMessage(errcode));
    }

    int steps_taken = 0;
    if (errcode == 0)
    {
        double t = 0.0;
        double tleft = 1.0;
        const int max_steps = 100000;
        while (errcode == 0 && tleft > 0.0 && steps_taken < max_steps)
        {
            errcode = MSXstep(&t, &tleft);
            steps_taken++;
        }
        context.expect(errcode == 0, "MSXstep must advance through the full example duration without error: " + msxErrorMessage(errcode));
        context.expect(steps_taken > 1 && steps_taken < max_steps, "MSXstep must take a bounded, non-trivial number of quality steps to cover the example's duration");
    }

    if (errcode == 0 && species_index > 0 && node_index > 0)
    {
        double concentration = -1.0;
        const int qual_errcode = MSXgetqual(MSX_NODE, node_index, species_index, &concentration);
        context.expect(qual_errcode == 0, "MSXgetqual must read an AS3 result at junction D without error: " + msxErrorMessage(qual_errcode));
        context.expect(concentration >= 0.0, "AS3 concentration at junction D must be a physically valid non-negative value after the full run");
    }

    MSXclose();
    MSXENclose();
}


// EPANET-MSX calls every hydraulic edge a LINK internally, and its
// [QUALITY] LINK and [PARAMETERS] PIPE parsers resolve identifiers through
// ENgetlinkindex(). That parser behavior accepts pump and valve IDs even
// though those objects are not reaction-bearing pipes. Lock the native
// behavior down so AOWIS does not accidentally broaden its solver-neutral
// pipe model merely because of a backend parser alias.
void scenarioMsxVendorNonPipeLinkParserAliases(AowisEpanetTests::TestContext &context)
{
    QTemporaryDir scratch_dir;
    context.expect(scratch_dir.isValid(), "must be able to create the non-pipe MSX parser fixture");
    if (!scratch_dir.isValid())
        return;

    const QString inp_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.inp"));
    const QString msx_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.msx"));
    const QString rpt_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.rpt"));
    const QString out_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.out"));

    const bool inp_written = writeUtf8TextFile(inp_path_qt, msxNonPipeLinkInpFixture());
    const bool msx_written = writeUtf8TextFile(msx_path_qt, msxNonPipeLinkModelFixture());
    context.expect(inp_written, "must write the non-pipe EPANET fixture");
    context.expect(msx_written, "must write the non-pipe MSX fixture");
    if (!inp_written || !msx_written)
        return;

    const std::string inp_path = inp_path_qt.toStdString();
    const std::string rpt_path = rpt_path_qt.toStdString();
    const std::string out_path = out_path_qt.toStdString();
    std::string msx_path = msx_path_qt.toStdString();

    int errcode = MSXENopen(inp_path.c_str(), rpt_path.c_str(), out_path.c_str());
    context.expect(errcode == 0, "MSXENopen must open the pump/valve semantics fixture: " + msxErrorMessage(errcode));
    if (errcode != 0)
        return;

    errcode = MSXopen(msx_path.data());
    context.expect(errcode == 0, "MSXopen must accept LINK/PIPE records that name pump and valve IDs: " + msxErrorMessage(errcode));
    if (errcode != 0)
    {
        MSXENclose();
        return;
    }

    int pump_index = 0;
    int valve_index = 0;
    int species_index = 0;
    int parameter_index = 0;
    std::string pump_id = "PU1";
    std::string valve_id = "V1";
    std::string species_id = "S";
    std::string parameter_id = "K";

    context.expect(ENgetlinkindex(pump_id.c_str(), &pump_index) == 0 && pump_index > 0, "EPANET must resolve the pump as a generic link");
    context.expect(ENgetlinkindex(valve_id.c_str(), &valve_index) == 0 && valve_index > 0, "EPANET must resolve the valve as a generic link");
    context.expect(MSXgetindex(MSX_SPECIES, species_id.data(), &species_index) == 0 && species_index > 0, "MSX must resolve the fixture species");
    context.expect(MSXgetindex(MSX_PARAMETER, parameter_id.data(), &parameter_index) == 0 && parameter_index > 0, "MSX must resolve the fixture parameter");

    EN_API_FLOAT_TYPE pump_length = 0.0;
    EN_API_FLOAT_TYPE valve_length = 0.0;
    context.expect(ENgetlinkvalue(pump_index, EN_LENGTH, &pump_length) == 0, "EPANET must expose pump length");
    context.expect(ENgetlinkvalue(valve_index, EN_LENGTH, &valve_length) == 0, "EPANET must expose valve length");
    context.expectNear(
        static_cast<double>(pump_length),
        0.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "link", "PU1", "length"},
        "EPANET pumps are zero-length links");
    context.expectNear(
        static_cast<double>(valve_length),
        0.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "link", "V1", "length"},
        "EPANET valves are zero-length links");

    double value = 0.0;
    context.expect(MSXgetinitqual(MSX_LINK, pump_index, species_index, &value) == 0, "MSX must expose the parser-stored pump LINK initial value");
    context.expectNear(
        value,
        9.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "PU1", "stored_initial_link_value"});
    context.expect(MSXgetinitqual(MSX_LINK, valve_index, species_index, &value) == 0, "MSX must expose the parser-stored valve LINK initial value");
    context.expectNear(
        value,
        11.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "V1", "stored_initial_link_value"});

    context.expect(MSXgetparameter(MSX_LINK, pump_index, parameter_index, &value) == 0, "MSX must expose the parser-stored pump PIPE parameter value");
    context.expectNear(
        value,
        4.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "PU1", "stored_pipe_parameter"});
    context.expect(MSXgetparameter(MSX_LINK, valve_index, parameter_index, &value) == 0, "MSX must expose the parser-stored valve PIPE parameter value");
    context.expectNear(
        value,
        6.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "V1", "stored_pipe_parameter"});

    MSXclose();
    MSXENclose();
}

// Zero-length pump/valve links receive no MSX transport segments. At the
// quality API boundary their current value therefore falls back to the mean
// quality of their endpoint nodes, rather than the LINK initial value that
// the permissive parser happened to store. This is the runtime distinction
// that keeps AOWIS pipe initial conditions and pipe parameter overrides
// intentionally pipe-only.
void scenarioMsxVendorZeroVolumeLinkQualitySemantics(AowisEpanetTests::TestContext &context)
{
    QTemporaryDir scratch_dir;
    context.expect(scratch_dir.isValid(), "must be able to create the zero-volume MSX semantics fixture");
    if (!scratch_dir.isValid())
        return;

    const QString inp_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.inp"));
    const QString msx_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.msx"));
    const QString rpt_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.rpt"));
    const QString out_path_qt = scratch_dir.filePath(QStringLiteral("nonpipe.out"));

    const bool inp_written = writeUtf8TextFile(inp_path_qt, msxNonPipeLinkInpFixture());
    const bool msx_written = writeUtf8TextFile(msx_path_qt, msxNonPipeLinkModelFixture());
    context.expect(inp_written, "must write the zero-volume EPANET fixture");
    context.expect(msx_written, "must write the zero-volume MSX fixture");
    if (!inp_written || !msx_written)
        return;

    const std::string inp_path = inp_path_qt.toStdString();
    const std::string rpt_path = rpt_path_qt.toStdString();
    const std::string out_path = out_path_qt.toStdString();
    std::string msx_path = msx_path_qt.toStdString();

    int errcode = MSXENopen(inp_path.c_str(), rpt_path.c_str(), out_path.c_str());
    context.expect(errcode == 0, "MSXENopen must open the zero-volume link fixture: " + msxErrorMessage(errcode));
    if (errcode != 0)
        return;

    errcode = MSXopen(msx_path.data());
    context.expect(errcode == 0, "MSXopen must open the zero-volume link reaction model: " + msxErrorMessage(errcode));
    if (errcode != 0)
    {
        MSXENclose();
        return;
    }

    errcode = MSXsolveH();
    context.expect(errcode == 0, "MSXsolveH must solve the pump/valve fixture before quality initialization: " + msxErrorMessage(errcode));
    if (errcode != 0)
    {
        MSXclose();
        MSXENclose();
        return;
    }

    errcode = MSXinit(0);
    context.expect(errcode == 0, "MSXinit must initialize the pump/valve fixture: " + msxErrorMessage(errcode));
    if (errcode != 0)
    {
        MSXclose();
        MSXENclose();
        return;
    }

    int pump_index = 0;
    int valve_index = 0;
    int reservoir_index = 0;
    int junction_one_index = 0;
    int junction_two_index = 0;
    int species_index = 0;
    std::string pump_id = "PU1";
    std::string valve_id = "V1";
    std::string reservoir_id = "R1";
    std::string junction_one_id = "J1";
    std::string junction_two_id = "J2";
    std::string species_id = "S";

    context.expect(ENgetlinkindex(pump_id.c_str(), &pump_index) == 0 && pump_index > 0, "EPANET must resolve PU1");
    context.expect(ENgetlinkindex(valve_id.c_str(), &valve_index) == 0 && valve_index > 0, "EPANET must resolve V1");
    context.expect(ENgetnodeindex(reservoir_id.c_str(), &reservoir_index) == 0 && reservoir_index > 0, "EPANET must resolve R1");
    context.expect(ENgetnodeindex(junction_one_id.c_str(), &junction_one_index) == 0 && junction_one_index > 0, "EPANET must resolve J1");
    context.expect(ENgetnodeindex(junction_two_id.c_str(), &junction_two_index) == 0 && junction_two_index > 0, "EPANET must resolve J2");
    context.expect(MSXgetindex(MSX_SPECIES, species_id.data(), &species_index) == 0 && species_index > 0, "MSX must resolve species S");

    double reservoir_quality = 0.0;
    double junction_one_quality = 0.0;
    double junction_two_quality = 0.0;
    double pump_quality = 0.0;
    double valve_quality = 0.0;

    context.expect(MSXgetqual(MSX_NODE, reservoir_index, species_index, &reservoir_quality) == 0, "MSX must expose initial R1 quality");
    context.expect(MSXgetqual(MSX_NODE, junction_one_index, species_index, &junction_one_quality) == 0, "MSX must expose initial J1 quality");
    context.expect(MSXgetqual(MSX_NODE, junction_two_index, species_index, &junction_two_quality) == 0, "MSX must expose initial J2 quality");
    context.expect(MSXgetqual(MSX_LINK, pump_index, species_index, &pump_quality) == 0, "MSX must expose current pump link quality");
    context.expect(MSXgetqual(MSX_LINK, valve_index, species_index, &valve_quality) == 0, "MSX must expose current valve link quality");

    context.expectNear(
        pump_quality,
        (reservoir_quality + junction_one_quality) / 2.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "PU1", "current_link_value"},
        "zero-volume pump quality must fall back to endpoint-node averaging instead of its stored LINK initial value");
    context.expectNear(
        valve_quality,
        (junction_one_quality + junction_two_quality) / 2.0,
        AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
        {-1, "quality", "V1", "current_link_value"},
        "zero-volume valve quality must fall back to endpoint-node averaging instead of its stored LINK initial value");

    MSXclose();
    MSXENclose();
}

// Proves the Lew-style handoff directly at AOWIS's internal boundary:
// hydraulics are solved by the normal handle-based AOWIS EPANET executor,
// persisted once together with the configured INP snapshot from that same
// project, and EpanetMsxProject consumes the pair through MSXENopen +
// MSXusehydfile. This keeps the internal handoff covered independently of
// the public EpanetRunner integration scenarios below.
void scenarioMsxAowisHydraulicHandoff(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    addSimpleChlorineMsxModel(network);

    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    context.expect(status.success, "AOWIS project must prepare before producing hydraulics for MSX");
    if (!status.success)
        return;

    EpanetMultiQualityRunExecutor hydraulic_executor(prepared_project, true);
    EpanetResultRun hydraulic_result;
    hydraulic_result = hydraulic_executor.run(std::move(hydraulic_result));
    context.expect(hydraulic_result.result_timeline.status.success, "AOWIS hydraulics must succeed before the MSX handoff");
    context.expect(hydraulic_executor.hasHydraulicFile(), "AOWIS hydraulic executor must persist the .hyd file requested for MSX");
    context.expect(!hydraulic_executor.hydraulicInpText().trimmed().isEmpty(), "AOWIS hydraulic executor must capture the configured INP snapshot paired with the .hyd file");
    if (!hydraulic_result.result_timeline.status.success || !hydraulic_executor.hasHydraulicFile())
        return;

    const QString hydraulic_file_path = hydraulic_executor.hydraulicFilePath();
    context.expect(QFileInfo(hydraulic_file_path).size() > 0, "MSX handoff hydraulic file must be non-empty");

    EpanetMsxProject msx_project;
    MultiSpeciesSimulationResultTimeline timeline;
    bool cancelled = false;
    status = msx_project.run(
        network,
        MultiSpeciesRunOptions{},
        hydraulic_executor.hydraulicInpText(),
        hydraulic_file_path,
        timeline,
        std::function<bool()>(),
        cancelled);

    context.expect(status.success, "EpanetMsxProject must consume AOWIS hydraulics through MSXusehydfile without solving hydraulics again");
    context.expect(!cancelled, "MSX hydraulic-handoff proof must complete without cancellation");
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "MSX hydraulic-handoff timeline must be Valid");
    context.expect(!timeline.results.isEmpty(), "MSX hydraulic-handoff timeline must contain quality timesteps");
    context.expect(timelineHasSpeciesValue(timeline), "MSX hydraulic-handoff results must contain at least one junction species value");
}

void scenarioMsxConfiguredInpRoughnessKc(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.options_hydraulic.headloss_formula = HydraulicHeadlossFormula::ChezyManning;
    constexpr double expected_roughness = 0.017;
    for (HydraulicLinkPipe &pipe : network.links_pipes)
        pipe.roughness_chezy_manning = expected_roughness;

    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    context.expect(status.success, "Kc configured-INP fixture must prepare successfully");
    if (!status.success)
        return;

    EpanetMultiQualityRunExecutor hydraulic_executor(prepared_project, true);
    EpanetResultRun hydraulic_result;
    hydraulic_result = hydraulic_executor.run(std::move(hydraulic_result));
    context.expect(hydraulic_result.result_timeline.status.success, "Kc configured-INP fixture hydraulics must succeed");
    context.expect(hydraulic_executor.hasHydraulicFile(), "Kc configured-INP fixture must persist reusable hydraulics");
    context.expect(!hydraulic_executor.hydraulicInpText().trimmed().isEmpty(), "Kc configured-INP fixture must capture a configured INP snapshot");
    if (!hydraulic_result.result_timeline.status.success
        || !hydraulic_executor.hasHydraulicFile()
        || hydraulic_executor.hydraulicInpText().trimmed().isEmpty())
    {
        return;
    }

    QTemporaryDir scratch_dir;
    context.expect(scratch_dir.isValid(), "Kc configured-INP fixture must create a temporary directory");
    if (!scratch_dir.isValid())
        return;

    const QString inp_path = scratch_dir.filePath(QStringLiteral("configured.inp"));
    const QString rpt_path = scratch_dir.filePath(QStringLiteral("configured.rpt"));
    const QString out_path = scratch_dir.filePath(QStringLiteral("configured.out"));

    QFile inp_file(inp_path);
    const bool inp_opened = inp_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    context.expect(inp_opened, "Kc configured-INP fixture must write the captured INP snapshot");
    if (!inp_opened)
        return;
    inp_file.write(hydraulic_executor.hydraulicInpText().toUtf8());
    const bool inp_write_ok = inp_file.error() == QFileDevice::NoError;
    inp_file.close();
    context.expect(inp_write_ok, "Kc configured-INP fixture must write the complete captured INP snapshot");
    if (!inp_write_ok)
        return;

    const QByteArray inp_path_native = QFile::encodeName(inp_path);
    const QByteArray rpt_path_native = QFile::encodeName(rpt_path);
    const QByteArray out_path_native = QFile::encodeName(out_path);
    int error = MSXENopen(inp_path_native.constData(), rpt_path_native.constData(), out_path_native.constData());
    context.expect(error == 0, "MSXENopen must accept the exact configured INP snapshot paired with the saved hydraulics: " + msxErrorMessage(error));
    if (error != 0)
        return;

    const HydraulicLinkPipe &pipe = network.links_pipes.first();
    const QByteArray pipe_id_utf8 = pipe.id.toUtf8();
    int link_index = 0;
    error = ENgetlinkindex(pipe_id_utf8.constData(), &link_index);
    context.expect(error == 0 && link_index > 0, "MSX's legacy EPANET layer must resolve a pipe from the configured INP snapshot");

    float backend_roughness = 0.0F;
    if (error == 0 && link_index > 0)
    {
        error = ENgetlinkvalue(link_index, EN_ROUGHNESS, &backend_roughness);
        context.expect(error == 0, "MSX's legacy EPANET layer must expose the configured pipe roughness used to initialize Kc");
    }

    MSXENclose();

    if (error != 0 || link_index <= 0)
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-7, 1.0e-6};
    context.expectNear(
        static_cast<double>(backend_roughness),
        expected_roughness,
        tolerance,
        {0, "pipe", pipe.id.toStdString(), "Kc"},
        "The configured INP snapshot opened by MSX must expose the hydraulically configured roughness rather than the construction-time placeholder 1.0");
}

NetworkHydraulic combinedQualityMsxNetwork()
{
    NetworkHydraulic network = cleanNet1();
    if (!network.nodes_reservoirs.isEmpty())
        network.nodes_reservoirs.first().initial_chemical_concentration_mg_per_l = 1.0;
    addSimpleChlorineMsxModel(network);
    return network;
}

WaterQualitySolverOptions chemicalQualityOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::Chemical;
    options.chemical_name = QStringLiteral("Chlorine");
    return options;
}

WaterQualitySolverOptions waterAgeQualityOptions()
{
    WaterQualitySolverOptions options;
    options.analysis = WaterQualityAnalysisType::WaterAge;
    return options;
}

template<typename EntityResult>
void compareQualityEntities(
    AowisEpanetTests::TestContext &context,
    const QList<EntityResult> &actual,
    const QList<EntityResult> &expected,
    std::int64_t time_s,
    const std::string &entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, std::string(), "count"},
        "combined and isolated standard-quality entity counts must match");
    if (actual.size() != expected.size())
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-12, 1.0e-10};
    for (qsizetype entity_index = 0; entity_index < actual.size(); entity_index++)
    {
        const EntityResult &actual_entity = actual.at(entity_index);
        const EntityResult &expected_entity = expected.at(entity_index);
        const std::string entity_id = actual_entity.id.toStdString();
        context.expectEqual(
            entity_id,
            expected_entity.id.toStdString(),
            {time_s, entity_type, entity_id, "id"},
            "combined and isolated standard-quality entity ordering must match");
        context.expect(
            actual_entity.uuid == expected_entity.uuid,
            "combined and isolated standard-quality entity UUIDs must match");
        context.expectNear(
            actual_entity.chemical_concentration_mg_per_l,
            expected_entity.chemical_concentration_mg_per_l,
            tolerance,
            {time_s, entity_type, entity_id, "chemical_concentration_mg_per_l"});
        context.expectNear(
            actual_entity.water_age_h,
            expected_entity.water_age_h,
            tolerance,
            {time_s, entity_type, entity_id, "water_age_h"});
        context.expectNear(
            actual_entity.source_trace_percent,
            expected_entity.source_trace_percent,
            tolerance,
            {time_s, entity_type, entity_id, "source_trace_percent"});
    }
}

void compareQualityTimelines(
    AowisEpanetTests::TestContext &context,
    const WaterQualitySimulationResultTimeline &actual,
    const WaterQualitySimulationResultTimeline &expected)
{
    context.expect(actual.status.success == expected.status.success, "combined and isolated standard-quality statuses must match");
    context.expect(actual.validity == expected.validity, "combined and isolated standard-quality validity must match");
    context.expect(actual.analysis == expected.analysis, "combined and isolated standard-quality analysis types must match");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        {-1, "quality", std::string(), "timesteps"},
        "combined and isolated standard-quality timestep counts must match");
    if (actual.results.size() != expected.results.size())
        return;

    for (qsizetype step_index = 0; step_index < actual.results.size(); step_index++)
    {
        const WaterQualitySimulationResult &actual_step = actual.results.at(step_index);
        const WaterQualitySimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(actual_step.time_elapsed_s);
        context.expectEqual(
            time_s,
            static_cast<std::int64_t>(expected_step.time_elapsed_s),
            {time_s, "quality", std::string(), "time_elapsed_s"},
            "combined and isolated standard-quality timelines must use the same timestamps");
        compareQualityEntities(context, actual_step.nodes_junctions, expected_step.nodes_junctions, time_s, "junction");
        compareQualityEntities(context, actual_step.nodes_reservoirs, expected_step.nodes_reservoirs, time_s, "reservoir");
        compareQualityEntities(context, actual_step.nodes_tanks, expected_step.nodes_tanks, time_s, "tank");
        compareQualityEntities(context, actual_step.links_pipes, expected_step.links_pipes, time_s, "pipe");
        compareQualityEntities(context, actual_step.links_pumps, expected_step.links_pumps, time_s, "pump");
        compareQualityEntities(context, actual_step.links_valves, expected_step.links_valves, time_s, "valve");
    }
}

void compareMsxSpeciesValues(
    AowisEpanetTests::TestContext &context,
    const QList<MultiSpeciesResultValue> &actual,
    const QList<MultiSpeciesResultValue> &expected,
    std::int64_t time_s,
    const std::string &entity_type,
    const std::string &entity_id)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, entity_id, "species_values.size"},
        "combined and isolated MSX species counts must match");
    if (actual.size() != expected.size())
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-12, 1.0e-10};
    for (qsizetype species_index = 0; species_index < actual.size(); species_index++)
    {
        const MultiSpeciesResultValue &actual_value = actual.at(species_index);
        const MultiSpeciesResultValue &expected_value = expected.at(species_index);
        context.expect(
            actual_value.species_uuid == expected_value.species_uuid,
            "combined and isolated MSX species UUIDs must match");
        context.expectNear(
            actual_value.value,
            expected_value.value,
            tolerance,
            {time_s, entity_type, entity_id, "concentration"});
    }
}

bool findMsxSpeciesConcentration(
    const QList<MultiSpeciesResultValue> &values,
    const QUuid &species_uuid,
    double &concentration)
{
    for (const MultiSpeciesResultValue &value : values)
    {
        if (value.species_uuid == species_uuid)
        {
            concentration = value.value;
            return true;
        }
    }
    return false;
}

template<typename EntityResult>
void compareFilteredMsxEntities(
    AowisEpanetTests::TestContext &context,
    const QList<EntityResult> &filtered,
    const QList<EntityResult> &full,
    const QUuid &selected_species_uuid,
    std::int64_t time_s,
    const std::string &entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(filtered.size()),
        static_cast<std::int64_t>(full.size()),
        {time_s, entity_type, std::string(), "count"},
        "output filtering must not alter the MSX entity set");
    if (filtered.size() != full.size())
        return;

    const AowisEpanetTests::NumericTolerance tolerance{1.0e-12, 1.0e-10};
    for (qsizetype entity_index = 0; entity_index < filtered.size(); entity_index++)
    {
        const EntityResult &filtered_entity = filtered.at(entity_index);
        const EntityResult &full_entity = full.at(entity_index);
        const std::string entity_id = filtered_entity.id.toStdString();

        context.expect(filtered_entity.uuid == full_entity.uuid, "output filtering must not alter MSX entity ordering");
        context.expectEqual(
            static_cast<std::int64_t>(filtered_entity.species_values.size()),
            std::int64_t{1},
            {time_s, entity_type, entity_id, "species_values.size"},
            "a one-species output filter must return exactly one species value per entity");

        if (!filtered_entity.species_values.isEmpty())
        {
            context.expect(
                filtered_entity.species_values.first().species_uuid == selected_species_uuid,
                "the returned MSX species value must match the requested output species");
        }

        double filtered_concentration = 0.0;
        double full_concentration = 0.0;
        const bool filtered_found = findMsxSpeciesConcentration(
            filtered_entity.species_values, selected_species_uuid, filtered_concentration);
        const bool full_found = findMsxSpeciesConcentration(
            full_entity.species_values, selected_species_uuid, full_concentration);
        context.expect(filtered_found, "the filtered result must contain the requested species");
        context.expect(full_found, "the full result must contain the requested species");
        if (filtered_found && full_found)
        {
            context.expectNear(
                filtered_concentration,
                full_concentration,
                tolerance,
                {time_s, entity_type, entity_id, "concentration"});
        }
    }
}

template<typename EntityResult>
void compareMsxEntities(
    AowisEpanetTests::TestContext &context,
    const QList<EntityResult> &actual,
    const QList<EntityResult> &expected,
    std::int64_t time_s,
    const std::string &entity_type)
{
    context.expectEqual(
        static_cast<std::int64_t>(actual.size()),
        static_cast<std::int64_t>(expected.size()),
        {time_s, entity_type, std::string(), "count"},
        "combined and isolated MSX entity counts must match");
    if (actual.size() != expected.size())
        return;

    for (qsizetype entity_index = 0; entity_index < actual.size(); entity_index++)
    {
        const EntityResult &actual_entity = actual.at(entity_index);
        const EntityResult &expected_entity = expected.at(entity_index);
        const std::string entity_id = actual_entity.id.toStdString();
        context.expectEqual(
            entity_id,
            expected_entity.id.toStdString(),
            {time_s, entity_type, entity_id, "id"},
            "combined and isolated MSX entity ordering must match");
        context.expect(
            actual_entity.uuid == expected_entity.uuid,
            "combined and isolated MSX entity UUIDs must match");
        compareMsxSpeciesValues(
            context,
            actual_entity.species_values,
            expected_entity.species_values,
            time_s,
            entity_type,
            entity_id);
    }
}

void compareMsxTimelines(
    AowisEpanetTests::TestContext &context,
    const MultiSpeciesSimulationResultTimeline &actual,
    const MultiSpeciesSimulationResultTimeline &expected)
{
    context.expect(actual.status.success == expected.status.success, "combined and isolated MSX statuses must match");
    context.expect(actual.validity == expected.validity, "combined and isolated MSX validity must match");
    context.expectEqual(
        static_cast<std::int64_t>(actual.results.size()),
        static_cast<std::int64_t>(expected.results.size()),
        {-1, "msx", std::string(), "timesteps"},
        "combined and isolated MSX timestep counts must match");
    if (actual.results.size() != expected.results.size())
        return;

    for (qsizetype step_index = 0; step_index < actual.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &actual_step = actual.results.at(step_index);
        const MultiSpeciesSimulationResult &expected_step = expected.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(actual_step.time_elapsed_s);
        context.expectEqual(
            time_s,
            static_cast<std::int64_t>(expected_step.time_elapsed_s),
            {time_s, "msx", std::string(), "time_elapsed_s"},
            "combined and isolated MSX timelines must use the same timestamps");
        compareMsxEntities(context, actual_step.nodes_junctions, expected_step.nodes_junctions, time_s, "junction");
        compareMsxEntities(context, actual_step.nodes_reservoirs, expected_step.nodes_reservoirs, time_s, "reservoir");
        compareMsxEntities(context, actual_step.nodes_tanks, expected_step.nodes_tanks, time_s, "tank");
        compareMsxEntities(context, actual_step.links_pipes, expected_step.links_pipes, time_s, "pipe");
        compareMsxEntities(context, actual_step.links_pumps, expected_step.links_pumps, time_s, "pump");
        compareMsxEntities(context, actual_step.links_valves, expected_step.links_valves, time_s, "valve");
    }
}

// Runs a real reaction model through AOWIS's own EpanetRunner -- not the raw
// MSX toolkit directly, as above, but the actual translation path a caller
// of this adapter would use: NetworkHydraulic with multi_species attached,
// EpanetRunRequest::multi_species_run set, EpanetRunner::run(). Proves
// EpanetMsxProject's export -> MSXENopen/MSXopen/MSXusehydfile/MSXinit/MSXstep
// -> result-reading sequence produces real, non-trivial species
// concentrations, not just that the pieces individually work in isolation.
void scenarioMsxIntegrationEndToEnd(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const HydraulicNodeReservoir &source_node = network.nodes_reservoirs.first();

    MultiSpeciesSpecies chlorine;
    chlorine.id = QStringLiteral("CL2");
    chlorine.uuid = QUuid::createUuid();
    chlorine.type = MultiSpeciesSpeciesType::Bulk;
    chlorine.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(chlorine);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = chlorine.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = chlorine.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("-0.5*CL2");
    network.multi_species.reactions.append(tank_reaction);

    // A reservoir's water quality belongs in its initial-quality value, not
    // a SOURCES entry: EPANET's own documentation is explicit that CONCEN
    // sources are for modeling a booster injecting quality into external
    // (negative-demand) inflow at a node, not for setting what concentration
    // a reservoir already contains -- and the vendored EPANET-MSX example
    // (Examples/example.msx) sets its own reservoir's arsenic level the same
    // way, via [QUALITY] NODE, with no [SOURCES] entry at all.
    MultiSpeciesNodeInitialQuality initial_quality;
    initial_quality.node_uuid = source_node.uuid;
    initial_quality.species_uuid = chlorine.uuid;
    initial_quality.value = 1.0;
    network.multi_species.initial_quality_nodes.append(initial_quality);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "a network with a valid multi-species model must run successfully end to end");
    context.expect(result.result_timeline.status.success, "the normal hydraulic timeline must also succeed alongside the multi-species run");
    context.expect(result.multi_species_result.has_value(), "a request with multi_species_run set must populate multi_species_result");

    if (result.multi_species_result.has_value())
    {
        const EpanetMultiSpeciesResult &multi_species_result = result.multi_species_result.value();
        context.expect(multi_species_result.state == EpanetRunState::Success, "multi_species_result.state must be Success for a valid run");
        context.expect(multi_species_result.result_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "multi_species_result's timeline must be Valid");
        context.expect(multi_species_result.result_timeline.simulation_start_utc.isValid(), "multi_species_result must preserve a valid simulation_start_utc");
        context.expect(
            multi_species_result.result_timeline.simulation_start_utc == result.result_timeline.simulation_start_utc,
            "MSX and hydraulics must report the same simulation_start_utc for one public run");
        context.expect(!multi_species_result.result_timeline.results.isEmpty(), "multi_species_result must contain at least one timestep of results");

        bool found_species_value = false;
        bool found_nonzero_concentration = false;
        for (const MultiSpeciesSimulationResult &step : multi_species_result.result_timeline.results)
        {
            for (const MultiSpeciesSimulationResultNodeJunction &junction_result : step.nodes_junctions)
            {
                for (const MultiSpeciesResultValue &value : junction_result.species_values)
                {
                    found_species_value = true;
                    if (value.value > 0.0)
                        found_nonzero_concentration = true;
                }
            }
        }
        context.expect(found_species_value, "at least one junction result must carry a CL2 concentration value");
        context.expect(found_nonzero_concentration, "chlorine dosed at the reservoir must show up as a non-zero concentration somewhere in the network over the 24-hour run");
    }
}


void scenarioMsxKnownAnswerFractionalMicrogramDecay(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1;
    network.timestep_hydraulic_s = 1;
    network.timestep_quality_s = 1;
    network.timestep_report_s = 1;

    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (HydraulicNodeJunctionDemand &demand : junction.demands)
            demand.base_demand_m3_per_h = 0.0;
    }
    for (HydraulicLinkPump &pump : network.links_pumps)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.solver_method = MultiSpeciesSolverMethod::RungeKutta5;
    network.multi_species.options.timestep_s = 0.125;

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("UGDECAY");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = MultiSpeciesUnits::Micrograms;
    species.absolute_tolerance = 1.0e-10;
    species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(species);

    constexpr double rate_per_s = 0.69314718055994530942;

    MultiSpeciesConstant rate;
    rate.id = QStringLiteral("K");
    rate.uuid = QUuid::createUuid();
    rate.value = rate_per_s;
    network.multi_species.constants.append(rate);

    for (const MultiSpeciesReactionLocation location : {MultiSpeciesReactionLocation::Pipe, MultiSpeciesReactionLocation::Tank})
    {
        MultiSpeciesReaction reaction;
        reaction.uuid = QUuid::createUuid();
        reaction.species_uuid = species.uuid;
        reaction.location = location;
        reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
        reaction.expression = QStringLiteral("-K*UGDECAY");
        network.multi_species.reactions.append(reaction);
    }

    constexpr double initial_canonical_mg_per_l = 0.002;
    MultiSpeciesGlobalInitialQuality initial;
    initial.species_uuid = species.uuid;
    initial.value = initial_canonical_mg_per_l;
    network.multi_species.initial_quality_global.append(initial);

    const QUuid tank_uuid = network.nodes_tanks.first().uuid;

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "the analytical fractional-step MSX fixture must run successfully");
    context.expect(result.multi_species_result.has_value(), "the analytical fractional-step MSX fixture must return a multi-species result");
    if (!result.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &timeline = result.multi_species_result->result_timeline;
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the analytical fractional-step MSX timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(timeline.results.size()),
        std::int64_t{8},
        {-1, "quality", std::string(), "timesteps"},
        "a one-second run at a 0.125-second MSX timestep must produce exactly eight result steps");
    if (timeline.results.size() != 8)
        return;

    const AowisEpanetTests::NumericTolerance time_tolerance{1.0e-12, 0.0};
    const AowisEpanetTests::NumericTolerance concentration_tolerance{1.0e-9, 1.0e-6};

    for (qsizetype step_index = 0; step_index < timeline.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &step = timeline.results.at(step_index);
        const double expected_time_s = 0.125 * static_cast<double>(step_index + 1);
        context.expectNear(
            step.time_elapsed_s,
            expected_time_s,
            time_tolerance,
            {-1, "quality", "2", "time_elapsed_s"},
            "fractional MSX timesteps must survive the complete AOWIS execution path without integer truncation");

        bool tank_found = false;
        bool species_found = false;
        double actual_canonical_mg_per_l = 0.0;
        for (const MultiSpeciesSimulationResultNodeTank &tank : step.nodes_tanks)
        {
            if (tank.uuid != tank_uuid)
                continue;

            tank_found = true;
            species_found = findMsxSpeciesConcentration(
                tank.species_values,
                species.uuid,
                actual_canonical_mg_per_l);
            break;
        }

        context.expect(tank_found, "the analytical MSX result step must contain the fixture tank");
        context.expect(species_found, "the analytical MSX tank result must contain the microgram-backed species");
        if (!tank_found || !species_found)
            continue;

        const double expected_canonical_mg_per_l =
            initial_canonical_mg_per_l * std::exp(-rate_per_s * expected_time_s);
        context.expectNear(
            actual_canonical_mg_per_l,
            expected_canonical_mg_per_l,
            concentration_tolerance,
            {
                static_cast<std::int64_t>(expected_time_s * 1000.0),
                "tank",
                "2",
                "species_value_mg_per_l"
            },
            "MSX first-order tank decay must match the analytical solution after solver UG conversion and canonical mg/L result conversion");
    }
}



NetworkHydraulic parameterTermOverrideNetwork(bool pipe_override)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1;
    network.timestep_hydraulic_s = 1;
    network.timestep_quality_s = 1;
    network.timestep_report_s = 1;

    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (HydraulicNodeJunctionDemand &demand : junction.demands)
            demand.base_demand_m3_per_h = 0.0;
    }
    for (HydraulicLinkPump &pump : network.links_pumps)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.solver_method = MultiSpeciesSolverMethod::RungeKutta5;
    network.multi_species.options.timestep_s = 0.125;

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("PTERM");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = MultiSpeciesUnits::Milligrams;
    species.absolute_tolerance = 1.0e-12;
    species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(species);

    MultiSpeciesParameter parameter;
    parameter.id = QStringLiteral("K");
    parameter.uuid = QUuid::createUuid();
    parameter.default_value = 0.0;
    network.multi_species.parameters.append(parameter);

    MultiSpeciesTerm term;
    term.id = QStringLiteral("LOSS");
    term.uuid = QUuid::createUuid();
    term.expression = QStringLiteral("K*PTERM");
    network.multi_species.terms.append(term);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = species.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = pipe_override ? QStringLiteral("-LOSS") : QStringLiteral("0");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = species.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = pipe_override ? QStringLiteral("0") : QStringLiteral("-LOSS");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesGlobalInitialQuality initial;
    initial.species_uuid = species.uuid;
    initial.value = 1.0;
    network.multi_species.initial_quality_global.append(initial);

    const double half_life_rate_per_s = std::log(2.0);
    if (pipe_override)
    {
        MultiSpeciesParameterOverridePipe override_value;
        override_value.pipe_uuid = network.links_pipes.first().uuid;
        override_value.parameter_uuid = parameter.uuid;
        override_value.value = half_life_rate_per_s;
        network.multi_species.parameter_overrides_pipes.append(override_value);
    }
    else
    {
        MultiSpeciesParameterOverrideTank override_value;
        override_value.tank_uuid = network.nodes_tanks.first().uuid;
        override_value.parameter_uuid = parameter.uuid;
        override_value.value = half_life_rate_per_s;
        network.multi_species.parameter_overrides_tanks.append(override_value);
    }

    return network;
}



const MultiSpeciesSimulationResultNodeJunction *findMsxJunctionResult(
    const MultiSpeciesSimulationResult &step,
    const QString &id);

NetworkHydraulic fixedDispersionPecletNetwork(double peclet_limit)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1800;
    network.timestep_hydraulic_s = 300;
    network.timestep_quality_s = 60;
    network.timestep_report_s = 300;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.timestep_s = 60.0;
    network.multi_species.options.peclet_number_threshold = peclet_limit;

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("DISP");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = MultiSpeciesUnits::Milligrams;
    species.absolute_tolerance = 1.0e-10;
    species.relative_tolerance = 1.0e-10;
    species.longitudinal_dispersion_coefficient_m2_per_s = 500.0;
    network.multi_species.species.append(species);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = species.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = species.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesGlobalInitialQuality initial;
    initial.species_uuid = species.uuid;
    initial.value = 0.0;
    network.multi_species.initial_quality_global.append(initial);

    MultiSpeciesNodeSource source;
    source.node_uuid = network.nodes_junctions.first().uuid;
    source.species_uuid = species.uuid;
    source.type = MultiSpeciesSourceType::Setpoint;
    source.value = 1.0;
    network.multi_species.sources.append(source);

    return network;
}

void scenarioMsxFixedDispersionPecletRuntime(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic gated_network = fixedDispersionPecletNetwork(1.0);
    const NetworkHydraulic enabled_network = fixedDispersionPecletNetwork(1.0e9);
    const QString downstream_node_id = gated_network.nodes_junctions.at(1).id;

    EpanetRunRequest gated_request;
    gated_request.network = gated_network;
    gated_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest enabled_request;
    enabled_request.network = enabled_network;
    enabled_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun gated = EpanetRunner().run(gated_request);
    const EpanetResultRun enabled = EpanetRunner().run(enabled_request);

    context.expect(gated.status.success, "the Peclet-gated fixed-dispersion run must succeed");
    context.expect(enabled.status.success, "the Peclet-enabled fixed-dispersion run must succeed");
    context.expect(gated.multi_species_result.has_value(), "the Peclet-gated run must return MSX results");
    context.expect(enabled.multi_species_result.has_value(), "the Peclet-enabled run must return MSX results");
    if (!gated.multi_species_result.has_value() || !enabled.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &gated_timeline =
        gated.multi_species_result->result_timeline;
    const MultiSpeciesSimulationResultTimeline &enabled_timeline =
        enabled.multi_species_result->result_timeline;

    context.expect(
        gated_timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        "the Peclet-gated fixed-dispersion timeline must be Valid");
    context.expect(
        enabled_timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        "the Peclet-enabled fixed-dispersion timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(enabled_timeline.results.size()),
        static_cast<std::int64_t>(gated_timeline.results.size()),
        {-1, "quality", downstream_node_id.toStdString(), "dispersion.timesteps"});
    if (gated_timeline.results.isEmpty()
        || gated_timeline.results.size() != enabled_timeline.results.size())
    {
        return;
    }

    const MultiSpeciesSimulationResult &gated_final = gated_timeline.results.last();
    const MultiSpeciesSimulationResult &enabled_final = enabled_timeline.results.last();

    const MultiSpeciesSimulationResultNodeJunction *gated_downstream =
        findMsxJunctionResult(gated_final, downstream_node_id);
    const MultiSpeciesSimulationResultNodeJunction *enabled_downstream =
        findMsxJunctionResult(enabled_final, downstream_node_id);

    context.expect(
        gated_downstream != nullptr,
        "the Peclet-gated result must contain the downstream junction");
    context.expect(
        enabled_downstream != nullptr,
        "the Peclet-enabled result must contain the downstream junction");
    if (gated_downstream == nullptr || enabled_downstream == nullptr)
        return;

    context.expectEqual(
        static_cast<std::int64_t>(gated_downstream->species_values.size()),
        std::int64_t{1},
        {
            static_cast<std::int64_t>(gated_final.time_elapsed_s),
            "junction",
            downstream_node_id.toStdString(),
            "gated.species_values.size"
        });
    context.expectEqual(
        static_cast<std::int64_t>(enabled_downstream->species_values.size()),
        std::int64_t{1},
        {
            static_cast<std::int64_t>(enabled_final.time_elapsed_s),
            "junction",
            downstream_node_id.toStdString(),
            "enabled.species_values.size"
        });
    if (gated_downstream->species_values.size() != 1
        || enabled_downstream->species_values.size() != 1)
    {
        return;
    }

    const double gated_value = gated_downstream->species_values.first().value;
    const double enabled_value = enabled_downstream->species_values.first().value;

    context.expect(
        gated_value < 1.0e-6,
        "with PECLET=1, fixed dispersion in the long upstream pipe must be gated off before the advective front reaches the downstream node");
    context.expect(
        enabled_value > gated_value + 1.0e-4,
        "raising the Peclet threshold must activate the configured fixed longitudinal dispersion and measurably transport species upstream of the advective front");
    context.expect(
        enabled_value < 1.0,
        "the early downstream fixed-dispersion response must remain below the 1 mg/L source setpoint");
}

void scenarioMsxFormulaEquilibriumKnownAnswer(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1;
    network.timestep_hydraulic_s = 1;
    network.timestep_quality_s = 1;
    network.timestep_report_s = 1;

    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (HydraulicNodeJunctionDemand &demand : junction.demands)
            demand.base_demand_m3_per_h = 0.0;
    }
    for (HydraulicLinkPump &pump : network.links_pumps)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.solver_method = MultiSpeciesSolverMethod::RungeKutta5;
    network.multi_species.options.timestep_s = 0.25;

    MultiSpeciesSpecies rate_species;
    rate_species.id = QStringLiteral("A");
    rate_species.uuid = QUuid::createUuid();
    rate_species.type = MultiSpeciesSpeciesType::Bulk;
    rate_species.units = MultiSpeciesUnits::Milligrams;
    rate_species.absolute_tolerance = 1.0e-12;
    rate_species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(rate_species);

    MultiSpeciesSpecies formula_species;
    formula_species.id = QStringLiteral("B");
    formula_species.uuid = QUuid::createUuid();
    formula_species.type = MultiSpeciesSpeciesType::Bulk;
    formula_species.units = MultiSpeciesUnits::Milligrams;
    formula_species.absolute_tolerance = 1.0e-12;
    formula_species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(formula_species);

    MultiSpeciesSpecies equilibrium_species;
    equilibrium_species.id = QStringLiteral("C");
    equilibrium_species.uuid = QUuid::createUuid();
    equilibrium_species.type = MultiSpeciesSpeciesType::Bulk;
    equilibrium_species.units = MultiSpeciesUnits::Milligrams;
    equilibrium_species.absolute_tolerance = 1.0e-12;
    equilibrium_species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(equilibrium_species);

    for (const MultiSpeciesReactionLocation location
         : {MultiSpeciesReactionLocation::Pipe, MultiSpeciesReactionLocation::Tank})
    {
        MultiSpeciesReaction rate_reaction;
        rate_reaction.uuid = QUuid::createUuid();
        rate_reaction.species_uuid = rate_species.uuid;
        rate_reaction.location = location;
        rate_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
        rate_reaction.expression = QStringLiteral("0");
        network.multi_species.reactions.append(rate_reaction);

        MultiSpeciesReaction formula_reaction;
        formula_reaction.uuid = QUuid::createUuid();
        formula_reaction.species_uuid = formula_species.uuid;
        formula_reaction.location = location;
        formula_reaction.expression_type = MultiSpeciesReactionExpressionType::Formula;
        formula_reaction.expression = QStringLiteral("2*A");
        network.multi_species.reactions.append(formula_reaction);

        MultiSpeciesReaction equilibrium_reaction;
        equilibrium_reaction.uuid = QUuid::createUuid();
        equilibrium_reaction.species_uuid = equilibrium_species.uuid;
        equilibrium_reaction.location = location;
        equilibrium_reaction.expression_type = MultiSpeciesReactionExpressionType::Equilibrium;
        equilibrium_reaction.expression = QStringLiteral("A-C");
        network.multi_species.reactions.append(equilibrium_reaction);
    }

    MultiSpeciesGlobalInitialQuality initial;
    initial.species_uuid = rate_species.uuid;
    initial.value = 1.0;
    network.multi_species.initial_quality_global.append(initial);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "the MSX FORMULA/EQUIL known-answer fixture must run successfully");
    context.expect(result.multi_species_result.has_value(), "the MSX FORMULA/EQUIL known-answer fixture must return MSX results");
    if (!result.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &timeline =
        result.multi_species_result->result_timeline;
    context.expect(
        timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        "the MSX FORMULA/EQUIL known-answer timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(timeline.results.size()),
        std::int64_t{4},
        {-1, "quality", std::string(), "formula_equilibrium.timesteps"},
        "a one-second run at a 0.25-second MSX timestep must return four result steps");
    if (timeline.results.size() != 4)
        return;

    const MultiSpeciesSimulationResult &final_step = timeline.results.last();
    // EPANET-MSX's Newton solver builds its equilibrium Jacobian with a
    // fixed 1.0e-7 finite-difference perturbation. Keep this known-answer
    // assertion just above that backend numerical scale rather than imposing
    // ODE-species tolerances on the separate algebraic equilibrium solver.
    const AowisEpanetTests::NumericTolerance tolerance{2.0e-7, 0.0};

    const auto verify_values =
        [&](const QList<MultiSpeciesResultValue> &values,
            const std::string &entity_type,
            const std::string &entity_id)
        {
            double a_value = 0.0;
            double b_value = 0.0;
            double c_value = 0.0;

            const bool found_a = findMsxSpeciesConcentration(
                values,
                rate_species.uuid,
                a_value);
            const bool found_b = findMsxSpeciesConcentration(
                values,
                formula_species.uuid,
                b_value);
            const bool found_c = findMsxSpeciesConcentration(
                values,
                equilibrium_species.uuid,
                c_value);

            context.expect(found_a, "the FORMULA/EQUIL result must contain RATE species A");
            context.expect(found_b, "the FORMULA/EQUIL result must contain FORMULA species B");
            context.expect(found_c, "the FORMULA/EQUIL result must contain EQUIL species C");
            if (!found_a || !found_b || !found_c)
                return;

            context.expectNear(
                a_value,
                1.0,
                tolerance,
                {
                    static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                    entity_type,
                    entity_id,
                    "rate_species"
                },
                "RATE A=0 must preserve A=1");
            context.expectNear(
                b_value,
                2.0,
                tolerance,
                {
                    static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                    entity_type,
                    entity_id,
                    "formula_species"
                },
                "FORMULA B=2*A must evaluate to B=2");
            context.expectNear(
                c_value,
                1.0,
                tolerance,
                {
                    static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                    entity_type,
                    entity_id,
                    "equilibrium_species"
                },
                "EQUIL A-C=0 must solve to C=1");
        };

    bool found_tank = false;
    const QUuid tank_uuid = network.nodes_tanks.first().uuid;
    for (const MultiSpeciesSimulationResultNodeTank &tank : final_step.nodes_tanks)
    {
        if (tank.uuid != tank_uuid)
            continue;

        found_tank = true;
        verify_values(
            tank.species_values,
            "tank",
            network.nodes_tanks.first().id.toStdString());
        break;
    }
    context.expect(found_tank, "the FORMULA/EQUIL result must contain the fixture tank");

    bool found_pipe = false;
    const QUuid pipe_uuid = network.links_pipes.first().uuid;
    for (const MultiSpeciesSimulationResultLinkPipe &pipe : final_step.links_pipes)
    {
        if (pipe.uuid != pipe_uuid)
            continue;

        found_pipe = true;
        verify_values(
            pipe.species_values,
            "pipe",
            network.links_pipes.first().id.toStdString());
        break;
    }
    context.expect(found_pipe, "the FORMULA/EQUIL result must contain the fixture pipe");
}

void scenarioMsxParameterTermOverridesKnownAnswer(AowisEpanetTests::TestContext &context)
{
    const AowisEpanetTests::NumericTolerance concentration_tolerance{1.0e-8, 1.0e-6};

    {
        const NetworkHydraulic network = parameterTermOverrideNetwork(false);
        const QUuid species_uuid = network.multi_species.species.first().uuid;
        const QUuid tank_uuid = network.nodes_tanks.first().uuid;

        EpanetRunRequest request;
        request.network = network;
        request.multi_species_run = MultiSpeciesRunOptions{};

        const EpanetResultRun result = EpanetRunner().run(request);
        context.expect(result.status.success, "the tank parameter/TERM override fixture must run successfully");
        context.expect(result.multi_species_result.has_value(), "the tank parameter/TERM override fixture must return MSX results");
        if (result.multi_species_result.has_value())
        {
            const MultiSpeciesSimulationResultTimeline &timeline =
                result.multi_species_result->result_timeline;
            context.expect(
                timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
                "the tank parameter/TERM override timeline must be Valid");
            context.expect(!timeline.results.isEmpty(), "the tank parameter/TERM override timeline must contain results");

            if (!timeline.results.isEmpty())
            {
                const MultiSpeciesSimulationResult &final_step = timeline.results.last();
                bool found_tank = false;
                bool found_species = false;
                double actual_value = 0.0;

                for (const MultiSpeciesSimulationResultNodeTank &tank : final_step.nodes_tanks)
                {
                    if (tank.uuid != tank_uuid)
                        continue;

                    found_tank = true;
                    found_species = findMsxSpeciesConcentration(
                        tank.species_values,
                        species_uuid,
                        actual_value);
                    break;
                }

                context.expect(found_tank, "the tank parameter/TERM result must contain the overridden tank");
                context.expect(found_species, "the tank parameter/TERM result must contain the fixture species");
                if (found_tank && found_species)
                {
                    context.expectNear(
                        actual_value,
                        0.5,
                        concentration_tolerance,
                        {
                            static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                            "tank",
                            network.nodes_tanks.first().id.toStdString(),
                            "parameter_term_override"
                        },
                        "a tank K override of ln(2)/s used through TERM LOSS=K*PTERM must produce an exact one-second half-life");
                }
            }
        }
    }

    {
        const NetworkHydraulic network = parameterTermOverrideNetwork(true);
        const QUuid species_uuid = network.multi_species.species.first().uuid;
        const QUuid overridden_pipe_uuid = network.links_pipes.first().uuid;
        const QUuid default_pipe_uuid = network.links_pipes.at(1).uuid;

        EpanetRunRequest request;
        request.network = network;
        request.multi_species_run = MultiSpeciesRunOptions{};

        const EpanetResultRun result = EpanetRunner().run(request);
        context.expect(result.status.success, "the pipe parameter/TERM override fixture must run successfully");
        context.expect(result.multi_species_result.has_value(), "the pipe parameter/TERM override fixture must return MSX results");
        if (!result.multi_species_result.has_value())
            return;

        const MultiSpeciesSimulationResultTimeline &timeline =
            result.multi_species_result->result_timeline;
        context.expect(
            timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
            "the pipe parameter/TERM override timeline must be Valid");
        context.expect(!timeline.results.isEmpty(), "the pipe parameter/TERM override timeline must contain results");
        if (timeline.results.isEmpty())
            return;

        const MultiSpeciesSimulationResult &final_step = timeline.results.last();
        bool found_overridden_pipe = false;
        bool found_default_pipe = false;
        bool found_overridden_species = false;
        bool found_default_species = false;
        double overridden_value = 0.0;
        double default_value = 0.0;

        for (const MultiSpeciesSimulationResultLinkPipe &pipe : final_step.links_pipes)
        {
            if (pipe.uuid == overridden_pipe_uuid)
            {
                found_overridden_pipe = true;
                found_overridden_species = findMsxSpeciesConcentration(
                    pipe.species_values,
                    species_uuid,
                    overridden_value);
            }
            else if (pipe.uuid == default_pipe_uuid)
            {
                found_default_pipe = true;
                found_default_species = findMsxSpeciesConcentration(
                    pipe.species_values,
                    species_uuid,
                    default_value);
            }
        }

        context.expect(found_overridden_pipe, "the pipe parameter/TERM result must contain the overridden pipe");
        context.expect(found_default_pipe, "the pipe parameter/TERM result must contain an unoverridden comparison pipe");
        context.expect(found_overridden_species, "the overridden pipe must contain the fixture species");
        context.expect(found_default_species, "the comparison pipe must contain the fixture species");

        if (found_overridden_species)
        {
            context.expectNear(
                overridden_value,
                0.5,
                concentration_tolerance,
                {
                    static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                    "pipe",
                    network.links_pipes.first().id.toStdString(),
                    "parameter_term_override"
                },
                "a pipe K override of ln(2)/s used through TERM LOSS=K*PTERM must produce an exact one-second half-life");
        }

        if (found_default_species)
        {
            context.expectNear(
                default_value,
                1.0,
                concentration_tolerance,
                {
                    static_cast<std::int64_t>(final_step.time_elapsed_s * 1000.0),
                    "pipe",
                    network.links_pipes.at(1).id.toStdString(),
                    "parameter_default_value"
                },
                "an unoverridden pipe must retain the parameter default K=0 and therefore preserve its initial concentration");
        }
    }
}

void scenarioMsxMassBalanceFinalSummary(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1;
    network.timestep_hydraulic_s = 1;
    network.timestep_quality_s = 1;
    network.timestep_report_s = 1;

    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (HydraulicNodeJunctionDemand &demand : junction.demands)
            demand.base_demand_m3_per_h = 0.0;
    }
    for (HydraulicLinkPump &pump : network.links_pumps)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.solver_method = MultiSpeciesSolverMethod::RungeKutta5;
    network.multi_species.options.timestep_s = 0.25;

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("MB");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = MultiSpeciesUnits::Milligrams;
    species.absolute_tolerance = 1.0e-10;
    species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(species);

    for (const MultiSpeciesReactionLocation location
         : {MultiSpeciesReactionLocation::Pipe, MultiSpeciesReactionLocation::Tank})
    {
        MultiSpeciesReaction reaction;
        reaction.uuid = QUuid::createUuid();
        reaction.species_uuid = species.uuid;
        reaction.location = location;
        reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
        reaction.expression = QStringLiteral("-0.5*MB");
        network.multi_species.reactions.append(reaction);
    }

    MultiSpeciesGlobalInitialQuality initial;
    initial.species_uuid = species.uuid;
    initial.value = 1.0;
    network.multi_species.initial_quality_global.append(initial);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "the MSX mass-balance fixture must run successfully");
    context.expect(result.multi_species_result.has_value(), "the MSX mass-balance fixture must return a multi-species result");
    if (!result.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &timeline =
        result.multi_species_result->result_timeline;
    context.expect(
        timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        "the MSX mass-balance timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(timeline.results.size()),
        std::int64_t{4},
        {-1, "quality", std::string(), "mass_balance.timesteps"},
        "a one-second run at a 0.25-second MSX timestep must return four result steps");
    if (timeline.results.size() != 4)
        return;

    for (qsizetype index = 0; index + 1 < timeline.results.size(); index++)
    {
        context.expect(
            timeline.results.at(index).statistics.mass_balance_ratios.isEmpty(),
            "MSX full-run mass-balance ratios must not be exposed on intermediate timesteps");
    }

    const MultiSpeciesSimulationResult &final_step = timeline.results.last();
    context.expectEqual(
        static_cast<std::int64_t>(final_step.statistics.mass_balance_ratios.size()),
        std::int64_t{1},
        {static_cast<std::int64_t>(final_step.time_elapsed_s), "quality", std::string(), "mass_balance_ratios.size"},
        "the final MSX result must contain one mass-balance ratio for the selected species");
    if (final_step.statistics.mass_balance_ratios.size() != 1)
        return;

    const MultiSpeciesMassBalanceRatio &mass_balance =
        final_step.statistics.mass_balance_ratios.first();
    context.expect(
        mass_balance.species_uuid == species.uuid,
        "the final MSX mass-balance ratio must map back to the AOWIS species UUID");
    context.expect(
        std::isfinite(mass_balance.ratio),
        "the final MSX mass-balance ratio must be finite");
    context.expectNear(
        mass_balance.ratio,
        1.0,
        AowisEpanetTests::NumericTolerance{1.0e-6, 1.0e-6},
        {
            static_cast<std::int64_t>(final_step.time_elapsed_s),
            "quality",
            species.id.toStdString(),
            "mass_balance_ratio"
        },
        "MSX must close the reacting species mass balance across initial, reacted, and final stored mass");
}

void scenarioMsxWallSpeciesPipeOnlyResults(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1;
    network.timestep_hydraulic_s = 1;
    network.timestep_quality_s = 1;
    network.timestep_report_s = 1;

    for (HydraulicNodeJunction &junction : network.nodes_junctions)
    {
        for (HydraulicNodeJunctionDemand &demand : junction.demands)
            demand.base_demand_m3_per_h = 0.0;
    }
    for (HydraulicLinkPump &pump : network.links_pumps)
        pump.initial_status = HydraulicLinkPumpInitialStatus::Off;

    network.multi_species.options.area_units = MultiSpeciesAreaUnits::SquareCentimetres;
    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Seconds;
    network.multi_species.options.timestep_s = 1.0;

    MultiSpeciesSpecies bulk_species;
    bulk_species.id = QStringLiteral("BULKMG");
    bulk_species.uuid = QUuid::createUuid();
    bulk_species.type = MultiSpeciesSpeciesType::Bulk;
    bulk_species.units = MultiSpeciesUnits::Milligrams;
    network.multi_species.species.append(bulk_species);

    MultiSpeciesSpecies wall_species;
    wall_species.id = QStringLiteral("WALLMMOL");
    wall_species.uuid = QUuid::createUuid();
    wall_species.type = MultiSpeciesSpeciesType::Wall;
    wall_species.units = MultiSpeciesUnits::Millimoles;
    network.multi_species.species.append(wall_species);

    // EPANET-MSX requires pipe chemistry to define one equation for every
    // species, while tank chemistry must define equations for BULK species
    // only. These zero-rate equations keep this fixture chemically inert so
    // it tests result-domain and canonical-unit semantics in isolation.
    MultiSpeciesReaction bulk_pipe_reaction;
    bulk_pipe_reaction.uuid = QUuid::createUuid();
    bulk_pipe_reaction.species_uuid = bulk_species.uuid;
    bulk_pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    bulk_pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    bulk_pipe_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(bulk_pipe_reaction);

    MultiSpeciesReaction wall_pipe_reaction;
    wall_pipe_reaction.uuid = QUuid::createUuid();
    wall_pipe_reaction.species_uuid = wall_species.uuid;
    wall_pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    wall_pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    wall_pipe_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(wall_pipe_reaction);

    MultiSpeciesReaction bulk_tank_reaction;
    bulk_tank_reaction.uuid = QUuid::createUuid();
    bulk_tank_reaction.species_uuid = bulk_species.uuid;
    bulk_tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    bulk_tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    bulk_tank_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(bulk_tank_reaction);

    MultiSpeciesGlobalInitialQuality bulk_initial;
    bulk_initial.species_uuid = bulk_species.uuid;
    bulk_initial.value = 1.25;
    network.multi_species.initial_quality_global.append(bulk_initial);

    const HydraulicLinkPipe &fixture_pipe = network.links_pipes.first();
    constexpr double wall_initial_mmol_per_m2 = 2.5;
    MultiSpeciesPipeInitialQuality wall_initial;
    wall_initial.pipe_uuid = fixture_pipe.uuid;
    wall_initial.species_uuid = wall_species.uuid;
    wall_initial.value = wall_initial_mmol_per_m2;
    network.multi_species.initial_quality_pipes.append(wall_initial);

    EpanetRunRequest request;
    request.network = network;
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);
    context.expect(result.status.success, "the wall-species result-domain fixture must run successfully");
    context.expect(result.multi_species_result.has_value(), "the wall-species result-domain fixture must return a multi-species result");
    if (!result.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &timeline = result.multi_species_result->result_timeline;
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the wall-species result-domain timeline must be Valid");
    context.expect(!timeline.results.isEmpty(), "the wall-species result-domain fixture must produce at least one result step");
    if (timeline.results.isEmpty())
        return;

    const MultiSpeciesSimulationResult &step = timeline.results.first();
    double ignored_value = 0.0;

    for (const MultiSpeciesSimulationResultNodeJunction &junction : step.nodes_junctions)
    {
        context.expect(
            !findMsxSpeciesConcentration(junction.species_values, wall_species.uuid, ignored_value),
            "junction results must not expose EPANET-MSX's synthetic zero for a WALL species");
        context.expect(
            findMsxSpeciesConcentration(junction.species_values, bulk_species.uuid, ignored_value),
            "junction results must continue to expose BULK species");
    }

    for (const MultiSpeciesSimulationResultNodeReservoir &reservoir : step.nodes_reservoirs)
    {
        context.expect(
            !findMsxSpeciesConcentration(reservoir.species_values, wall_species.uuid, ignored_value),
            "reservoir results must not expose a WALL species because nodes have no pipe-wall surface");
    }

    for (const MultiSpeciesSimulationResultNodeTank &tank : step.nodes_tanks)
    {
        context.expect(
            !findMsxSpeciesConcentration(tank.species_values, wall_species.uuid, ignored_value),
            "tank results must not expose a WALL species because MSX node-quality access returns a synthetic zero");
    }

    for (const MultiSpeciesSimulationResultLinkPump &pump : step.links_pumps)
    {
        context.expect(
            !findMsxSpeciesConcentration(pump.species_values, wall_species.uuid, ignored_value),
            "pump results must not expose a pipe-wall species on a zero-volume non-pipe link");
    }

    for (const MultiSpeciesSimulationResultLinkValve &valve : step.links_valves)
    {
        context.expect(
            !findMsxSpeciesConcentration(valve.species_values, wall_species.uuid, ignored_value),
            "valve results must not expose a pipe-wall species on a zero-volume non-pipe link");
    }

    bool fixture_pipe_found = false;
    bool wall_species_found = false;
    double actual_wall_mmol_per_m2 = 0.0;
    for (const MultiSpeciesSimulationResultLinkPipe &pipe : step.links_pipes)
    {
        if (pipe.uuid != fixture_pipe.uuid)
            continue;

        fixture_pipe_found = true;
        wall_species_found = findMsxSpeciesConcentration(
            pipe.species_values,
            wall_species.uuid,
            actual_wall_mmol_per_m2);
        break;
    }

    context.expect(fixture_pipe_found, "the wall-species result-domain fixture must return the configured pipe");
    context.expect(wall_species_found, "actual pipe results must expose WALL species");
    if (wall_species_found)
    {
        context.expectNear(
            actual_wall_mmol_per_m2,
            wall_initial_mmol_per_m2,
            AowisEpanetTests::NumericTolerance{1.0e-10, 1.0e-8},
            {-1, "pipe", fixture_pipe.id.toStdString(), "wall_species_value_mmol_per_m2"},
            "WALL species must round-trip from canonical mmol/m2 through MSX CM2 units and back to canonical AOWIS units");
    }
}

void scenarioMsxBackendDiagnostics(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    addSimpleChlorineMsxModel(network);

    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    context.expect(status.success, "MSX backend-diagnostic fixture must prepare successfully");
    if (!status.success)
        return;

    EpanetMultiQualityRunExecutor hydraulic_executor(prepared_project, true);
    EpanetResultRun hydraulic_result;
    hydraulic_result = hydraulic_executor.run(std::move(hydraulic_result));
    context.expect(hydraulic_result.result_timeline.status.success, "MSX backend-diagnostic fixture hydraulics must succeed");
    context.expect(hydraulic_executor.hasHydraulicFile(), "MSX backend-diagnostic fixture must persist hydraulics");
    if (!hydraulic_result.result_timeline.status.success || !hydraulic_executor.hasHydraulicFile())
        return;

    QFile hydraulic_file(hydraulic_executor.hydraulicFilePath());
    const bool opened = hydraulic_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    context.expect(opened, "MSX backend-diagnostic fixture must be able to corrupt the saved hydraulic file");
    if (!opened)
        return;
    hydraulic_file.write("not an EPANET hydraulic file");
    hydraulic_file.close();

    EpanetMsxProject msx_project;
    MultiSpeciesSimulationResultTimeline timeline;
    bool cancelled = false;
    status = msx_project.run(
        network,
        MultiSpeciesRunOptions{},
        hydraulic_executor.hydraulicInpText(),
        hydraulic_executor.hydraulicFilePath(),
        timeline,
        std::function<bool()>(),
        cancelled);

    context.expect(!status.success, "corrupted hydraulics must fail the MSX backend run");
    context.expect(!cancelled, "a backend failure must not be reported as cancellation");
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Invalid, "MSX failure before the first usable result must be Invalid");
    context.expect(!timeline.status.success, "failed MSX timeline must expose a failed status");
    context.expect(timeline.status.backend_name == QStringLiteral("EPANET-MSX"), "MSX failure status must identify EPANET-MSX as the backend");
    context.expect(timeline.status.backend_error_code != 0, "MSX failure status must preserve the backend error code");
    context.expect(timeline.status.backend_operation == QStringLiteral("MSXusehydfile"), "corrupted hydraulics must identify MSXusehydfile as the failing backend operation");
    context.expect(!timeline.status.message_backend.isEmpty(), "MSX failure status must preserve the backend error text");
    context.expect(timeline.status.stage == HydraulicSimulationStatusStage::RunQuality, "MSX hydraulic-file failure must be classified in the RunQuality stage");
    context.expect(timeline.status.operation == HydraulicSimulationStatusOperation::RunMultiSpecies, "MSX hydraulic-file failure must identify RunMultiSpecies");
    context.expect(!timeline.diagnostics.isEmpty(), "failed MSX timeline must carry a structured diagnostic");

    if (!timeline.diagnostics.isEmpty())
    {
        const HydraulicSimulationDiagnostic &diagnostic = timeline.diagnostics.first();
        context.expect(diagnostic.backend_name == timeline.status.backend_name, "MSX diagnostic must preserve backend identity from the failure status");
        context.expect(diagnostic.backend_error_code == timeline.status.backend_error_code, "MSX diagnostic must preserve the backend error code");
        context.expect(diagnostic.backend_operation == timeline.status.backend_operation, "MSX diagnostic must preserve the backend operation");
        context.expect(diagnostic.message_backend == timeline.status.message_backend, "MSX diagnostic must preserve the backend error text");
        context.expect(diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Fatal, "MSX hydraulic-file failure must be a fatal child diagnostic");
    }
}

void scenarioMsxCancellationPartial(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    addSimpleChlorineMsxModel(network);

    EpanetPreparedProject prepared_project;
    HydraulicSimulationStatus status = prepared_project.prepare(network);
    context.expect(status.success, "MSX cancellation fixture must prepare successfully");
    if (!status.success)
        return;

    EpanetMultiQualityRunExecutor hydraulic_executor(prepared_project, true);
    EpanetResultRun hydraulic_result;
    hydraulic_result = hydraulic_executor.run(std::move(hydraulic_result));
    context.expect(hydraulic_result.result_timeline.status.success, "MSX cancellation fixture hydraulics must succeed");
    context.expect(hydraulic_executor.hasHydraulicFile(), "MSX cancellation fixture must persist hydraulics");
    if (!hydraulic_result.result_timeline.status.success || !hydraulic_executor.hasHydraulicFile())
        return;

    EpanetMsxProject msx_project;
    MultiSpeciesSimulationResultTimeline timeline;
    const QDateTime expected_start_utc = QDateTime::fromMSecsSinceEpoch(1788379200000LL, QTimeZone::UTC);
    timeline.simulation_start_utc = expected_start_utc;

    int cancellation_checks = 0;
    const std::function<bool()> cancel_after_one_completed_step = [&cancellation_checks]()
    {
        cancellation_checks++;
        return cancellation_checks >= 5;
    };

    bool cancelled = false;
    status = msx_project.run(
        network,
        MultiSpeciesRunOptions{},
        hydraulic_executor.hydraulicInpText(),
        hydraulic_executor.hydraulicFilePath(),
        timeline,
        cancel_after_one_completed_step,
        cancelled);

    context.expect(status.success, "MSX cancellation is not a backend failure");
    context.expect(cancelled, "MSX project must report deterministic mid-run cancellation");
    context.expect(cancellation_checks >= 5, "MSX cancellation callback must be polled during stepping");
    context.expect(timeline.validity == MultiSpeciesSimulationResultValidity::Partial, "MSX cancellation after a completed timestep must preserve Partial validity");
    context.expect(!timeline.results.isEmpty(), "MSX cancellation after stepping must preserve completed timesteps");
    context.expect(timeline.simulation_start_utc == expected_start_utc, "MSX run must preserve the caller-provided simulation_start_utc");
    context.expect(timeline.status.success, "cancelled MSX timeline status must remain successful rather than inventing a backend failure");
}

void scenarioMsxOutputSpeciesFilterPreservesCoupledChemistry(AowisEpanetTests::TestContext &context)
{
    NetworkHydraulic network = cleanNet1();
    const std::pair<QUuid, QUuid> species = addCoupledTwoSpeciesMsxModel(network);

    EpanetRunRequest full_request;
    full_request.network = network;
    full_request.multi_species_run = MultiSpeciesRunOptions{};

    MultiSpeciesRunOptions filtered_options;
    filtered_options.output_species_uuids.append(species.first);

    EpanetRunRequest filtered_request;
    filtered_request.network = network;
    filtered_request.multi_species_run = filtered_options;

    const EpanetResultRun full = EpanetRunner().run(full_request);
    const EpanetResultRun filtered = EpanetRunner().run(filtered_request);

    context.expect(full.status.success, "the full coupled two-species MSX reference run must succeed");
    context.expect(filtered.status.success, "selecting one output species must not break coupled MSX chemistry");
    context.expect(full.multi_species_result.has_value(), "the full coupled MSX run must return multi-species results");
    context.expect(filtered.multi_species_result.has_value(), "the filtered coupled MSX run must return multi-species results");
    if (!full.multi_species_result.has_value() || !filtered.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &full_timeline = full.multi_species_result->result_timeline;
    const MultiSpeciesSimulationResultTimeline &filtered_timeline = filtered.multi_species_result->result_timeline;
    context.expect(full_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the full coupled MSX timeline must be Valid");
    context.expect(filtered_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the filtered coupled MSX timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(filtered_timeline.results.size()),
        static_cast<std::int64_t>(full_timeline.results.size()),
        {-1, "quality", std::string(), "timesteps"},
        "output filtering must not alter the MSX timestep sequence");
    if (filtered_timeline.results.size() != full_timeline.results.size())
        return;

    bool full_result_contains_unrequested_species = false;
    for (qsizetype step_index = 0; step_index < filtered_timeline.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &filtered_step = filtered_timeline.results.at(step_index);
        const MultiSpeciesSimulationResult &full_step = full_timeline.results.at(step_index);
        const std::int64_t time_s = static_cast<std::int64_t>(filtered_step.time_elapsed_s);
        context.expectEqual(
            time_s,
            static_cast<std::int64_t>(full_step.time_elapsed_s),
            {time_s, "quality", std::string(), "time_elapsed_s"},
            "output filtering must not alter MSX simulation times");

        compareFilteredMsxEntities(context, filtered_step.nodes_junctions, full_step.nodes_junctions, species.first, time_s, "junction");
        compareFilteredMsxEntities(context, filtered_step.nodes_reservoirs, full_step.nodes_reservoirs, species.first, time_s, "reservoir");
        compareFilteredMsxEntities(context, filtered_step.nodes_tanks, full_step.nodes_tanks, species.first, time_s, "tank");
        compareFilteredMsxEntities(context, filtered_step.links_pipes, full_step.links_pipes, species.first, time_s, "pipe");
        compareFilteredMsxEntities(context, filtered_step.links_pumps, full_step.links_pumps, species.first, time_s, "pump");
        compareFilteredMsxEntities(context, filtered_step.links_valves, full_step.links_valves, species.first, time_s, "valve");

        for (const MultiSpeciesSimulationResultNodeJunction &junction : full_step.nodes_junctions)
        {
            double concentration = 0.0;
            if (findMsxSpeciesConcentration(junction.species_values, species.second, concentration))
            {
                full_result_contains_unrequested_species = true;
                break;
            }
        }
    }

    context.expect(
        full_result_contains_unrequested_species,
        "the unfiltered reference must prove the coupled species was genuinely solved and available as output");
}


NetworkHydraulic patternedMassSourceNetwork(
    MultiSpeciesUnits units,
    double pattern_multiplier)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 3600;
    network.timestep_hydraulic_s = 900;
    network.timestep_quality_s = 300;
    network.timestep_report_s = 900;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Minutes;
    network.multi_species.options.timestep_s = 300.0;

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("SOURCE");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = units;
    species.absolute_tolerance = 1.0e-10;
    species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(species);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = species.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = species.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesPattern pattern;
    pattern.id = QStringLiteral("BOOST");
    pattern.uuid = QUuid::createUuid();
    pattern.multipliers = {pattern_multiplier};
    network.multi_species.patterns.append(pattern);

    MultiSpeciesNodeSource source;
    source.node_uuid = network.nodes_junctions.at(1).uuid;
    source.species_uuid = species.uuid;
    source.type = MultiSpeciesSourceType::Mass;
    source.value = 60.0;
    source.pattern_uuid = pattern.uuid;
    network.multi_species.sources.append(source);

    return network;
}

const MultiSpeciesSimulationResultNodeJunction *findMsxJunctionResult(
    const MultiSpeciesSimulationResult &step,
    const QString &id)
{
    for (const MultiSpeciesSimulationResultNodeJunction &junction : step.nodes_junctions)
    {
        if (junction.id == id)
            return &junction;
    }
    return nullptr;
}

void scenarioMsxPatternedMassSourceCanonicalUnits(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic full_mg_network = patternedMassSourceNetwork(
        MultiSpeciesUnits::Milligrams,
        1.0);
    const NetworkHydraulic half_mg_network = patternedMassSourceNetwork(
        MultiSpeciesUnits::Milligrams,
        0.5);
    const NetworkHydraulic half_ug_network = patternedMassSourceNetwork(
        MultiSpeciesUnits::Micrograms,
        0.5);

    const QString source_node_id = full_mg_network.nodes_junctions.at(1).id;

    EpanetRunRequest full_mg_request;
    full_mg_request.network = full_mg_network;
    full_mg_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest half_mg_request;
    half_mg_request.network = half_mg_network;
    half_mg_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest half_ug_request;
    half_ug_request.network = half_ug_network;
    half_ug_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun full_mg = EpanetRunner().run(full_mg_request);
    const EpanetResultRun half_mg = EpanetRunner().run(half_mg_request);
    const EpanetResultRun half_ug = EpanetRunner().run(half_ug_request);

    context.expect(full_mg.status.success, "the full-strength MG patterned MASS-source run must succeed");
    context.expect(half_mg.status.success, "the half-strength MG patterned MASS-source run must succeed");
    context.expect(half_ug.status.success, "the half-strength UG patterned MASS-source run must succeed");
    context.expect(full_mg.multi_species_result.has_value(), "the full-strength MG run must return MSX results");
    context.expect(half_mg.multi_species_result.has_value(), "the half-strength MG run must return MSX results");
    context.expect(half_ug.multi_species_result.has_value(), "the half-strength UG run must return MSX results");
    if (!full_mg.multi_species_result.has_value()
        || !half_mg.multi_species_result.has_value()
        || !half_ug.multi_species_result.has_value())
    {
        return;
    }

    const MultiSpeciesSimulationResultTimeline &full_mg_timeline = full_mg.multi_species_result->result_timeline;
    const MultiSpeciesSimulationResultTimeline &half_mg_timeline = half_mg.multi_species_result->result_timeline;
    const MultiSpeciesSimulationResultTimeline &half_ug_timeline = half_ug.multi_species_result->result_timeline;

    context.expect(full_mg_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the full-strength MG source timeline must be Valid");
    context.expect(half_mg_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the half-strength MG source timeline must be Valid");
    context.expect(half_ug_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "the half-strength UG source timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(half_mg_timeline.results.size()),
        static_cast<std::int64_t>(full_mg_timeline.results.size()),
        {-1, "quality", std::string(), "patterned_source.timesteps"});
    context.expectEqual(
        static_cast<std::int64_t>(half_ug_timeline.results.size()),
        static_cast<std::int64_t>(half_mg_timeline.results.size()),
        {-1, "quality", std::string(), "canonical_source.timesteps"});
    if (full_mg_timeline.results.size() != half_mg_timeline.results.size()
        || half_mg_timeline.results.size() != half_ug_timeline.results.size())
    {
        return;
    }

    bool found_positive_source_value = false;
    for (qsizetype step_index = 0; step_index < full_mg_timeline.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &full_mg_step = full_mg_timeline.results.at(step_index);
        const MultiSpeciesSimulationResult &half_mg_step = half_mg_timeline.results.at(step_index);
        const MultiSpeciesSimulationResult &half_ug_step = half_ug_timeline.results.at(step_index);

        context.expectNear(
            half_mg_step.time_elapsed_s,
            full_mg_step.time_elapsed_s,
            AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
            {-1, "quality", std::string(), "patterned_source.time_elapsed_s"});
        context.expectNear(
            half_ug_step.time_elapsed_s,
            half_mg_step.time_elapsed_s,
            AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
            {-1, "quality", std::string(), "canonical_source.time_elapsed_s"});

        const MultiSpeciesSimulationResultNodeJunction *full_mg_source =
            findMsxJunctionResult(full_mg_step, source_node_id);
        const MultiSpeciesSimulationResultNodeJunction *half_mg_source =
            findMsxJunctionResult(half_mg_step, source_node_id);
        const MultiSpeciesSimulationResultNodeJunction *half_ug_source =
            findMsxJunctionResult(half_ug_step, source_node_id);

        context.expect(full_mg_source != nullptr, "the full-strength source node must be present in MSX results");
        context.expect(half_mg_source != nullptr, "the half-strength MG source node must be present in MSX results");
        context.expect(half_ug_source != nullptr, "the half-strength UG source node must be present in MSX results");
        if (full_mg_source == nullptr || half_mg_source == nullptr || half_ug_source == nullptr)
            continue;

        context.expectEqual(
            static_cast<std::int64_t>(full_mg_source->species_values.size()),
            std::int64_t{1},
            {static_cast<std::int64_t>(full_mg_step.time_elapsed_s), "junction", source_node_id.toStdString(), "species_values.size"});
        context.expectEqual(
            static_cast<std::int64_t>(half_mg_source->species_values.size()),
            std::int64_t{1},
            {static_cast<std::int64_t>(half_mg_step.time_elapsed_s), "junction", source_node_id.toStdString(), "species_values.size"});
        context.expectEqual(
            static_cast<std::int64_t>(half_ug_source->species_values.size()),
            std::int64_t{1},
            {static_cast<std::int64_t>(half_ug_step.time_elapsed_s), "junction", source_node_id.toStdString(), "species_values.size"});
        if (full_mg_source->species_values.size() != 1
            || half_mg_source->species_values.size() != 1
            || half_ug_source->species_values.size() != 1)
        {
            continue;
        }

        const double full_mg_value = full_mg_source->species_values.first().value;
        const double half_mg_value = half_mg_source->species_values.first().value;
        const double half_ug_value = half_ug_source->species_values.first().value;

        if (full_mg_value > 1.0e-12)
            found_positive_source_value = true;

        context.expectNear(
            half_mg_value,
            full_mg_value * 0.5,
            AowisEpanetTests::NumericTolerance{1.0e-8, 1.0e-7},
            {
                static_cast<std::int64_t>(full_mg_step.time_elapsed_s),
                "junction",
                source_node_id.toStdString(),
                "patterned_mass_source_half_scale"
            });
        context.expectNear(
            half_ug_value,
            half_mg_value,
            AowisEpanetTests::NumericTolerance{1.0e-8, 1.0e-7},
            {
                static_cast<std::int64_t>(half_mg_step.time_elapsed_s),
                "junction",
                source_node_id.toStdString(),
                "patterned_mass_source_canonical_units"
            });
    }

    context.expect(
        found_positive_source_value,
        "the patterned MASS source must produce a positive canonical concentration at its source node");
}


NetworkHydraulic nonMassSourceNetwork(
    MultiSpeciesUnits units,
    MultiSpeciesSourceType source_type,
    bool negative_source_demand)
{
    NetworkHydraulic network = cleanNet1();
    network.duration_s = 1800;
    network.timestep_hydraulic_s = 300;
    network.timestep_quality_s = 60;
    network.timestep_report_s = 300;

    network.multi_species.options.rate_units = MultiSpeciesRateUnits::Minutes;
    network.multi_species.options.timestep_s = 60.0;

    const qsizetype source_node_index = 1;
    if (negative_source_demand)
    {
        HydraulicNodeJunction &source_node = network.nodes_junctions[source_node_index];
        if (!source_node.demands.isEmpty())
            source_node.demands[0].base_demand_m3_per_h = -100.0;
    }

    MultiSpeciesSpecies species;
    species.id = QStringLiteral("SOURCE");
    species.uuid = QUuid::createUuid();
    species.type = MultiSpeciesSpeciesType::Bulk;
    species.units = units;
    species.absolute_tolerance = 1.0e-10;
    species.relative_tolerance = 1.0e-10;
    network.multi_species.species.append(species);

    MultiSpeciesReaction pipe_reaction;
    pipe_reaction.uuid = QUuid::createUuid();
    pipe_reaction.species_uuid = species.uuid;
    pipe_reaction.location = MultiSpeciesReactionLocation::Pipe;
    pipe_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    pipe_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(pipe_reaction);

    MultiSpeciesReaction tank_reaction;
    tank_reaction.uuid = QUuid::createUuid();
    tank_reaction.species_uuid = species.uuid;
    tank_reaction.location = MultiSpeciesReactionLocation::Tank;
    tank_reaction.expression_type = MultiSpeciesReactionExpressionType::Rate;
    tank_reaction.expression = QStringLiteral("0");
    network.multi_species.reactions.append(tank_reaction);

    MultiSpeciesNodeSource source;
    source.node_uuid = network.nodes_junctions.at(source_node_index).uuid;
    source.species_uuid = species.uuid;
    source.type = source_type;
    source.value = 1.25;
    network.multi_species.sources.append(source);

    return network;
}

void compareNonMassSourceCanonicalUnits(
    AowisEpanetTests::TestContext &context,
    MultiSpeciesSourceType source_type,
    bool negative_source_demand,
    bool expect_positive,
    const std::string &case_name)
{
    const NetworkHydraulic mg_network =
        nonMassSourceNetwork(MultiSpeciesUnits::Milligrams, source_type, negative_source_demand);
    const NetworkHydraulic ug_network =
        nonMassSourceNetwork(MultiSpeciesUnits::Micrograms, source_type, negative_source_demand);
    const QString source_node_id = mg_network.nodes_junctions.at(1).id;

    EpanetRunRequest mg_request;
    mg_request.network = mg_network;
    mg_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest ug_request;
    ug_request.network = ug_network;
    ug_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun mg_result = EpanetRunner().run(mg_request);
    const EpanetResultRun ug_result = EpanetRunner().run(ug_request);

    context.expect(mg_result.status.success, case_name + ": MG source run must succeed");
    context.expect(ug_result.status.success, case_name + ": UG source run must succeed");
    context.expect(mg_result.multi_species_result.has_value(), case_name + ": MG run must return MSX results");
    context.expect(ug_result.multi_species_result.has_value(), case_name + ": UG run must return MSX results");
    if (!mg_result.multi_species_result.has_value() || !ug_result.multi_species_result.has_value())
        return;

    const MultiSpeciesSimulationResultTimeline &mg_timeline = mg_result.multi_species_result->result_timeline;
    const MultiSpeciesSimulationResultTimeline &ug_timeline = ug_result.multi_species_result->result_timeline;

    context.expect(
        mg_timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        case_name + ": MG timeline must be Valid");
    context.expect(
        ug_timeline.validity == MultiSpeciesSimulationResultValidity::Valid,
        case_name + ": UG timeline must be Valid");
    context.expectEqual(
        static_cast<std::int64_t>(ug_timeline.results.size()),
        static_cast<std::int64_t>(mg_timeline.results.size()),
        {-1, "quality", source_node_id.toStdString(), case_name + ".timesteps"});

    if (mg_timeline.results.size() != ug_timeline.results.size())
        return;

    bool found_positive_value = false;
    for (qsizetype step_index = 0; step_index < mg_timeline.results.size(); step_index++)
    {
        const MultiSpeciesSimulationResult &mg_step = mg_timeline.results.at(step_index);
        const MultiSpeciesSimulationResult &ug_step = ug_timeline.results.at(step_index);

        context.expectNear(
            ug_step.time_elapsed_s,
            mg_step.time_elapsed_s,
            AowisEpanetTests::NumericTolerance{1.0e-12, 0.0},
            {
                static_cast<std::int64_t>(mg_step.time_elapsed_s),
                "quality",
                source_node_id.toStdString(),
                case_name + ".time_elapsed_s"
            });

        const MultiSpeciesSimulationResultNodeJunction *mg_source =
            findMsxJunctionResult(mg_step, source_node_id);
        const MultiSpeciesSimulationResultNodeJunction *ug_source =
            findMsxJunctionResult(ug_step, source_node_id);

        context.expect(mg_source != nullptr, case_name + ": MG source node must be present");
        context.expect(ug_source != nullptr, case_name + ": UG source node must be present");
        if (mg_source == nullptr || ug_source == nullptr)
            continue;

        if (mg_source->species_values.size() != 1 || ug_source->species_values.size() != 1)
        {
            context.expect(false, case_name + ": source node must carry exactly one species value");
            continue;
        }

        const double mg_value = mg_source->species_values.first().value;
        const double ug_value = ug_source->species_values.first().value;
        if (mg_value > 1.0e-12)
            found_positive_value = true;

        context.expectNear(
            ug_value,
            mg_value,
            AowisEpanetTests::NumericTolerance{1.0e-8, 1.0e-7},
            {
                static_cast<std::int64_t>(mg_step.time_elapsed_s),
                "junction",
                source_node_id.toStdString(),
                case_name + ".canonical_units"
            });
    }

    context.expect(
        found_positive_value == expect_positive,
        expect_positive
            ? case_name + ": source mode must produce a positive canonical concentration"
            : case_name + ": source mode must remain inactive for this hydraulic condition");
}

void scenarioMsxRemainingSourceModesCanonicalUnits(AowisEpanetTests::TestContext &context)
{
    compareNonMassSourceCanonicalUnits(
        context,
        MultiSpeciesSourceType::Concentration,
        true,
        true,
        "concentration_negative_demand");

    compareNonMassSourceCanonicalUnits(
        context,
        MultiSpeciesSourceType::Concentration,
        false,
        false,
        "concentration_positive_demand");

    compareNonMassSourceCanonicalUnits(
        context,
        MultiSpeciesSourceType::FlowPaced,
        false,
        true,
        "flowpaced");

    compareNonMassSourceCanonicalUnits(
        context,
        MultiSpeciesSourceType::Setpoint,
        false,
        true,
        "setpoint");
}

void scenarioMsxWithStandardQuality(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();
    EpanetRunRequest request;
    request.network = network;
    request.quality_runs = {chemicalQualityOptions(), waterAgeQualityOptions()};
    request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun result = EpanetRunner().run(request);

    context.expect(result.status.success, "a valid combined standard-quality + MSX request must succeed");
    context.expect(result.state == EpanetRunState::Success, "a valid combined standard-quality + MSX request must have Success aggregate state");
    context.expect(result.result_timeline.status.success, "combined execution must retain a successful hydraulic timeline");
    context.expectEqual(
        static_cast<std::int64_t>(result.quality_results.size()),
        std::int64_t{2},
        {-1, "quality", std::string(), "quality_results.size"},
        "both standard EPANET quality runs must be preserved");
    for (const EpanetQualityResult &quality_result : result.quality_results)
        context.expect(quality_result.state == EpanetRunState::Success, "each standard EPANET quality run must succeed before MSX executes");

    context.expect(result.multi_species_result.has_value(), "combined execution must populate multi_species_result");
    if (result.multi_species_result.has_value())
    {
        context.expect(result.multi_species_result->state == EpanetRunState::Success, "MSX must succeed after the standard EPANET quality runs");
        context.expect(result.multi_species_result->result_timeline.validity == MultiSpeciesSimulationResultValidity::Valid, "combined MSX timeline must be Valid");
        context.expect(timelineHasSpeciesValue(result.multi_species_result->result_timeline), "combined MSX execution must contain species values");
    }
}

void scenarioMsxStandardQualityIsolatedEquivalence(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();
    const WaterQualitySolverOptions chemical = chemicalQualityOptions();
    const WaterQualitySolverOptions water_age = waterAgeQualityOptions();

    EpanetRunRequest combined_request;
    combined_request.network = network;
    combined_request.quality_runs = {chemical, water_age};
    combined_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest quality_only_request;
    quality_only_request.network = network;
    quality_only_request.quality_runs = {chemical, water_age};

    EpanetRunRequest msx_only_request;
    msx_only_request.network = network;
    msx_only_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun combined = EpanetRunner().run(combined_request);
    const EpanetResultRun quality_only = EpanetRunner().run(quality_only_request);
    const EpanetResultRun msx_only = EpanetRunner().run(msx_only_request);

    context.expect(combined.status.success, "combined reference run must succeed");
    context.expect(quality_only.status.success, "isolated standard-quality reference run must succeed");
    context.expect(msx_only.status.success, "isolated MSX reference run must succeed");
    context.expectEqual(
        static_cast<std::int64_t>(combined.quality_results.size()),
        static_cast<std::int64_t>(quality_only.quality_results.size()),
        {-1, "quality", std::string(), "quality_results.size"},
        "combined and isolated runs must return the same number of standard-quality results");

    if (combined.quality_results.size() == quality_only.quality_results.size())
    {
        for (qsizetype quality_index = 0; quality_index < combined.quality_results.size(); quality_index++)
        {
            const EpanetQualityResult &combined_quality = combined.quality_results.at(quality_index);
            const EpanetQualityResult &isolated_quality = quality_only.quality_results.at(quality_index);
            context.expect(combined_quality.state == isolated_quality.state, "combined and isolated standard-quality run states must match");
            compareQualityTimelines(context, combined_quality.result_timeline, isolated_quality.result_timeline);
        }
    }

    context.expect(combined.multi_species_result.has_value(), "combined run must contain MSX results for equivalence comparison");
    context.expect(msx_only.multi_species_result.has_value(), "isolated MSX run must contain MSX results for equivalence comparison");
    if (combined.multi_species_result.has_value() && msx_only.multi_species_result.has_value())
    {
        context.expect(combined.multi_species_result->state == msx_only.multi_species_result->state, "combined and isolated MSX run states must match");
        compareMsxTimelines(
            context,
            combined.multi_species_result->result_timeline,
            msx_only.multi_species_result->result_timeline);
    }
}

void scenarioMsxStandardQualityFailureIsolation(AowisEpanetTests::TestContext &context)
{
    const NetworkHydraulic network = combinedQualityMsxNetwork();

    WaterQualitySolverOptions invalid_trace;
    invalid_trace.analysis = WaterQualityAnalysisType::SourceTrace;
    invalid_trace.trace_node_uuid = QUuid::createUuid();

    EpanetRunRequest combined_request;
    combined_request.network = network;
    combined_request.quality_runs = {invalid_trace};
    combined_request.multi_species_run = MultiSpeciesRunOptions{};

    EpanetRunRequest msx_only_request;
    msx_only_request.network = network;
    msx_only_request.multi_species_run = MultiSpeciesRunOptions{};

    const EpanetResultRun combined = EpanetRunner().run(combined_request);
    const EpanetResultRun msx_only = EpanetRunner().run(msx_only_request);

    context.expect(!combined.status.success, "an invalid standard-quality child must still make the aggregate combined run report failure");
    context.expect(combined.state == EpanetRunState::Error, "an invalid standard-quality child must make the aggregate combined state Error");
    context.expect(combined.result_timeline.status.success, "standard-quality failure must not invalidate the already-completed hydraulics");
    context.expectEqual(
        static_cast<std::int64_t>(combined.quality_results.size()),
        std::int64_t{1},
        {-1, "quality", std::string(), "quality_results.size"});
    if (!combined.quality_results.isEmpty())
        context.expect(combined.quality_results.first().state == EpanetRunState::Error, "the invalid standard-quality child must be isolated as Error");

    context.expect(combined.multi_species_result.has_value(), "MSX must still execute after an isolated standard-quality validation failure");
    context.expect(msx_only.multi_species_result.has_value(), "isolated MSX reference must produce a result");
    if (combined.multi_species_result.has_value() && msx_only.multi_species_result.has_value())
    {
        context.expect(combined.multi_species_result->state == EpanetRunState::Success, "MSX must succeed despite the independent standard-quality child failure");
        compareMsxTimelines(
            context,
            combined.multi_species_result->result_timeline,
            msx_only.multi_species_result->result_timeline);
    }
}
}

namespace AowisEpanetTests
{
void registerMsxVendorSmokeScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "proof-msx-vendor-open-init-step-close",
        "Open, solve, step, and read a result from the vendored EPANET-MSX example network and reaction model through the raw MSX toolkit sequence, proving the submodule is correctly vendored, built against this adapter's own EPANET rather than MSX's bundled copy, and linked.",
        {"proof", "quality"},
        &scenarioMsxVendorOpenInitStepClose});
    registry.add(ScenarioDefinition{
        "proof-msx-aowis-hydraulic-handoff",
        "Solve hydraulics once through AOWIS EPANET, persist the resulting .hyd file, and prove EpanetMsxProject consumes that exact file through MSXusehydfile to produce MSX quality results.",
        {"proof", "hydraulic", "quality"},
        &scenarioMsxAowisHydraulicHandoff});
    registry.add(ScenarioDefinition{
        "proof-msx-nonpipe-link-parser-aliases",
        "Prove native EPANET-MSX accepts pump and valve IDs in [QUALITY] LINK and [PARAMETERS] PIPE records through generic link lookup while EPANET still exposes both objects as zero-length links.",
        {"proof", "quality"},
        &scenarioMsxVendorNonPipeLinkParserAliases});
    registry.add(ScenarioDefinition{
        "proof-msx-zero-volume-link-quality-semantics",
        "Prove native EPANET-MSX gives zero-volume pump and valve links no stored transport volume: current link quality falls back to endpoint-node averaging rather than the parser-stored LINK initial value.",
        {"proof", "hydraulic", "quality"},
        &scenarioMsxVendorZeroVolumeLinkQualitySemantics});
}

void registerMsxIntegrationScenarios(ScenarioRegistry &registry)
{
    registry.add(ScenarioDefinition{
        "contract-msx-integration-end-to-end",
        "Run a network with a real reaction model through EpanetRunner::run() and confirm multi_species_result comes back Valid with real, non-zero species concentrations.",
        {"contract", "quality"},
        &scenarioMsxIntegrationEndToEnd});
    registry.add(ScenarioDefinition{
        "contract-msx-known-answer-fractional-microgram-decay",
        "Run an isolated tank with a microgram-backed first-order species at 0.125-second MSX steps and compare every returned canonical mg/L value against the analytical exponential-decay solution.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxKnownAnswerFractionalMicrogramDecay});
    registry.add(ScenarioDefinition{
        "contract-msx-fixed-dispersion-peclet-runtime",
        "Run the same canonical fixed longitudinal dispersion with a restrictive and permissive Peclet threshold and prove the threshold controls early downstream dispersive transport before the advective front arrives.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxFixedDispersionPecletRuntime});
    registry.add(ScenarioDefinition{
        "contract-msx-formula-equilibrium-known-answer",
        "Execute FORMULA and EQUIL reaction expressions in both pipes and tanks and verify exact algebraic known answers alongside a stationary RATE species.",
        {"contract", "quality", "proof"},
        &scenarioMsxFormulaEquilibriumKnownAnswer});
    registry.add(ScenarioDefinition{
        "contract-msx-parameter-term-overrides-known-answer",
        "Execute named TERM expressions through a PARAM coefficient, prove explicit tank and pipe parameter overrides against a one-second analytical half-life, and prove the unoverridden pipe retains the parameter default value.",
        {"contract", "quality", "proof"},
        &scenarioMsxParameterTermOverridesKnownAnswer});
    registry.add(ScenarioDefinition{
        "contract-msx-mass-balance-final-summary",
        "Read EPANET-MSX's computed per-species mass-balance ratio through the isolated backend bridge and expose it only on the final AOWIS MSX result.",
        {"contract", "quality", "proof"},
        &scenarioMsxMassBalanceFinalSummary});
    registry.add(ScenarioDefinition{
        "contract-msx-wall-species-pipe-only-results",
        "Require WALL species to appear only on actual pipe results, never as MSX synthetic zero values on nodes or zero-volume non-pipe links, while proving canonical mmol/m2 round-trips through CM2 backend units.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxWallSpeciesPipeOnlyResults});
    registry.add(ScenarioDefinition{
        "contract-msx-configured-inp-roughness-kc",
        "Open the exact configured INP snapshot through MSX's linked EPANET layer and require EN_ROUGHNESS to match the configured Chezy-Manning value used to initialize MSX Kc, proving the snapshot comes from the same configured project as the reusable hydraulics.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxConfiguredInpRoughnessKc});
    registry.add(ScenarioDefinition{
        "contract-msx-backend-diagnostics",
        "Corrupt an otherwise valid saved hydraulic file and require the MSXusehydfile failure to preserve EPANET-MSX backend code, text, operation, stage, validity, and diagnostic provenance.",
        {"contract", "quality", "negative"},
        &scenarioMsxBackendDiagnostics});
    registry.add(ScenarioDefinition{
        "contract-msx-cancellation-partial",
        "Cancel MSX deterministically after a completed quality timestep and require preserved results, Partial validity, successful cancellation status, and stable simulation-start provenance.",
        {"contract", "quality", "cancellation"},
        &scenarioMsxCancellationPartial});
    registry.add(ScenarioDefinition{
        "contract-msx-output-species-filter-coupled-chemistry",
        "Request only one species in the result while solving a two-species coupled reaction model, proving output selection neither prunes chemistry nor changes the selected species concentration.",
        {"contract", "quality", "proof"},
        &scenarioMsxOutputSpeciesFilterPreservesCoupledChemistry});
    registry.add(ScenarioDefinition{
        "contract-msx-patterned-mass-source-canonical-units",
        "Execute a patterned MSX MASS source and prove both pattern scaling and MG/UG backend representations produce the same canonical AOWIS concentration timeline.",
        {"contract", "quality", "proof"},
        &scenarioMsxPatternedMassSourceCanonicalUnits});
    registry.add(ScenarioDefinition{
        "contract-msx-remaining-source-modes-canonical-units",
        "Execute CONCEN, FLOWPACED, and SETPOINT MSX sources, prove CONCEN activates only for negative-demand inflow, and require MG/UG backend representations to produce identical canonical AOWIS concentration timelines.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxRemainingSourceModesCanonicalUnits});
    registry.add(ScenarioDefinition{
        "contract-msx-with-standard-quality",
        "Run standard EPANET chemical and water-age analyses followed by MSX in one request against one hydraulic solution, and require all child results to succeed.",
        {"contract", "hydraulic", "quality"},
        &scenarioMsxWithStandardQuality});
    registry.add(ScenarioDefinition{
        "contract-msx-standard-quality-isolated-equivalence",
        "Compare standard-quality and MSX outputs from one combined request against isolated reference requests, proving sequential orchestration does not change either solver's results.",
        {"contract", "hydraulic", "quality", "proof"},
        &scenarioMsxStandardQualityIsolatedEquivalence});
    registry.add(ScenarioDefinition{
        "contract-msx-standard-quality-failure-isolation",
        "Fail one standard-quality child after hydraulics and prove MSX still executes successfully against the same saved hydraulic solution with results matching an isolated MSX run.",
        {"contract", "hydraulic", "quality", "proof", "negative"},
        &scenarioMsxStandardQualityFailureIsolation});
}
}
