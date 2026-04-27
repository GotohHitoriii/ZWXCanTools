#include "uds_upgrade_manager.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace
{
constexpr int kDefaultResponseTimeoutMs = 1500;
constexpr int kExtendedResponseTimeoutMs = 10000;
constexpr int kTransferExitTimeoutMs = 3000;
constexpr int kResetTimeoutMs = 4000;
constexpr int kPollIntervalMs = 10;
constexpr int kTesterPresentIntervalMs = 1800;
constexpr char kPaddingByte = static_cast<char>(0xFF);

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString trimLine(const QString &line)
{
    QString text = line.trimmed();
    if (!text.isEmpty() && text.endsWith(QLatin1Char('\r'))) {
        text.chop(1);
    }
    return text;
}

bool parseHexByte(const QString &text, quint8 *value)
{
    bool ok = false;
    const uint parsed = text.toUInt(&ok, 16);
    if (!ok || parsed > 0xFFU) {
        return false;
    }
    *value = static_cast<quint8>(parsed);
    return true;
}

bool parseHexWord(const QString &text, quint16 *value)
{
    bool ok = false;
    const uint parsed = text.toUInt(&ok, 16);
    if (!ok || parsed > 0xFFFFU) {
        return false;
    }
    *value = static_cast<quint16>(parsed);
    return true;
}
} // namespace

UdsUpgradeManager::UdsUpgradeManager(DeviceController *controller, QObject *parent)
    : QObject(parent)
    , controller_(controller)
    , pollTimer_(new QTimer(this))
    , testerPresentTimer_(new QTimer(this))
{
    pollTimer_->setInterval(kPollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, [this]() {
        pollBus();
        pollSender();

        if (sender_.state == SenderState::WaitingFlowControl && sender_.deadlineMs > 0 && nowMs() > sender_.deadlineMs) {
            failUpgrade(QStringLiteral("等待流控帧超时"));
            return;
        }

        if (pendingResponse_ != PendingResponse::None && responseDeadlineMs_ > 0 && nowMs() > responseDeadlineMs_) {
            if (pendingResponse_ == PendingResponse::EcuReset) {
                completeSuccess(QStringLiteral("固件传输完成，复位请求已发送"));
                return;
            }
            failUpgrade(QStringLiteral("等待 ECU 响应超时"));
        }
    });

    testerPresentTimer_->setInterval(kTesterPresentIntervalMs);
    connect(testerPresentTimer_, &QTimer::timeout, this, &UdsUpgradeManager::maybeSendTesterPresent);
}

bool UdsUpgradeManager::isRunning() const
{
    return running_;
}

bool UdsUpgradeManager::startUpgrade(const UdsUpgradeConfig &config)
{
    if (running_) {
        return false;
    }
    if (controller_ == nullptr) {
        return false;
    }

    const auto &state = controller_->state();
    const bool channelStarted = config.channelIndex == 0 ? state.channel0Started : state.channel1Started;
    if (!state.deviceOpen || !channelStarted) {
        appendLog(QStringLiteral("升级前请先打开设备并启动 CAN%1").arg(config.channelIndex));
        return false;
    }

    QVector<UdsUpgradeSegment> segments;
    QString summary;
    QString errorMessage;
    if (!loadFirmwareImage(config, &segments, &summary, &errorMessage)) {
        appendLog(errorMessage);
        return false;
    }

    config_ = config;
    segments_ = segments;
    firmwareSummary_ = summary;
    resetRuntimeState();

    totalBytes_ = 0;
    for (const auto &segment : segments_) {
        totalBytes_ += segment.data.size();
    }

    running_ = true;
    Q_EMIT runningChanged(true);
    Q_EMIT progressChanged(0);
    setStatus(QStringLiteral("升级中"), QStringLiteral("busy"));
    appendLog(QStringLiteral("已加载固件：%1").arg(firmwareSummary_));
    appendLog(QStringLiteral("通信配置：CAN%1, PHY 0x%2, RSP 0x%3, FUN 0x%4")
                  .arg(config_.channelIndex)
                  .arg(frameIdText(config_.physicalId))
                  .arg(frameIdText(config_.responseId))
                  .arg(frameIdText(config_.functionalId)));
    startPolling();
    startSequence();
    return true;
}

void UdsUpgradeManager::cancel()
{
    if (!running_) {
        return;
    }
    appendLog(QStringLiteral("升级已由用户停止"));
    setStatus(QStringLiteral("已停止"), QStringLiteral("idle"));
    stopPolling();
    resetRuntimeState();
    running_ = false;
    Q_EMIT runningChanged(false);
    Q_EMIT finished(false, QStringLiteral("升级已取消"));
}

