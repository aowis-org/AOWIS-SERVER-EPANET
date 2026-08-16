#include "native_epanet_reference_runner.h"

#include <epanet2_2.h>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace AowisEpanetTests
{
namespace
{
constexpr double kMetresPerFoot = 0.3048;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kCubicMetresPerHourPerCubicFootPerSecond = 101.94;
constexpr double kGallonsPerMinutePerCubicFootPerSecond = 448.831;
constexpr double kMillionGallonsPerDayPerCubicFootPerSecond = 0.64632;
constexpr double kImperialMillionGallonsPerDayPerCubicFootPerSecond = 0.5382;
constexpr double kAcreFeetPerDayPerCubicFootPerSecond = 1.9837;
constexpr double kLitresPerSecondPerCubicFootPerSecond = 28.317;
constexpr double kLitresPerMinutePerCubicFootPerSecond = 1699.0;
constexpr double kMillionLitresPerDayPerCubicFootPerSecond = 2.4466;
constexpr double kCubicMetresPerDayPerCubicFootPerSecond = 2446.6;
constexpr double kCubicMetresPerSecondPerCubicFootPerSecond = 0.028317;
constexpr double kPsiPerFoot = 0.4333;
constexpr double kKilopascalsPerPsi = 6.895;
constexpr double kBarPerPsi = 0.068948;

void checkEpanet(int error_code, const char *operation)
{
    if (error_code == 0)
        return;

    char message[EN_MAXMSG + 1] = {};
    EN_geterror(error_code, message, EN_MAXMSG);
    throw std::runtime_error(std::string(operation) + " failed with EPANET error "
        + std::to_string(error_code) + ": " + message);
}

int checkEpanetAllowWarning(int error_code, const char *operation)
{
    if (error_code >= 0 && error_code < 100)
        return error_code;

    checkEpanet(error_code, operation);
    return error_code;
}

int nodeIndex(EN_Project project, const char *id)
{
    int index = 0;
    checkEpanet(EN_getnodeindex(project, id, &index), "EN_getnodeindex");
    return index;
}

int linkIndex(EN_Project project, const char *id)
{
    int index = 0;
    checkEpanet(EN_getlinkindex(project, id, &index), "EN_getlinkindex");
    return index;
}

void configureCanonicalMetricUnits(EN_Project project)
{
    checkEpanet(EN_setflowunits(project, EN_CMH), "EN_setflowunits(EN_CMH)");
    checkEpanet(EN_setoption(project, EN_PRESS_UNITS, EN_METERS), "EN_setoption(EN_PRESS_UNITS)");
}

int addPattern(EN_Project project, const char *id, double *factors, int factor_count)
{
    checkEpanet(EN_addpattern(project, id), "EN_addpattern");
    int pattern_index = 0;
    checkEpanet(EN_getpatternindex(project, id, &pattern_index), "EN_getpatternindex");
    checkEpanet(EN_setpattern(project, pattern_index, factors, factor_count), "EN_setpattern");
    return pattern_index;
}

void setFormulaRoughness(EN_Project project, int headloss_formula, double default_roughness, double pipe_10_roughness)
{
    checkEpanet(EN_setoption(project, EN_HEADLOSSFORM, static_cast<double>(headloss_formula)), "EN_setoption(EN_HEADLOSSFORM)");
    int link_count = 0;
    checkEpanet(EN_getcount(project, EN_LINKCOUNT, &link_count), "EN_getcount(EN_LINKCOUNT)");
    for (int link_index = 1; link_index <= link_count; link_index++)
    {
        int link_type = 0;
        checkEpanet(EN_getlinktype(project, link_index, &link_type), "EN_getlinktype");
        if (link_type != EN_PIPE && link_type != EN_CVPIPE)
            continue;

        char id[EN_MAXID + 1] = {};
        checkEpanet(EN_getlinkid(project, link_index, id), "EN_getlinkid");
        const double roughness = std::string(id) == "10" ? pipe_10_roughness : default_roughness;
        checkEpanet(EN_setlinkvalue(project, link_index, EN_ROUGHNESS, roughness), "EN_setlinkvalue(EN_ROUGHNESS)");
    }
}

void disableNet1PumpControls(EN_Project project)
{
    checkEpanet(EN_setcontrolenabled(project, 1, 0), "EN_setcontrolenabled(disable 1)");
    checkEpanet(EN_setcontrolenabled(project, 2, 0), "EN_setcontrolenabled(disable 2)");
}

void setPumpCurve(EN_Project project, double *flows, double *heads, int point_count)
{
    checkEpanet(EN_setcurve(project, 1, flows, heads, point_count), "EN_setcurve(pump head)");
    checkEpanet(EN_setcurvetype(project, 1, EN_PUMP_CURVE), "EN_setcurvetype(EN_PUMP_CURVE)");
    const int pump_index = linkIndex(project, "9");
    checkEpanet(EN_setlinkvalue(project, pump_index, EN_PUMP_HCURVE, 1.0), "EN_setlinkvalue(EN_PUMP_HCURVE)");
}

double feetToCanonicalMetres(double feet)
{
    return feet * kMetresPerFoot;
}

double gallonsPerMinuteToCanonicalCubicMetresPerHour(double gallons_per_minute)
{
    return gallons_per_minute / kGallonsPerMinutePerCubicFootPerSecond
        * kCubicMetresPerHourPerCubicFootPerSecond;
}

void configureCanonicalMetricNet1Inputs(EN_Project project)
{
    // Reapply Net1 through the canonical metric API values used by AOWIS.
    // Merely switching the loaded US fixture to CMH/metres leaves tiny round-trip
    // differences that pressure-breaker valves can amplify at solver tolerance.
    configureCanonicalMetricUnits(project);

    struct JunctionInput
    {
        const char *id;
        double elevation_ft;
        double demand_gpm;
    };

    const JunctionInput junctions[] = {
        {"10", 710.0, 0.0},
        {"11", 710.0, 150.0},
        {"12", 700.0, 150.0},
        {"13", 695.0, 100.0},
        {"21", 700.0, 150.0},
        {"22", 695.0, 200.0},
        {"23", 690.0, 150.0},
        {"31", 700.0, 100.0},
        {"32", 710.0, 100.0}
    };

    for (const JunctionInput &junction : junctions)
    {
        checkEpanet(EN_setjuncdata(
            project,
            nodeIndex(project, junction.id),
            feetToCanonicalMetres(junction.elevation_ft),
            gallonsPerMinuteToCanonicalCubicMetresPerHour(junction.demand_gpm),
            "1"),
            "EN_setjuncdata(canonical Net1)");
    }

    checkEpanet(EN_setnodevalue(
        project,
        nodeIndex(project, "9"),
        EN_ELEVATION,
        feetToCanonicalMetres(800.0)),
        "EN_setnodevalue(canonical reservoir head)");

    checkEpanet(EN_settankdata(
        project,
        nodeIndex(project, "2"),
        feetToCanonicalMetres(850.0),
        feetToCanonicalMetres(120.0),
        feetToCanonicalMetres(100.0),
        feetToCanonicalMetres(150.0),
        feetToCanonicalMetres(50.5),
        0.0,
        ""),
        "EN_settankdata(canonical Net1)");

    struct PipeInput
    {
        const char *id;
        double length_ft;
        double diameter_in;
    };

    const PipeInput pipes[] = {
        {"10", 10530.0, 18.0},
        {"11", 5280.0, 14.0},
        {"12", 5280.0, 10.0},
        {"21", 5280.0, 10.0},
        {"22", 5280.0, 12.0},
        {"31", 5280.0, 6.0},
        {"110", 200.0, 18.0},
        {"111", 5280.0, 10.0},
        {"112", 5280.0, 12.0},
        {"113", 5280.0, 8.0},
        {"121", 5280.0, 8.0},
        {"122", 5280.0, 6.0}
    };

    for (const PipeInput &pipe : pipes)
    {
        checkEpanet(EN_setpipedata(
            project,
            linkIndex(project, pipe.id),
            feetToCanonicalMetres(pipe.length_ft),
            pipe.diameter_in * 25.4,
            100.0,
            0.0),
            "EN_setpipedata(canonical Net1)");
    }

    double pump_flows[] = {gallonsPerMinuteToCanonicalCubicMetresPerHour(1500.0)};
    double pump_heads[] = {feetToCanonicalMetres(250.0)};
    setPumpCurve(project, pump_flows, pump_heads, 1);

    const int pump_index = linkIndex(project, "9");
    checkEpanet(EN_setlinkvalue(project, pump_index, EN_INITSETTING, 1.0), "EN_setlinkvalue(canonical pump speed)");
    checkEpanet(EN_setlinkvalue(project, pump_index, EN_INITSTATUS, EN_OPEN), "EN_setlinkvalue(canonical pump status)");
}

void applyReferenceVariant(EN_Project project, NativeReferenceVariant variant)
{
    switch (variant)
    {
    case NativeReferenceVariant::None:
        return;
    case NativeReferenceVariant::DdaStress:
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_setoption(project, EN_DEMANDMULT, 10.0), "EN_setoption(EN_DEMANDMULT)");
        return;
    case NativeReferenceVariant::PdaStress:
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_setoption(project, EN_DEMANDMULT, 10.0), "EN_setoption(EN_DEMANDMULT)");
        checkEpanet(EN_setdemandmodel(project, EN_PDA, 20.0, 100.0, 0.5), "EN_setdemandmodel");
        return;
    case NativeReferenceVariant::Leakage:
    {
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        const int pipe_index = linkIndex(project, "21");
        checkEpanet(EN_setlinkvalue(project, pipe_index, EN_LEAK_AREA, 1.0), "EN_setlinkvalue(EN_LEAK_AREA)");
        checkEpanet(EN_setlinkvalue(project, pipe_index, EN_LEAK_EXPAN, 0.1), "EN_setlinkvalue(EN_LEAK_EXPAN)");
        return;
    }
    case NativeReferenceVariant::OverflowDisabled:
    case NativeReferenceVariant::OverflowEnabled:
    {
        const int tank_index = nodeIndex(project, "2");
        checkEpanet(EN_setnodevalue(project, tank_index, EN_TANKLEVEL, 130.0), "EN_setnodevalue(EN_TANKLEVEL)");
        checkEpanet(EN_setnodevalue(project, tank_index, EN_MAXLEVEL, 130.0), "EN_setnodevalue(EN_MAXLEVEL)");
        checkEpanet(EN_setnodevalue(
            project,
            tank_index,
            EN_CANOVERFLOW,
            variant == NativeReferenceVariant::OverflowEnabled ? 1.0 : 0.0),
            "EN_setnodevalue(EN_CANOVERFLOW)");
        checkEpanet(EN_settimeparam(project, EN_DURATION, 3600), "EN_settimeparam(EN_DURATION)");
        return;
    }
    case NativeReferenceVariant::Pcv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");

        int valve_index = linkIndex(project, "22");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_PCV, EN_UNCONDITIONAL), "EN_setlinktype(EN_PCV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 12.0 * 25.4), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.19), "EN_setlinkvalue(EN_MINORLOSS)");

        checkEpanet(EN_addcurve(project, "ValveCurve"), "EN_addcurve");
        int curve_index = 0;
        checkEpanet(EN_getcurveindex(project, "ValveCurve", &curve_index), "EN_getcurveindex");
        double positions[] = {0.0, 25.0, 50.0, 75.0, 100.0};
        double relative_flows[] = {0.0, 8.9, 18.4, 40.6, 100.0};
        checkEpanet(EN_setcurve(project, curve_index, positions, relative_flows, 5), "EN_setcurve");
        checkEpanet(EN_setcurvetype(project, curve_index, EN_VALVE_CURVE), "EN_setcurvetype");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_PCV_CURVE, static_cast<double>(curve_index)), "EN_setlinkvalue(EN_PCV_CURVE)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 35.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::DemandPattern:
    {
        checkEpanet(EN_settimeparam(project, EN_DURATION, 21600), "EN_settimeparam(EN_DURATION)");

        checkEpanet(EN_setpatternid(project, 1, "Pat1"), "EN_setpatternid");
        int default_pattern_index = 0;
        checkEpanet(EN_getpatternindex(project, "Pat1", &default_pattern_index), "EN_getpatternindex(Pat1)");
        if (default_pattern_index != 1)
            throw std::runtime_error("Renamed upstream default demand pattern did not retain index 1");

        checkEpanet(EN_addpattern(project, "Step3Pattern"), "EN_addpattern");
        int pattern_index = 0;
        checkEpanet(EN_getpatternindex(project, "Step3Pattern", &pattern_index), "EN_getpatternindex");
        double factors[] = {3.1, 3.2, 3.3, 3.4};
        checkEpanet(EN_setpattern(project, pattern_index, factors, 4), "EN_setpattern");
        const int junction_index = nodeIndex(project, "12");
        checkEpanet(EN_setdemandpattern(project, junction_index, 1, pattern_index), "EN_setdemandpattern");
        return;
    }
    case NativeReferenceVariant::SimpleControl:
    {
        const int pump_index = linkIndex(project, "9");
        const int tank_index = nodeIndex(project, "2");

        checkEpanet(EN_setcontrol(project, 1, EN_LOWLEVEL, 0, 0.0, 0, 0.0), "EN_setcontrol(disable 1)");
        checkEpanet(EN_setcontrol(project, 2, EN_HILEVEL, 0, 0.0, 0, 0.0), "EN_setcontrol(disable 2)");
        checkEpanet(EN_setcontrolenabled(project, 1, 0), "EN_setcontrolenabled(disable 1)");
        checkEpanet(EN_setcontrolenabled(project, 2, 0), "EN_setcontrolenabled(disable 2)");

        int control_index = 0;
        checkEpanet(EN_addcontrol(project, EN_LOWLEVEL, pump_index, EN_OPEN, tank_index, 110.0, &control_index), "EN_addcontrol(low)");
        if (control_index != 3)
            throw std::runtime_error("EPANET did not assign upstream simple-control index 3");
        checkEpanet(EN_addcontrol(project, EN_HILEVEL, pump_index, EN_CLOSED, tank_index, 140.0, &control_index), "EN_addcontrol(high)");
        if (control_index != 4)
            throw std::runtime_error("EPANET did not assign upstream simple-control index 4");
        return;
    }
    case NativeReferenceVariant::JunctionReservoirInputs:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 7200), "EN_settimeparam(EN_DURATION)");

        const int junction_index = nodeIndex(project, "11");
        checkEpanet(EN_setjuncdata(project, junction_index, 215.0, 34.0, "1"), "EN_setjuncdata(step4 junction)");
        checkEpanet(EN_setnodevalue(project, junction_index, EN_EMITTER, 1.75), "EN_setnodevalue(EN_EMITTER)");

        double factors[] = {1.0, 1.05};
        const int pattern_index = addPattern(project, "STEP4_RES_HEAD", factors, 2);
        const int reservoir_index = nodeIndex(project, "9");
        checkEpanet(EN_setnodevalue(project, reservoir_index, EN_ELEVATION, 250.0), "EN_setnodevalue(reservoir head)");
        checkEpanet(EN_setnodevalue(project, reservoir_index, EN_PATTERN, static_cast<double>(pattern_index)), "EN_setnodevalue(reservoir pattern)");
        return;
    }
    case NativeReferenceVariant::DemandCategories:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 7200), "EN_settimeparam(EN_DURATION)");

        double primary_factors[] = {1.0, 2.0};
        double secondary_factors[] = {0.5, 1.5};
        double constant_factors[] = {1.0};
        addPattern(project, "STEP4_PRIMARY", primary_factors, 2);
        addPattern(project, "STEP4_SECONDARY", secondary_factors, 2);
        addPattern(project, "STEP4_CONSTANT", constant_factors, 1);

        const int junction_index = nodeIndex(project, "12");
        checkEpanet(EN_setjuncdata(project, junction_index, 213.36, 20.0, "STEP4_PRIMARY"), "EN_setjuncdata(step4 primary demand)");
        checkEpanet(EN_setdemandname(project, junction_index, 1, "PrimaryDemand"), "EN_setdemandname(primary)");
        checkEpanet(EN_adddemand(project, junction_index, 7.0, "STEP4_CONSTANT", "SecondaryDemand"), "EN_adddemand(constant)");
        checkEpanet(EN_adddemand(project, junction_index, 5.0, "STEP4_SECONDARY", "TertiaryDemand"), "EN_adddemand(secondary pattern)");
        return;
    }
    case NativeReferenceVariant::TankUniformArea:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        const int tank_index = nodeIndex(project, "2");
        const double area_m2 = 200.0;
        const double diameter_m = std::sqrt(4.0 * area_m2 / kPi);
        checkEpanet(EN_settankdata(project, tank_index, 255.0, 40.0, 30.0, 50.0, diameter_m, 40.0, ""), "EN_settankdata(uniform area)");
        return;
    }
    case NativeReferenceVariant::TankVolumeAtMaximum:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        const int tank_index = nodeIndex(project, "2");
        const double area_m2 = (4040.0 - 40.0) / (50.0 - 30.0);
        const double diameter_m = std::sqrt(4.0 * area_m2 / kPi);
        checkEpanet(EN_settankdata(project, tank_index, 255.0, 40.0, 30.0, 50.0, diameter_m, 40.0, ""), "EN_settankdata(volume at maximum)");
        return;
    }
    case NativeReferenceVariant::TankVolumeCurve:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_addcurve(project, "STEP4_TANK_VOLUME"), "EN_addcurve(step4 tank volume)");
        int curve_index = 0;
        checkEpanet(EN_getcurveindex(project, "STEP4_TANK_VOLUME", &curve_index), "EN_getcurveindex(step4 tank volume)");
        double levels[] = {30.0, 35.0, 40.0, 45.0, 50.0};
        double volumes[] = {40.0, 600.0, 1500.0, 2700.0, 4200.0};
        checkEpanet(EN_setcurve(project, curve_index, levels, volumes, 5), "EN_setcurve(step4 tank volume)");
        checkEpanet(EN_setcurvetype(project, curve_index, EN_VOLUME_CURVE), "EN_setcurvetype(EN_VOLUME_CURVE)");
        const int tank_index = nodeIndex(project, "2");
        checkEpanet(EN_settankdata(project, tank_index, 255.0, 40.0, 30.0, 50.0, 0.0, 40.0, "STEP4_TANK_VOLUME"), "EN_settankdata(volume curve)");
        return;
    }
    case NativeReferenceVariant::PipeInputs:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");

        const int pipe_111 = linkIndex(project, "111");
        checkEpanet(EN_setpipedata(project, pipe_111, 1234.0, 275.0, 127.0, 0.65), "EN_setpipedata(step4 pipe 111)");

        int pipe_113 = linkIndex(project, "113");
        int node_from = 0;
        int node_to = 0;
        checkEpanet(EN_getlinknodes(project, pipe_113, &node_from, &node_to), "EN_getlinknodes(pipe 113)");
        checkEpanet(EN_setlinknodes(project, pipe_113, node_to, node_from), "EN_setlinknodes(reverse pipe 113)");
        checkEpanet(EN_setlinktype(project, &pipe_113, EN_CVPIPE, EN_UNCONDITIONAL), "EN_setlinktype(EN_CVPIPE)");

        const int pipe_122 = linkIndex(project, "122");
        checkEpanet(EN_setlinkvalue(project, pipe_122, EN_INITSTATUS, EN_CLOSED), "EN_setlinkvalue(EN_INITSTATUS)");
        return;
    }
    case NativeReferenceVariant::DarcyWeisbach:
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        setFormulaRoughness(project, EN_DW, 0.25, 0.35);
        return;
    case NativeReferenceVariant::ChezyManning:
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        setFormulaRoughness(project, EN_CM, 0.013, 0.017);
        return;
    case NativeReferenceVariant::PumpThreePoint:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        double flows[] = {0.0, 300.0, 600.0};
        double heads[] = {90.0, 65.0, 20.0};
        setPumpCurve(project, flows, heads, 3);
        return;
    }
    case NativeReferenceVariant::PumpMultiPoint:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        double flows[] = {0.0, 200.0, 400.0, 650.0};
        double heads[] = {95.0, 82.0, 55.0, 15.0};
        setPumpCurve(project, flows, heads, 4);
        return;
    }
    case NativeReferenceVariant::PumpConstantPower:
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_PUMP_POWER, 75.0), "EN_setlinkvalue(EN_PUMP_POWER)");
        return;
    case NativeReferenceVariant::PumpInitialSpeed:
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_INITSETTING, 0.8), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    case NativeReferenceVariant::PumpSpeedPattern:
    {
        configureCanonicalMetricUnits(project);
        disableNet1PumpControls(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 7200), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_settimeparam(project, EN_PATTERNSTEP, 3600), "EN_settimeparam(EN_PATTERNSTEP)");
        double factors[] = {0.8, 1.0, 1.15};
        const int pattern_index = addPattern(project, "STEP5_SPEED", factors, 3);
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_LINKPATTERN, static_cast<double>(pattern_index)), "EN_setlinkvalue(EN_LINKPATTERN)");
        return;
    }
    case NativeReferenceVariant::PumpInitialOff:
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_INITSTATUS, EN_CLOSED), "EN_setlinkvalue(EN_INITSTATUS)");
        return;
    case NativeReferenceVariant::PumpConstantEfficiency:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_addcurve(project, "STEP5_CONST_EFF"), "EN_addcurve(STEP5_CONST_EFF)");
        int curve_index = 0;
        checkEpanet(EN_getcurveindex(project, "STEP5_CONST_EFF", &curve_index), "EN_getcurveindex(STEP5_CONST_EFF)");
        double flows[] = {0.0};
        double efficiencies[] = {83.0};
        checkEpanet(EN_setcurve(project, curve_index, flows, efficiencies, 1), "EN_setcurve(STEP5_CONST_EFF)");
        checkEpanet(EN_setcurvetype(project, curve_index, EN_EFFIC_CURVE), "EN_setcurvetype(EN_EFFIC_CURVE)");
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_PUMP_ECURVE, static_cast<double>(curve_index)), "EN_setlinkvalue(EN_PUMP_ECURVE)");
        return;
    }
    case NativeReferenceVariant::PumpEfficiencyCurve:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_addcurve(project, "STEP5_EFF"), "EN_addcurve(STEP5_EFF)");
        int curve_index = 0;
        checkEpanet(EN_getcurveindex(project, "STEP5_EFF", &curve_index), "EN_getcurveindex(STEP5_EFF)");
        double flows[] = {0.0, 200.0, 400.0, 650.0};
        double efficiencies[] = {60.0, 76.0, 88.0, 79.0};
        checkEpanet(EN_setcurve(project, curve_index, flows, efficiencies, 4), "EN_setcurve(STEP5_EFF)");
        checkEpanet(EN_setcurvetype(project, curve_index, EN_EFFIC_CURVE), "EN_setcurvetype(EN_EFFIC_CURVE)");
        checkEpanet(EN_setlinkvalue(project, linkIndex(project, "9"), EN_PUMP_ECURVE, static_cast<double>(curve_index)), "EN_setlinkvalue(EN_PUMP_ECURVE)");
        return;
    }
    case NativeReferenceVariant::PumpGlobalEnergy:
    {
        configureCanonicalMetricUnits(project);
        disableNet1PumpControls(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 7200), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_settimeparam(project, EN_PATTERNSTEP, 3600), "EN_settimeparam(EN_PATTERNSTEP)");
        checkEpanet(EN_setoption(project, EN_GLOBALEFFIC, 82.0), "EN_setoption(EN_GLOBALEFFIC)");
        checkEpanet(EN_setoption(project, EN_GLOBALPRICE, 0.2), "EN_setoption(EN_GLOBALPRICE)");
        checkEpanet(EN_setoption(project, EN_DEMANDCHARGE, 1.5), "EN_setoption(EN_DEMANDCHARGE)");
        double factors[] = {1.0, 2.0};
        const int pattern_index = addPattern(project, "STEP5_GLOBAL_PRICE", factors, 2);
        checkEpanet(EN_setoption(project, EN_GLOBALPATTERN, static_cast<double>(pattern_index)), "EN_setoption(EN_GLOBALPATTERN)");
        return;
    }
    case NativeReferenceVariant::PumpEnergyPattern:
    {
        configureCanonicalMetricUnits(project);
        disableNet1PumpControls(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 7200), "EN_settimeparam(EN_DURATION)");
        checkEpanet(EN_settimeparam(project, EN_PATTERNSTEP, 3600), "EN_settimeparam(EN_PATTERNSTEP)");
        checkEpanet(EN_setoption(project, EN_GLOBALEFFIC, 80.0), "EN_setoption(EN_GLOBALEFFIC)");
        checkEpanet(EN_setoption(project, EN_GLOBALPRICE, 0.1), "EN_setoption(EN_GLOBALPRICE)");
        checkEpanet(EN_setoption(project, EN_DEMANDCHARGE, 0.75), "EN_setoption(EN_DEMANDCHARGE)");
        double global_factors[] = {4.0, 4.0};
        const int global_pattern_index = addPattern(project, "STEP5_GLOBAL_UNUSED", global_factors, 2);
        checkEpanet(EN_setoption(project, EN_GLOBALPATTERN, static_cast<double>(global_pattern_index)), "EN_setoption(EN_GLOBALPATTERN)");
        double pump_factors[] = {1.0, 0.5};
        const int pump_pattern_index = addPattern(project, "STEP5_PUMP_PRICE", pump_factors, 2);
        const int pump_index = linkIndex(project, "9");
        checkEpanet(EN_setlinkvalue(project, pump_index, EN_PUMP_ECOST, 0.3), "EN_setlinkvalue(EN_PUMP_ECOST)");
        checkEpanet(EN_setlinkvalue(project, pump_index, EN_PUMP_EPAT, static_cast<double>(pump_pattern_index)), "EN_setlinkvalue(EN_PUMP_EPAT)");
        return;
    }
    case NativeReferenceVariant::PumpCannotSupplyHead:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        double flows[] = {100.0};
        double heads[] = {5.0};
        setPumpCurve(project, flows, heads, 1);
        return;
    }
    case NativeReferenceVariant::PumpCannotSupplyFlow:
    {
        configureCanonicalMetricUnits(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        double flows[] = {0.0, 5.0, 10.0, 20.0};
        double heads[] = {100.0, 95.0, 90.0, 80.0};
        setPumpCurve(project, flows, heads, 4);
        return;
    }
    case NativeReferenceVariant::ValvePrv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "121");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_PRV, EN_UNCONDITIONAL), "EN_setlinktype(EN_PRV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 230.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.35), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 80.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::ValvePsv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "10");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_PSV, EN_UNCONDITIONAL), "EN_setlinktype(EN_PSV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 400.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.22), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 86.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::ValvePbv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "121");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_PBV, EN_UNCONDITIONAL), "EN_setlinktype(EN_PBV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 225.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.33), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 5.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::ValveFcv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "121");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_FCV, EN_UNCONDITIONAL), "EN_setlinktype(EN_FCV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 210.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.44), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 20.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::ValveTcv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "121");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_TCV, EN_UNCONDITIONAL), "EN_setlinktype(EN_TCV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 205.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.18), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSETTING, 12.0), "EN_setlinkvalue(EN_INITSETTING)");
        return;
    }
    case NativeReferenceVariant::ValveGpv:
    {
        configureCanonicalMetricNet1Inputs(project);
        checkEpanet(EN_settimeparam(project, EN_DURATION, 0), "EN_settimeparam(EN_DURATION)");
        int valve_index = linkIndex(project, "121");
        checkEpanet(EN_setlinktype(project, &valve_index, EN_GPV, EN_UNCONDITIONAL), "EN_setlinktype(EN_GPV)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_DIAMETER, 220.0), "EN_setlinkvalue(EN_DIAMETER)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_MINORLOSS, 0.26), "EN_setlinkvalue(EN_MINORLOSS)");
        checkEpanet(EN_addcurve(project, "STEP6_GPV"), "EN_addcurve(STEP6_GPV)");
        int curve_index = 0;
        checkEpanet(EN_getcurveindex(project, "STEP6_GPV", &curve_index), "EN_getcurveindex(STEP6_GPV)");
        double flows[] = {0.0, 20.0, 40.0, 80.0};
        double head_losses[] = {0.0, 0.4, 2.0, 8.0};
        checkEpanet(EN_setcurve(project, curve_index, flows, head_losses, 4), "EN_setcurve(STEP6_GPV)");
        checkEpanet(EN_setcurvetype(project, curve_index, EN_HLOSS_CURVE), "EN_setcurvetype(EN_HLOSS_CURVE)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_GPV_CURVE, static_cast<double>(curve_index)), "EN_setlinkvalue(EN_GPV_CURVE)");
        checkEpanet(EN_setlinkvalue(project, valve_index, EN_INITSTATUS, EN_OPEN), "EN_setlinkvalue(EN_INITSTATUS)");
        return;
    }
    }
    throw std::runtime_error("Unknown native reference variant");
}

