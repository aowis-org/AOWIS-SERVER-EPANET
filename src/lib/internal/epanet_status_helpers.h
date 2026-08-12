#ifndef AOWIS_EPANET_STATUS_HELPERS_H
#define AOWIS_EPANET_STATUS_HELPERS_H

#include <QString>
#include <QUuid>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

class EpanetProject;

bool isEpanetWarningCode(int return_code);
bool isEpanetErrorCode(int return_code);
HydraulicSimulationStatus processEpanetReturnCode(const EpanetProject &project, int return_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message);
HydraulicSimulationStatus processEpanetReturnCode(const EpanetProject &project, int return_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message);
HydraulicSimulationStatus makeEpanetStatus(HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message);
HydraulicSimulationStatus makeEpanetStatus(HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message);
HydraulicSimulationStatus makeEpanetError(const EpanetProject &project, int error_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QString &message);
HydraulicSimulationStatus makeEpanetError(const EpanetProject &project, int error_code, HydraulicSimulationStatusStage stage, HydraulicSimulationStatusOperation operation, const QString &backend_operation, HydraulicSimulationStatusEntityType entity_type, const QString &entity_id, const QUuid &entity_uuid, const QString &message);
HydraulicSimulationStatus makeEpanetSuccess();

#endif // AOWIS_EPANET_STATUS_HELPERS_H
