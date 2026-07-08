#ifndef EPANET_WRAPPER_H
#define EPANET_WRAPPER_H

#include <QObject>
#include <QDebug>

#include <QString>

#if __has_include(<epanet2_2.h>)
#include <epanet2_2.h>
#elif __has_include(<epanet2.h>)
#include <epanet2.h>
#else
#error "Could not find EPANET header."
#endif

#include "_enums_structs.h"

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    void run();
    
    
private:
    EN_Project project = nullptr;
    
    void addReservoir(Reservoir reservoir);
    void addJunction(Junction junction);
    void addPipe(Pipe Pipe);
    
    void runHydraulics();
    void readResults();
    
signals:
        
};

#endif // EPANET_WRAPPER_H
