#include "epanet_network_validator_parts.h"
#include "epanet_network_validator_support.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace
{
QString valveTypeName(HydraulicLinkValveType type)
{
    switch (type)
    {
    case HydraulicLinkValveType::PRV:
        return QStringLiteral("PRV");
    case HydraulicLinkValveType::PSV:
        return QStringLiteral("PSV");
    case HydraulicLinkValveType::PBV:
        return QStringLiteral("PBV");
    case HydraulicLinkValveType::FCV:
        return QStringLiteral("FCV");
    case HydraulicLinkValveType::TCV:
        return QStringLiteral("TCV");
    case HydraulicLinkValveType::PCV:
        return QStringLiteral("PCV");
    case HydraulicLinkValveType::GPV:
        return QStringLiteral("GPV");
    }
    return QStringLiteral("unknown valve type");
}

QHash<QUuid, QString> nodeIds(const NetworkHydraulic &network)
{
    QHash<QUuid, QString> ids;
    for (const HydraulicNodeJunction &junction : network.nodes_junctions)
        ids.insert(junction.uuid, junction.id);
    for (const HydraulicNodeReservoir &reservoir : network.nodes_reservoirs)
        ids.insert(reservoir.uuid, reservoir.id);
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
        ids.insert(tank.uuid, tank.id);
    return ids;
}

QString endpointDescription(const HydraulicLinkValve &valve, const QHash<QUuid, QString> &node_ids)
{
    const QString start_id = node_ids.value(valve.node_uuid_from, valve.node_uuid_from.toString(QUuid::WithoutBraces));
    const QString end_id = node_ids.value(valve.node_uuid_to, valve.node_uuid_to.toString(QUuid::WithoutBraces));
    return QStringLiteral("%1 (%2): start=%3, end=%4")
        .arg(valve.id, valveTypeName(valve.type), start_id, end_id);
}

bool illegalPrvPair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    if (first.type != HydraulicLinkValveType::PRV || second.type != HydraulicLinkValveType::PRV)
        return false;

    if (first.node_uuid_to == second.node_uuid_to)
    {
        junction_uuid = first.node_uuid_to;
        reason = QStringLiteral("two PRVs cannot control the same downstream junction");
        return true;
    }
    if (first.node_uuid_to == second.node_uuid_from)
    {
        junction_uuid = first.node_uuid_to;
        reason = QStringLiteral("two PRVs cannot be connected in series");
        return true;
    }
    if (first.node_uuid_from == second.node_uuid_to)
    {
        junction_uuid = first.node_uuid_from;
        reason = QStringLiteral("two PRVs cannot be connected in series");
        return true;
    }
    return false;
}

bool illegalPsvPair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    if (first.type != HydraulicLinkValveType::PSV || second.type != HydraulicLinkValveType::PSV)
        return false;

    if (first.node_uuid_from == second.node_uuid_from)
    {
        junction_uuid = first.node_uuid_from;
        reason = QStringLiteral("two PSVs cannot control the same upstream junction");
        return true;
    }
    if (first.node_uuid_to == second.node_uuid_from)
    {
        junction_uuid = first.node_uuid_to;
        reason = QStringLiteral("two PSVs cannot be connected in series");
        return true;
    }
    if (first.node_uuid_from == second.node_uuid_to)
    {
        junction_uuid = first.node_uuid_from;
        reason = QStringLiteral("two PSVs cannot be connected in series");
        return true;
    }
    return false;
}

bool illegalPrvPsvPair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    const HydraulicLinkValve *prv = nullptr;
    const HydraulicLinkValve *psv = nullptr;
    if (first.type == HydraulicLinkValveType::PRV && second.type == HydraulicLinkValveType::PSV)
    {
        prv = &first;
        psv = &second;
    }
    else if (first.type == HydraulicLinkValveType::PSV && second.type == HydraulicLinkValveType::PRV)
    {
        prv = &second;
        psv = &first;
    }
    else
    {
        return false;
    }

    if (prv->node_uuid_to != psv->node_uuid_from)
        return false;

    junction_uuid = prv->node_uuid_to;
    reason = QStringLiteral("a PSV cannot use the controlled downstream junction of a PRV as its upstream junction");
    return true;
}

