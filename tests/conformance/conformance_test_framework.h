#ifndef AOWIS_EPANET_CONFORMANCE_TEST_FRAMEWORK_H
#define AOWIS_EPANET_CONFORMANCE_TEST_FRAMEWORK_H

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace AowisEpanetTests
{
enum class HydraulicQuantity
{
    Dimensionless,
    FlowM3PerHour,
    HeadMetres,
    PressureHeadMetres,
    LengthMetres,
    VelocityMetresPerSecond,
    VolumeM3,
    PowerKw,
    EnergyKwh,
    Cost,
    Percent,
    FrictionFactor,
    Setting
};

struct NumericTolerance
{
    double absolute = 0.0;
    double relative = 0.0;
};

NumericTolerance toleranceFor(HydraulicQuantity quantity);
std::string_view quantityName(HydraulicQuantity quantity);

struct ComparisonContext
{
    std::int64_t time_s = -1;
    std::string entity_type;
    std::string entity_id;
    std::string field;
};

class TestContext
{
public:
    TestContext(std::string scenario_name, std::ostream &error_stream);

    void expect(bool condition, std::string_view message);
    void expectEqual(bool actual, bool expected, const ComparisonContext &comparison, std::string_view message = {});
    void expectEqual(std::int64_t actual, std::int64_t expected, const ComparisonContext &comparison, std::string_view message = {});
    void expectEqual(std::string_view actual, std::string_view expected, const ComparisonContext &comparison, std::string_view message = {});
    void expectNear(double actual, double expected, HydraulicQuantity quantity, const ComparisonContext &comparison, std::string_view message = {});
    void expectNear(double actual, double expected, NumericTolerance tolerance, const ComparisonContext &comparison, std::string_view message = {});

    int failureCount() const;
    const std::string &scenarioName() const;

private:
    void writeComparisonHeader(const ComparisonContext &comparison);
    void writeMessage(std::string_view message);

    std::string scenario_name_;
    std::ostream *error_stream_ = nullptr;
    int failure_count_ = 0;
};

using ScenarioFunction = void (*)(TestContext &context);

struct ScenarioDefinition
{
    std::string name;
    std::string description;
    std::vector<std::string> labels;
    ScenarioFunction function = nullptr;
};

class ScenarioRegistry
{
public:
    void add(ScenarioDefinition scenario);
    const ScenarioDefinition *find(std::string_view name) const;
    const std::vector<ScenarioDefinition> &scenarios() const;

private:
    std::vector<ScenarioDefinition> scenarios_;
};

int runTestProgram(int argc, char *argv[], const ScenarioRegistry &registry, std::ostream &output_stream, std::ostream &error_stream);
}

#endif // AOWIS_EPANET_CONFORMANCE_TEST_FRAMEWORK_H
