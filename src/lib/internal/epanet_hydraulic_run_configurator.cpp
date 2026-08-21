#include "epanet_hydraulic_run_configurator.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_status_helpers.h"

#include <aowis/model/hydraulic/network_hydraulic.h>

namespace
{
bool resolveHeadlossFormula(HydraulicHeadlossFormula formula, int &backend_formula)
{
    switch (formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        backend_formula = EN_HW;
        return true;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        backend_formula = EN_DW;
        return true;
    case HydraulicHeadlossFormula::ChezyManning:
        backend_formula = EN_CM;
        return true;
    }

    return false;
}

bool resolvePipeRoughness(
    const HydraulicLinkPipe &pipe,
    HydraulicHeadlossFormula headloss_formula,
    double &roughness)
{
    switch (headloss_formula)
    {
    case HydraulicHeadlossFormula::HazenWilliams:
        roughness = pipe.roughness_hazen_williams;
        return true;
    case HydraulicHeadlossFormula::DarcyWeisbach:
        roughness = pipe.roughness_darcy_weisbach_mm;
        return true;
    case HydraulicHeadlossFormula::ChezyManning:
        roughness = pipe.roughness_chezy_manning;
        return true;
    }

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
}

HydraulicSimulationStatus configureEpanetHydraulicRun(
    EpanetProject &project,
    const NetworkHydraulic &request,
    const EpanetIndexRegistry &indices)
{
    HydraulicSimulationStatus first_failure = makeEpanetSuccess();

    int backend_formula = 0;
    if (!resolveHeadlossFormula(request.options_hydraulic.headloss_formula, backend_formula))
    {
        const HydraulicSimulationStatus status = makeEpanetStatus(
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureHydraulics,
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("Unsupported hydraulic headloss formula"));
        collectConfigurationFailure(project, status, first_failure);
        return first_failure;
    }

    int error = EN_setoption(project.handle(), EN_HEADLOSSFORM, static_cast<double>(backend_formula));
    if (error != 0)
    {
        const HydraulicSimulationStatus status = processEpanetReturnCode(
            project,
            error,
            HydraulicSimulationStatusStage::ConfigureOptions,
            HydraulicSimulationStatusOperation::ConfigureHydraulics,
            QStringLiteral("EN_setoption(EN_HEADLOSSFORM)"),
            HydraulicSimulationStatusEntityType::HydraulicSolver,
            QString(),
            QStringLiteral("Failed to configure EPANET headloss formula"));
        collectConfigurationFailure(project, status, first_failure);
        if (!status.success)
            return first_failure;
    }

    for (const HydraulicLinkPipe &pipe : request.links_pipes)
    {
        const int link_index = indices.links_pipes.value(pipe.uuid, 0);
        double roughness = 0.0;
        HydraulicSimulationStatus status = makeEpanetSuccess();

        if (link_index <= 0)
        {
            status = makeEpanetStatus(
                HydraulicSimulationStatusStage::ConfigureOptions,
                HydraulicSimulationStatusOperation::ResolveEntity,
                HydraulicSimulationStatusEntityType::Pipe,
                pipe.id,
                pipe.uuid,
                QStringLiteral("Failed to resolve pipe for hydraulic run configuration"));
            collectConfigurationFailure(project, status, first_failure);
            continue;
        }

        if (!resolvePipeRoughness(pipe, request.options_hydraulic.headloss_formula, roughness))
        {
            status = makeEpanetStatus(
                HydraulicSimulationStatusStage::ConfigureOptions,
                HydraulicSimulationStatusOperation::ConfigureHydraulics,
                HydraulicSimulationStatusEntityType::Pipe,
                pipe.id,
                pipe.uuid,
                QStringLiteral("Unsupported hydraulic headloss formula"));
            collectConfigurationFailure(project, status, first_failure);
            continue;
        }

        error = EN_setlinkvalue(project.handle(), link_index, EN_ROUGHNESS, roughness);
        if (error != 0)
        {
            status = processEpanetReturnCode(
                project,
                error,
                HydraulicSimulationStatusStage::ConfigureOptions,
                HydraulicSimulationStatusOperation::ConfigureHydraulics,
                QStringLiteral("EN_setlinkvalue(EN_ROUGHNESS)"),
                HydraulicSimulationStatusEntityType::Pipe,
                pipe.id,
                pipe.uuid,
                QStringLiteral("Failed to configure formula-specific pipe roughness"));
            collectConfigurationFailure(project, status, first_failure);
        }
    }

    return first_failure.success ? makeEpanetSuccess() : first_failure;
}