bool illegalPrvFcvPair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    const HydraulicLinkValve *prv = nullptr;
    const HydraulicLinkValve *fcv = nullptr;
    if (first.type == HydraulicLinkValveType::PRV && second.type == HydraulicLinkValveType::FCV)
    {
        prv = &first;
        fcv = &second;
    }
    else if (first.type == HydraulicLinkValveType::FCV && second.type == HydraulicLinkValveType::PRV)
    {
        prv = &second;
        fcv = &first;
    }
    else
    {
        return false;
    }

    if (prv->node_uuid_to != fcv->node_uuid_from)
        return false;

    junction_uuid = prv->node_uuid_to;
    reason = QStringLiteral("a PRV cannot control the upstream junction of an FCV");
    return true;
}

bool illegalFcvPsvPair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    const HydraulicLinkValve *fcv = nullptr;
    const HydraulicLinkValve *psv = nullptr;
    if (first.type == HydraulicLinkValveType::FCV && second.type == HydraulicLinkValveType::PSV)
    {
        fcv = &first;
        psv = &second;
    }
    else if (first.type == HydraulicLinkValveType::PSV && second.type == HydraulicLinkValveType::FCV)
    {
        fcv = &second;
        psv = &first;
    }
    else
    {
        return false;
    }

    if (fcv->node_uuid_to != psv->node_uuid_from)
        return false;

    junction_uuid = fcv->node_uuid_to;
    reason = QStringLiteral("a PSV cannot use the downstream junction of an FCV as its upstream junction");
    return true;
}

bool illegalValvePair(
    const HydraulicLinkValve &first,
    const HydraulicLinkValve &second,
    QUuid &junction_uuid,
    QString &reason)
{
    return illegalPrvPair(first, second, junction_uuid, reason)
        || illegalPsvPair(first, second, junction_uuid, reason)
        || illegalPrvPsvPair(first, second, junction_uuid, reason)
        || illegalPrvFcvPair(first, second, junction_uuid, reason)
        || illegalFcvPsvPair(first, second, junction_uuid, reason);
}
}

namespace EpanetNetworkValidatorParts
{
QList<HydraulicSimulationStatus> validateTopology(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationStatus> failures;
    const QList<HydraulicLinkValve> valves = EpanetNetworkValidatorSupport::enabledEntities(network.links_valves);
    const QList<HydraulicNodeJunction> junctions = EpanetNetworkValidatorSupport::enabledEntities(network.nodes_junctions);
    const QSet<QUuid> enabled_junction_uuids = EpanetNetworkValidatorSupport::entityUuids(junctions);
    const QHash<QUuid, QString> node_ids = nodeIds(network);

    for (int first_index = 0; first_index < valves.size(); first_index++)
    {
        const HydraulicLinkValve &first = valves.at(first_index);
        if (!enabled_junction_uuids.contains(first.node_uuid_from) || !enabled_junction_uuids.contains(first.node_uuid_to))
            continue;

        for (int second_index = first_index + 1; second_index < valves.size(); second_index++)
        {
            const HydraulicLinkValve &second = valves.at(second_index);
            if (!enabled_junction_uuids.contains(second.node_uuid_from) || !enabled_junction_uuids.contains(second.node_uuid_to))
                continue;

            QUuid junction_uuid;
            QString reason;
            if (!illegalValvePair(first, second, junction_uuid, reason))
                continue;

            const QString junction_id = node_ids.value(junction_uuid, junction_uuid.toString(QUuid::WithoutBraces));
            const QString message = QStringLiteral(
                "EPANET cannot represent valve '%1' (%2) together with valve '%3' (%4) at junction '%5': %6")
                .arg(second.id, valveTypeName(second.type), first.id, valveTypeName(first.type), junction_id, reason);
            const QStringList details = {
                QStringLiteral("EPANET restriction: Error 220 (illegal valve connection to another valve)"),
                QStringLiteral("Valve 1: %1").arg(endpointDescription(first, node_ids)),
                QStringLiteral("Valve 2: %1").arg(endpointDescription(second, node_ids)),
                QStringLiteral("Conflicting junction: %1").arg(junction_id),
                QStringLiteral("Reason: %1").arg(reason),
                QStringLiteral("Revise the topology so these regulating valves do not impose incompatible constraints on the same junction.")
            };
            failures.append(EpanetNetworkValidatorSupport::validationStatus(
                HydraulicSimulationStatusOperation::SetEntityMetadata,
                HydraulicSimulationStatusEntityType::Valve,
                second.id,
                second.uuid,
                message,
                details));
        }
    }

    return failures;
}
}
