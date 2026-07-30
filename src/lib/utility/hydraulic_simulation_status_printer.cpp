#include <aowis/epanet/utility/hydraulic_simulation_status_printer.h>

#include <cstdio>

#include <QMetaEnum>
#include <QRegularExpression>
#include <QTextStream>

namespace
{
template<typename EnumType>
QString enumKey(EnumType value)
{
    const QMetaEnum meta_enum = QMetaEnum::fromType<EnumType>();
    const char *key = meta_enum.valueToKey(static_cast<int>(value));
    return key == nullptr ? QStringLiteral("Unknown") : QString::fromLatin1(key);
}

template<typename EnumType>
QString enumLabel(EnumType value)
{
    QString text = enumKey(value);
    if (text == QStringLiteral("Unknown"))
        return text;

    static const QRegularExpression acronym_boundary(QStringLiteral("([A-Z]+)([A-Z][a-z])"));
    static const QRegularExpression camel_case_boundary(QStringLiteral("([a-z0-9])([A-Z])"));
    text.replace(acronym_boundary, QStringLiteral("\\1 \\2"));
    text.replace(camel_case_boundary, QStringLiteral("\\1 \\2"));
    text = text.toLower();
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}
}

QString HydraulicSimulationStatusPrinter::toString(const HydraulicSimulationStatus &status)
{
    QString output;
    QTextStream stream(&output);
    stream << "--------------------------------------------------\n";
    stream << "SIMULATION STATUS: " << (status.success ? "SUCCESS" : "ERROR") << '\n';
    if (!status.backend_name.isEmpty())
        stream << "Backend:       " << status.backend_name << '\n';
    if (!status.message.isEmpty())
        stream << "Message:       " << status.message << '\n';
    if (!status.message_backend.isEmpty())
        stream << "Backend error: " << status.message_backend << '\n';
    if (status.backend_error_code != 0)
        stream << "Error code:    " << status.backend_error_code << '\n';
    if (!status.backend_operation.isEmpty())
        stream << "Backend call:  " << status.backend_operation << '\n';
    if (status.stage != HydraulicSimulationStatusStage::None)
        stream << "Stage:         " << enumLabel(status.stage) << '\n';
    if (status.operation != HydraulicSimulationStatusOperation::None)
        stream << "Operation:     " << enumLabel(status.operation) << '\n';
    if (status.property != HydraulicSimulationStatusProperty::None)
        stream << "Property:      " << enumLabel(status.property) << '\n';
    if (status.entity.type != HydraulicSimulationStatusEntityType::None)
    {
        stream << "Entity type:   " << enumLabel(status.entity.type) << '\n';
        if (!status.entity.id.isEmpty())
            stream << "Entity ID:     " << status.entity.id << '\n';
        if (!status.entity.uuid.isNull())
            stream << "Entity UUID:   " << status.entity.uuid.toString(QUuid::WithoutBraces) << '\n';
        if (status.entity.index > 0)
            stream << "Entity index:  " << status.entity.index << '\n';
    }
    if (!status.details.isEmpty())
    {
        stream << "Details:\n";
        for (const QString &detail : status.details)
            stream << "  - " << detail << '\n';
    }
    stream << "--------------------------------------------------\n";
    return output;
}

void HydraulicSimulationStatusPrinter::print(const HydraulicSimulationStatus &status)
{
    FILE *output_file = status.success ? stdout : stderr;
    QTextStream stream(output_file);
    stream << toString(status);
    stream.flush();
}
