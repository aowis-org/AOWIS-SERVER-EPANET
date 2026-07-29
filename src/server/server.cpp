#include "server.h"

#include <QDebug>
#include <QHostAddress>
#include <QHttpServerResponse>

#include <aowis/epanet/dummy/dummy_networks.h>
#include <aowis/epanet/epanet_runner.h>
#include <aowis/epanet/utility/epanet_result_printer.h>
#include <aowis/epanet/utility/epanet_status_printer.h>

Server::Server(QCoreApplication *app, QObject *parent)
    : QObject(parent), app(app)
{
    setupRoutes();
}

void Server::setupRoutes()
{
    const NetworkHydraulic network = DummyNetworks::networkTanks();

    EpanetRunner runner;
    const EpanetResultRun run_result = runner.run(network);

    EpanetStatusPrinter::print(run_result.result_timeline.status);
    EpanetResultPrinter::print(run_result.result_timeline);
    qDebug().noquote() << run_result.report_lines.join('\n');

    this->http.route("/status", []()
    {
        return QHttpServerResponse("AOWIS EPANET server", QHttpServerResponse::StatusCode::Ok);
    });

    this->tcp = new QTcpServer(this->app);
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
