#pragma once

#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QHash>

class DeviceController;
class QTcpServer;
class QTcpSocket;
class QWebSocket;
class QWebSocketServer;

class AiCommandBridge final : public QObject
{
    Q_OBJECT

public:
    explicit AiCommandBridge(DeviceController *controller, QObject *parent = nullptr);
    ~AiCommandBridge() override;

    bool startLocalServer(quint16 webSocketPort = 17651, quint16 tcpPort = 17652);
    void stopLocalServer();
    Q_INVOKABLE QJsonObject execute(const QJsonObject &command);

Q_SIGNALS:
    void bridgeMessage(const QString &message);

private:
    QJsonObject stateToJson() const;
    void sendState(QWebSocket *client);
    void broadcastState();
    void handleTcpClient(QTcpSocket *client);
    void handleTcpReadyRead(QTcpSocket *client);
    void sendTcpResponse(QTcpSocket *client, const QJsonObject &response) const;

    DeviceController *controller_ = nullptr;
    QWebSocketServer *server_ = nullptr;
    QTcpServer *tcpServer_ = nullptr;
    QHash<QTcpSocket *, QByteArray> tcpBuffers_;
};
