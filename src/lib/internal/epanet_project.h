#ifndef AOWIS_EPANET_PROJECT_H
#define AOWIS_EPANET_PROJECT_H

#include <QString>

#include <aowis/epanet/epanet_api.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetReportCollector;
struct NetworkHydraulic;

class EpanetProject
{
public:
    EpanetProject() = default;
    ~EpanetProject();

    EpanetProject(const EpanetProject &) = delete;
    EpanetProject &operator=(const EpanetProject &) = delete;

    HydraulicSimulationStatus create();
    HydraulicSimulationStatus initialize(const NetworkHydraulic &request, EpanetReportCollector &report_collector);
    HydraulicSimulationStatus retrieveInpText(QString &inp_text) const;
    EN_Project handle() const;
    QString errorMessage(int error_code) const;

private:
    EN_Project project = nullptr;
};

#endif // AOWIS_EPANET_PROJECT_H