bool UdsUpgradeManager::loadFirmwareImage(const UdsUpgradeConfig &config,
                                          QVector<UdsUpgradeSegment> *segments,
                                          QString *summary,
                                          QString *errorMessage) const
{
    if (segments == nullptr || summary == nullptr || errorMessage == nullptr) {
        return false;
    }

    segments->clear();
    summary->clear();
    errorMessage->clear();

    const QFileInfo info(config.firmwarePath);
    if (!info.exists() || !info.isFile()) {
        *errorMessage = QStringLiteral("固件文件不存在");
        return false;
    }

    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("bin") || suffix == QStringLiteral("pkg")) {
        return loadBinaryFirmware(config, segments, summary, errorMessage);
    }
    if (suffix == QStringLiteral("hex")) {
        return loadIntelHexFirmware(config.firmwarePath, segments, summary, errorMessage);
    }
    if (suffix == QStringLiteral("s19") || suffix == QStringLiteral("s28") || suffix == QStringLiteral("s37")
        || suffix == QStringLiteral("mot")) {
        return loadMotorolaFirmware(config.firmwarePath, segments, summary, errorMessage);
    }

    *errorMessage = QStringLiteral("暂不支持该固件格式：%1").arg(info.suffix());
    return false;
}

bool UdsUpgradeManager::loadBinaryFirmware(const UdsUpgradeConfig &config,
                                           QVector<UdsUpgradeSegment> *segments,
                                           QString *summary,
                                           QString *errorMessage) const
{
    QFile file(config.firmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("无法打开固件文件");
        return false;
    }

    UdsUpgradeSegment segment;
    segment.startAddress = config.downloadAddress;
    segment.data = file.readAll();
    if (segment.data.isEmpty()) {
        *errorMessage = QStringLiteral("PKG/BIN 固件为空");
        return false;
    }

    segments->append(segment);
    const QString typeLabel = QFileInfo(config.firmwarePath).suffix().toUpper();
    *summary = QStringLiteral("%1, %2, 1 段, 起始地址 0x%3, %4 字节")
                   .arg(QFileInfo(config.firmwarePath).fileName())
                   .arg(typeLabel.isEmpty() ? QStringLiteral("BIN") : typeLabel)
                   .arg(QStringLiteral("%1").arg(segment.startAddress, 8, 16, QLatin1Char('0')).toUpper())
                   .arg(segment.data.size());
    return true;
}

bool UdsUpgradeManager::loadIntelHexFirmware(const QString &path,
                                             QVector<UdsUpgradeSegment> *segments,
                                             QString *summary,
                                             QString *errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = QStringLiteral("无法打开 HEX 固件文件");
        return false;
    }

    QTextStream stream(&file);
    quint32 baseAddress = 0;
    int lineNumber = 0;
    while (!stream.atEnd()) {
        const QString line = trimLine(stream.readLine());
        ++lineNumber;
        if (line.isEmpty()) {
            continue;
        }
        if (!line.startsWith(QLatin1Char(':')) || line.size() < 11) {
            *errorMessage = QStringLiteral("HEX 第 %1 行格式无效").arg(lineNumber);
            return false;
        }

        quint8 count = 0;
        quint16 address = 0;
        quint8 recordType = 0;
        if (!parseHexByte(line.mid(1, 2), &count) || !parseHexWord(line.mid(3, 4), &address)
            || !parseHexByte(line.mid(7, 2), &recordType)) {
            *errorMessage = QStringLiteral("HEX 第 %1 行头部无效").arg(lineNumber);
            return false;
        }

        if (line.size() < 11 + count * 2) {
            *errorMessage = QStringLiteral("HEX 第 %1 行长度不足").arg(lineNumber);
            return false;
        }

        QByteArray data;
        data.reserve(count);
        for (int i = 0; i < count; ++i) {
            quint8 value = 0;
            if (!parseHexByte(line.mid(9 + i * 2, 2), &value)) {
                *errorMessage = QStringLiteral("HEX 第 %1 行数据无效").arg(lineNumber);
                return false;
            }
            data.append(static_cast<char>(value));
        }

        if (recordType == 0x00) {
            UdsUpgradeSegment segment;
            segment.startAddress = baseAddress + address;
            segment.data = data;
            segments->append(segment);
        } else if (recordType == 0x01) {
            break;
        } else if (recordType == 0x02) {
            if (data.size() != 2) {
                *errorMessage = QStringLiteral("HEX 第 %1 行扩展段地址无效").arg(lineNumber);
                return false;
            }
            baseAddress = static_cast<quint32>((static_cast<quint8>(data.at(0)) << 8)
                                               | static_cast<quint8>(data.at(1)))
                << 4;
        } else if (recordType == 0x04) {
            if (data.size() != 2) {
                *errorMessage = QStringLiteral("HEX 第 %1 行扩展线性地址无效").arg(lineNumber);
                return false;
            }
            baseAddress = static_cast<quint32>((static_cast<quint8>(data.at(0)) << 8)
                                               | static_cast<quint8>(data.at(1)))
                << 16;
        }
    }

    if (!mergeSegments(segments, errorMessage)) {
        return false;
    }
    if (segments->isEmpty()) {
        *errorMessage = QStringLiteral("HEX 固件中没有可下载数据");
        return false;
    }

    int byteCount = 0;
    for (const auto &segment : std::as_const(*segments)) {
        byteCount += segment.data.size();
    }
    *summary = QStringLiteral("%1, HEX, %2 段, %3 字节")
                   .arg(QFileInfo(path).fileName())
                   .arg(segments->size())
                   .arg(byteCount);
    return true;
}

