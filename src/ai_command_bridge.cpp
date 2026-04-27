#include "ai_command_bridge.h"

#include "device_controller.h"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QWebSocketServer>

namespace
{
QString compactHexText(const QString &text, int maxNibbles)
{
    QString hex;
    hex.reserve(maxNibbles);
    for (const QChar ch : text) {
        const QChar upper = ch.toUpper();
        if (ch.isDigit() || (upper >= QChar(QLatin1Char('A')) && upper <= QChar(QLatin1Char('F')))) {
            hex.append(upper);
            if (hex.size() >= maxNibbles) {
                break;
            }
        }
    }
    return hex;
}

QByteArray frameDataBytes(const QString &text, bool *ok)
{
    const QString hex = compactHexText(text, 16);
    if (ok != nullptr) {
        *ok = hex.size() % 2 == 0;
    }
    if (hex.size() % 2 != 0) {
        return {};
    }

    QByteArray data;
    data.reserve(hex.size() / 2);
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        bool byteOk = false;
        const auto value = static_cast<char>(hex.mid(i, 2).toUInt(&byteOk, 16));
        if (!byteOk) {
            if (ok != nullptr) {
                *ok = false;
            }
            return {};
        }
        data.append(value);
    }
    return data;
}

QString frameIdText(unsigned int frameId, bool extendedFrame)
{
    return QStringLiteral("%1").arg(frameId, extendedFrame ? 8 : 3, 16, QLatin1Char('0')).toUpper();
}

QString frameDataText(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (const char byte : data) {
        bytes.append(QStringLiteral("%1").arg(static_cast<unsigned char>(byte), 2, 16, QLatin1Char('0')).toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}
} // namespace

AiCommandBridge::AiCommandBridge(DeviceController *controller, QObject *parent)
    : QObject(parent)
    , controller_(controller)
{
    connect(controller_, &DeviceController::stateChanged, this, [this](const DeviceUiState &) {
        broadcastState();
    });
}

AiCommandBridge::~AiCommandBridge()
{
    stopLocalServer();
}

bool AiCommandBridge::startLocalServer(quint16 webSocketPort, quint16 tcpPort)
{
    Q_UNUSED(webSocketPort);

    if (tcpServer_ != nullptr) {
        return true;
    }

    if (tcpServer_ == nullptr) {
        tcpServer_ = new QTcpServer(this);
        if (!tcpServer_->listen(QHostAddress::LocalHost, tcpPort)) {
            tcpServer_->deleteLater();
            tcpServer_ = nullptr;
        } else {
            connect(tcpServer_, &QTcpServer::newConnection, this, [this]() {
                while (tcpServer_->hasPendingConnections()) {
                    auto *client = tcpServer_->nextPendingConnection();
                    if (client != nullptr) {
                        handleTcpClient(client);
                    }
                }
            });
        }
    }

    const bool anyServerStarted = tcpServer_ != nullptr;
    Q_EMIT bridgeMessage(anyServerStarted ? QStringLiteral("AI 本地接口已启动") : QStringLiteral("AI 本地接口启动失败"));
    return anyServerStarted;
}

void AiCommandBridge::stopLocalServer()
{
    if (server_ != nullptr) {
        const auto webSocketClients = server_->findChildren<QWebSocket *>();
        for (auto *client : webSocketClients) {
            if (client == nullptr) {
                continue;
            }
            client->disconnect();
            client->close();
        }

        server_->disconnect();
        server_->close();
        server_ = nullptr;
    }

    const auto tcpClients = tcpBuffers_.keys();
    tcpBuffers_.clear();
    for (auto *client : tcpClients) {
        if (client == nullptr) {
            continue;
        }
        client->disconnect();
        client->abort();
    }

    if (tcpServer_ != nullptr) {
        const auto strayTcpClients = tcpServer_->findChildren<QTcpSocket *>();
        for (auto *client : strayTcpClients) {
            if (client == nullptr) {
                continue;
            }
            client->disconnect();
            client->abort();
        }

        tcpServer_->disconnect();
        tcpServer_->close();
        tcpServer_ = nullptr;
    }
}

QJsonObject AiCommandBridge::execute(const QJsonObject &command)
{
    const QString action = command.value(QStringLiteral("action")).toString();

    if (action == QStringLiteral("get_state")) {
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("set_device")) {
        controller_->setDeviceType(command.value(QStringLiteral("deviceType")).toString(QStringLiteral("USBCAN-II")));
        controller_->setDeviceIndex(command.value(QStringLiteral("deviceIndex")).toInt(0));
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("open_device")) {
        controller_->openDevice();
        return {
            {QStringLiteral("ok"), controller_->state().deviceOpen},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("close_device")) {
        controller_->closeDevice();
        return {
            {QStringLiteral("ok"), true},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("start_channel")) {
        const int channel = command.value(QStringLiteral("channel")).toInt(0);
        const bool ok = controller_->startChannel(
            channel,
            command.value(QStringLiteral("bitrate")).toString(QStringLiteral("250kbps")),
            command.value(QStringLiteral("workMode")).toString(QStringLiteral("正常模式")));
        return {
            {QStringLiteral("ok"), ok},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("send_frame")) {
        const int channel = command.value(QStringLiteral("channel")).toInt(0);
        const QString frameId = compactHexText(command.value(QStringLiteral("frameId")).toString(), 8);
        const QString frameType = command.value(QStringLiteral("frameType")).toString(QStringLiteral("扩展帧"));
        const QString frameFormat = command.value(QStringLiteral("frameFormat")).toString(QStringLiteral("数据帧"));
        bool dataOk = false;
        const QByteArray data = frameDataBytes(command.value(QStringLiteral("data")).toString(), &dataOk);

        if (frameId.isEmpty()) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("invalid_frame_id")},
                {QStringLiteral("state"), stateToJson()},
            };
        }
        if (!dataOk) {
            return {
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("invalid_frame_data")},
                {QStringLiteral("state"), stateToJson()},
            };
        }

        const bool extendedFrame = frameType != QStringLiteral("标准帧");
        const bool remoteFrame = frameFormat == QStringLiteral("远程帧");
        const bool ok = controller_->transmitFrame(channel, frameId, extendedFrame, remoteFrame, data);
        return {
            {QStringLiteral("ok"), ok},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    if (action == QStringLiteral("get_rx_frames")) {
        const int channel = command.value(QStringLiteral("channel")).toInt(0);
        const int maxFrames = qBound(1, command.value(QStringLiteral("maxFrames")).toInt(99), 99);
        QVector<CanRxFrame> newlyReceivedFrames;
        const bool ok = controller_->receiveFrames(channel, maxFrames, &newlyReceivedFrames);
        const QVector<CanRxFrame> frames = controller_->cachedReceiveFrames(channel);

        const QString filterText = compactHexText(command.value(QStringLiteral("filterId")).toString(), 8);
        bool hasFilter = !filterText.isEmpty();
        bool filterOk = false;
        const unsigned int filterId = filterText.toUInt(&filterOk, 16);
        hasFilter = hasFilter && filterOk;

        QJsonArray frameArray;
        for (const auto &frame : std::as_const(frames)) {
            if (hasFilter && frame.frameId != filterId) {
                continue;
            }

            QJsonObject frameObject;
            frameObject.insert(QStringLiteral("frameId"), frameIdText(frame.frameId, frame.extendedFrame));
            frameObject.insert(QStringLiteral("frameType"), frame.extendedFrame ? QStringLiteral("扩展帧") : QStringLiteral("标准帧"));
            frameObject.insert(QStringLiteral("frameFormat"), frame.remoteFrame ? QStringLiteral("远程帧") : QStringLiteral("数据帧"));
            frameObject.insert(QStringLiteral("dlc"), frame.data.size());
            frameObject.insert(QStringLiteral("data"), frame.remoteFrame ? QString() : frameDataText(frame.data));
            frameObject.insert(QStringLiteral("timestampUs"), QString::number(frame.timestampUs));
            frameArray.append(frameObject);
        }

        return {
            {QStringLiteral("ok"), ok},
            {QStringLiteral("frames"), frameArray},
            {QStringLiteral("state"), stateToJson()},
        };
    }

    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("unknown_action")},
    };
}

