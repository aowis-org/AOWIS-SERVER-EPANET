#include "epanet_network_validator.h"

#include "epanet_network_validator_parts.h"
#include "epanet_network_validator_support.h"
#include "epanet_status_helpers.h"

#include <QList>

namespace
{
HydraulicSimulationStatus qualityRunValidationStatus(HydraulicSimulationStatus status)
{
    if (!status.success)
        status.stage = HydraulicSimulationStatusStage::ConfigureOptions;
    return status;
}

QList<HydraulicSimulationStatus> validateQualityRunReferences(
    const NetworkHydraulic &network,
    const WaterQualitySolverOptions &options)
{
    QList<HydraulicSimulationStatus> failures;
    if (options.analysis != WaterQualityAnalysisType::SourceTrace)
        return failures;

    NetworkHydraulic enabled_network = network;
    enabled_network.nodes_junctions = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_junctions);
    enabled_network.nodes_reservoirs = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_reservoirs);
    enabled_network.nodes_tanks = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_tanks);

    const HydraulicSimulationStatus status = qualityRunValidationStatus(EpanetNetworkValidatorSupport::validateReference(
        EpanetNetworkValidatorSupport::nodeUuids(network),
        EpanetNetworkValidatorSupport::nodeUuids(enabled_network),
        options.trace_node_uuid,
        HydraulicSimulationStatusEntityType::QualitySolver,
        network.id,
        network.uuid,
        QStringLiteral("source-trace node")));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    return failures;
}

QList<HydraulicSimulationStatus> validateQualityRunNumerics(
    const NetworkHydraulic &network,
    const WaterQualitySolverOptions &options)
{
    QList<HydraulicSimulationStatus> failures;
    HydraulicSimulationStatus status = qualityRunValidationStatus(EpanetNetworkValidatorSupport::validateFiniteNonNegative(
        options.chemical_tolerance_mg_per_l,
        HydraulicSimulationStatusEntityType::QualitySolver,
        network.id,
        network.uuid,
        QStringLiteral("quality_run.chemical_tolerance_mg_per_l")));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = qualityRunValidationStatus(EpanetNetworkValidatorSupport::validateFiniteNonNegative(
        options.water_age_tolerance_h,
        HydraulicSimulationStatusEntityType::QualitySolver,
        network.id,
        network.uuid,
        QStringLiteral("quality_run.water_age_tolerance_h")));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = qualityRunValidationStatus(EpanetNetworkValidatorSupport::validateFiniteNonNegative(
        options.source_trace_tolerance_percent,
        HydraulicSimulationStatusEntityType::QualitySolver,
        network.id,
        network.uuid,
        QStringLiteral("quality_run.source_trace_tolerance_percent")));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);
    status = qualityRunValidationStatus(EpanetNetworkValidatorSupport::validateFiniteNonNegative(
        options.relative_diffusivity,
        HydraulicSimulationStatusEntityType::QualitySolver,
        network.id,
        network.uuid,
        QStringLiteral("quality_run.relative_diffusivity")));
    EpanetNetworkValidatorSupport::appendValidationFailure(failures, status);

    if (options.analysis == WaterQualityAnalysisType::Chemical && options.chemical_name.trimmed().isEmpty())
    {
        failures.append(makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureQuality,
            HydraulicSimulationStatusEntityType::QualitySolver,
            network.id,
            network.uuid,
            QStringLiteral("Chemical water-quality analysis requires a chemical name")));
    }

    return failures;
}

HydraulicSimulationStatus validateQualityRun(
    const NetworkHydraulic &network,
    const WaterQualitySolverOptions &options,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    QList<HydraulicSimulationStatus> failures = validateQualityRunReferences(network, options);
    EpanetNetworkValidatorSupport::appendValidationFailures(failures, validateQualityRunNumerics(network, options));

    if (validation_failures != nullptr)
        *validation_failures = failures;

    return failures.isEmpty() ? makeEpanetSuccess() : failures.first();
}

HydraulicSimulationStatus validateNetwork(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    QList<HydraulicSimulationStatus> failures = EpanetNetworkValidatorParts::validateIdentities(network);
    EpanetNetworkValidatorSupport::appendValidationFailures(failures, EpanetNetworkValidatorParts::validateReferences(network));
    EpanetNetworkValidatorSupport::appendValidationFailures(failures, EpanetNetworkValidatorParts::validateNumerics(network));

    if (validation_failures != nullptr)
        *validation_failures = failures;

    return failures.isEmpty() ? makeEpanetSuccess() : failures.first();
}
}

HydraulicSimulationStatus validateEpanetNetwork(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    return validateNetwork(network, validation_failures);
}

HydraulicSimulationStatus validateEpanetQualityRun(
    const NetworkHydraulic &network,
    const WaterQualitySolverOptions &options,
    QList<HydraulicSimulationStatus> *validation_failures)
{
    return validateQualityRun(network, options, validation_failures);
}