bool UdsUpgradeManager::loadMotorolaFirmware(const QString &path,
                                             QVector<UdsUpgradeSegment> *segments,
                                             QString *summary,
                                             QString *errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = QStringLiteral("无法打开 S19 固件文件");
        return false;
    }

    QTextStream stream(&file);
    int lineNumber = 0;
    while (!stream.atEnd()) {
        const QString line = trimLine(stream.readLine());
        ++lineNumber;
        if (line.isEmpty()) {
            continue;
        }
        if (!line.startsWith(QLatin1Char('S')) || line.size() < 4) {
            continue;
        }

        const QChar recordType = line.at(1);
        int addressBytes = 0;
        if (recordType == QLatin1Char('1')) {
            addressBytes = 2;
        } else if (recordType == QLatin1Char('2')) {
            addressBytes = 3;
        } else if (recordType == QLatin1Char('3')) {
            addressBytes = 4;
        } else if (recordType == QLatin1Char('7') || recordType == QLatin1Char('8') || recordType == QLatin1Char('9')) {
            break;
        } else {
            continue;
        }

        quint8 byteCount = 0;
        if (!parseHexByte(line.mid(2, 2), &byteCount)) {
            *errorMessage = QStringLiteral("S19 第 %1 行长度字段无效").arg(lineNumber);
            return false;
        }

        const int minimumHexChars = 4 + byteCount * 2;
        if (line.size() < minimumHexChars) {
            *errorMessage = QStringLiteral("S19 第 %1 行长度不足").arg(lineNumber);
            return false;
        }

        quint32 address = 0;
        int offset = 4;
        for (int i = 0; i < addressBytes; ++i) {
            quint8 byte = 0;
            if (!parseHexByte(line.mid(offset, 2), &byte)) {
                *errorMessage = QStringLiteral("S19 第 %1 行地址无效").arg(lineNumber);
                return false;
            }
            address = (address << 8) | byte;
            offset += 2;
        }

        const int dataBytes = byteCount - addressBytes - 1;
        QByteArray data;
        data.reserve(qMax(0, dataBytes));
        for (int i = 0; i < dataBytes; ++i) {
            quint8 byte = 0;
            if (!parseHexByte(line.mid(offset + i * 2, 2), &byte)) {
                *errorMessage = QStringLiteral("S19 第 %1 行数据无效").arg(lineNumber);
                return false;
            }
            data.append(static_cast<char>(byte));
        }

        if (!data.isEmpty()) {
            UdsUpgradeSegment segment;
            segment.startAddress = address;
            segment.data = data;
            segments->append(segment);
        }
    }

    if (!mergeSegments(segments, errorMessage)) {
        return false;
    }
    if (segments->isEmpty()) {
        *errorMessage = QStringLiteral("S19 固件中没有可下载数据");
        return false;
    }

    int byteCount = 0;
    for (const auto &segment : std::as_const(*segments)) {
        byteCount += segment.data.size();
    }
    *summary = QStringLiteral("%1, S19, %2 段, %3 字节")
                   .arg(QFileInfo(path).fileName())
                   .arg(segments->size())
                   .arg(byteCount);
    return true;
}

bool UdsUpgradeManager::mergeSegments(QVector<UdsUpgradeSegment> *segments, QString *errorMessage) const
{
    if (segments == nullptr || errorMessage == nullptr) {
        return false;
    }

    std::sort(segments->begin(), segments->end(), [](const UdsUpgradeSegment &left, const UdsUpgradeSegment &right) {
        return left.startAddress < right.startAddress;
    });

    QVector<UdsUpgradeSegment> merged;
    for (const auto &segment : std::as_const(*segments)) {
        if (segment.data.isEmpty()) {
            continue;
        }

        if (merged.isEmpty()) {
            merged.append(segment);
            continue;
        }

        UdsUpgradeSegment &last = merged.last();
        const quint32 lastEnd = last.startAddress + static_cast<quint32>(last.data.size());
        if (segment.startAddress < lastEnd) {
            *errorMessage = QStringLiteral("固件记录存在地址重叠，当前版本暂不支持");
            return false;
        }
        if (segment.startAddress == lastEnd) {
            last.data.append(segment.data);
        } else {
            merged.append(segment);
        }
    }

    *segments = merged;
    return true;
}

void UdsUpgradeManager::resetRuntimeState()
{
    step_ = Step::Idle;
    pendingResponse_ = PendingResponse::None;
    pendingResponseAfterSend_ = PendingResponse::None;
    nextStepAfterResponse_ = Step::Idle;
    pendingDescription_.clear();
    responseDeadlineMs_ = 0;
    responseTimeoutMs_ = 0;
    segmentIndex_ = 0;
    segmentOffset_ = 0;
    transferredBytes_ = 0;
    currentBlockSize_ = 0;
    currentBlockSequence_ = 0;
    ecuMaxTransferLength_ = 0;
    sender_ = {};
    receiver_ = {};
}

