#include "epanet_status_helpers.h"
#include "epanet_project.h"

namespace
{
const QString backendName()
{
    return QStringLiteral("EPANET");
}
}

bool isEpanetWarningCode(int return_code)
{
    return return_code > 0 && return_code < 100;
}

bool isEpanetErrorCode(int return_code)
{
    return return_code < 0 || return_code >= 100;
}

HydraulicSimulationStatus processEpanetReturnCode(const EpanetProject &project, int return_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    return processEpanetReturnCode(project, return_code, stage, operation, backend_operation, entity_type, entity_id, QUuid(), message);
}

HydraulicSimulationStatus processEpanetReturnCode(const EpanetProject &project, int return_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message)
{
    if (return_code == 0)
        return makeEpanetSuccess();

    HydraulicSimulationStatus status = makeEpanetError(project, return_code, stage, operation, backend_operation, entity_type, entity_id, entity_uuid, message);
    if (isEpanetErrorCode(return_code) || !isEpanetWarningCode(return_code))
        return status;

    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Warning;
    diagnostic.stage = status.stage;
    diagnostic.operation = status.operation;
    diagnostic.property = status.property;
    diagnostic.entity = status.entity;
    diagnostic.message = status.message;
    diagnostic.details = status.details;
    diagnostic.backend_name = status.backend_name;
    diagnostic.backend_error_code = status.backend_error_code;
    diagnostic.backend_operation = status.backend_operation;
    diagnostic.message_backend = status.message_backend;
    project.appendDiagnostic(diagnostic);

    return makeEpanetSuccess();
}

HydraulicSimulationStatus makeEpanetStatus(HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    return makeEpanetStatus(stage, operation, entity_type, entity_id, QUuid(), message);
}

HydraulicSimulationStatus makeEpanetStatus(HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message)
{
    HydraulicSimulationStatus status;
    status.success = false;
    status.stage = stage;
    status.operation = operation;
    status.entity.type = entity_type;
    status.entity.id = entity_id;
    status.entity.uuid = entity_uuid;
    status.message = message;
    status.backend_name = backendName();
    return status;
}

HydraulicSimulationStatus makeEpanetError(const EpanetProject &project, int error_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    return makeEpanetError(project, error_code, stage, operation, backend_operation, entity_type, entity_id, QUuid(), message);
}

HydraulicSimulationStatus makeEpanetError(const EpanetProject &project, int error_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message)
{
    HydraulicSimulationStatus status = makeEpanetStatus(stage, operation, entity_type, entity_id, entity_uuid, message);
    status.backend_error_code = error_code;
    status.backend_operation = backend_operation;
    status.message_backend = project.errorMessage(error_code);
    return status;
}

HydraulicSimulationStatus makeEpanetSuccess()
{
    HydraulicSimulationStatus status;
    status.success = true;
    status.backend_name = backendName();
    return status;
}
