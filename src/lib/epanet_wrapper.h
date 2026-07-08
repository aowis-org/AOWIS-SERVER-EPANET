#ifndef EPANET_WRAPPER_H
#define EPANET_WRAPPER_H

#include <QObject>

#include <QDebug>

class EpanetWrapper : public QObject
{
    Q_OBJECT
    
public:
    explicit EpanetWrapper(QObject *parent = nullptr);
    
    void run();
    
signals:
    
    
private:
    
    
};

#endif // EPANET_WRAPPER_H
