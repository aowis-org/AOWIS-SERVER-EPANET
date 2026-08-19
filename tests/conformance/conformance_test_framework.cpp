#include "conformance_test_framework.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace AowisEpanetTests
{
namespace
{
bool isValidScenarioName(std::string_view name)
{
    if (name.empty())
        return false;

    for (const char character : name)
    {
        const bool is_lowercase_letter = character >= 'a' && character <= 'z';
        const bool is_digit = character >= '0' && character <= '9';
        if (!is_lowercase_letter && !is_digit && character != '-')
            return false;
    }
    return true;
}

std::string labelsText(const std::vector<std::string> &labels)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < labels.size(); index++)
    {
        if (index > 0)
            stream << ',';
        stream << labels.at(index);
    }
    return stream.str();
}

int runScenario(const ScenarioDefinition &scenario, std::ostream &output_stream, std::ostream &error_stream)
{
    TestContext context(scenario.name, error_stream);
    scenario.function(context);

    if (context.failureCount() == 0)
    {
        output_stream << "PASS: " << scenario.name << '\n';
        return 0;
    }

    error_stream << "Scenario failed with " << context.failureCount() << " assertion(s): " << scenario.name << '\n';
    return 1;
}

int verifyScenarioManifest(int argc, char *argv[], const ScenarioRegistry &registry, std::ostream &output_stream, std::ostream &error_stream)
{
    std::set<std::string> registered_names;
    for (const ScenarioDefinition &scenario : registry.scenarios())
        registered_names.insert(scenario.name);

    std::set<std::string> expected_names;
    for (int argument_index = 2; argument_index < argc; argument_index++)
        expected_names.insert(argv[argument_index]);

    if (registered_names == expected_names)
    {
        output_stream << "Scenario manifest matches all " << registered_names.size() << " registered scenarios.\n";
        return 0;
    }

    for (const std::string &name : registered_names)
    {
        if (!expected_names.contains(name))
            error_stream << "Scenario is registered in C++ but missing from CTest: " << name << '\n';
    }
    for (const std::string &name : expected_names)
    {
        if (!registered_names.contains(name))
            error_stream << "Scenario is registered in CTest but missing from C++: " << name << '\n';
    }
    return 1;
}
}

NumericTolerance toleranceFor(HydraulicQuantity quantity)
{
    switch (quantity)
    {
    case HydraulicQuantity::Dimensionless:
        return {1.0e-9, 1.0e-7};
    case HydraulicQuantity::FlowM3PerHour:
        return {1.0e-6, 1.0e-6};
    case HydraulicQuantity::HeadMetres:
    case HydraulicQuantity::PressureHeadMetres:
    case HydraulicQuantity::LengthMetres:
        return {1.0e-7, 1.0e-6};
    case HydraulicQuantity::VelocityMetresPerSecond:
        return {1.0e-8, 1.0e-6};
    case HydraulicQuantity::VolumeM3:
        return {1.0e-6, 1.0e-6};
    case HydraulicQuantity::PowerKw:
    case HydraulicQuantity::EnergyKwh:
        return {1.0e-6, 1.0e-6};
    case HydraulicQuantity::Cost:
        return {1.0e-8, 1.0e-6};
    case HydraulicQuantity::Percent:
        return {1.0e-6, 1.0e-6};
    case HydraulicQuantity::FrictionFactor:
        return {1.0e-10, 1.0e-6};
    case HydraulicQuantity::Setting:
        return {1.0e-7, 1.0e-6};
    }
    throw std::invalid_argument("Unknown hydraulic quantity");
}

std::string_view quantityName(HydraulicQuantity quantity)
{
    switch (quantity)
    {
    case HydraulicQuantity::Dimensionless:
        return "dimensionless";
    case HydraulicQuantity::FlowM3PerHour:
        return "flow_m3_per_h";
    case HydraulicQuantity::HeadMetres:
        return "hydraulic_head_m";
    case HydraulicQuantity::PressureHeadMetres:
        return "pressure_head_m";
    case HydraulicQuantity::LengthMetres:
        return "length_m";
    case HydraulicQuantity::VelocityMetresPerSecond:
        return "velocity_m_per_s";
    case HydraulicQuantity::VolumeM3:
        return "volume_m3";
    case HydraulicQuantity::PowerKw:
        return "power_kw";
    case HydraulicQuantity::EnergyKwh:
        return "energy_kwh";
    case HydraulicQuantity::Cost:
        return "cost";
    case HydraulicQuantity::Percent:
        return "percent";
    case HydraulicQuantity::FrictionFactor:
        return "friction_factor";
    case HydraulicQuantity::Setting:
        return "setting";
    }
    return "unknown";
}

TestContext::TestContext(std::string scenario_name, std::ostream &error_stream)
    : scenario_name_(std::move(scenario_name)),
      error_stream_(&error_stream)
{
}

void TestContext::expect(bool condition, std::string_view message)
{
    if (condition)
        return;

    *this->error_stream_ << "FAIL\n  Scenario: " << this->scenario_name_ << '\n';
    writeMessage(message);
    this->failure_count_++;
}

void TestContext::expectEqual(bool actual, bool expected, const ComparisonContext &comparison, std::string_view message)
{
    if (actual == expected)
        return;

    writeComparisonHeader(comparison);
    *this->error_stream_ << "  Expected: " << std::boolalpha << expected << '\n'
                        << "  Actual: " << std::boolalpha << actual << '\n';
    writeMessage(message);
    this->failure_count_++;
}

