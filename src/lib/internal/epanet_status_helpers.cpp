#include "epanet_status_helpers.h"
#include "epanet_project.h"

namespace
{
const QString backendName()
{
    return QStringLiteral("EPANET");
}
}

HydraulicSimulationStatus makeEpanetStatus(HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    HydraulicSimulationStatus status;
    status.success = false;
    status.stage = stage;
    status.operation = operation;
    status.entity.type = entity_type;
    status.entity.id = entity_id;
    status.message = message;
    status.backend_name = backendName();
    return status;
}

HydraulicSimulationStatus makeEpanetError(const EpanetProject &project, int error_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    HydraulicSimulationStatus status = makeEpanetStatus(stage, operation, entity_type, entity_id, message);
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
