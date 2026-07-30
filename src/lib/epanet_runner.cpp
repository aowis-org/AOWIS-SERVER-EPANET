#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_hydraulic_solver.h"
#include "internal/epanet_index_registry.h"
#include "internal/epanet_network_builder.h"
#include "internal/epanet_project.h"
#include "internal/epanet_report_collector.h"
#include "internal/epanet_result_reader.h"
#include "internal/epanet_status_helpers.h"

#include <utility>

#include <QDateTime>

namespace
{
EpanetResultInp finishInp(EpanetResultInp result, const HydraulicSimulationStatus &status, const EpanetReportCollector &report_collector)
{
    result.status = status;
    result.report_lines = report_collector.lines();
    return result;
}

EpanetResultRun finishRun(EpanetResultRun result, const HydraulicSimulationStatus &status, const EpanetReportCollector &report_collector)
{
    result.result_timeline.status = status;
    result.report_lines = report_collector.lines();
    return result;
}

HydraulicSimulationStatus prepareProject(const NetworkHydraulic &request, EpanetProject &project, EpanetReportCollector &report_collector, EpanetIndexRegistry &indices)
{
    HydraulicSimulationStatus status = project.create();
    if (!status.success)
        return status;

    status = project.initialize(request, report_collector);
    if (!status.success)
        return status;

    EpanetNetworkBuilder network_builder(project, indices);
    return network_builder.build(request);
}
}

EpanetResultInp EpanetRunner::retrieveInp(const NetworkHydraulic &request) const
{
    EpanetResultInp result;
    EpanetReportCollector report_collector;
    EpanetProject project;
    EpanetIndexRegistry indices;

    HydraulicSimulationStatus status = prepareProject(request, project, report_collector, indices);
    if (!status.success)
        return finishInp(std::move(result), status, report_collector);

    status = project.retrieveInpText(result.inp_text);
    return finishInp(std::move(result), status, report_collector);
}

EpanetResultRun EpanetRunner::run(const NetworkHydraulic &request) const
{
    EpanetResultRun result;
    result.result_timeline.simulation_start_utc = QDateTime::currentDateTimeUtc();

    EpanetReportCollector report_collector;
    EpanetProject project;
    EpanetIndexRegistry indices;

    HydraulicSimulationStatus status = prepareProject(request, project, report_collector, indices);
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    EpanetResultReader result_reader(project, request, indices);
    EpanetHydraulicSolver hydraulic_solver(project, request, result_reader);
    status = hydraulic_solver.run(result.result_timeline);
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    int error = EN_saveH(project.handle());
    if (error != 0)
    {
        status = makeEpanetError(project, error, HydraulicSimulationStatusStage::SaveHydraulics, HydraulicSimulationStatusOperation::SaveHydraulics, QStringLiteral("EN_saveH"), HydraulicSimulationStatusEntityType::HydraulicSolver, QString(), QStringLiteral("Failed to save EPANET hydraulic results"));
        return finishRun(std::move(result), status, report_collector);
    }

    error = EN_report(project.handle());
    if (error != 0)
    {
        status = makeEpanetError(project, error, HydraulicSimulationStatusStage::GenerateReport, HydraulicSimulationStatusOperation::GenerateReport, QStringLiteral("EN_report"), HydraulicSimulationStatusEntityType::Report, QString(), QStringLiteral("Failed to generate EPANET report"));
        return finishRun(std::move(result), status, report_collector);
    }

    return finishRun(std::move(result), makeEpanetSuccess(), report_collector);
}
