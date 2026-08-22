#ifndef AOWIS_EPANET_NETWORK_VALIDATOR_SUPPORT_H
#define AOWIS_EPANET_NETWORK_VALIDATOR_SUPPORT_H

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace EpanetNetworkValidatorSupport
{
template<typename Entity>
QList<Entity> enabledEntities(const QList<Entity> &entities)
{
    QList<Entity> enabled_entities;
    enabled_entities.reserve(entities.size());

    for (const Entity &entity : entities)
    {
        if (entity.metadata.enabled)
            enabled_entities.append(entity);
    }

    return enabled_entities;
}

template<typename Entity>
QSet<QUuid> entityUuids(const QList<Entity> &entities)
{
    QSet<QUuid> uuids;
    uuids.reserve(entities.size());

    for (const Entity &entity : entities)
        uuids.insert(entity.uuid);

    return uuids;
}

QSet<QUuid> nodeUuids(const NetworkHydraulic &network);
QSet<QUuid> linkUuids(const NetworkHydraulic &network);
HydraulicSimulationStatus validationStatus(
    HydraulicSimulationStatusOperation operation,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &message,
    const QStringList &details = {});
void collectIdentityFailures(
    QList<HydraulicSimulationStatus> &failures,
    QSet<QUuid> &all_uuids,
    QSet<QString> &ids,
    const QString &namespace_name,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_name,
    const QString &id,
    const QUuid &uuid);
HydraulicSimulationStatusOperation numericValidationOperation(HydraulicSimulationStatusEntityType entity_type);
HydraulicSimulationStatus invalidNumeric(
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name,
    const QString &requirement);
HydraulicSimulationStatus validateFinite(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name);
HydraulicSimulationStatus validateLongitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name);
HydraulicSimulationStatus validateLatitudeDeg(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name);
HydraulicSimulationStatus validateFiniteNonNegative(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name);
HydraulicSimulationStatus validateFinitePositive(
    double value,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &field_name);
HydraulicSimulationStatus validateReference(
    const QSet<QUuid> &all_uuids,
    const QSet<QUuid> &enabled_uuids,
    const QUuid &target_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null = false);
HydraulicSimulationStatus validatePatternReference(
    const QSet<QUuid> &pattern_uuids,
    const QUuid &pattern_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null);
HydraulicSimulationStatus validateCurveReference(
    const QSet<QUuid> &curve_uuids,
    const QUuid &curve_uuid,
    HydraulicSimulationStatusEntityType entity_type,
    const QString &entity_id,
    const QUuid &entity_uuid,
    const QString &relationship,
    bool allow_null);
void appendValidationFailure(QList<HydraulicSimulationStatus> &failures, const HydraulicSimulationStatus &status);
void appendValidationFailures(
    QList<HydraulicSimulationStatus> &failures,
    const QList<HydraulicSimulationStatus> &additional_failures);
}

#endif // AOWIS_EPANET_NETWORK_VALIDATOR_SUPPORT_H
