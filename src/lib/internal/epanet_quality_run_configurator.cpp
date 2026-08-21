#include "epanet_quality_run_configurator.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/model/hydraulic/network_hydraulic.h>

#include <QByteArray>

#include <array>
#include <cmath>
#include <functional>

namespace
{
bool resolveWaterQualityAnalysisType(WaterQualityAnalysisType type, int &backend_type)
{
    switch (type)
    {
    case WaterQualityAnalysisType::None:
        backend_type = EN_NONE;
        return true;
    case WaterQualityAnalysisType::Chemical:
        backend_type = EN_CHEM;
        return true;
    case WaterQualityAnalysisType::WaterAge:
        backend_type = EN_AGE;
        return true;
    case WaterQualityAnalysisType::SourceTrace:
        backend_type = EN_TRACE;
        return true;
    }

    return false;
}

bool resolveQualitySourceType(HydraulicNodeQualitySourceType type, int &backend_type)
{
    switch (type)
    {
    case HydraulicNodeQualitySourceType::None:
        return false;
    case HydraulicNodeQualitySourceType::Concentration:
        backend_type = EN_CONCEN;
        return true;
    case HydraulicNodeQualitySourceType::MassBooster:
        backend_type = EN_MASS;
        return true;
    case HydraulicNodeQualitySourceType::FlowPacedBooster:
        backend_type = EN_FLOWPACED;
        return true;
    case HydraulicNodeQualitySourceType::SetpointBooster:
        backend_type = EN_SETPOINT;
        return true;
    }

    return false;
}

bool resolveTankMixingModel(HydraulicNodeTankMixingModel model, int &backend_model)
{
    switch (model)
    {
    case HydraulicNodeTankMixingModel::CompleteMix:
        backend_model = EN_MIX1;
        return true;
    case HydraulicNodeTankMixingModel::TwoCompartment:
        backend_model = EN_MIX2;
        return true;
    case HydraulicNodeTankMixingModel::FirstInFirstOut:
        backend_model = EN_FIFO;
        return true;
    case HydraulicNodeTankMixingModel::LastInFirstOut:
        backend_model = EN_LIFO;
        return true;
    }

    return false;
}

template<typename NodeType>
double initialQualityValue(const WaterQualitySolverOptions &options, const NodeType &node)
{
    switch (options.analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        return node.initial_chemical_concentration_mg_per_l;
    case WaterQualityAnalysisType::WaterAge:
        return node.initial_water_age_h;
    case WaterQualityAnalysisType::SourceTrace:
        return node.initial_source_trace_percent;
    case WaterQualityAnalysisType::None:
        return 0.0;
    }

    return 0.0;
}

double qualityTolerance(const WaterQualitySolverOptions &options)
{
    switch (options.analysis)
    {
    case WaterQualityAnalysisType::Chemical:
        return options.chemical_tolerance_mg_per_l;
    case WaterQualityAnalysisType::WaterAge:
        return options.water_age_tolerance_h;
    case WaterQualityAnalysisType::SourceTrace:
        return options.source_trace_tolerance_percent;
    case WaterQualityAnalysisType::None:
        return options.chemical_tolerance_mg_per_l;
    }

    return options.chemical_tolerance_mg_per_l;
}

double effectivePipeWallReactionCoefficient(
    const NetworkHydraulic &request,
    const HydraulicLinkPipe &pipe)
{
    if (pipe.override_reactions)
        return pipe.wall_reaction.coefficient;

    const double factor = request.options_reaction.roughness_reaction_factor;
    if (factor == 0.0)
        return request.options_reaction.global_pipe_wall_reaction.coefficient;

    switch (request.options_hydraulic.headloss_formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        return pipe.roughness_hazen_williams > 0.0 ? factor / pipe.roughness_hazen_williams : 0.0;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        if (pipe.roughness_darcy_weisbach_mm <= 0.0 || pipe.diameter_mm <= 0.0)
            return 0.0;
        return factor / std::abs(std::log(pipe.roughness_darcy_weisbach_mm / pipe.diameter_mm));
    case HydraulicHeadlossFormula::ChezyManning:
        return factor * pipe.roughness_chezy_manning;
    }

    return 0.0;
}

bool resolveTraceNodeId(const NetworkHydraulic &request, const QUuid &uuid, QByteArray &backend_id)
{
    for (const HydraulicNodeJunction &node : request.nodes_junctions)
    {
        if (node.uuid == uuid)
        {
            backend_id = node.id.toUtf8();
            return !backend_id.isEmpty();
        }
    }

    for (const HydraulicNodeReservoir &node : request.nodes_reservoirs)
    {
        if (node.uuid == uuid)
        {
            backend_id = node.id.toUtf8();
            return !backend_id.isEmpty();
        }
    }

    for (const HydraulicNodeTank &node : request.nodes_tanks)
    {
        if (node.uuid == uuid)
        {
            backend_id = node.id.toUtf8();
            return !backend_id.isEmpty();
        }
    }

    backend_id.clear();
    return false;
}

void collectConfigurationFailure(
    EpanetProject &project,
    const HydraulicSimulationStatus &status,
    HydraulicSimulationStatus &first_failure)
{
    if (status.success)
        return;

    project.appendDiagnostic(epanetDiagnosticFromStatus(status, HydraulicSimulationDiagnosticSeverity::Error));
    if (first_failure.success)
        first_failure = status;
}

HydraulicSimulationStatus configureQualityAnalysis(
    EpanetProject &project,
    const NetworkHydraulic &request,
    const EpanetIndexRegistry &indices)
{
    const WaterQualitySolverOptions &options = request.options_quality;
    int backend_analysis = 0;
    if (!resolveWaterQualityAnalysisType(options.analysis, backend_analysis))
        return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Unsupported water-quality analysis type"));

    QByteArray trace_node_id;
    if (options.analysis == WaterQualityAnalysisType::SourceTrace)
    {
        if (!resolveTraceNodeId(request, options.trace_node_uuid, trace_node_id))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ResolveEntity, HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to resolve the source-trace node"));
    }

    const QByteArray chemical_name = options.chemical_name.isEmpty() ? QByteArray("Chemical") : options.chemical_name.toUtf8();
    const int error_quality_type = EN_setqualtype(project.handle(), backend_analysis, chemical_name.constData(), "mg/L", trace_node_id.constData());
    if (error_quality_type != 0)
        return processEpanetReturnCode(project, error_quality_type, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setqualtype"), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to configure the water-quality analysis type"));

