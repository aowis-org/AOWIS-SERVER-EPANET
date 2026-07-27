#ifndef SERVER_H
#define SERVER_H

#include <QCoreApplication>
#include <QHttpServer>
#include <QObject>
#include <QTcpServer>

class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QCoreApplication *app, QObject *parent = nullptr);

private:
    void setupRoutes();

    QCoreApplication *app = nullptr;
    QHttpServer http;
    QTcpServer *tcp = nullptr;
};

#endif