class NativeProject
{
public:
    NativeProject()
    {
        checkEpanet(EN_createproject(&this->handle_), "EN_createproject");
    }

    ~NativeProject()
    {
        if (this->hydraulics_open_)
            EN_closeH(this->handle_);
        if (this->project_open_)
            EN_close(this->handle_);
        if (this->handle_ != nullptr)
            EN_deleteproject(this->handle_);
    }

    EN_Project handle() const
    {
        return this->handle_;
    }

    void markProjectOpen()
    {
        this->project_open_ = true;
    }

    void markHydraulicsOpen()
    {
        this->hydraulics_open_ = true;
    }

    void closeHydraulics()
    {
        checkEpanet(EN_closeH(this->handle_), "EN_closeH");
        this->hydraulics_open_ = false;
    }

    void closeProject()
    {
        checkEpanet(EN_close(this->handle_), "EN_close");
        this->project_open_ = false;
    }

private:
    EN_Project handle_ = nullptr;
    bool project_open_ = false;
    bool hydraulics_open_ = false;
};

struct NativeUnitSystem
{
    int flow_units = EN_CMH;
    int pressure_units = EN_METERS;
    double specific_gravity = 1.0;
};

struct PumpEnergyAccumulator
{
    QString pump_id;
    double online_hours = 0.0;
    double efficiency_percent_hours = 0.0;
    double kw_per_flow_hours = 0.0;
    double power_kw_hours = 0.0;
    double peak_power_kw = 0.0;
    double total_cost = 0.0;
};