QJsonObject AiCommandBridge::stateToJson() const
{
    const DeviceUiState &state = controller_->state();
    return {
        {QStringLiteral("deviceType"), state.deviceType},
        {QStringLiteral("deviceIndex"), state.deviceIndex},
        {QStringLiteral("deviceOpen"), state.deviceOpen},
        {QStringLiteral("channel0Started"), state.channel0Started},
        {QStringLiteral("channel1Started"), state.channel1Started},
        {QStringLiteral("bitrate"), state.bitrate},
        {QStringLiteral("workMode"), state.workMode},
    };
}

void AiCommandBridge::sendState(QWebSocket *client)
{
    if (client == nullptr) {
        return;
    }
    client->sendTextMessage(QJsonDocument({
        {QStringLiteral("event"), QStringLiteral("state_changed")},
        {QStringLiteral("state"), stateToJson()},
    }).toJson(QJsonDocument::Compact));
}

void AiCommandBridge::broadcastState()
{
    // Current AI control mode is request/response only. Clients can call
    // get_state whenever they need synchronized UI state.
}

void AiCommandBridge::handleTcpClient(QTcpSocket *client)
{
    client->setParent(this);
    tcpBuffers_.insert(client, {});

    connect(client, &QTcpSocket::readyRead, this, [this, client]() {
        handleTcpReadyRead(client);
    });
    connect(client, &QTcpSocket::disconnected, this, [this, client]() {
        tcpBuffers_.remove(client);
        client->deleteLater();
    });
}

void AiCommandBridge::handleTcpReadyRead(QTcpSocket *client)
{
    if (client == nullptr) {
        return;
    }

    QByteArray &buffer = tcpBuffers_[client];
    buffer.append(client->readAll());

    int newlineIndex = buffer.indexOf('\n');
    while (newlineIndex >= 0) {
        const QByteArray line = buffer.left(newlineIndex).trimmed();
        buffer.remove(0, newlineIndex + 1);

        if (!line.isEmpty()) {
            const auto document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                sendTcpResponse(client, {
                                            {QStringLiteral("ok"), false},
                                            {QStringLiteral("error"), QStringLiteral("invalid_json")},
                                        });
            } else {
                sendTcpResponse(client, execute(document.object()));
            }
        }

        newlineIndex = buffer.indexOf('\n');
    }
}

void AiCommandBridge::sendTcpResponse(QTcpSocket *client, const QJsonObject &response) const
{
    if (client == nullptr) {
        return;
    }

    QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    payload.append('\n');
    client->write(payload);
    client->flush();
}
