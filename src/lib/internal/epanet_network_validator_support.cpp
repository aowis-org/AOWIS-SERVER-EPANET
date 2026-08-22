#include "epanet_network_validator_support.h"

#include "epanet_status_helpers.h"

#include <cmath>

namespace EpanetNetworkValidatorSupport
{
QSet<QUuid> nodeUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> uuids = entityUuids(network.nodes_junctions);
    uuids.unite(entityUuids(network.nodes_reservoirs));
    uuids.unite(entityUuids(network.nodes_tanks));
    return uuids;
}

QSet<QUuid> linkUuids(const NetworkHydraulic &network)
{
    QSet<QUuid> uuids = entityUuids(network.links_pipes);
    uuids.unite(entityUuids(network.links_pumps));
    uuids.unite(entityUuids(network.links_valves));
    return uuids;
}

HydraulicSimulationStatus validationStatus(
    HydraulicSimulationStatusOperation operation,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &message,
    const QStringList &details)
{
    HydraulicSimulationStatus status = makeEpanetStatus(
        HydraulicSimulationStatusStage::BuildNetwork,
        operation,
        entity_type,
        entity_id,
        entity_uuid,
        message);
    status.details = details;
    return status;
}

void collectIdentityFailures(
    QList<HydraulicSimulationStatus> &failures,
    QSet<QUuid> &all_uuids,
    QSet<QString> &ids,
    const QString &namespace_name,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_name,
    const QString &id,
    const QUuid &uuid)
{
    const bool uuid_missing = uuid.isNull();
    const bool id_missing = id.isEmpty();

    if (uuid_missing)
    {
        failures.append(validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no UUID").arg(entity_name)));
    }

    if (id_missing)
    {
        failures.append(validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            id,
            uuid,
            QStringLiteral("%1 has no ID").arg(entity_name)));
    }

    if (!uuid_missing)
    {
        if (all_uuids.contains(uuid))
        {
            failures.append(validationStatus(
                HydraulicSimulationStatusOperation::ResolveEntity,
                entity_type,
                id,
                uuid,
                QStringLiteral("%1 UUID is duplicated across the hydraulic model").arg(entity_name),
                {QStringLiteral("Duplicate UUID: %1").arg(uuid.toString(QUuid::WithoutBraces))}));
        }
        else
        {
            all_uuids.insert(uuid);
        }
    }

    if (!id_missing)
    {
        if (ids.contains(id))
        {
            failures.append(validationStatus(
                HydraulicSimulationStatusOperation::ResolveEntity,
                entity_type,
                id,
                uuid,
                QStringLiteral("%1 ID is duplicated in the EPANET %2 namespace").arg(entity_name, namespace_name),
                {QStringLiteral("Duplicate ID: %1").arg(id)}));
        }
        else
        {
            ids.insert(id);
        }
    }
}

HydraulicSimulationStatusOperation numericValidationOperation(HydraulicSimulationStatusEntityType entity_type)
{
    switch (entity_type)
    {
    case HydraulicSimulationStatusEntityType::Pattern:
        return HydraulicSimulationStatusOperation::AddPattern;
    case HydraulicSimulationStatusEntityType::Curve:
        return HydraulicSimulationStatusOperation::AddCurve;
    case HydraulicSimulationStatusEntityType::HydraulicSolver:
        return HydraulicSimulationStatusOperation::ConfigureHydraulics;
    case HydraulicSimulationStatusEntityType::QualitySolver:
        return HydraulicSimulationStatusOperation::ConfigureQuality;
    case HydraulicSimulationStatusEntityType::Report:
        return HydraulicSimulationStatusOperation::ConfigureReport;
    case HydraulicSimulationStatusEntityType::Control:
        return HydraulicSimulationStatusOperation::AddControl;
    case HydraulicSimulationStatusEntityType::Rule:
        return HydraulicSimulationStatusOperation::AddRule;
    default:
        return HydraulicSimulationStatusOperation::SetEntityMetadata;
    }
}

HydraulicSimulationStatus invalidNumeric(
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name,
    const QString &requirement)
{
    return validationStatus(
        numericValidationOperation(entity_type),
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 contains an invalid numeric value").arg(entity_id),
        {QStringLiteral("%1: %2").arg(field_name, requirement)});
}

HydraulicSimulationStatus validateFinite(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value))
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite"));
}

HydraulicSimulationStatus validateLongitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= -180.0 && value <= 180.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and between -180 and 180 degrees"));
}

HydraulicSimulationStatus validateLatitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= -90.0 && value <= 90.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and between -90 and 90 degrees"));
}

HydraulicSimulationStatus validateFiniteNonNegative(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value >= 0.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and non-negative"));
}

HydraulicSimulationStatus validateFinitePositive(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name)
{
    if (std::isfinite(value) && value > 0.0)
        return makeEpanetSuccess();
    return invalidNumeric(entity_type, entity_id, entity_uuid, field_name, QStringLiteral("must be finite and positive"));
}

HydraulicSimulationStatus validateReference(
    const QSet<QUuid> &all_uuids,
    const QSet<QUuid> &enabled_uuids,
    const QUuid &target_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null)
{
    if (target_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (!all_uuids.contains(target_uuid))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
            {QStringLiteral("Referenced UUID: %1").arg(target_uuid.toString(QUuid::WithoutBraces))});

    if (!enabled_uuids.contains(target_uuid))
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 references a disabled %2").arg(entity_id, relationship),
            {QStringLiteral("Referenced UUID: %1").arg(target_uuid.toString(QUuid::WithoutBraces))});

    return makeEpanetSuccess();
}

HydraulicSimulationStatus validatePatternReference(
    const QSet<QUuid> &pattern_uuids,
    const QUuid &pattern_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null)
{
    if (pattern_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (pattern_uuids.contains(pattern_uuid))
        return makeEpanetSuccess();

    return validationStatus(
        HydraulicSimulationStatusOperation::ResolveEntity,
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
        {QStringLiteral("Referenced UUID: %1").arg(pattern_uuid.toString(QUuid::WithoutBraces))});
}

HydraulicSimulationStatus validateCurveReference(
    const QSet<QUuid> &curve_uuids,
    const QUuid &curve_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null)
{
    if (curve_uuid.isNull())
    {
        if (allow_null)
            return makeEpanetSuccess();
        return validationStatus(
            HydraulicSimulationStatusOperation::ResolveEntity,
            entity_type,
            entity_id,
            entity_uuid,
            QStringLiteral("%1 has no %2 UUID").arg(entity_id, relationship));
    }

    if (curve_uuids.contains(curve_uuid))
        return makeEpanetSuccess();

    return validationStatus(
        HydraulicSimulationStatusOperation::ResolveEntity,
        entity_type,
        entity_id,
        entity_uuid,
        QStringLiteral("%1 references a missing %2").arg(entity_id, relationship),
        {QStringLiteral("Referenced UUID: %1").arg(curve_uuid.toString(QUuid::WithoutBraces))});
}

void appendValidationFailure(QList<HydraulicSimulationStatus> &failures, const HydraulicSimulationStatus &status)
{
    if (!status.success)
        failures.append(status);
}

void appendValidationFailures(
    QList<HydraulicSimulationStatus> &failures,
    const QList<HydraulicSimulationStatus> &additional_failures)
{
    for (const HydraulicSimulationStatus &failure : additional_failures)
        failures.append(failure);
}
}
