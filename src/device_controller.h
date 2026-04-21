#pragma once

#include "can_device_backend.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <array>

struct DeviceUiState
{
    QString deviceType = QStringLiteral("USBCAN-II");
    int deviceIndex = 0;
    bool deviceOpen = false;
    bool channel0Started = false;
    bool channel1Started = false;
    QString bitrate = QStringLiteral("250kbps");
    QString workMode = QStringLiteral("正常模式");
};

class DeviceController final : public QObject
{
    Q_OBJECT

public:
    explicit DeviceController(QObject *parent = nullptr);

    const DeviceUiState &state() const;

public Q_SLOTS:
    void setDeviceType(const QString &deviceType);
    void setDeviceIndex(int deviceIndex);
    void openDevice();
    void closeDevice();
    bool startChannel(int channelIndex, const QString &bitrate, const QString &workMode);
    bool transmitFrame(int channelIndex, const QString &frameId, bool extendedFrame, bool remoteFrame, const QByteArray &data);
    bool receiveFrames(int channelIndex, int maxFrames, QVector<CanRxFrame> *frames);
    QVector<CanRxFrame> cachedReceiveFrames(int channelIndex) const;

Q_SIGNALS:
    void stateChanged(const DeviceUiState &state);
    void operationMessage(const QString &message);

private:
    void emitState(const QString &message);

    CanDeviceBackend backend_;
    DeviceUiState state_;
    std::array<QVector<CanRxFrame>, 2> receiveHistory_;
};