void UdsUpgradeManager::startSequence()
{
    step_ = Step::EnterProgrammingSession;
    appendLog(QStringLiteral("步骤 1/6：请求进入编程会话 0x%1")
                  .arg(QStringLiteral("%1").arg(config_.programSessionSubFunction, 2, 16, QLatin1Char('0')).toUpper()));
    QByteArray payload;
    payload.append(static_cast<char>(0x10));
    payload.append(static_cast<char>(config_.programSessionSubFunction));
    beginRequest(payload, PendingResponse::SessionControl, Step::RequestSeed,
                 QStringLiteral("进入编程会话"), kExtendedResponseTimeoutMs);
}

void UdsUpgradeManager::startPolling()
{
    pollTimer_->start();
    testerPresentTimer_->start();
}

void UdsUpgradeManager::stopPolling()
{
    pollTimer_->stop();
    testerPresentTimer_->stop();
}

void UdsUpgradeManager::pollBus()
{
    if (!running_) {
        return;
    }

    QVector<CanRxFrame> receivedFrames;
    if (!controller_->receiveFrames(config_.channelIndex, 32, &receivedFrames) || receivedFrames.isEmpty()) {
        return;
    }

    for (const auto &frame : std::as_const(receivedFrames)) {
        handleIncomingFrame(frame);
        if (!running_) {
            return;
        }
    }
}

void UdsUpgradeManager::pollSender()
{
    if (!running_ || sender_.state != SenderState::SendingConsecutiveFrames) {
        return;
    }
    if (nowMs() < sender_.nextConsecutiveFrameAtMs) {
        return;
    }

    if (sender_.offset >= sender_.payload.size()) {
        sender_.state = SenderState::Idle;
        startWaitingForResponse();
        return;
    }

    QByteArray canPayload;
    canPayload.reserve(8);
    canPayload.append(static_cast<char>(0x20 | (sender_.sequenceNumber & 0x0F)));
    const int copyLength = qMin(7, sender_.payload.size() - sender_.offset);
    canPayload.append(sender_.payload.mid(sender_.offset, copyLength));
    if (!sendRawCanFrame(config_.physicalId, canPayload)) {
        failUpgrade(QStringLiteral("发送连续帧失败"));
        return;
    }

    sender_.offset += copyLength;
    sender_.sequenceNumber = static_cast<quint8>((sender_.sequenceNumber + 1) & 0x0F);
    if (sender_.sequenceNumber == 0) {
        sender_.sequenceNumber = 0x00;
    }
    sender_.sentInBlock += 1;
    sender_.nextConsecutiveFrameAtMs = nowMs() + sender_.stMinMs;

    if (sender_.offset >= sender_.payload.size()) {
        sender_.state = SenderState::Idle;
        startWaitingForResponse();
        return;
    }

    if (sender_.blockSize > 0 && sender_.sentInBlock >= sender_.blockSize) {
        sender_.state = SenderState::WaitingFlowControl;
        sender_.sentInBlock = 0;
        sender_.deadlineMs = nowMs() + kDefaultResponseTimeoutMs;
    }
}

void UdsUpgradeManager::maybeSendTesterPresent()
{
    if (!running_ || sender_.state != SenderState::Idle || pendingResponse_ != PendingResponse::None) {
        return;
    }

    QByteArray payload;
    payload.append(static_cast<char>(0x3E));
    payload.append(static_cast<char>(0x80));
    sendRawCanFrame(config_.functionalId, payload);
}

void UdsUpgradeManager::handleIncomingFrame(const CanRxFrame &frame)
{
    if (frame.remoteFrame || frame.frameId != config_.responseId || frame.data.isEmpty()) {
        return;
    }

    const quint8 pciType = (static_cast<quint8>(frame.data.at(0)) >> 4) & 0x0F;
    if (sender_.state == SenderState::WaitingFlowControl && pciType == 0x03) {
        handleFlowControlFrame(frame.data);
        return;
    }

    if (pciType == 0x00) {
        const int length = qMin<int>(static_cast<quint8>(frame.data.at(0)) & 0x0F, frame.data.size() - 1);
        handleIsoTpPayload(frame.data.mid(1, length));
        return;
    }

    if (pciType == 0x01) {
        const int expectedLength = ((static_cast<quint8>(frame.data.at(0)) & 0x0F) << 8)
            | static_cast<quint8>(frame.data.at(1));
        receiver_.active = true;
        receiver_.expectedLength = expectedLength;
        receiver_.payload = frame.data.mid(2);
        receiver_.nextSequenceNumber = 1;

        QByteArray flowControl;
        flowControl.append(static_cast<char>(0x30));
        flowControl.append(static_cast<char>(0x00));
        flowControl.append(static_cast<char>(0x00));
        sendRawCanFrame(config_.physicalId, flowControl);

        if (receiver_.payload.size() >= receiver_.expectedLength) {
            const QByteArray payload = receiver_.payload.left(receiver_.expectedLength);
            receiver_ = {};
            handleIsoTpPayload(payload);
        }
        return;
    }

    if (pciType == 0x02 && receiver_.active) {
        const quint8 sequenceNumber = static_cast<quint8>(frame.data.at(0)) & 0x0F;
        if (sequenceNumber != receiver_.nextSequenceNumber) {
            failUpgrade(QStringLiteral("接收多帧序号错误"));
            return;
        }

        receiver_.nextSequenceNumber = static_cast<quint8>((receiver_.nextSequenceNumber + 1) & 0x0F);
        receiver_.payload.append(frame.data.mid(1));
        if (receiver_.payload.size() >= receiver_.expectedLength) {
            const QByteArray payload = receiver_.payload.left(receiver_.expectedLength);
            receiver_ = {};
            handleIsoTpPayload(payload);
        }
    }
}

