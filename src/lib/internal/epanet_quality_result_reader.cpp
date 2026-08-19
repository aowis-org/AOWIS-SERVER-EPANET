#include "epanet_quality_result_reader.h"

#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

namespace
{
HydraulicSimulationStatus readNodeValue(const EpanetProject &project, int node_index, int backend_property, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, HydraulicSimulationStatusProperty property, const QString &message, double &value)
{
    const int error = EN_getnodevalue(project.handle(), node_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ReadNodeResult, QStringLiteral("EN_getnodevalue"), entity_type, entity_id, entity_uuid, message);
    if (!status.success)
    {
        status.property = property;
        status.entity.index = node_index;
    }
    return status;
}

HydraulicSimulationStatus readLinkValue(const EpanetProject &project, int link_index, int backend_property, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message, double &value)
{
    const int error = EN_getlinkvalue(project.handle(), link_index, backend_property, &value);
    if (error == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ReadLinkResult, QStringLiteral("EN_getlinkvalue"), entity_type, entity_id, entity_uuid, message);
    if (!status.success)
    {
        status.property = HydraulicSimulationStatusProperty::Quality;
        status.entity.index = link_index;
    }
    return status;
}

void assignQualityValue(WaterQualityAnalysisType analysis, double backend_value, double &chemical_concentration_mg_per_l, double &water_age_h, double &source_trace_percent)
{
    switch (analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        chemical_concentration_mg_per_l = backend_value;
        break;
    case WaterQualityAnalysisType::WaterAge:
        water_age_h = backend_value;
        break;
    case WaterQualityAnalysisType::SourceTrace:
        source_trace_percent = backend_value;
        break;
    case WaterQualityAnalysisType::None:
        break;
    }
}
}

EpanetQualityResultReader::EpanetQualityResultReader(const EpanetProject &project, const NetworkHydraulic &network, const EpanetIndexRegistry &indices)
    : project(project), network(network), indices(indices)
{
}

