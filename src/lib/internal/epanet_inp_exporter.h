#ifndef AOWIS_EPANET_INP_EXPORTER_H
#define AOWIS_EPANET_INP_EXPORTER_H

#include <QList>
#include <QString>

#include <aowis/model/hydraulic/hydraulic_simulation_status.h>
#include <aowis/model/map/map_annotation.h>

class EpanetProject;
struct NetworkHydraulicDistribution;

// `request` is the prepared (exported) network. `annotations` are written as
// [LABELS]; anchored positions are resolved in `annotation_network`, the
// complete network the annotations belong to (validated by the caller).
HydraulicSimulationStatus retrieveEpanetInpText(
    EpanetProject &project,
    const NetworkHydraulicDistribution &request,
    const NetworkHydraulicDistribution &annotation_network,
    const QList<MapAnnotation> &annotations,
    QString &inp_text);

#endif // AOWIS_EPANET_INP_EXPORTER_H
