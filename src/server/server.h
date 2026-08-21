#ifndef SERVER_H
#define SERVER_H

#include <QObject>
#include <QHttpServer>
#include <QTcpServer>

class EpanetSimulationManager;

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);

private:
    void setupRoutes();

    EpanetSimulationManager *simulation_manager = nullptr;
    QHttpServer http;
    QTcpServer *tcp = nullptr;
};

#endif
