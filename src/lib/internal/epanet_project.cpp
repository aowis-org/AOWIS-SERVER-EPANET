#include "epanet_project.h"

#include "epanet_status_helpers.h"

EpanetProject::~EpanetProject()
{
    if (this->project != nullptr)
        EN_deleteproject(this->project);
}

HydraulicSimulationStatus EpanetProject::create()
{
    if (this->project != nullptr)
        return makeEpanetStatus(HydraulicSimulationStatusStage::CreateBackendContext, HydraulicSimulationStatusOperation::CreateBackendContext, HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project already exists"));

    const int error = EN_createproject(&this->project);
    if (error != 0)
    {
        const HydraulicSimulationStatus epanet_status = processEpanetReturnCode(*this, error, HydraulicSimulationStatusStage::CreateBackendContext, HydraulicSimulationStatusOperation::CreateBackendContext, QStringLiteral("EN_createproject"), HydraulicSimulationStatusEntityType::Project, QString(), QStringLiteral("EPANET project creation failed"));
        if (!epanet_status.success)
            return epanet_status;
    }

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
        return QStringLiteral("Unknown EPANET error code %1").arg(error_code);

    return QString::fromUtf8(message);
}

const QList<HydraulicSimulationDiagnostic> &EpanetProject::diagnostics() const
{
    return this->diagnostics_collected;
}

void EpanetProject::appendDiagnostic(const HydraulicSimulationDiagnostic &diagnostic) const
{
    this->diagnostics_collected.append(diagnostic);
}
