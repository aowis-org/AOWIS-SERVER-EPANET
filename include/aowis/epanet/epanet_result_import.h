#ifndef AOWIS_EPANET_RESULT_IMPORT_H
#define AOWIS_EPANET_RESULT_IMPORT_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

#include <aowis/epanet/epanet_run_request.h>
#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

enum class EpanetImportMapUnits
{
    None,
    Meters,
    Feet,
    Degrees,
    Unknown
};

struct EpanetImportSourcePoint
{
    double x = 0.0;
    double y = 0.0;
};

struct EpanetImportSourceMapLabel
{
    EpanetImportSourcePoint point;
    QString text;
    QString anchor_node_id;
};

struct EpanetImportSourceBackdrop
{
    bool present = false;
    bool has_lower_left = false;
    EpanetImportSourcePoint lower_left;
    bool has_upper_right = false;
    EpanetImportSourcePoint upper_right;
    QString file;
    EpanetImportSourcePoint offset;
};

struct EpanetImportSourceGeometry
{
    EpanetImportMapUnits units = EpanetImportMapUnits::None;
    QString units_text;
    bool units_declared = false;
    bool epsg_code_declared = false;
    int epsg_code = 0;
    QString coordinate_reference_name;
    bool georeferenced = false;
    bool transformed_to_wgs84 = false;
    QHash<QString, EpanetImportSourcePoint> node_coordinates;
    QHash<QString, QList<EpanetImportSourcePoint>> link_vertices;
    QList<EpanetImportSourceMapLabel> labels;
    EpanetImportSourceBackdrop backdrop;
};

struct EpanetResultImport
{
    EpanetRunRequest request;
    HydraulicSimulationStatus status;
    QList<HydraulicSimulationDiagnostic> diagnostics;
    EpanetImportSourceGeometry source_geometry;
    bool complete = false;
};

Q_DECLARE_METATYPE(EpanetResultImport)

#endif // AOWIS_EPANET_RESULT_IMPORT_H
