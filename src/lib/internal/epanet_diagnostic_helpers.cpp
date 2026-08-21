#include "epanet_diagnostic_helpers.h"

#include <QtGlobal>

namespace
{
HydraulicSimulationDiagnosticSeverity inferredSeverity(const HydraulicSimulationStatus &status)
{
    return status.backend_error_code > 0 && status.backend_error_code < 100
        ? HydraulicSimulationDiagnosticSeverity::Warning
        : HydraulicSimulationDiagnosticSeverity::Error;
}
}

HydraulicSimulationDiagnostic epanetDiagnosticFromStatus(const HydraulicSimulationStatus &status)
{
    return epanetDiagnosticFromStatus(status, inferredSeverity(status));
}

HydraulicSimulationDiagnostic epanetDiagnosticFromStatus(
    const HydraulicSimulationStatus &status,
    HydraulicSimulationDiagnosticSeverity severity)
{
    HydraulicSimulationDiagnostic diagnostic;
    diagnostic.severity = severity;
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
    return diagnostic;
}

bool epanetDiagnosticsEquivalent(
    const HydraulicSimulationDiagnostic &left,
    const HydraulicSimulationDiagnostic &right)
{
    return left.stage == right.stage
        && left.operation == right.operation
        && left.entity.uuid == right.entity.uuid
        && left.entity.type == right.entity.type
        && left.backend_error_code == right.backend_error_code
        && left.backend_operation == right.backend_operation
        && left.message == right.message
        && left.details == right.details
        && left.message_backend == right.message_backend;
}

void appendEpanetDiagnosticIfUnique(
    QList<HydraulicSimulationDiagnostic> &diagnostics,
    const HydraulicSimulationDiagnostic &diagnostic)
{
    for (const HydraulicSimulationDiagnostic &existing : diagnostics)
    {
        if (epanetDiagnosticsEquivalent(existing, diagnostic))
            return;
    }

    diagnostics.append(diagnostic);
}

void appendEpanetDiagnostics(
    QList<HydraulicSimulationDiagnostic> &target,
    const QList<HydraulicSimulationDiagnostic> &source,
    qsizetype begin_index,
    qsizetype end_index)
{
    const qsizetype bounded_begin = qMax<qsizetype>(0, begin_index);
    const qsizetype requested_end = end_index < 0 ? source.size() : end_index;
    const qsizetype bounded_end = qMin<qsizetype>(source.size(), requested_end);
    for (qsizetype index = bounded_begin; index < bounded_end; index++)
        appendEpanetDiagnosticIfUnique(target, source.at(index));
}

void appendEpanetReportDiagnostics(
    QList<HydraulicSimulationDiagnostic> &diagnostics,
    const QStringList &report_lines)
{
    for (const QString &line : report_lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        HydraulicSimulationDiagnostic diagnostic;
        if (trimmed.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive))
            diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Error;
        else if (trimmed.contains(QStringLiteral("WARNING"), Qt::CaseInsensitive))
            diagnostic.severity = HydraulicSimulationDiagnosticSeverity::Warning;
        else
            continue;

        diagnostic.stage = HydraulicSimulationStatusStage::GenerateReport;
        diagnostic.operation = HydraulicSimulationStatusOperation::GenerateReport;
        diagnostic.entity.type = HydraulicSimulationStatusEntityType::Report;
        diagnostic.message = trimmed;
        diagnostic.backend_name = QStringLiteral("EPANET");
        diagnostic.backend_operation = QStringLiteral("report callback");
        appendEpanetDiagnosticIfUnique(diagnostics, diagnostic);
    }
}

EpanetDiagnosticCheckpoint::EpanetDiagnosticCheckpoint(const QList<HydraulicSimulationDiagnostic> &diagnostics)
{
    this->index = diagnostics.size();
}

void EpanetDiagnosticCheckpoint::appendSince(
    QList<HydraulicSimulationDiagnostic> &target,
    const QList<HydraulicSimulationDiagnostic> &source) const
{
    appendEpanetDiagnostics(target, source, this->index, source.size());
}
