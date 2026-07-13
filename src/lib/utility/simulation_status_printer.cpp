#include "simulation_status_printer.h"

#include <QMetaEnum>
#include <QRegularExpression>
#include <QTextStream>

#include <cstdio>

namespace
{
    template<typename EnumType>
    QString enumKey(EnumType value)
    {
        const QMetaEnum meta_enum =
            QMetaEnum::fromType<EnumType>();
        
        const char *key =
            meta_enum.valueToKey(static_cast<int>(value));
        
        if (key == nullptr)
            return QStringLiteral("Unknown");
        
        return QString::fromLatin1(key);
    }
    
    template<typename EnumType>
    QString enumLabel(EnumType value)
    {
        QString text = enumKey(value);
        
        if (text == QStringLiteral("Unknown"))
            return text;
        
        static const QRegularExpression acronym_boundary(
            QStringLiteral("([A-Z]+)([A-Z][a-z])")
            );
        
        static const QRegularExpression camel_case_boundary(
            QStringLiteral("([a-z0-9])([A-Z])")
            );
        
        text.replace(
            acronym_boundary,
            QStringLiteral("\\1 \\2")
            );
        
        text.replace(
            camel_case_boundary,
            QStringLiteral("\\1 \\2")
            );
        
        text = text.toLower();
        
        if (!text.isEmpty())
            text[0] = text[0].toUpper();
        
        return text;
    }
}

QString SimulationStatusPrinter::toString(
    const EpanetStatus &status
)
{
    QString output;
    QTextStream stream(&output);
    
    stream << "--------------------------------------------------\n";
    stream << "EPANET STATUS: "
           << (status.success ? "SUCCESS" : "ERROR")
           << '\n';
    
    if (!status.message.isEmpty())
    {
        stream << "Message:       "
               << status.message
               << '\n';
    }
    
    if (!status.message_epanet.isEmpty())
    {
        stream << "EPANET:        "
               << status.message_epanet
               << '\n';
    }
    
    if (status.epanet_error_code != 0)
    {
        stream << "Error code:    "
               << status.epanet_error_code
               << '\n';
    }
    
    if (status.stage != EpanetStage::None)
    {
        stream << "Stage:         "
               << enumLabel(status.stage)
               << '\n';
    }
    
    if (status.operation != EpanetOperation::None)
    {
        stream << "Operation:     "
               << enumKey(status.operation)
               << '\n';
    }
    
    if (status.property != EpanetProperty::None)
    {
        stream << "Property:      "
               << enumLabel(status.property)
               << '\n';
    }
    
    if (status.entity.type != EpanetEntityType::None)
    {
        stream << "Entity type:   "
               << enumLabel(status.entity.type)
               << '\n';
        
        if (!status.entity.id.isEmpty())
        {
            stream << "Entity ID:     "
                   << status.entity.id
                   << '\n';
        }
        
        if (status.entity.index > 0)
        {
            stream << "Entity index:  "
                   << status.entity.index
                   << '\n';
        }
    }
    
    if (!status.details.isEmpty())
    {
        stream << "Details:\n";
        
        for (const QString &detail : status.details)
        {
            stream << "  - "
                   << detail
                   << '\n';
        }
    }
    
    stream << "--------------------------------------------------\n";
    
    return output;
}

void SimulationStatusPrinter::print(
    const EpanetStatus &status
)
{
    FILE *output_file =
        status.success ? stdout : stderr;
    
    QTextStream stream(output_file);
    stream << toString(status);
    stream.flush();
}
