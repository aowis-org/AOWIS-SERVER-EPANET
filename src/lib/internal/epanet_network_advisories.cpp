#include "epanet_network_advisories.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_status_helpers.h"

namespace
{
void collectTankStorageRangeAdvisories(
    const NetworkHydraulic &network,
    QList<HydraulicSimulationDiagnostic> &advisories)
{
    for (const HydraulicNodeTank &tank : network.nodes_tanks)
    {
        if (!tank.metadata.enabled)
            continue;
        if (tank.water_level_minimum_m < tank.water_level_maximum_m)
            continue;

        // EPANET itself accepts this without complaint: a link that would
        // need to raise a tank's level past MaxLevel (or drain it below
        // MinLevel) is simply kept closed for as long as that would be
        // required, so the network still simulates - see the "is closed" /
        // "temporarily closed" lines EPANET's own status reporting produces
        // for exactly this case. But a tank with no usable range between its
        // minimum and maximum level can never fill or drain at all, which is
        // far more often an unfilled-in placeholder than an intentional
        // design choice (e.g. deliberately modeling a second fixed-head
        // boundary node). Flag it without blocking the run.
        HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::BuildNetwork,
            HydraulicSimulationStatusOperation::SetEntityMetadata,
            HydraulicSimulationStatusEntityType::Tank,
            tank.id,
            tank.uuid,
            QStringLiteral("Tank %1 has no usable storage range").arg(tank.id));
        status.details.append(QStringLiteral(
            "MinLevel (%1 m) is not below MaxLevel (%2 m), so the tank cannot rise or "
            "fall between them. It will behave as a fixed-head node for the whole run "
            "and any link that would need to fill or drain it stays closed. This is "
            "occasionally intentional, but usually means MaxLevel (or MinLevel) was "
            "never set to the tank's real operating range.")
            .arg(tank.water_level_minimum_m)
            .arg(tank.water_level_maximum_m));

        appendEpanetDiagnosticIfUnique(
            advisories,
            epanetDiagnosticFromStatus(status, HydraulicSimulationDiagnosticSeverity::Warning));
    }
}
}

QList<HydraulicSimulationDiagnostic> collectEpanetNetworkAdvisories(const NetworkHydraulic &network)
{
    QList<HydraulicSimulationDiagnostic> advisories;
    collectTankStorageRangeAdvisories(network, advisories);
    return advisories;
}