struct FlowBalanceAccumulator
{
    double covered_seconds = 0.0;
    double total_inflow = 0.0;
    double total_outflow = 0.0;
    double consumer_demand = 0.0;
    double demand_deficit = 0.0;
    double emitter_flow = 0.0;
    double leakage_flow = 0.0;
    double storage_flow = 0.0;
};

bool usesUsLengthUnits(int flow_units)
{
    return flow_units >= EN_CFS && flow_units <= EN_AFD;
}

double flowToCubicMetresPerHour(double value, int flow_units)
{
    switch (flow_units)
    {
    case EN_CFS:
        return value * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_GPM:
        return value / kGallonsPerMinutePerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_MGD:
        return value / kMillionGallonsPerDayPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_IMGD:
        return value / kImperialMillionGallonsPerDayPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_AFD:
        return value / kAcreFeetPerDayPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_LPS:
        return value / kLitresPerSecondPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_LPM:
        return value / kLitresPerMinutePerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_MLD:
        return value / kMillionLitresPerDayPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_CMH:
        return value;
    case EN_CMD:
        return value / kCubicMetresPerDayPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    case EN_CMS:
        return value / kCubicMetresPerSecondPerCubicFootPerSecond
            * kCubicMetresPerHourPerCubicFootPerSecond;
    }
    throw std::runtime_error("Native reference runner encountered unknown EPANET flow units");
}

