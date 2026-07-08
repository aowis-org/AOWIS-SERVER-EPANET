#include "epanet_wrapper.h"

EpanetWrapper::EpanetWrapper(QObject *parent)
    : QObject(parent)
{
    
}

void EpanetWrapper::run()
{
    qDebug() << "running ...";
}
