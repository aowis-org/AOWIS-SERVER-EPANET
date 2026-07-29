#include "epanet_status_helpers.h"
#include "epanet_project.h"

EpanetStatus makeEpanetStatus(EpanetStatusStage stage, EpanetStatusOperation operation, EpanetStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    EpanetStatus status;
    status.success = false;
    status.stage = stage;
    status.operation = operation;
    status.entity.type = entity_type;
    status.entity.id = entity_id;
    status.message = message;
    return status;
}

EpanetStatus makeEpanetError(const EpanetProject &project, int error_code, EpanetStatusStage stage, EpanetStatusOperation operation, EpanetStatusEntityType entity_type, const QString &entity_id, const QString &message)
{
    EpanetStatus status = makeEpanetStatus(stage, operation, entity_type, entity_id, message);
    status.epanet_error_code = error_code;
    status.message_epanet = project.errorMessage(error_code);
    return status;
}

EpanetStatus makeEpanetSuccess()
{
    EpanetStatus status;
    status.success = true;
    return status;
}
