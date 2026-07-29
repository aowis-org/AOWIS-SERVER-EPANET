#ifndef AOWIS_EPANET_STATUS_HELPERS_H
#define AOWIS_EPANET_STATUS_HELPERS_H

#include <QString>
#include <aowis/model/hydraulic/epanet_status.h>

class EpanetProject;

EpanetStatus makeEpanetStatus(EpanetStatusStage stage, EpanetStatusOperation operation, EpanetStatusEntityType entity_type, const QString &entity_id, const QString &message);
EpanetStatus makeEpanetError(const EpanetProject &project, int error_code, EpanetStatusStage stage, EpanetStatusOperation operation, EpanetStatusEntityType entity_type, const QString &entity_id, const QString &message);
EpanetStatus makeEpanetSuccess();

#endif