void UdsUpgradeManager::handleFlowControlFrame(const QByteArray &frameData)
{
    if (frameData.size() < 3) {
        failUpgrade(QStringLiteral("收到的流控帧无效"));
        return;
    }

    const quint8 flowStatus = static_cast<quint8>(frameData.at(0)) & 0x0F;
    if (flowStatus == 0x01) {
        sender_.deadlineMs = nowMs() + kDefaultResponseTimeoutMs;
        return;
    }
    if (flowStatus != 0x00) {
        failUpgrade(QStringLiteral("ECU 拒绝继续接收多帧数据"));
        return;
    }

    sender_.blockSize = static_cast<quint8>(frameData.at(1));
    const quint8 stMinValue = static_cast<quint8>(frameData.at(2));
    if (stMinValue <= 0x7F) {
        sender_.stMinMs = stMinValue;
    } else {
        sender_.stMinMs = 0;
    }
    sender_.sentInBlock = 0;
    sender_.state = SenderState::SendingConsecutiveFrames;
    sender_.nextConsecutiveFrameAtMs = nowMs();
    sender_.deadlineMs = 0;
}

void UdsUpgradeManager::handleIsoTpPayload(const QByteArray &payload)
{
    if (!running_ || payload.isEmpty()) {
        return;
    }

    if (payload.at(0) == static_cast<char>(0x7F)) {
        if (payload.size() < 3) {
            failUpgrade(QStringLiteral("收到格式错误的否定响应"));
            return;
        }

        const quint8 originalService = static_cast<quint8>(payload.at(1));
        const quint8 negativeCode = static_cast<quint8>(payload.at(2));
        if (negativeCode == 0x78) {
            appendLog(QStringLiteral("ECU 忙，继续等待服务 0x%1 的最终响应")
                          .arg(QStringLiteral("%1").arg(originalService, 2, 16, QLatin1Char('0')).toUpper()));
            extendResponseTimeout(kExtendedResponseTimeoutMs);
            return;
        }

        failUpgrade(QStringLiteral("服务 0x%1 被拒绝：0x%2 %3")
                        .arg(QStringLiteral("%1").arg(originalService, 2, 16, QLatin1Char('0')).toUpper())
                        .arg(QStringLiteral("%1").arg(negativeCode, 2, 16, QLatin1Char('0')).toUpper())
                        .arg(negativeResponseText(negativeCode)));
        return;
    }

    handlePositiveResponse(pendingResponse_, payload);
}

bool UdsUpgradeManager::beginRequest(const QByteArray &payload,
                                     PendingResponse responseKind,
                                     Step nextStep,
                                     const QString &description,
                                     int timeoutMs)
{
    pendingResponseAfterSend_ = responseKind;
    nextStepAfterResponse_ = nextStep;
    pendingDescription_ = description;
    responseTimeoutMs_ = timeoutMs;

    if (!description.isEmpty()) {
        appendLog(QStringLiteral("发送 %1").arg(description));
    }
    return sendIsoTpPayload(payload);
}

bool UdsUpgradeManager::sendIsoTpPayload(const QByteArray &payload)
{
    sender_ = {};
    receiver_ = {};
    pendingResponse_ = PendingResponse::None;
    responseDeadlineMs_ = 0;

    if (payload.size() <= 7) {
        QByteArray canPayload;
        canPayload.append(static_cast<char>(payload.size() & 0x0F));
        canPayload.append(payload);
        if (!sendRawCanFrame(config_.physicalId, canPayload)) {
            failUpgrade(QStringLiteral("发送请求失败"));
            return false;
        }
        startWaitingForResponse();
        return true;
    }

    QByteArray canPayload;
    canPayload.reserve(8);
    canPayload.append(static_cast<char>(0x10 | ((payload.size() >> 8) & 0x0F)));
    canPayload.append(static_cast<char>(payload.size() & 0xFF));
    canPayload.append(payload.left(6));
    if (!sendRawCanFrame(config_.physicalId, canPayload)) {
        failUpgrade(QStringLiteral("发送首帧失败"));
        return false;
    }

    sender_.payload = payload;
    sender_.offset = 6;
    sender_.state = SenderState::WaitingFlowControl;
    sender_.sequenceNumber = 1;
    sender_.deadlineMs = nowMs() + kDefaultResponseTimeoutMs;
    return true;
}

