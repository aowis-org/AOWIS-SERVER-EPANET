#ifndef EPANET_STATUS_H
#define EPANET_STATUS_H

#include <QObject>
#include <QString>
#include <QStringList>

class EpanetEnums
{
    Q_GADGET
    
public:
    enum class Stage
    {
        None,
        
        CreateProject,
        InitializeProject,
        
        BuildNetwork,
        AddTankVolumeCurve,
        AddReservoir,
        AddJunction,
        AddTank,
        AddPipe,
        
        RunHydraulics,
        ReadResults,
        ReadJunctionResults,
        ReadTankResults,
        ReadPipeResults,
        
        CloseHydraulics,
        SaveHydraulics,
        GenerateReport,
        Cleanup
    };
    Q_ENUM(Stage)
    
    enum class Operation
    {
        None,
        
        EN_createproject,
        EN_deleteproject,
        
        EN_init,
        
        EN_addcurve,
        EN_getcurveindex,
        EN_setcurve,
        
        EN_addnode,
        EN_setnodevalue,
        EN_settankdata,
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
    Q_ENUM(Operation)
    
    enum class Property
    {
        None,
        
        Elevation,
        Head,
        Pressure,
        Level,
        Volume,
        
        Flow,
        Velocity,
        Headloss,
        
        InitialStatus
    };
    Q_ENUM(Property)
    
    enum class EntityType
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
    Q_ENUM(EntityType)
};

using EpanetStage = EpanetEnums::Stage;
using EpanetOperation = EpanetEnums::Operation;
using EpanetProperty = EpanetEnums::Property;
using EpanetEntityType = EpanetEnums::EntityType;

struct EpanetEntity
{
    EpanetEntityType type = EpanetEntityType::None;
    
    QString id;
    int index = 0;
};

struct EpanetStatus
{
    bool success = true;
    
    int epanet_error_code = 0;
    
    EpanetStage stage = EpanetStage::None;
    EpanetOperation operation = EpanetOperation::None;
    EpanetProperty property = EpanetProperty::None;
    
    EpanetEntity entity;
    
    QString message;
    QString message_epanet;
    QStringList details;
};

#endif // EPANET_STATUS_H
