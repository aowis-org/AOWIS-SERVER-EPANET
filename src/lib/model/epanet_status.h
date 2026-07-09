#ifndef EPANET_STATUS_H
#define EPANET_STATUS_H

#include <QString>
#include <QStringList>

enum class EpanetStage
{
    None,
    
    CreateProject,
    InitializeProject,
    BuildNetwork,
    
    AddReservoir,
    AddJunction,
    AddPipe,
    
    RunHydraulics,
    ReadResults,
    ReadJunctionResults,
    ReadPipeResults,
    
    CloseHydraulics,
    SaveHydraulics,
    GenerateReport,
    Cleanup
};

enum class EpanetOperation
{
    None,
    
    EN_createproject,
    EN_deleteproject,
    
    EN_init,
    
    EN_addnode,
    EN_setnodevalue,
    EN_setjuncdata,
    
    EN_addlink,
    EN_setpipedata,
    EN_setlinkvalue,
    
    EN_setreport,
    EN_setreportcallback,
    EN_setreportcallbackuserdata,
    
    EN_openH,
    EN_initH,
    EN_runH,
    EN_nextH,
    EN_closeH,
    EN_saveH,
    
    EN_report,
    
    EN_getnodeindex,
    EN_getnodevalue,
    EN_getlinkindex,
    EN_getlinkvalue,
    
    EN_geterror
};

enum class EpanetEntityType
{
    None,
    
    Project,
    Network,
    
    Node,
    Junction,
    Reservoir,
    Tank,
    
    Link,
    Pipe,
    Pump,
    Valve,
    
    Pattern,
    Curve,
    Control,
    Rule,
    
    HydraulicSolver,
    Report,
    Result
};

struct EpanetEntity
{
    EpanetEntityType type = EpanetEntityType::None;
    
    QString id;             // "P1", "J42", ...
    int index = 0;
};

struct EpanetStatus
{
    bool success = true;
    
    int epanet_error_code = 0;
    
    EpanetStage stage = EpanetStage::None;
    EpanetOperation operation = EpanetOperation::None;
    
    EpanetEntity entity;
    
    QString message;        // AOWIS/user-facing message
    QString epanet_message; // decoded EPANET message if available
    QStringList details;    // optional extra context
};

#endif // EPANET_STATUS_H
