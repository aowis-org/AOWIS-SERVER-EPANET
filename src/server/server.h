#ifndef SERVER_H
#define SERVER_H

#include <QByteArray>
#include <QCoreApplication>
#include <QFuture>
#include <QHash>
#include <QHostAddress>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <QTcpServer>

struct PendingList
{
    QPromise<QHttpServerResponse> **items = nullptr;
    int count = 0;
    int capacity = 0;
    
    void append(QPromise<QHttpServerResponse> *promise)
    {
        if (this->count == this->capacity)
        {
            const int new_capacity = this->capacity == 0 ? 4 : this->capacity * 2;
            QPromise<QHttpServerResponse> **new_items =
                new QPromise<QHttpServerResponse> *[new_capacity];
            
            for (int i = 0; i < this->count; ++i)
            {
                new_items[i] = this->items[i];
            }
            
            delete[] this->items;
            this->items = new_items;
            this->capacity = new_capacity;
        }
        
        this->items[this->count++] = promise;
    }
    
    ~PendingList()
    {
        delete[] this->items;
    }
};

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
    QMutex mutex_pending;
    QHash<QString, PendingList> connections_pending;
    
private slots:
    void onTileReady(QString key, QByteArray data);
};

#endif // SERVER_H