double headToMetres(double value, int flow_units)
{
    return usesUsLengthUnits(flow_units) ? value * kMetresPerFoot : value;
}

double velocityToMetresPerSecond(double value, int flow_units)
{
    return usesUsLengthUnits(flow_units) ? value * kMetresPerFoot : value;
}

double volumeToCubicMetres(double value, int flow_units)
{
    return usesUsLengthUnits(flow_units)
        ? value * kMetresPerFoot * kMetresPerFoot * kMetresPerFoot
        : value;
}

double diameterToMetres(double value, int flow_units)
{
    return usesUsLengthUnits(flow_units) ? value * 0.0254 : value / 1000.0;
}

double pressureToHeadMetres(double value, const NativeUnitSystem &units)
{
    switch (units.pressure_units)
    {
    case EN_PSI:
        return value / (kPsiPerFoot * units.specific_gravity) * kMetresPerFoot;
    case EN_KPA:
        return value / (kKilopascalsPerPsi * kPsiPerFoot * units.specific_gravity)
            * kMetresPerFoot;
    case EN_METERS:
        return value;
    case EN_BAR:
        return value / (kBarPerPsi * kPsiPerFoot * units.specific_gravity)
            * kMetresPerFoot;
    case EN_FEET:
        return value * kMetresPerFoot;
    }
    throw std::runtime_error("Native reference runner encountered unknown EPANET pressure units");
}

