#pragma once

#include "device_controller.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

struct UdsUpgradeSegment
{
    quint32 startAddress = 0;
    QByteArray data;
};

struct UdsUpgradeConfig
{
    int channelIndex = 0;
    quint32 functionalId = 0x0CD4FDFD;
    quint32 physicalId = 0x0CD617FD;
    quint32 responseId = 0x0CD6FD17;
    quint32 seedKeyOperator = 0xEDB88317;
    quint32 downloadAddress = 0x00000000;
    quint8 programSessionSubFunction = 0x02;
    quint8 seedRequestSubFunction = 0x01;
    quint8 keySendSubFunction = 0x02;
    quint8 hardResetSubFunction = 0x01;
    QString firmwarePath;
};

class QTimer;

class UdsUpgradeManager final : public QObject
{
    Q_OBJECT

public:
    explicit UdsUpgradeManager(DeviceController *controller, QObject *parent = nullptr);

    bool isRunning() const;
    bool startUpgrade(const UdsUpgradeConfig &config);

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    void logMessage(const QString &message);
    void progressChanged(int progress);
    void statusChanged(const QString &text, const QString &state);
    void finished(bool success, const QString &summary);
    void runningChanged(bool running);

private:
    enum class Step
    {
        Idle,
        EnterProgrammingSession,
        RequestSeed,
        SendKey,
        RequestDownload,
        TransferData,
        RequestTransferExit,
        SendReset
    };

    enum class PendingResponse
    {
        None,
        SessionControl,
        SecuritySeed,
        SecurityKey,
        RequestDownload,
        TransferData,
        TransferExit,
        EcuReset
    };

    enum class SenderState
    {
        Idle,
        WaitingFlowControl,
        SendingConsecutiveFrames
    };

    struct IsoTpSender
    {
        SenderState state = SenderState::Idle;
        QByteArray payload;
        int offset = 0;
        int blockSize = 0;
        int sentInBlock = 0;
        int stMinMs = 0;
        quint8 sequenceNumber = 1;
        qint64 deadlineMs = 0;
        qint64 nextConsecutiveFrameAtMs = 0;
    };

    struct IsoTpReceiver
    {
        bool active = false;
        QByteArray payload;
        int expectedLength = 0;
        quint8 nextSequenceNumber = 1;
    };

    bool loadFirmwareImage(const UdsUpgradeConfig &config, QVector<UdsUpgradeSegment> *segments, QString *summary,
                           QString *errorMessage) const;
    bool loadBinaryFirmware(const UdsUpgradeConfig &config, QVector<UdsUpgradeSegment> *segments, QString *summary,
                            QString *errorMessage) const;
    bool loadIntelHexFirmware(const QString &path, QVector<UdsUpgradeSegment> *segments, QString *summary,
                              QString *errorMessage) const;
    bool loadMotorolaFirmware(const QString &path, QVector<UdsUpgradeSegment> *segments, QString *summary,
                              QString *errorMessage) const;
    bool mergeSegments(QVector<UdsUpgradeSegment> *segments, QString *errorMessage) const;

    void resetRuntimeState();
    void startSequence();
    void startPolling();
    void stopPolling();
    void pollBus();
    void pollSender();
    void maybeSendTesterPresent();
    void handleIncomingFrame(const CanRxFrame &frame);
    void handleFlowControlFrame(const QByteArray &frameData);
    void handleIsoTpPayload(const QByteArray &payload);

    bool beginRequest(const QByteArray &payload, PendingResponse responseKind, Step nextStep, const QString &description,
                      int timeoutMs);
    bool sendIsoTpPayload(const QByteArray &payload);
    bool sendRawCanFrame(quint32 frameId, const QByteArray &payload);
    void startWaitingForResponse();

    void requestSecuritySeed();
    void sendSecurityKey(const QByteArray &seedBytes);
    void requestCurrentSegmentDownload();
    void sendNextTransferBlock();
    void requestTransferExit();
    void sendFinalReset();

    void handlePositiveResponse(PendingResponse kind, const QByteArray &payload);
    void extendResponseTimeout(int timeoutMs);
    void completeSuccess(const QString &summary);
    void failUpgrade(const QString &reason);
    void setStatus(const QString &text, const QString &state);
    void appendLog(const QString &message);
    void updateProgress();
    QByteArray buildSecurityKey(const QByteArray &seedBytes) const;

    static QString frameIdText(quint32 frameId);
    static QString bytesToHex(const QByteArray &data);
    static QString negativeResponseText(quint8 nrc);

    DeviceController *controller_ = nullptr;
    QTimer *pollTimer_ = nullptr;
    QTimer *testerPresentTimer_ = nullptr;

    UdsUpgradeConfig config_;
    QVector<UdsUpgradeSegment> segments_;
    QString firmwareSummary_;
    Step step_ = Step::Idle;
    PendingResponse pendingResponse_ = PendingResponse::None;
    PendingResponse pendingResponseAfterSend_ = PendingResponse::None;
    Step nextStepAfterResponse_ = Step::Idle;
    QString pendingDescription_;
    qint64 responseDeadlineMs_ = 0;
    int responseTimeoutMs_ = 0;
    int segmentIndex_ = 0;
    int segmentOffset_ = 0;
    int totalBytes_ = 0;
    int transferredBytes_ = 0;
    int currentBlockSize_ = 0;
    quint8 currentBlockSequence_ = 0;
    quint16 ecuMaxTransferLength_ = 0;
    bool running_ = false;
    IsoTpSender sender_;
    IsoTpReceiver receiver_;
};
