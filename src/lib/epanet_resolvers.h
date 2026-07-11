#ifndef EPANET_RESOLVERS_H
#define EPANET_RESOLVERS_H

#include <QObject>
#include <QtMath>

#include "model/simulation_request.h"

class EpanetResolvers : public QObject
{
    Q_OBJECT
public:
    explicit EpanetResolvers(QObject *parent = nullptr);
    
    double resolveTankBottomElevation(const Tank &tank);
    double resolveTankDiameter(const Tank &tank);

signals:
};

#endif // EPANET_RESOLVERS_H