double nodeValue(EN_Project project, int node_index, int property)
{
    double value = 0.0;
    checkEpanet(EN_getnodevalue(project, node_index, property, &value), "EN_getnodevalue");
    return value;
}

double linkValue(EN_Project project, int link_index, int property)
{
    double value = 0.0;
    checkEpanet(EN_getlinkvalue(project, link_index, property, &value), "EN_getlinkvalue");
    return value;
}

double statisticValue(EN_Project project, int statistic)
{
    double value = 0.0;
    checkEpanet(EN_getstatistic(project, statistic, &value), "EN_getstatistic");
    return value;
}

double optionValue(EN_Project project, int option)
{
    double value = 0.0;
    checkEpanet(EN_getoption(project, option, &value), "EN_getoption");
    return value;
}

long timeParameter(EN_Project project, int parameter)
{
    long value = 0;
    checkEpanet(EN_gettimeparam(project, parameter, &value), "EN_gettimeparam");
    return value;
}

int objectCount(EN_Project project, int object_type)
{
    int count = 0;
    checkEpanet(EN_getcount(project, object_type, &count), "EN_getcount");
    return count;
}

QString nodeId(EN_Project project, int node_index)
{
    char id[EN_MAXID + 1] = {};
    checkEpanet(EN_getnodeid(project, node_index, id), "EN_getnodeid");
    return QString::fromUtf8(id);
}

QString linkId(EN_Project project, int link_index)
{
    char id[EN_MAXID + 1] = {};
    checkEpanet(EN_getlinkid(project, link_index, id), "EN_getlinkid");
    return QString::fromUtf8(id);
}

NativeUnitSystem readUnitSystem(EN_Project project)
{
    NativeUnitSystem units;
    checkEpanet(EN_getflowunits(project, &units.flow_units), "EN_getflowunits");
    units.pressure_units = static_cast<int>(optionValue(project, EN_PRESS_UNITS));
    units.specific_gravity = optionValue(project, EN_SP_GRAVITY);
    return units;
}

void appendNodeResult(EN_Project project, int node_index, const NativeUnitSystem &units, NativeHydraulicResult &result)
{
    int node_type = 0;
    checkEpanet(EN_getnodetype(project, node_index, &node_type), "EN_getnodetype");
    const QString id = nodeId(project, node_index);

    if (node_type == EN_JUNCTION)
    {
        NativeJunctionResult junction;
        junction.id = id;
        junction.demand_requested_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_FULLDEMAND), units.flow_units);
        junction.demand_delivered_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_DEMANDFLOW), units.flow_units);
        junction.demand_deficit_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_DEMANDDEFICIT), units.flow_units);
        junction.total_demand_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_DEMAND), units.flow_units);
        junction.emitter_flow_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_EMITTERFLOW), units.flow_units);
        junction.leakage_flow_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_LEAKAGEFLOW), units.flow_units);
        junction.head_m = headToMetres(nodeValue(project, node_index, EN_HEAD), units.flow_units);
        junction.pressure_head_m = pressureToHeadMetres(nodeValue(project, node_index, EN_PRESSURE), units);
        junction.appears_in_control = nodeValue(project, node_index, EN_NODE_INCONTROL) != 0.0;
        result.nodes_junctions.append(junction);
        return;
    }

    if (node_type == EN_RESERVOIR)
    {
        NativeReservoirResult reservoir;
        reservoir.id = id;
        reservoir.net_demand_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_DEMAND), units.flow_units);
        reservoir.head_m = headToMetres(nodeValue(project, node_index, EN_HEAD), units.flow_units);
        reservoir.pressure_head_m = pressureToHeadMetres(nodeValue(project, node_index, EN_PRESSURE), units);
        reservoir.appears_in_control = nodeValue(project, node_index, EN_NODE_INCONTROL) != 0.0;
        result.nodes_reservoirs.append(reservoir);
        return;
    }

    if (node_type == EN_TANK)
    {
        NativeTankResult tank;
        tank.id = id;
        tank.net_demand_m3_per_h = flowToCubicMetresPerHour(nodeValue(project, node_index, EN_DEMAND), units.flow_units);
        tank.head_m = headToMetres(nodeValue(project, node_index, EN_HEAD), units.flow_units);
        tank.pressure_head_m = pressureToHeadMetres(nodeValue(project, node_index, EN_PRESSURE), units);
        tank.water_level_m = headToMetres(nodeValue(project, node_index, EN_TANKLEVEL), units.flow_units);
        tank.volume_m3 = volumeToCubicMetres(nodeValue(project, node_index, EN_TANKVOLUME), units.flow_units);
        tank.mixing_zone_volume_m3 = volumeToCubicMetres(nodeValue(project, node_index, EN_MIXZONEVOL), units.flow_units);
        tank.appears_in_control = nodeValue(project, node_index, EN_NODE_INCONTROL) != 0.0;
        result.nodes_tanks.append(tank);
        return;
    }

    throw std::runtime_error("Native reference runner encountered unknown EPANET node type");
}