bool UdsUpgradeManager::sendRawCanFrame(quint32 frameId, const QByteArray &payload)
{
    QByteArray canData = payload;
    if (canData.size() < 8) {
        canData.append(QByteArray(8 - canData.size(), kPaddingByte));
    } else if (canData.size() > 8) {
        canData = canData.left(8);
    }

    const bool extendedFrame = frameId > 0x7FFU;
    return controller_->transmitFrame(config_.channelIndex, frameIdText(frameId), extendedFrame, false, canData);
}

void UdsUpgradeManager::startWaitingForResponse()
{
    if (pendingResponseAfterSend_ == PendingResponse::None) {
        return;
    }

    pendingResponse_ = pendingResponseAfterSend_;
    pendingResponseAfterSend_ = PendingResponse::None;
    responseDeadlineMs_ = nowMs() + responseTimeoutMs_;
}

void UdsUpgradeManager::requestSecuritySeed()
{
    step_ = Step::RequestSeed;
    appendLog(QStringLiteral("步骤 2/6：请求安全访问种子 0x%1")
                  .arg(QStringLiteral("%1").arg(config_.seedRequestSubFunction, 2, 16, QLatin1Char('0')).toUpper()));
    QByteArray payload;
    payload.append(static_cast<char>(0x27));
    payload.append(static_cast<char>(config_.seedRequestSubFunction));
    beginRequest(payload, PendingResponse::SecuritySeed, Step::SendKey, QStringLiteral("27 服务请求种子"),
                 kDefaultResponseTimeoutMs);
}

void UdsUpgradeManager::sendSecurityKey(const QByteArray &seedBytes)
{
    step_ = Step::SendKey;
    const QByteArray keyBytes = buildSecurityKey(seedBytes);
    appendLog(QStringLiteral("步骤 3/6：发送安全访问密钥"));
    appendLog(QStringLiteral("27 算子：0x%1")
                  .arg(QStringLiteral("%1").arg(config_.seedKeyOperator, 8, 16, QLatin1Char('0')).toUpper()));

    QByteArray payload;
    payload.append(static_cast<char>(0x27));
    payload.append(static_cast<char>(config_.keySendSubFunction));
    payload.append(keyBytes);
    beginRequest(payload, PendingResponse::SecurityKey, Step::RequestDownload, QStringLiteral("27 服务发送密钥"),
                 kDefaultResponseTimeoutMs);
}

void UdsUpgradeManager::requestCurrentSegmentDownload()
{
    if (segmentIndex_ >= segments_.size()) {
        sendFinalReset();
        return;
    }

    step_ = Step::RequestDownload;
    segmentOffset_ = 0;
    currentBlockSequence_ = 0;
    ecuMaxTransferLength_ = 0;

    const auto &segment = segments_.at(segmentIndex_);
    appendLog(QStringLiteral("步骤 4/6：请求下载，第 %1/%2 段，地址 0x%3，长度 %4 字节")
                  .arg(segmentIndex_ + 1)
                  .arg(segments_.size())
                  .arg(QStringLiteral("%1").arg(segment.startAddress, 8, 16, QLatin1Char('0')).toUpper())
                  .arg(segment.data.size()));

    QByteArray payload;
    payload.append(static_cast<char>(0x34));
    payload.append(static_cast<char>(0x00));
    payload.append(static_cast<char>(0x44));
    payload.append(static_cast<char>((segment.startAddress >> 24) & 0xFF));
    payload.append(static_cast<char>((segment.startAddress >> 16) & 0xFF));
    payload.append(static_cast<char>((segment.startAddress >> 8) & 0xFF));
    payload.append(static_cast<char>(segment.startAddress & 0xFF));
    const quint32 length = static_cast<quint32>(segment.data.size());
    payload.append(static_cast<char>((length >> 24) & 0xFF));
    payload.append(static_cast<char>((length >> 16) & 0xFF));
    payload.append(static_cast<char>((length >> 8) & 0xFF));
    payload.append(static_cast<char>(length & 0xFF));
    beginRequest(payload, PendingResponse::RequestDownload, Step::TransferData, QStringLiteral("34 请求下载"),
                 kDefaultResponseTimeoutMs);
}

void UdsUpgradeManager::sendNextTransferBlock()
{
    if (segmentIndex_ >= segments_.size()) {
        requestTransferExit();
        return;
    }

    const auto &segment = segments_.at(segmentIndex_);
    if (segmentOffset_ >= segment.data.size()) {
        requestTransferExit();
        return;
    }

    step_ = Step::TransferData;
    const int maxTransferLength = ecuMaxTransferLength_ > 2 ? ecuMaxTransferLength_ : 0x0100;
    const int blockPayloadLength = qMax(1, maxTransferLength - 2);
    currentBlockSize_ = qMin(blockPayloadLength, segment.data.size() - segmentOffset_);
    currentBlockSequence_ = static_cast<quint8>((currentBlockSequence_ + 1) & 0xFF);
    if (currentBlockSequence_ == 0) {
        currentBlockSequence_ = 1;
    }

    QByteArray payload;
    payload.append(static_cast<char>(0x36));
    payload.append(static_cast<char>(currentBlockSequence_));
    payload.append(segment.data.mid(segmentOffset_, currentBlockSize_));
    beginRequest(payload, PendingResponse::TransferData, Step::TransferData,
                 QStringLiteral("36 传输数据块 #%1").arg(currentBlockSequence_), kDefaultResponseTimeoutMs);
}

