#include "device_controller.h"

#include <QDateTime>

DeviceController::DeviceController(QObject *parent)
    : QObject(parent)
{
}

const DeviceUiState &DeviceController::state() const
{
    return state_;
}

void DeviceController::setDeviceType(const QString &deviceType)
{
    if (state_.deviceType == deviceType) {
        return;
    }

    state_.deviceType = deviceType;
    emitState(QStringLiteral("设备类型已更新"));
}

void DeviceController::setDeviceIndex(int deviceIndex)
{
    if (state_.deviceIndex == deviceIndex) {
        return;
    }

    state_.deviceIndex = deviceIndex;
    emitState(QStringLiteral("设备索引已更新"));
}

void DeviceController::openDevice()
{
    if (state_.deviceOpen) {
        Q_EMIT operationMessage(QStringLiteral("设备已经打开"));
        return;
    }

    QString errorMessage;
    if (!backend_.open(state_.deviceType, state_.deviceIndex, &errorMessage)) {
        Q_EMIT operationMessage(QStringLiteral("打开设备失败：%1").arg(errorMessage));
        return;
    }

    state_.deviceOpen = true;
    emitState(QStringLiteral("设备已打开"));
}

void DeviceController::closeDevice()
{
    if (!state_.deviceOpen) {
        Q_EMIT operationMessage(QStringLiteral("设备尚未打开"));
        return;
    }

    state_.deviceOpen = false;
    state_.channel0Started = false;
    state_.channel1Started = false;
    receiveHistory_[0].clear();
    receiveHistory_[1].clear();
    backend_.close();
    emitState(QStringLiteral("设备已关闭"));
}

bool DeviceController::startChannel(int channelIndex, const QString &bitrate, const QString &workMode)
{
    if (!state_.deviceOpen) {
        Q_EMIT operationMessage(QStringLiteral("请先打开设备"));
        return false;
    }

    if (channelIndex != 0 && channelIndex != 1) {
        Q_EMIT operationMessage(QStringLiteral("无效的通道索引"));
        return false;
    }

    QString errorMessage;
    if (!backend_.startChannel(channelIndex, bitrate, workMode, &errorMessage)) {
        Q_EMIT operationMessage(QStringLiteral("启动通道失败：%1").arg(errorMessage));
        return false;
    }

    state_.bitrate = bitrate;
    state_.workMode = workMode;

    if (channelIndex == 0) {
        state_.channel0Started = true;
    } else {
        state_.channel1Started = true;
    }
    receiveHistory_.at(static_cast<size_t>(channelIndex)).clear();

    emitState(QStringLiteral("通道已启动"));
    return true;
}

bool DeviceController::transmitFrame(int channelIndex,
                                     const QString &frameId,
                                     bool extendedFrame,
                                     bool remoteFrame,
                                     const QByteArray &data)
{
    if (!state_.deviceOpen) {
        Q_EMIT operationMessage(QStringLiteral("请先打开设备"));
        return false;
    }
    if (channelIndex == 0 && !state_.channel0Started) {
        Q_EMIT operationMessage(QStringLiteral("请先启动CAN0"));
        return false;
    }
    if (channelIndex == 1 && !state_.channel1Started) {
        Q_EMIT operationMessage(QStringLiteral("请先启动CAN1"));
        return false;
    }
    if (channelIndex != 0 && channelIndex != 1) {
        Q_EMIT operationMessage(QStringLiteral("无效的通道索引"));
        return false;
    }

    bool idOk = false;
    const unsigned int id = frameId.toUInt(&idOk, 16);
    if (!idOk) {
        Q_EMIT operationMessage(QStringLiteral("帧ID无效"));
        return false;
    }

    QString errorMessage;
    if (!backend_.transmitFrame(channelIndex, id, extendedFrame, remoteFrame, data, &errorMessage)) {
        Q_EMIT operationMessage(QStringLiteral("发送失败：%1").arg(errorMessage));
        return false;
    }

    Q_EMIT operationMessage(QStringLiteral("CAN%1发送成功").arg(channelIndex));
    return true;
}

bool DeviceController::receiveFrames(int channelIndex, int maxFrames, QVector<CanRxFrame> *frames)
{
    if (frames == nullptr) {
        return false;
    }
    frames->clear();
    if (!state_.deviceOpen) {
        return false;
    }
    if (channelIndex == 0 && !state_.channel0Started) {
        return false;
    }
    if (channelIndex == 1 && !state_.channel1Started) {
        return false;
    }
    if (channelIndex != 0 && channelIndex != 1) {
        return false;
    }

    QString errorMessage;
    if (!backend_.receiveFrames(channelIndex, maxFrames, frames, &errorMessage)) {
        Q_EMIT operationMessage(QStringLiteral("接收失败：%1").arg(errorMessage));
        return false;
    }

    auto &history = receiveHistory_.at(static_cast<size_t>(channelIndex));
    for (auto &frame : *frames) {
        frame.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
        history.append(frame);
        while (history.size() > 99) {
            history.removeFirst();
        }
    }
    return true;
}

QVector<CanRxFrame> DeviceController::cachedReceiveFrames(int channelIndex) const
{
    if (channelIndex != 0 && channelIndex != 1) {
        return {};
    }
    return receiveHistory_.at(static_cast<size_t>(channelIndex));
}

void DeviceController::emitState(const QString &message)
{
    Q_EMIT stateChanged(state_);
    Q_EMIT operationMessage(message);
}
