#include "epanet_prepared_project.h"

#include "epanet_diagnostic_helpers.h"
#include "epanet_network_builder.h"
#include "epanet_network_preparer.h"
#include "epanet_project_initializer.h"

#include <QList>

HydraulicSimulationStatus EpanetPreparedProject::prepare(const NetworkHydraulic &request)
{
    QList<HydraulicSimulationStatus> validation_failures;
    HydraulicSimulationStatus status = prepareEpanetNetwork(request, this->prepared_network_, &validation_failures);
    for (const HydraulicSimulationStatus &validation_failure : validation_failures)
        this->project_.appendDiagnostic(epanetDiagnosticFromStatus(validation_failure));

    if (!status.success)
        return status;

    status = this->project_.create();
    if (!status.success)
        return status;

    status = initializeEpanetProject(this->project_, this->prepared_network_, this->report_collector_);
    if (!status.success)
        return status;

    EpanetNetworkBuilder network_builder(this->project_, this->indices_);
    return network_builder.build(this->prepared_network_);
}

EpanetProject &EpanetPreparedProject::project()
{
    return this->project_;
}

const EpanetProject &EpanetPreparedProject::project() const
{
    return this->project_;
}

NetworkHydraulic &EpanetPreparedProject::network()
{
    return this->prepared_network_;
}

const NetworkHydraulic &EpanetPreparedProject::network() const
{
    return this->prepared_network_;
}

EpanetIndexRegistry &EpanetPreparedProject::indices()
{
    return this->indices_;
}

const EpanetIndexRegistry &EpanetPreparedProject::indices() const
{
    return this->indices_;
}

EpanetReportCollector &EpanetPreparedProject::reportCollector()
{
    return this->report_collector_;
}

const EpanetReportCollector &EpanetPreparedProject::reportCollector() const
{
    return this->report_collector_;
}
