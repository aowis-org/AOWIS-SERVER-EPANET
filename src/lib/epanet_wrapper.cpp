#include "epanet_wrapper.h"


EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SimulationResultTimeline>(
        "SimulationResultTimeline"
    );
    qRegisterMetaType<EpanetStatus>("EpanetStatus");
}

SimulationResultTimeline EpanetWrapper::run(
    const SimulationRequest &request
)
{
    if (this->epanet_project != nullptr)
    {
        EpanetStatus status;
        status.success = false;
        status.stage = EpanetStage::CreateProject;
        status.operation = EpanetOperation::None;
        status.entity.type = EpanetEntityType::None;
        status.message = "EPANET project is already running";
        status.details << "Variable this->epanet_project != nullptr";
        
        emit signalSimulationFailed(status);
        
        SimulationResultTimeline timeline;
        timeline.status = status;
        return timeline;
    }
    
    this->simulation_request = request;
    this->simulation_result_timeline = SimulationResultTimeline();
    this->simulation_result_timeline.simulation_start_utc =
        QDateTime::currentDateTimeUtc();
    
    this->epanet_report.clear();
    
    const auto returnFailure =
        [this](
            const EpanetStatus &status,
            bool cleanup_project
            ) -> SimulationResultTimeline
    {
        this->simulation_result_timeline.status = status;
        
        if (cleanup_project)
            cleanupProject();
        
        emit signalSimulationFailed(status);
        
        return this->simulation_result_timeline;
    };
    
    int error = EN_createproject(&this->epanet_project);
    
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::CreateProject;
        status.operation = EpanetOperation::EN_createproject;
        status.entity.type = EpanetEntityType::None;
        status.message = "EPANET project creation failed";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, false);
    }
    
    /*
     * Register the callback before EN_init() so the EPANET
     * report header is captured.
     */
    error = EN_setreportcallbackuserdata(
        this->epanet_project,
        this
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_setreportcallbackuserdata;
        status.entity.type = EpanetEntityType::Report;
        status.message = "Failed to set EPANET report callback data before initialization";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_setreportcallback(
        this->epanet_project,
        &EpanetWrapper::epanetReportCallback
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_setreportcallback;
        status.entity.type = EpanetEntityType::Report;
        status.message = "Failed to set EPANET report callback before initialization";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_init(
        this->epanet_project,
        "",
        "",
        EN_LPS,
        EN_HW
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_init;
        status.entity.type = EpanetEntityType::None;
        status.message = "EPANET project initialization failed";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    /*
     * EN_init() resets the report callback internally.
     * Restore it for the remainder of the simulation.
     */
    error = EN_setreportcallbackuserdata(
        this->epanet_project,
        this
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_setreportcallbackuserdata;
        status.entity.type = EpanetEntityType::Report;
        status.message = "Failed to restore EPANET report callback data after initialization";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_setreportcallback(
        this->epanet_project,
        &EpanetWrapper::epanetReportCallback
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_setreportcallback;
        status.entity.type = EpanetEntityType::Report;
        status.message = "Failed to restore EPANET report callback after initialization";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_settimeparam(
        this->epanet_project,
        EN_DURATION,
        request.duration_s
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_settimeparam;
        status.entity.type = EpanetEntityType::Project;
        status.message = "Failed to set simulation duration";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_settimeparam(
        this->epanet_project,
        EN_HYDSTEP,
        request.hydraulic_timestep_s
    );
    if (error != 0)
    {
        EpanetStatus status;
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::InitializeProject;
        status.operation = EpanetOperation::EN_settimeparam;
        status.entity.type = EpanetEntityType::Project;
        status.message = "Failed to set hydraulic timestep";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    EpanetStatus status = addEntities(request);
    
    if (!status.success)
        return returnFailure(status, true);
    
    status = runHydraulics();
    
    if (!status.success)
        return returnFailure(status, true);
    
    error = EN_closeH(this->epanet_project);
    
    if (error != 0)
    {
        status = EpanetStatus();
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::CloseHydraulics;
        status.operation = EpanetOperation::EN_closeH;
        status.entity.type = EpanetEntityType::HydraulicSolver;
        status.message = "Failed to close EPANET hydraulics";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_saveH(this->epanet_project);
    
    if (error != 0)
    {
        status = EpanetStatus();
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::SaveHydraulics;
        status.operation = EpanetOperation::EN_saveH;
        status.entity.type = EpanetEntityType::HydraulicSolver;
        status.message = "Failed to save EPANET hydraulic results";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    error = EN_report(this->epanet_project);
    
    if (error != 0)
    {
        status = EpanetStatus();
        status.success = false;
        status.epanet_error_code = error;
        status.stage = EpanetStage::GenerateReport;
        status.operation = EpanetOperation::EN_report;
        status.entity.type = EpanetEntityType::Report;
        status.message = "Failed to generate EPANET report";
        status.message_epanet = getEpanetErrorMessage(error);
        
        return returnFailure(status, true);
    }
    
    cleanupProject();
    
    status = EpanetStatus();
    status.success = true;
    
    this->simulation_result_timeline.status = status;
    
    emit signalSimulationFinished(this->simulation_result_timeline);
    
    return this->simulation_result_timeline;
}

QStringList EpanetWrapper::reportTextList() const
{
    return this->epanet_report;
}
QString EpanetWrapper::reportText() const
{
    return this->epanet_report.join('\n');
}

void EpanetWrapper::epanetReportCallback(
    void *user_data,
    void *project_handle,
    const char *line
)
{
    Q_UNUSED(project_handle)
    
    EpanetWrapper *wrapper = static_cast<EpanetWrapper *>(user_data);
    
    if (wrapper == nullptr)
        return;
    
    if (line == nullptr)
        return;
    
    wrapper->epanet_report.append(QString::fromUtf8(line));
}

void EpanetWrapper::cleanupProject()
{
    if (this->epanet_project != nullptr)
    {
        EN_deleteproject(this->epanet_project);
        this->epanet_project = nullptr;
    }
}

QString EpanetWrapper::getEpanetErrorMessage(int error_code) const
{
    if (error_code == 0)
        return QString();
    
    char message[256] = "";
    
    int result = EN_geterror(
        error_code,
        message,
        sizeof(message)
        );
    
    if (result != 0)
        return QString("Unknown EPANET error code %1").arg(error_code);
    
    return QString::fromUtf8(message);
}