void UdsUpgradeManager::requestTransferExit()
{
    step_ = Step::RequestTransferExit;
    appendLog(QStringLiteral("步骤 5/6：请求退出传输"));
    QByteArray payload;
    payload.append(static_cast<char>(0x37));
    beginRequest(payload, PendingResponse::TransferExit, Step::SendReset, QStringLiteral("37 请求退出传输"),
                 kTransferExitTimeoutMs);
}

void UdsUpgradeManager::sendFinalReset()
{
    step_ = Step::SendReset;
    appendLog(QStringLiteral("步骤 6/6：发送 ECU 复位 0x%1")
                  .arg(QStringLiteral("%1").arg(config_.hardResetSubFunction, 2, 16, QLatin1Char('0')).toUpper()));
    QByteArray payload;
    payload.append(static_cast<char>(0x11));
    payload.append(static_cast<char>(config_.hardResetSubFunction));
    beginRequest(payload, PendingResponse::EcuReset, Step::Idle, QStringLiteral("11 ECU 复位"), kResetTimeoutMs);
}

void UdsUpgradeManager::handlePositiveResponse(PendingResponse kind, const QByteArray &payload)
{
    if (kind == PendingResponse::None) {
        return;
    }

    pendingResponse_ = PendingResponse::None;
    responseDeadlineMs_ = 0;

    switch (kind) {
    case PendingResponse::SessionControl:
        if (payload.size() < 2 || payload.at(0) != static_cast<char>(0x50)
            || static_cast<quint8>(payload.at(1)) != config_.programSessionSubFunction) {
            failUpgrade(QStringLiteral("编程会话响应无效"));
            return;
        }
        appendLog(QStringLiteral("已进入编程会话"));
        requestSecuritySeed();
        return;
    case PendingResponse::SecuritySeed:
        if (payload.size() < 6 || payload.at(0) != static_cast<char>(0x67)
            || static_cast<quint8>(payload.at(1)) != config_.seedRequestSubFunction) {
            failUpgrade(QStringLiteral("安全访问种子响应无效"));
            return;
        }
        appendLog(QStringLiteral("收到种子：%1").arg(bytesToHex(payload.mid(2))));
        sendSecurityKey(payload.mid(2, 4));
        return;
    case PendingResponse::SecurityKey:
        if (payload.size() < 2 || payload.at(0) != static_cast<char>(0x67)
            || static_cast<quint8>(payload.at(1)) != config_.keySendSubFunction) {
            failUpgrade(QStringLiteral("安全访问密钥响应无效"));
            return;
        }
        appendLog(QStringLiteral("安全访问已解锁"));
        requestCurrentSegmentDownload();
        return;
    case PendingResponse::RequestDownload:
        if (payload.size() < 2 || payload.at(0) != static_cast<char>(0x74)) {
            failUpgrade(QStringLiteral("请求下载响应无效"));
            return;
        }
        if (payload.size() >= 4) {
            ecuMaxTransferLength_ = static_cast<quint16>((static_cast<quint8>(payload.at(2)) << 8)
                                                         | static_cast<quint8>(payload.at(3)));
            appendLog(QStringLiteral("ECU 单次接收上限：%1 字节").arg(ecuMaxTransferLength_));
        } else {
            ecuMaxTransferLength_ = 0x0100;
            appendLog(QStringLiteral("ECU 未返回单次接收上限，使用默认值 256 字节"));
        }
        sendNextTransferBlock();
        return;
    case PendingResponse::TransferData:
        if (payload.size() < 2 || payload.at(0) != static_cast<char>(0x76)
            || static_cast<quint8>(payload.at(1)) != currentBlockSequence_) {
            failUpgrade(QStringLiteral("传输数据块响应无效"));
            return;
        }
        segmentOffset_ += currentBlockSize_;
        transferredBytes_ += currentBlockSize_;
        updateProgress();
        appendLog(QStringLiteral("数据块 #%1 已确认，累计 %2 / %3 字节")
                      .arg(currentBlockSequence_)
                      .arg(transferredBytes_)
                      .arg(totalBytes_));
        if (segmentOffset_ < segments_.at(segmentIndex_).data.size()) {
            sendNextTransferBlock();
        } else {
            appendLog(QStringLiteral("第 %1 段传输完成").arg(segmentIndex_ + 1));
            requestTransferExit();
        }
        return;
    case PendingResponse::TransferExit:
        if (payload.isEmpty() || payload.at(0) != static_cast<char>(0x77)) {
            failUpgrade(QStringLiteral("退出传输响应无效"));
            return;
        }
        appendLog(QStringLiteral("第 %1 段退出传输成功").arg(segmentIndex_ + 1));
        segmentIndex_ += 1;
        if (segmentIndex_ < segments_.size()) {
            requestCurrentSegmentDownload();
        } else {
            sendFinalReset();
        }
        return;
    case PendingResponse::EcuReset:
        if (payload.size() < 2 || payload.at(0) != static_cast<char>(0x51)
            || static_cast<quint8>(payload.at(1)) != config_.hardResetSubFunction) {
            failUpgrade(QStringLiteral("ECU 复位响应无效"));
            return;
        }
        completeSuccess(QStringLiteral("固件升级完成，ECU 已接受复位请求"));
        return;
    case PendingResponse::None:
        return;
    }
}

