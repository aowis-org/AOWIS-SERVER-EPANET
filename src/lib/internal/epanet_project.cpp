#include "epanet_project.h"
#include "epanet_report_collector.h"
#include "epanet_status_helpers.h"

#include <aowis/model/hydraulic/network.h>

EpanetProject::~EpanetProject()
{
    if (this->project != nullptr)
        EN_deleteproject(this->project);
}

EpanetStatus EpanetProject::create()
{
    if (this->project != nullptr)
        return makeEpanetStatus(EpanetStage::CreateProject, EpanetOperation::None, EpanetEntityType::Project, QString(), "EPANET project already exists");

    const int error = EN_createproject(&this->project);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::CreateProject, EpanetOperation::EN_createproject, EpanetEntityType::Project, QString(), "EPANET project creation failed");

    return makeEpanetSuccess();
}

EpanetStatus EpanetProject::initialize(const NetworkHydraulic &request, EpanetReportCollector &report_collector)
{
    int error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_setreportcallbackuserdata, EpanetEntityType::Report, QString(), "Failed to set EPANET report callback data");

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_setreportcallback, EpanetEntityType::Report, QString(), "Failed to set EPANET report callback");

    error = EN_init(this->project, "", "", EN_LPS, EN_HW);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_init, EpanetEntityType::Project, QString(), "EPANET project initialization failed");

    error = EN_setreportcallbackuserdata(this->project, &report_collector);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_setreportcallbackuserdata, EpanetEntityType::Report, QString(), "Failed to restore EPANET report callback data");

    error = EN_setreportcallback(this->project, &EpanetReportCollector::callback);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_setreportcallback, EpanetEntityType::Report, QString(), "Failed to restore EPANET report callback");

    error = EN_settimeparam(this->project, EN_DURATION, request.duration_s);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_settimeparam, EpanetEntityType::Project, QString(), "Failed to set simulation duration");

    error = EN_settimeparam(this->project, EN_HYDSTEP, request.hydraulic_timestep_s);
    if (error != 0)
        return makeEpanetError(*this, error, EpanetStage::InitializeProject, EpanetOperation::EN_settimeparam, EpanetEntityType::Project, QString(), "Failed to set hydraulic timestep");

    return makeEpanetSuccess();
}

EN_Project EpanetProject::handle() const
{
    return this->project;
}

QString EpanetProject::errorMessage(int error_code) const
{
    if (error_code == 0)
        return QString();

    char message[256] = "";
    const int result = EN_geterror(error_code, message, sizeof(message));
    if (result != 0)
        return QString("Unknown EPANET error code %1").arg(error_code);

    return QString::fromUtf8(message);
}