    struct QualityOption
    {
        int option;
        double value;
        const char *name;
    };

    const std::array<QualityOption, 6> quality_options = {{
        {EN_TOLERANCE, qualityTolerance(options), "EN_TOLERANCE"},
        {EN_SP_DIFFUS, options.relative_diffusivity, "EN_SP_DIFFUS"},
        {EN_BULKORDER, request.options_reaction.global_pipe_bulk_reaction.order, "EN_BULKORDER"},
        {EN_WALLORDER, request.options_reaction.global_pipe_wall_reaction.order, "EN_WALLORDER"},
        {EN_TANKORDER, request.options_reaction.global_tank_bulk_reaction.order, "EN_TANKORDER"},
        {EN_CONCENLIMIT, request.options_reaction.limiting_concentration_mg_per_l, "EN_CONCENLIMIT"}
    }};

    for (const QualityOption &quality_option : quality_options)
    {
        const int error = EN_setoption(project.handle(), quality_option.option, quality_option.value);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setoption(%1)").arg(QString::fromLatin1(quality_option.name)), HydraulicSimulationStatusEntityType::QualitySolver, QString(), QStringLiteral("Failed to configure an EPANET water-quality option"));
    }

    const std::function<HydraulicSimulationStatus(int, const HydraulicNodeQualitySource &, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> configure_source =
        [&project, &indices](int node_index, const HydraulicNodeQualitySource &source, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        if (source.type == HydraulicNodeQualitySourceType::None)
            return makeEpanetSuccess();

        int backend_source_type = 0;
        if (!resolveQualitySourceType(source.type, backend_source_type))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, entity_type, id, uuid, QStringLiteral("Unsupported node water-quality source type"));

        int error = EN_setnodevalue(project.handle(), node_index, EN_SOURCETYPE, static_cast<double>(backend_source_type));
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_SOURCETYPE)"), entity_type, id, uuid, QStringLiteral("Failed to configure node water-quality source type"));

