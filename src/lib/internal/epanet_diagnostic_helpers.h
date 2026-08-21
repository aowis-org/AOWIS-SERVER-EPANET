#ifndef AOWIS_EPANET_DIAGNOSTIC_HELPERS_H
#define AOWIS_EPANET_DIAGNOSTIC_HELPERS_H

#include <QList>
#include <QStringList>

#include <aowis/model/hydraulic/hydraulic_simulation_diagnostics.h>
#include <aowis/model/hydraulic/hydraulic_simulation_status.h>

HydraulicSimulationDiagnostic epanetDiagnosticFromStatus(const HydraulicSimulationStatus &status);
HydraulicSimulationDiagnostic epanetDiagnosticFromStatus(
    const HydraulicSimulationStatus &status,
    HydraulicSimulationDiagnosticSeverity severity);

bool epanetDiagnosticsEquivalent(
    const HydraulicSimulationDiagnostic &left,
    const HydraulicSimulationDiagnostic &right);

void appendEpanetDiagnosticIfUnique(
    QList<HydraulicSimulationDiagnostic> &diagnostics,
    const HydraulicSimulationDiagnostic &diagnostic);

void appendEpanetDiagnostics(
    QList<HydraulicSimulationDiagnostic> &target,
    const QList<HydraulicSimulationDiagnostic> &source,
    qsizetype begin_index = 0,
    qsizetype end_index = -1);

void appendEpanetReportDiagnostics(
    QList<HydraulicSimulationDiagnostic> &diagnostics,
    const QStringList &report_lines);

class EpanetDiagnosticCheckpoint
{
public:
    EpanetDiagnosticCheckpoint() = default;
    explicit EpanetDiagnosticCheckpoint(const QList<HydraulicSimulationDiagnostic> &diagnostics);

    void appendSince(
        QList<HydraulicSimulationDiagnostic> &target,
        const QList<HydraulicSimulationDiagnostic> &source) const;

private:
    qsizetype index = 0;
};

#endif // AOWIS_EPANET_DIAGNOSTIC_HELPERS_H
