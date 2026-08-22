#include "epanet_project.h"

#include "epanet_status_helpers.h"

#include <QByteArray>
#include <QFile>

EpanetProject::~EpanetProject()
{
    if (this->project == nullptr)
        return;

    if (this->input_open)
        EN_close(this->project);
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

HydraulicSimulationStatus EpanetProject::openInput(const QString &input_file_path)
{
    HydraulicSimulationStatus status = create();
    if (!status.success)
        return status;

    const QByteArray input_path = QFile::encodeName(input_file_path);
    const int error = EN_open(this->project, input_path.constData(), "", "");
    if (error != 0)
    {
        const HydraulicSimulationStatus status = processEpanetReturnCode(
            *this,
            error,
            HydraulicSimulationStatusStage::OpenInput,
            HydraulicSimulationStatusOperation::OpenInput,
            QStringLiteral("EN_open"),
            HydraulicSimulationStatusEntityType::Project,
            input_file_path,
            QStringLiteral("Failed to open EPANET INP file"));
        if (!status.success)
            return status;
    }

    this->input_open = true;
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