void UdsUpgradeManager::extendResponseTimeout(int timeoutMs)
{
    responseDeadlineMs_ = nowMs() + timeoutMs;
}

void UdsUpgradeManager::completeSuccess(const QString &summary)
{
    appendLog(summary);
    Q_EMIT progressChanged(100);
    setStatus(QStringLiteral("已完成"), QStringLiteral("ok"));
    stopPolling();
    resetRuntimeState();
    running_ = false;
    Q_EMIT runningChanged(false);
    Q_EMIT finished(true, summary);
}

void UdsUpgradeManager::failUpgrade(const QString &reason)
{
    appendLog(reason);
    setStatus(QStringLiteral("失败"), QStringLiteral("error"));
    stopPolling();
    resetRuntimeState();
    running_ = false;
    Q_EMIT runningChanged(false);
    Q_EMIT finished(false, reason);
}

void UdsUpgradeManager::setStatus(const QString &text, const QString &state)
{
    Q_EMIT statusChanged(text, state);
}

void UdsUpgradeManager::appendLog(const QString &message)
{
    Q_EMIT logMessage(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), message));
}

void UdsUpgradeManager::updateProgress()
{
    if (totalBytes_ <= 0) {
        Q_EMIT progressChanged(0);
        return;
    }
    const int progress = qBound(0, static_cast<int>((static_cast<double>(transferredBytes_) * 100.0) / totalBytes_), 100);
    Q_EMIT progressChanged(progress);
}

QByteArray UdsUpgradeManager::buildSecurityKey(const QByteArray &seedBytes) const
{
    quint32 seed = 0;
    QByteArray paddedSeed = seedBytes.left(4);
    while (paddedSeed.size() < 4) {
        paddedSeed.append(char(0x00));
    }
    for (const char byte : paddedSeed) {
        seed = (seed << 8) | static_cast<quint8>(byte);
    }

    quint32 key = seed;
    for (int i = 0; i < 32; ++i) {
        if (key & 0x00000001U) {
            key = (key >> 1) ^ config_.seedKeyOperator;
        } else {
            key >>= 1;
        }
    }

    QByteArray result;
    result.append(static_cast<char>((key >> 24) & 0xFF));
    result.append(static_cast<char>((key >> 16) & 0xFF));
    result.append(static_cast<char>((key >> 8) & 0xFF));
    result.append(static_cast<char>(key & 0xFF));
    return result;
}

QString UdsUpgradeManager::frameIdText(quint32 frameId)
{
    return QStringLiteral("%1")
        .arg(frameId, frameId > 0x7FFU ? 8 : 3, 16, QLatin1Char('0'))
        .toUpper();
}

QString UdsUpgradeManager::bytesToHex(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (const char byte : data) {
        bytes.append(QStringLiteral("%1").arg(static_cast<quint8>(byte), 2, 16, QLatin1Char('0')).toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

QString UdsUpgradeManager::negativeResponseText(quint8 nrc)
{
    switch (nrc) {
    case 0x10:
        return QStringLiteral("General Reject");
    case 0x11:
        return QStringLiteral("Service Not Supported");
    case 0x12:
        return QStringLiteral("Sub-function Not Supported");
    case 0x13:
        return QStringLiteral("Incorrect Message Length");
    case 0x22:
        return QStringLiteral("Conditions Not Correct");
    case 0x24:
        return QStringLiteral("Request Sequence Error");
    case 0x31:
        return QStringLiteral("Request Out Of Range");
    case 0x33:
        return QStringLiteral("Security Access Denied");
    case 0x35:
        return QStringLiteral("Invalid Key");
    case 0x36:
        return QStringLiteral("Exceeded Number Of Attempts");
    case 0x37:
        return QStringLiteral("Required Time Delay Not Expired");
    case 0x70:
        return QStringLiteral("Upload Download Not Accepted");
    case 0x72:
        return QStringLiteral("General Programming Failure");
    case 0x73:
        return QStringLiteral("Wrong Block Sequence Counter");
    case 0x78:
        return QStringLiteral("Response Pending");
    default:
        return QStringLiteral("Unknown NRC");
    }
}