NativePumpState pumpState(double value)
{
    switch (static_cast<int>(value))
    {
    case EN_PUMP_XHEAD:
        return NativePumpState::CannotSupplyHead;
    case EN_PUMP_CLOSED:
        return NativePumpState::Closed;
    case EN_PUMP_OPEN:
        return NativePumpState::Open;
    case EN_PUMP_XFLOW:
        return NativePumpState::CannotSupplyFlow;
    }
    throw std::runtime_error("Native reference runner encountered unknown EPANET pump state");
}

double valveSettingToCanonical(double value, int link_type, const NativeUnitSystem &units)
{
    switch (link_type)
    {
    case EN_PRV:
    case EN_PSV:
    case EN_PBV:
        return pressureToHeadMetres(value, units);
    case EN_FCV:
        return flowToCubicMetresPerHour(value, units.flow_units);
    default:
        return value;
    }
}

void appendLinkResult(EN_Project project, int link_index, const NativeUnitSystem &units, NativeHydraulicResult &result)
{
    int link_type = 0;
    checkEpanet(EN_getlinktype(project, link_index, &link_type), "EN_getlinktype");
    const QString id = linkId(project, link_index);

    if (link_type == EN_PIPE || link_type == EN_CVPIPE)
    {
        NativePipeResult pipe;
        pipe.id = id;
        pipe.flow_m3_per_h = flowToCubicMetresPerHour(linkValue(project, link_index, EN_FLOW), units.flow_units);
        pipe.leakage_flow_m3_per_h = flowToCubicMetresPerHour(linkValue(project, link_index, EN_LINK_LEAKAGE), units.flow_units);
        pipe.velocity_m_per_s = velocityToMetresPerSecond(linkValue(project, link_index, EN_VELOCITY), units.flow_units);
        pipe.head_loss_m = headToMetres(linkValue(project, link_index, EN_HEADLOSS), units.flow_units);
        pipe.open = static_cast<int>(linkValue(project, link_index, EN_STATUS)) != EN_CLOSED;
        pipe.roughness = linkValue(project, link_index, EN_SETTING);
        pipe.appears_in_control = linkValue(project, link_index, EN_LINK_INCONTROL) != 0.0;

        const double length_m = headToMetres(linkValue(project, link_index, EN_LENGTH), units.flow_units);
        if (length_m > 0.0)
            pipe.unit_head_loss_m_per_km = pipe.head_loss_m / length_m * 1000.0;

        const double diameter_m = diameterToMetres(linkValue(project, link_index, EN_DIAMETER), units.flow_units);
        const double flow_cubic_feet_per_second = std::abs(pipe.flow_m3_per_h)
            / kCubicMetresPerHourPerCubicFootPerSecond;
        constexpr double tiny_flow_cubic_feet_per_second = 1.0e-6;
        if (length_m > 0.0 && diameter_m > 0.0 && flow_cubic_feet_per_second > tiny_flow_cubic_feet_per_second)
        {
            const double head_loss_ft = pipe.head_loss_m / kMetresPerFoot;
            const double diameter_ft = diameter_m / kMetresPerFoot;
            const double length_ft = length_m / kMetresPerFoot;
            pipe.friction_factor = 39.725 * head_loss_ft * std::pow(diameter_ft, 5.0)
                / length_ft / std::pow(flow_cubic_feet_per_second, 2.0);
        }

        result.links_pipes.append(pipe);
        return;
    }

    if (link_type == EN_PUMP)
    {
        NativePumpResult pump;
        pump.id = id;
        pump.flow_m3_per_h = flowToCubicMetresPerHour(linkValue(project, link_index, EN_FLOW), units.flow_units);
        pump.velocity_m_per_s = velocityToMetresPerSecond(linkValue(project, link_index, EN_VELOCITY), units.flow_units);
        pump.head_gain_m = -headToMetres(linkValue(project, link_index, EN_HEADLOSS), units.flow_units);
        pump.open = static_cast<int>(linkValue(project, link_index, EN_STATUS)) != EN_CLOSED;
        pump.state = pumpState(linkValue(project, link_index, EN_PUMP_STATE));
        pump.speed = linkValue(project, link_index, EN_SETTING);
        pump.efficiency_percent = linkValue(project, link_index, EN_PUMP_EFFIC) * 100.0;
        pump.power_kw = linkValue(project, link_index, EN_ENERGY);
        pump.appears_in_control = linkValue(project, link_index, EN_LINK_INCONTROL) != 0.0;
        result.links_pumps.append(pump);
        return;
    }

    if (link_type >= EN_PRV && link_type <= EN_PCV)
    {
        NativeValveResult valve;
        valve.id = id;
        valve.diameter_mm = diameterToMetres(linkValue(project, link_index, EN_DIAMETER), units.flow_units) * 1000.0;
        valve.minor_loss = linkValue(project, link_index, EN_MINORLOSS);
        valve.flow_m3_per_h = flowToCubicMetresPerHour(linkValue(project, link_index, EN_FLOW), units.flow_units);
        valve.velocity_m_per_s = velocityToMetresPerSecond(linkValue(project, link_index, EN_VELOCITY), units.flow_units);
        valve.head_loss_m = headToMetres(linkValue(project, link_index, EN_HEADLOSS), units.flow_units);
        const int status = static_cast<int>(linkValue(project, link_index, EN_STATUS));
        valve.open = status != EN_CLOSED;
        valve.active = status > EN_OPEN;
        valve.setting = valveSettingToCanonical(linkValue(project, link_index, EN_SETTING), link_type, units);
        valve.appears_in_control = linkValue(project, link_index, EN_LINK_INCONTROL) != 0.0;
        result.links_valves.append(valve);
        return;
    }

    throw std::runtime_error("Native reference runner encountered unknown EPANET link type");
}

NativeHydraulicStatistics readStatistics(EN_Project project, const NativeUnitSystem &units)
{
    NativeHydraulicStatistics statistics;
    statistics.hydraulic_iterations = static_cast<std::int64_t>(statisticValue(project, EN_ITERATIONS));
    statistics.relative_error = statisticValue(project, EN_RELATIVEERROR);
    statistics.maximum_head_error_m = headToMetres(statisticValue(project, EN_MAXHEADERROR), units.flow_units);
    statistics.maximum_flow_change_m3_per_h = flowToCubicMetresPerHour(statisticValue(project, EN_MAXFLOWCHANGE), units.flow_units);
    statistics.deficient_nodes = static_cast<std::int64_t>(statisticValue(project, EN_DEFICIENTNODES));
    statistics.demand_reduction_percent = statisticValue(project, EN_DEMANDREDUCTION);
    statistics.leakage_loss_percent = statisticValue(project, EN_LEAKAGELOSS);
    return statistics;
}