HydraulicSimulationStatus EpanetQualityResultReader::read(WaterQualitySimulationResult &result) const
{
    HydraulicSimulationStatus status = readNodesJunctions(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readNodesReservoirs(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readNodesTanks(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksPipes(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksPumps(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readLinksValves(result);
    if (!status.success)
    {
        result.status = status;
        return status;
    }

    status = readStatistics(result);
    result.status = status;
    return status;
}

HydraulicSimulationStatus EpanetQualityResultReader::readNodesJunctions(WaterQualitySimulationResult &result) const
{
    for (const HydraulicNodeJunction &junction : this->network.nodes_junctions)
    {
        const int index = this->indices.nodes_junctions.value(junction.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, QStringLiteral("Junction index is missing from the EPANET registry"));

        WaterQualitySimulationResultNodeJunction value;
        value.id = junction.id;
        value.uuid = junction.uuid;

        double backend_quality = 0.0;
        HydraulicSimulationStatus status = readNodeValue(this->project, index, EN_QUALITY, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::Quality, QStringLiteral("Failed to get junction quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);

        status = readNodeValue(this->project, index, EN_SOURCEMASS, HydraulicSimulationStatusEntityType::Junction, junction.id, junction.uuid, HydraulicSimulationStatusProperty::SourceMass, QStringLiteral("Failed to get junction source mass flow"), value.source_mass_flow_mg_per_min);
        if (!status.success)
            return status;

        result.nodes_junctions.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readNodesReservoirs(WaterQualitySimulationResult &result) const
{
    for (const HydraulicNodeReservoir &reservoir : this->network.nodes_reservoirs)
    {
        const int index = this->indices.nodes_reservoirs.value(reservoir.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, QStringLiteral("Reservoir index is missing from the EPANET registry"));

        WaterQualitySimulationResultNodeReservoir value;
        value.id = reservoir.id;
        value.uuid = reservoir.uuid;

        double backend_quality = 0.0;
        HydraulicSimulationStatus status = readNodeValue(this->project, index, EN_QUALITY, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::Quality, QStringLiteral("Failed to get reservoir quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);

        status = readNodeValue(this->project, index, EN_SOURCEMASS, HydraulicSimulationStatusEntityType::Reservoir, reservoir.id, reservoir.uuid, HydraulicSimulationStatusProperty::SourceMass, QStringLiteral("Failed to get reservoir source mass flow"), value.source_mass_flow_mg_per_min);
        if (!status.success)
            return status;

        result.nodes_reservoirs.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readNodesTanks(WaterQualitySimulationResult &result) const
{
    for (const HydraulicNodeTank &tank : this->network.nodes_tanks)
    {
        const int index = this->indices.nodes_tanks.value(tank.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Tank index is missing from the EPANET registry"));

        WaterQualitySimulationResultNodeTank value;
        value.id = tank.id;
        value.uuid = tank.uuid;

        double backend_quality = 0.0;
        HydraulicSimulationStatus status = readNodeValue(this->project, index, EN_QUALITY, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::Quality, QStringLiteral("Failed to get tank quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);

        status = readNodeValue(this->project, index, EN_SOURCEMASS, HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, HydraulicSimulationStatusProperty::SourceMass, QStringLiteral("Failed to get tank source mass flow"), value.source_mass_flow_mg_per_min);
        if (!status.success)
            return status;

        result.nodes_tanks.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readLinksPipes(WaterQualitySimulationResult &result) const
{
    for (const HydraulicLinkPipe &pipe : this->network.links_pipes)
    {
        const int index = this->indices.links_pipes.value(pipe.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Pipe index is missing from the EPANET registry"));

        WaterQualitySimulationResultLinkPipe value;
        value.id = pipe.id;
        value.uuid = pipe.uuid;
        double backend_quality = 0.0;
        const HydraulicSimulationStatus status = readLinkValue(this->project, index, EN_LINKQUAL, HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to get pipe quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);
        result.links_pipes.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readLinksPumps(WaterQualitySimulationResult &result) const
{
    for (const HydraulicLinkPump &pump : this->network.links_pumps)
    {
        const int index = this->indices.links_pumps.value(pump.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Pump index is missing from the EPANET registry"));

        WaterQualitySimulationResultLinkPump value;
        value.id = pump.id;
        value.uuid = pump.uuid;
        double backend_quality = 0.0;
        const HydraulicSimulationStatus status = readLinkValue(this->project, index, EN_LINKQUAL, HydraulicSimulationStatusEntityType::Pump, pump.id, pump.uuid, QStringLiteral("Failed to get pump quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);
        result.links_pumps.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readLinksValves(WaterQualitySimulationResult &result) const
{
    for (const HydraulicLinkValve &valve : this->network.links_valves)
    {
        const int index = this->indices.links_valves.value(valve.uuid, 0);
        if (index == 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ReadResults, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Valve index is missing from the EPANET registry"));

        WaterQualitySimulationResultLinkValve value;
        value.id = valve.id;
        value.uuid = valve.uuid;
        double backend_quality = 0.0;
        const HydraulicSimulationStatus status = readLinkValue(this->project, index, EN_LINKQUAL, HydraulicSimulationStatusEntityType::Valve, valve.id, valve.uuid, QStringLiteral("Failed to get valve quality"), backend_quality);
        if (!status.success)
            return status;
        assignQualityValue(this->network.options_quality.analysis, backend_quality, value.chemical_concentration_mg_per_l, value.water_age_h, value.source_trace_percent);
        result.links_valves.append(value);
    }
    return makeEpanetSuccess();
}

HydraulicSimulationStatus EpanetQualityResultReader::readStatistics(WaterQualitySimulationResult &result) const
{
    const int error = EN_getstatistic(this->project.handle(), EN_MASSBALANCE, &result.statistics.mass_balance_ratio);
    if (error == 0)
        return makeEpanetSuccess();

    return processEpanetReturnCode(this->project, error, HydraulicSimulationStatusStage::ReadStatistics, HydraulicSimulationStatusOperation::ReadStatistic, QStringLiteral("EN_getstatistic"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to get water-quality mass balance ratio"));
}
