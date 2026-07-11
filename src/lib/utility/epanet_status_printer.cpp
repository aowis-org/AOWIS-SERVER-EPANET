#include "epanet_status_printer.h"

#include <QTextStream>

#include <cstdio>

QString EpanetStatusPrinter::toString(const EpanetStatus &status)
{
    QString output;
    QTextStream stream(&output);

    stream << "--------------------------------------------------\n";
    stream << "EPANET STATUS: "
           << (status.success ? "SUCCESS" : "ERROR")
           << '\n';

    if (!status.message.isEmpty())
        stream << "Message:       " << status.message << '\n';

    if (!status.message_epanet.isEmpty())
        stream << "EPANET:        " << status.message_epanet << '\n';

    if (status.epanet_error_code != 0)
        stream << "Error code:    " << status.epanet_error_code << '\n';

    if (status.stage != EpanetStage::None)
        stream << "Stage:         " << stageToString(status.stage) << '\n';

    if (status.operation != EpanetOperation::None)
        stream << "Operation:     "
               << operationToString(status.operation)
               << '\n';

    if (status.property != EpanetProperty::None)
        stream << "Property:      "
               << propertyToString(status.property)
               << '\n';

    if (status.entity.type != EpanetEntityType::None)
    {
        stream << "Entity type:   "
               << entityTypeToString(status.entity.type)
               << '\n';

        if (!status.entity.id.isEmpty())
            stream << "Entity ID:     " << status.entity.id << '\n';

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

void EpanetStatusPrinter::print(const EpanetStatus &status)
{
    FILE *output_file = status.success ? stdout : stderr;

    QTextStream stream(output_file);
    stream << toString(status);
    stream.flush();
}

QString EpanetStatusPrinter::stageToString(EpanetStage stage)
{
    switch (stage)
    {
    case EpanetStage::None:
        return "None";
    case EpanetStage::CreateProject:
        return "Create project";
    case EpanetStage::InitializeProject:
        return "Initialize project";
    case EpanetStage::BuildNetwork:
        return "Build network";
    case EpanetStage::AddReservoir:
        return "Add reservoir";
    case EpanetStage::AddJunction:
        return "Add junction";
    case EpanetStage::AddPipe:
        return "Add pipe";
    case EpanetStage::RunHydraulics:
        return "Run hydraulics";
    case EpanetStage::ReadResults:
        return "Read results";
    case EpanetStage::ReadJunctionResults:
        return "Read junction results";
    case EpanetStage::ReadPipeResults:
        return "Read pipe results";
    case EpanetStage::CloseHydraulics:
        return "Close hydraulics";
    case EpanetStage::SaveHydraulics:
        return "Save hydraulics";
    case EpanetStage::GenerateReport:
        return "Generate report";
    case EpanetStage::Cleanup:
        return "Cleanup";
    }

    return "Unknown";
}

QString EpanetStatusPrinter::operationToString(EpanetOperation operation)
{
    switch (operation)
    {
    case EpanetOperation::None:
        return "None";
    case EpanetOperation::EN_createproject:
        return "EN_createproject";
    case EpanetOperation::EN_deleteproject:
        return "EN_deleteproject";
    case EpanetOperation::EN_init:
        return "EN_init";
    case EpanetOperation::EN_addnode:
        return "EN_addnode";
    case EpanetOperation::EN_setnodevalue:
        return "EN_setnodevalue";
    case EpanetOperation::EN_setjuncdata:
        return "EN_setjuncdata";
    case EpanetOperation::EN_addlink:
        return "EN_addlink";
    case EpanetOperation::EN_setpipedata:
        return "EN_setpipedata";
    case EpanetOperation::EN_setlinkvalue:
        return "EN_setlinkvalue";
    case EpanetOperation::EN_setreport:
        return "EN_setreport";
    case EpanetOperation::EN_setreportcallback:
        return "EN_setreportcallback";
    case EpanetOperation::EN_setreportcallbackuserdata:
        return "EN_setreportcallbackuserdata";
    case EpanetOperation::EN_openH:
        return "EN_openH";
    case EpanetOperation::EN_initH:
        return "EN_initH";
    case EpanetOperation::EN_runH:
        return "EN_runH";
    case EpanetOperation::EN_nextH:
        return "EN_nextH";
    case EpanetOperation::EN_closeH:
        return "EN_closeH";
    case EpanetOperation::EN_saveH:
        return "EN_saveH";
    case EpanetOperation::EN_report:
        return "EN_report";
    case EpanetOperation::EN_getnodeindex:
        return "EN_getnodeindex";
    case EpanetOperation::EN_getnodevalue:
        return "EN_getnodevalue";
    case EpanetOperation::EN_getlinkindex:
        return "EN_getlinkindex";
    case EpanetOperation::EN_getlinkvalue:
        return "EN_getlinkvalue";
    case EpanetOperation::EN_geterror:
        return "EN_geterror";
    }

    return "Unknown";
}

QString EpanetStatusPrinter::propertyToString(EpanetProperty property)
{
    switch (property)
    {
    case EpanetProperty::None:
        return "None";
    case EpanetProperty::Elevation:
        return "Elevation";
    case EpanetProperty::Head:
        return "Head";
    case EpanetProperty::Pressure:
        return "Pressure";
    case EpanetProperty::Flow:
        return "Flow";
    case EpanetProperty::Velocity:
        return "Velocity";
    case EpanetProperty::Headloss:
        return "Headloss";
    case EpanetProperty::InitialStatus:
        return "Initial status";
    }

    return "Unknown";
}

QString EpanetStatusPrinter::entityTypeToString(EpanetEntityType type)
{
    switch (type)
    {
    case EpanetEntityType::None:
        return "None";
    case EpanetEntityType::Project:
        return "Project";
    case EpanetEntityType::Network:
        return "Network";
    case EpanetEntityType::Node:
        return "Node";
    case EpanetEntityType::Junction:
        return "Junction";
    case EpanetEntityType::Reservoir:
        return "Reservoir";
    case EpanetEntityType::Tank:
        return "Tank";
    case EpanetEntityType::Link:
        return "Link";
    case EpanetEntityType::Pipe:
        return "Pipe";
    case EpanetEntityType::Pump:
        return "Pump";
    case EpanetEntityType::Valve:
        return "Valve";
    case EpanetEntityType::Pattern:
        return "Pattern";
    case EpanetEntityType::Curve:
        return "Curve";
    case EpanetEntityType::Control:
        return "Control";
    case EpanetEntityType::Rule:
        return "Rule";
    case EpanetEntityType::HydraulicSolver:
        return "Hydraulic solver";
    case EpanetEntityType::Report:
        return "Report";
    case EpanetEntityType::Result:
        return "Result";
    }

    return "Unknown";
}