NativeTimestepEvent readNextEvent(EN_Project project, const NativeReferenceConfiguration &configuration)
{
    int event_type = 0;
    long duration_s = 0;
    int element_index = 0;
    checkEpanet(EN_timetonextevent(project, &event_type, &duration_s, &element_index), "EN_timetonextevent");
    if (duration_s < 0)
        throw std::runtime_error("Native EPANET returned a negative next-event duration");

    NativeTimestepEvent event;
    event.time_until_event_s = duration_s;
    switch (event_type)
    {
    case EN_STEP_REPORT:
        event.type = NativeTimestepEventType::ReportStep;
        break;
    case EN_STEP_HYD:
        event.type = NativeTimestepEventType::HydraulicStep;
        break;
    case EN_STEP_WQ:
        event.type = NativeTimestepEventType::QualityStep;
        break;
    case EN_STEP_TANKEVENT:
        event.type = NativeTimestepEventType::TankEvent;
        event.tank_id = nodeId(project, element_index);
        break;
    case EN_STEP_CONTROLEVENT:
        event.type = NativeTimestepEventType::ControlEvent;
        event.control_id = configuration.control_ids_by_index.value(element_index);
        if (event.control_id.isEmpty())
            throw std::runtime_error("Native next-event control index has no scenario ID mapping");
        break;
    default:
        throw std::runtime_error("Native reference runner encountered unknown EPANET timestep event type");
    }
    return event;
}

void initializePumpAccumulators(const NativeHydraulicResult &result, QList<PumpEnergyAccumulator> &accumulators)
{
    if (!accumulators.isEmpty() || result.links_pumps.isEmpty())
        return;

    for (const NativePumpResult &pump : result.links_pumps)
    {
        PumpEnergyAccumulator accumulator;
        accumulator.pump_id = pump.id;
        accumulators.append(accumulator);
    }
}

double nativePatternFactor(EN_Project project, int pattern_index, long time_s)
{
    if (pattern_index <= 0)
        return 1.0;
    int pattern_length = 0;
    checkEpanet(EN_getpatternlen(project, pattern_index, &pattern_length), "EN_getpatternlen");
    if (pattern_length <= 0)
        return 1.0;
    const long pattern_step_s = timeParameter(project, EN_PATTERNSTEP);
    if (pattern_step_s <= 0)
        return 1.0;
    const long pattern_start_s = timeParameter(project, EN_PATTERNSTART);
    const long period = (time_s + pattern_start_s) / pattern_step_s;
    const int period_index = static_cast<int>(period % pattern_length) + 1;
    double factor = 1.0;
    checkEpanet(EN_getpatternvalue(project, pattern_index, period_index, &factor), "EN_getpatternvalue");
    return factor;
}

double nativePumpEnergyPrice(EN_Project project, const NativePumpResult &pump, long time_s)
{
    double price = optionValue(project, EN_GLOBALPRICE);
    int pattern_index = static_cast<int>(optionValue(project, EN_GLOBALPATTERN));
    const QByteArray pump_id = pump.id.toUtf8();
    int pump_index = 0;
    checkEpanet(EN_getlinkindex(project, pump_id.constData(), &pump_index), "EN_getlinkindex(pump energy)");
    const double pump_price = linkValue(project, pump_index, EN_PUMP_ECOST);
    const int pump_pattern_index = static_cast<int>(linkValue(project, pump_index, EN_PUMP_EPAT));
    if (pump_price > 0.0)
        price = pump_price;
    if (pump_pattern_index > 0)
        pattern_index = pump_pattern_index;
    return price * nativePatternFactor(project, pattern_index, time_s);
}

void accumulatePumpEnergy(EN_Project project, const NativeHydraulicResult &result, double interval_hours, long time_s, QList<PumpEnergyAccumulator> &accumulators, double &system_peak_power_kw)
{
    initializePumpAccumulators(result, accumulators);
    if (accumulators.size() != result.links_pumps.size())
        throw std::runtime_error("Native pump result set changed during the hydraulic timeline");

    double simultaneous_power_kw = 0.0;
    for (int index = 0; index < result.links_pumps.size(); index++)
    {
        const NativePumpResult &pump = result.links_pumps.at(index);
        if (pump.efficiency_percent <= 0.0)
            continue;

        PumpEnergyAccumulator &accumulator = accumulators[index];
        accumulator.online_hours += interval_hours;
        accumulator.efficiency_percent_hours += pump.efficiency_percent * interval_hours;
        accumulator.power_kw_hours += pump.power_kw * interval_hours;
        constexpr double minimum_energy_flow_m3_per_h = 1.0e-6 * kCubicMetresPerHourPerCubicFootPerSecond;
        const double energy_flow_m3_per_h = std::max(minimum_energy_flow_m3_per_h, std::abs(pump.flow_m3_per_h));
        accumulator.kw_per_flow_hours += pump.power_kw / energy_flow_m3_per_h * interval_hours;
        accumulator.peak_power_kw = std::max(accumulator.peak_power_kw, pump.power_kw);
        accumulator.total_cost += nativePumpEnergyPrice(project, pump, time_s) * pump.power_kw * interval_hours;
        simultaneous_power_kw += pump.power_kw;
    }
    system_peak_power_kw = std::max(system_peak_power_kw, simultaneous_power_kw);
}

void storePumpEnergy(double duration_hours, double demand_charge_per_kw, const QList<PumpEnergyAccumulator> &accumulators, double system_peak_power_kw, NativeHydraulicResult &result)
{
    for (const PumpEnergyAccumulator &accumulator : accumulators)
    {
        NativePumpEnergyUsage usage;
        usage.pump_id = accumulator.pump_id;
        if (duration_hours > 0.0)
            usage.time_online_percent = accumulator.online_hours / duration_hours * 100.0;
        else if (accumulator.online_hours > 0.0)
            usage.time_online_percent = 100.0;
        if (accumulator.online_hours > 0.0)
        {
            usage.average_efficiency_percent = accumulator.efficiency_percent_hours / accumulator.online_hours;
            usage.average_kw_per_flow_unit = accumulator.kw_per_flow_hours / accumulator.online_hours;
            usage.average_power_kw = accumulator.power_kw_hours / accumulator.online_hours;
        }
        usage.peak_power_kw = accumulator.peak_power_kw;
        if (duration_hours > 0.0)
            usage.average_cost_per_day = accumulator.total_cost * 24.0 / duration_hours;
        else
            usage.average_cost_per_day = accumulator.total_cost * 24.0;
        result.links_pump_energy_usage.append(usage);
        result.energy_usage.energy_cost_per_day += usage.average_cost_per_day;
    }

    result.energy_usage.peak_power_kw = system_peak_power_kw;
    result.energy_usage.demand_charge_per_day = system_peak_power_kw * demand_charge_per_kw;
    result.energy_usage.total_cost_per_day = result.energy_usage.energy_cost_per_day
        + result.energy_usage.demand_charge_per_day;
}

void accumulateFlowBalance(const NativeHydraulicResult &result, double interval_seconds, FlowBalanceAccumulator &accumulator)
{
    double total_inflow = 0.0;
    double total_outflow = 0.0;
    double consumer_demand = 0.0;
    double demand_deficit = 0.0;
    double emitter_flow = 0.0;
    double leakage_flow = 0.0;
    double storage_flow = 0.0;

    for (const NativeJunctionResult &junction : result.nodes_junctions)
    {
        if (junction.demand_delivered_m3_per_h < 0.0)
            total_inflow -= junction.demand_delivered_m3_per_h;
        else
        {
            consumer_demand += junction.demand_delivered_m3_per_h;
            total_outflow += junction.demand_delivered_m3_per_h;
        }
        emitter_flow += junction.emitter_flow_m3_per_h;
        total_outflow += junction.emitter_flow_m3_per_h;
        leakage_flow += junction.leakage_flow_m3_per_h;
        total_outflow += junction.leakage_flow_m3_per_h;
        demand_deficit += junction.demand_deficit_m3_per_h;
    }

    for (const NativeReservoirResult &reservoir : result.nodes_reservoirs)
    {
        if (reservoir.net_demand_m3_per_h >= 0.0)
            total_outflow += reservoir.net_demand_m3_per_h;
        else
            total_inflow -= reservoir.net_demand_m3_per_h;
    }

    for (const NativeTankResult &tank : result.nodes_tanks)
        storage_flow += tank.net_demand_m3_per_h;

    accumulator.covered_seconds += interval_seconds;
    accumulator.total_inflow += total_inflow * interval_seconds;
    accumulator.total_outflow += total_outflow * interval_seconds;
    accumulator.consumer_demand += consumer_demand * interval_seconds;
    accumulator.demand_deficit += demand_deficit * interval_seconds;
    accumulator.emitter_flow += emitter_flow * interval_seconds;
    accumulator.leakage_flow += leakage_flow * interval_seconds;
    accumulator.storage_flow += storage_flow * interval_seconds;
}

