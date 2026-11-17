#ifndef AOWIS_EPANET_PREPARED_PROJECT_H
#define AOWIS_EPANET_PREPARED_PROJECT_H

#include "epanet_index_registry.h"
#include "epanet_project.h"
#include "epanet_report_collector.h"

#include <aowis/model/hydraulic/network_hydraulic_distribution.h>

class EpanetPreparedProject
{
public:
    EpanetPreparedProject() = default;
    EpanetPreparedProject(const EpanetPreparedProject &) = delete;
    EpanetPreparedProject &operator=(const EpanetPreparedProject &) = delete;

    HydraulicSimulationStatus prepare(const NetworkHydraulicDistribution &request);

    EpanetProject &project();
    const EpanetProject &project() const;
    NetworkHydraulicDistribution &network();
    const NetworkHydraulicDistribution &network() const;
    EpanetIndexRegistry &indices();
    const EpanetIndexRegistry &indices() const;
    EpanetReportCollector &reportCollector();
    const EpanetReportCollector &reportCollector() const;

private:
    NetworkHydraulicDistribution prepared_network_;
    EpanetReportCollector report_collector_;
    EpanetProject project_;
    EpanetIndexRegistry indices_;
};

#endif // AOWIS_EPANET_PREPARED_PROJECT_H