void TestContext::expectEqual(std::int64_t actual, std::int64_t expected, const ComparisonContext &comparison, std::string_view message)
{
    if (actual == expected)
        return;

    writeComparisonHeader(comparison);
    *this->error_stream_ << "  Expected: " << expected << '\n'
                        << "  Actual: " << actual << '\n';
    writeMessage(message);
    this->failure_count_++;
}

void TestContext::expectEqual(std::string_view actual, std::string_view expected, const ComparisonContext &comparison, std::string_view message)
{
    if (actual == expected)
        return;

    writeComparisonHeader(comparison);
    *this->error_stream_ << "  Expected: " << expected << '\n'
                        << "  Actual: " << actual << '\n';
    writeMessage(message);
    this->failure_count_++;
}

void TestContext::expectNear(double actual, double expected, HydraulicQuantity quantity, const ComparisonContext &comparison, std::string_view message)
{
    expectNear(actual, expected, toleranceFor(quantity), comparison, message);
}

void TestContext::expectNear(double actual, double expected, NumericTolerance tolerance, const ComparisonContext &comparison, std::string_view message)
{
    const bool both_infinite_with_same_sign = std::isinf(actual) && std::isinf(expected) && std::signbit(actual) == std::signbit(expected);
    const double difference = std::abs(actual - expected);
    const double scale = std::max(std::abs(actual), std::abs(expected));
    const double allowed_difference = tolerance.absolute + tolerance.relative * scale;
    if (both_infinite_with_same_sign || (std::isfinite(actual) && std::isfinite(expected) && difference <= allowed_difference))
        return;

    writeComparisonHeader(comparison);
    *this->error_stream_ << std::setprecision(std::numeric_limits<double>::max_digits10)
                        << "  Expected: " << expected << '\n'
                        << "  Actual: " << actual << '\n'
                        << "  Difference: " << difference << '\n'
                        << "  Tolerance: abs=" << tolerance.absolute << ", rel=" << tolerance.relative
                        << ", allowed=" << allowed_difference << '\n';
    writeMessage(message);
    this->failure_count_++;
}

int TestContext::failureCount() const
{
    return this->failure_count_;
}

const std::string &TestContext::scenarioName() const
{
    return this->scenario_name_;
}

void TestContext::writeComparisonHeader(const ComparisonContext &comparison)
{
    *this->error_stream_ << "FAIL\n  Scenario: " << this->scenario_name_ << '\n';
    if (comparison.time_s >= 0)
        *this->error_stream_ << "  Time: " << comparison.time_s << " s\n";
    if (!comparison.entity_type.empty() || !comparison.entity_id.empty())
        *this->error_stream_ << "  Entity: " << comparison.entity_type << ' ' << comparison.entity_id << '\n';
    if (!comparison.field.empty())
        *this->error_stream_ << "  Field: " << comparison.field << '\n';
}

void TestContext::writeMessage(std::string_view message)
{
    if (!message.empty())
        *this->error_stream_ << "  Message: " << message << '\n';
}

void ScenarioRegistry::add(ScenarioDefinition scenario)
{
    if (!isValidScenarioName(scenario.name))
        throw std::invalid_argument("Scenario name must contain only lowercase letters, digits, and hyphens: " + scenario.name);
    if (scenario.description.empty())
        throw std::invalid_argument("Scenario description cannot be empty: " + scenario.name);
    if (scenario.function == nullptr)
        throw std::invalid_argument("Scenario function cannot be null: " + scenario.name);
    if (find(scenario.name) != nullptr)
        throw std::invalid_argument("Duplicate scenario name: " + scenario.name);

    this->scenarios_.push_back(std::move(scenario));
}

const ScenarioDefinition *ScenarioRegistry::find(std::string_view name) const
{
    for (const ScenarioDefinition &scenario : this->scenarios_)
    {
        if (scenario.name == name)
            return &scenario;
    }
    return nullptr;
}

const std::vector<ScenarioDefinition> &ScenarioRegistry::scenarios() const
{
    return this->scenarios_;
}

int runTestProgram(int argc, char *argv[], const ScenarioRegistry &registry, std::ostream &output_stream, std::ostream &error_stream)
{
    try
    {
        if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--all"))
        {
            int failed_scenarios = 0;
            for (const ScenarioDefinition &scenario : registry.scenarios())
                failed_scenarios += runScenario(scenario, output_stream, error_stream);
            return failed_scenarios == 0 ? 0 : 1;
        }

        const std::string_view command(argv[1]);
        if (command == "--list" && argc == 2)
        {
            for (const ScenarioDefinition &scenario : registry.scenarios())
                output_stream << scenario.name << '\t' << labelsText(scenario.labels) << '\t' << scenario.description << '\n';
            return 0;
        }

        if (command == "--scenario" && argc == 3)
        {
            const ScenarioDefinition *scenario = registry.find(argv[2]);
            if (scenario == nullptr)
            {
                error_stream << "Unknown scenario: " << argv[2] << '\n';
                return 2;
            }
            return runScenario(*scenario, output_stream, error_stream);
        }

        if (command == "--verify-scenarios" && argc >= 2)
            return verifyScenarioManifest(argc, argv, registry, output_stream, error_stream);

        error_stream << "Usage: " << argv[0] << " [--all | --list | --scenario NAME | --verify-scenarios NAME ...]\n";
        return 2;
    }
    catch (const std::exception &exception)
    {
        error_stream << "Test framework error: " << exception.what() << '\n';
        return 2;
    }
}
}
