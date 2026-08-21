#include "server.h"

#include <QDebug>
#include <QHostAddress>
#include <QHttpServerResponse>

#include <aowis/epanet/epanet_simulation_manager.h>

Server::Server(QObject *parent)
    : QObject(parent)
{
    this->simulation_manager = new EpanetSimulationManager(this);
    setupRoutes();
}

void Server::setupRoutes()
{
    this->http.route("/status", []()
    {
        return QHttpServerResponse("AOWIS EPANET server", QHttpServerResponse::StatusCode::Ok);
    });

    this->tcp = new QTcpServer(this);
    if (!this->tcp->listen(QHostAddress::Any, 8122))
    {
        qWarning() << "Failed to listen on port 8122";
        return;
    }

    if (!this->http.bind(this->tcp))
        qWarning() << "Failed to bind QHttpServer to QTcpServer";
    else
        qDebug() << "AOWIS EPANET server listening on port 8122";
}