        const double source_strength = source.type == HydraulicNodeQualitySourceType::MassBooster
            ? source.chemical_mass_flow_mg_per_min
            : source.chemical_concentration_mg_per_l;
        error = EN_setnodevalue(project.handle(), node_index, EN_SOURCEQUAL, source_strength);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_SOURCEQUAL)"), entity_type, id, uuid, QStringLiteral("Failed to configure node water-quality source strength"));

        const int pattern_index = source.pattern_uuid.isNull()
            ? 0
            : indices.patterns_time.value(source.pattern_uuid, 0);
        if (!source.pattern_uuid.isNull() && pattern_index <= 0)
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ResolveEntity, entity_type, id, uuid, QStringLiteral("Failed to resolve node water-quality source pattern"));

        error = EN_setnodevalue(project.handle(), node_index, EN_SOURCEPAT, static_cast<double>(pattern_index));
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_SOURCEPAT)"), entity_type, id, uuid, QStringLiteral("Failed to configure node water-quality source pattern"));

        return makeEpanetSuccess();
    };

    const std::function<HydraulicSimulationStatus(int, double, HydraulicSimulationStatusEntityType, const QString &, const QUuid &)> configure_initial =
        [&project](int node_index, double initial_quality, HydraulicSimulationStatusEntityType entity_type, const QString &id, const QUuid &uuid)
    {
        const int error = EN_setnodevalue(project.handle(), node_index, EN_INITQUAL, initial_quality);
        if (error == 0)
            return makeEpanetSuccess();
        return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_INITQUAL)"), entity_type, id, uuid, QStringLiteral("Failed to configure node initial water quality"));
    };

    for (const HydraulicNodeJunction &node : request.nodes_junctions)
    {
        const int node_index = indices.nodes_junctions.value(node.uuid, 0);
        HydraulicSimulationStatus status = configure_initial(node_index, initialQualityValue(options, node), HydraulicSimulationStatusEntityType::Junction, node.id, node.uuid);
        if (!status.success)
            return status;
        status = configure_source(node_index, node.quality_source, HydraulicSimulationStatusEntityType::Junction, node.id, node.uuid);
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeReservoir &node : request.nodes_reservoirs)
    {
        const int node_index = indices.nodes_reservoirs.value(node.uuid, 0);
        HydraulicSimulationStatus status = configure_initial(node_index, initialQualityValue(options, node), HydraulicSimulationStatusEntityType::Reservoir, node.id, node.uuid);
        if (!status.success)
            return status;
        status = configure_source(node_index, node.quality_source, HydraulicSimulationStatusEntityType::Reservoir, node.id, node.uuid);
        if (!status.success)
            return status;
    }

    for (const HydraulicNodeTank &node : request.nodes_tanks)
    {
        const int node_index = indices.nodes_tanks.value(node.uuid, 0);
        HydraulicSimulationStatus status = configure_initial(node_index, initialQualityValue(options, node), HydraulicSimulationStatusEntityType::Tank, node.id, node.uuid);
        if (!status.success)
            return status;
        status = configure_source(node_index, node.quality_source, HydraulicSimulationStatusEntityType::Tank, node.id, node.uuid);
        if (!status.success)
            return status;

        int backend_mixing_model = 0;
        if (!resolveTankMixingModel(node.mixing_model, backend_mixing_model))
            return makeEpanetStatus(HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, HydraulicSimulationStatusEntityType::Tank, node.id, node.uuid, QStringLiteral("Unsupported tank mixing model"));

        int error = EN_setnodevalue(project.handle(), node_index, EN_MIXMODEL, static_cast<double>(backend_mixing_model));
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_MIXMODEL)"), HydraulicSimulationStatusEntityType::Tank, node.id, node.uuid, QStringLiteral("Failed to configure tank mixing model"));

        error = EN_setnodevalue(project.handle(), node_index, EN_MIXFRACTION, node.mixing_fraction);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_MIXFRACTION)"), HydraulicSimulationStatusEntityType::Tank, node.id, node.uuid, QStringLiteral("Failed to configure tank mixing fraction"));
    }

    return makeEpanetSuccess();
}

HydraulicSimulationStatus configureQualityReactions(
    EpanetProject &project,
    const NetworkHydraulic &request,
    const EpanetIndexRegistry &indices)
{
    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        const int link_index = indices.links_pipes.value(pipe.uuid, 0);
        const double bulk_coefficient = pipe.override_reactions
            ? pipe.bulk_reaction.coefficient
            : request.options_reaction.global_pipe_bulk_reaction.coefficient;

        int error = EN_setlinkvalue(project.handle(), link_index, EN_KBULK, bulk_coefficient);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setlinkvalue(EN_KBULK)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to configure pipe bulk reaction coefficient"));

        const double wall_coefficient = effectivePipeWallReactionCoefficient(request, pipe);
        error = EN_setlinkvalue(project.handle(), link_index, EN_KWALL, wall_coefficient);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setlinkvalue(EN_KWALL)"), HydraulicSimulationStatusEntityType::Pipe, pipe.id, pipe.uuid, QStringLiteral("Failed to configure pipe wall reaction coefficient"));
    }

    for (const HydraulicNodeTank &tank : request.nodes_tanks)
    {
        const int node_index = indices.nodes_tanks.value(tank.uuid, 0);
        const double bulk_coefficient = tank.override_bulk_reaction
            ? tank.bulk_reaction.coefficient
            : request.options_reaction.global_tank_bulk_reaction.coefficient;
        const int error = EN_setnodevalue(project.handle(), node_index, EN_TANK_KBULK, bulk_coefficient);
        if (error != 0)
            return processEpanetReturnCode(project, error, HydraulicSimulationStatusStage::ConfigureOptions, HydraulicSimulationStatusOperation::ConfigureQuality, QStringLiteral("EN_setnodevalue(EN_TANK_KBULK)"), HydraulicSimulationStatusEntityType::Tank, tank.id, tank.uuid, QStringLiteral("Failed to configure tank bulk reaction coefficient"));
    }

    return makeEpanetSuccess();
}
}

HydraulicSimulationStatus configureEpanetQualityRun(
    EpanetProject &project,
    const NetworkHydraulic &request,
    const EpanetIndexRegistry &indices)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    HydraulicSimulationStatus status = configureQualityAnalysis(project, request, indices);
    collectConfigurationFailure(project, status, first_failure);

    status = configureQualityReactions(project, request, indices);
    collectConfigurationFailure(project, status, first_failure);

    return first_failure.success ? makeEpanetSuccess() : first_failure;
}