void storeFlowBalance(const FlowBalanceAccumulator &accumulator, NativeHydraulicResult &result)
{
    if (accumulator.covered_seconds <= 0.0)
        return;

    result.flow_balance.total_inflow_m3_per_h = accumulator.total_inflow / accumulator.covered_seconds;
    result.flow_balance.total_outflow_m3_per_h = accumulator.total_outflow / accumulator.covered_seconds;
    result.flow_balance.consumer_demand_m3_per_h = accumulator.consumer_demand / accumulator.covered_seconds;
    result.flow_balance.demand_deficit_m3_per_h = accumulator.demand_deficit / accumulator.covered_seconds;
    result.flow_balance.emitter_flow_m3_per_h = accumulator.emitter_flow / accumulator.covered_seconds;
    result.flow_balance.leakage_flow_m3_per_h = accumulator.leakage_flow / accumulator.covered_seconds;
    result.flow_balance.storage_flow_m3_per_h = accumulator.storage_flow / accumulator.covered_seconds;

    double adjusted_inflow = result.flow_balance.total_inflow_m3_per_h;
    double adjusted_outflow = result.flow_balance.total_outflow_m3_per_h;
    if (result.flow_balance.storage_flow_m3_per_h > 0.0)
        adjusted_outflow += result.flow_balance.storage_flow_m3_per_h;
    else
        adjusted_inflow -= result.flow_balance.storage_flow_m3_per_h;
    if (adjusted_inflow == adjusted_outflow)
        result.flow_balance.flow_balance_ratio = 1.0;
    else if (adjusted_inflow > 0.0)
        result.flow_balance.flow_balance_ratio = adjusted_outflow / adjusted_inflow;
}
}

NativeHydraulicTimeline runNativeEpanetReference(const NativeReferenceConfiguration &configuration)
{
    NativeHydraulicTimeline timeline;
    try
    {
        if (configuration.input_file.isEmpty())
            throw std::runtime_error("Native reference input file path is empty");

        QTemporaryDir temporary_directory;
        if (!temporary_directory.isValid())
            throw std::runtime_error("Could not create a temporary directory for the native EPANET report");

        NativeProject project;
        const QByteArray input_file = QFile::encodeName(configuration.input_file);
        const QByteArray report_file = QFile::encodeName(temporary_directory.filePath(QStringLiteral("native-reference.rpt")));
        checkEpanet(EN_open(project.handle(), input_file.constData(), report_file.constData(), ""), "EN_open");
        project.markProjectOpen();
        applyReferenceVariant(project.handle(), configuration.variant);

        const NativeUnitSystem units = readUnitSystem(project.handle());

        int demand_model = EN_DDA;
        double minimum_pressure = 0.0;
        double required_pressure = 0.0;
        double pressure_exponent = 0.0;
        checkEpanet(EN_getdemandmodel(
            project.handle(),
            &demand_model,
            &minimum_pressure,
            &required_pressure,
            &pressure_exponent),
            "EN_getdemandmodel");
        timeline.pressure_driven_demand = demand_model == EN_PDA;
        timeline.minimum_pressure_head_m = pressureToHeadMetres(minimum_pressure, units);
        timeline.required_pressure_head_m = pressureToHeadMetres(required_pressure, units);
        timeline.pressure_exponent = pressure_exponent;

        const long duration_s = timeParameter(project.handle(), EN_DURATION);
        if (duration_s < 0)
            throw std::runtime_error("Native EPANET returned a negative duration");
        const double demand_charge_per_kw = optionValue(project.handle(), EN_DEMANDCHARGE);

        checkEpanet(EN_openH(project.handle()), "EN_openH");
        project.markHydraulicsOpen();
        checkEpanet(EN_initH(project.handle(), EN_INITFLOW), "EN_initH");

        const int node_count = objectCount(project.handle(), EN_NODECOUNT);
        const int link_count = objectCount(project.handle(), EN_LINKCOUNT);
        long previous_time_s = -1;
        QList<PumpEnergyAccumulator> pump_energy_accumulators;
        double system_peak_power_kw = 0.0;
        FlowBalanceAccumulator flow_balance_accumulator;

        while (true)
        {
            long current_time_s = 0;
            const int run_error = checkEpanetAllowWarning(EN_runH(project.handle(), &current_time_s), "EN_runH");
            if (run_error > 0)
                timeline.warning_codes.append(run_error);
            if (current_time_s < 0)
                throw std::runtime_error("Native EPANET returned a negative elapsed time");
            if (previous_time_s >= 0 && current_time_s <= previous_time_s)
                throw std::runtime_error("Native EPANET hydraulic time did not advance");
            previous_time_s = current_time_s;

            NativeHydraulicResult result;
            result.time_elapsed_s = current_time_s;
            for (int node_index = 1; node_index <= node_count; node_index++)
                appendNodeResult(project.handle(), node_index, units, result);
            for (int link_index = 1; link_index <= link_count; link_index++)
                appendLinkResult(project.handle(), link_index, units, result);
            result.statistics = readStatistics(project.handle(), units);
            result.event_next = readNextEvent(project.handle(), configuration);

            long next_step_s = 0;
            const int next_error = checkEpanetAllowWarning(EN_nextH(project.handle(), &next_step_s), "EN_nextH");
            if (next_error > 0)
                timeline.warning_codes.append(next_error);
            if (next_step_s < 0)
                throw std::runtime_error("Native EPANET returned a negative hydraulic timestep");
            if (result.event_next.time_until_event_s != next_step_s)
            {
                result.event_next.type = NativeTimestepEventType::HydraulicStep;
                result.event_next.tank_id.clear();
                result.event_next.control_id.clear();
            }
            result.event_next.time_until_event_s = next_step_s;

            if (next_step_s > 0)
            {
                accumulatePumpEnergy(project.handle(), result, static_cast<double>(next_step_s) / 3600.0,
                    current_time_s, pump_energy_accumulators, system_peak_power_kw);
                accumulateFlowBalance(result, static_cast<double>(next_step_s), flow_balance_accumulator);
            }
            else if (duration_s == 0)
            {
                accumulatePumpEnergy(project.handle(), result, 1.0, current_time_s,
                    pump_energy_accumulators, system_peak_power_kw);
                accumulateFlowBalance(result, 1.0, flow_balance_accumulator);
            }

            timeline.results.append(result);
            if (next_step_s <= 0)
                break;
        }

        project.closeHydraulics();
        if (!timeline.results.isEmpty())
        {
            NativeHydraulicResult &final_result = timeline.results.last();
            storePumpEnergy(static_cast<double>(duration_s) / 3600.0, demand_charge_per_kw,
                pump_energy_accumulators, system_peak_power_kw, final_result);
            storeFlowBalance(flow_balance_accumulator, final_result);
        }
        project.closeProject();
        timeline.success = true;
    }
    catch (const std::exception &exception)
    {
        timeline.error = QString::fromUtf8(exception.what());
    }
    return timeline;
}
}
