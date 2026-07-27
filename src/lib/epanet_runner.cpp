#include <aowis/epanet/epanet_runner.h>

#include "internal/epanet_hydraulic_solver.h"
#include "internal/epanet_index_registry.h"
#include "internal/epanet_network_builder.h"
#include "internal/epanet_project.h"
#include "internal/epanet_report_collector.h"
#include "internal/epanet_result_reader.h"
#include "internal/epanet_status_helpers.h"

#include <QDateTime>
#include <utility>

namespace
{
EpanetRunResult finishRun(EpanetRunResult result, const EpanetStatus &status, const EpanetReportCollector &report_collector)
{
    result.timeline.status = status;
    result.report = report_collector.lines();
    return result;
}
}

EpanetRunResult EpanetRunner::run(const NetworkHydraulic &request) const
{
    EpanetRunResult result;
    result.timeline.simulation_start_utc = QDateTime::currentDateTimeUtc();

    EpanetReportCollector report_collector;
    EpanetProject project;

    EpanetStatus status = project.create();
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    status = project.initialize(request, report_collector);
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    EpanetIndexRegistry indices;
    EpanetNetworkBuilder network_builder(project, indices);
    status = network_builder.build(request);
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    EpanetResultReader result_reader(project, request, indices);
    EpanetHydraulicSolver hydraulic_solver(project, result_reader);
    status = hydraulic_solver.run(result.timeline);
    if (!status.success)
        return finishRun(std::move(result), status, report_collector);

    int error = EN_saveH(project.handle());
    if (error != 0)
    {
        status = makeEpanetError(project, error, EpanetStage::SaveHydraulics, EpanetOperation::EN_saveH, EpanetEntityType::HydraulicSolver, QString(), "Failed to save EPANET hydraulic results");
        return finishRun(std::move(result), status, report_collector);
    }

    error = EN_report(project.handle());
    if (error != 0)
    {
        status = makeEpanetError(project, error, EpanetStage::GenerateReport, EpanetOperation::EN_report, EpanetEntityType::Report, QString(), "Failed to generate EPANET report");
        return finishRun(std::move(result), status, report_collector);
    }

    return finishRun(std::move(result), makeEpanetSuccess(), report_collector);
}
