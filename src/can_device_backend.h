#pragma once

#include <QLibrary>
#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

struct CanRxFrame
{
    unsigned int frameId = 0;
    bool extendedFrame = false;
    bool remoteFrame = false;
    QByteArray data;
    quint64 timestampUs = 0;
    qint64 receivedAtMs = 0;
};

class CanDeviceBackend final
{
public:
    CanDeviceBackend() = default;
    ~CanDeviceBackend();

    CanDeviceBackend(const CanDeviceBackend &) = delete;
    CanDeviceBackend &operator=(const CanDeviceBackend &) = delete;
    CanDeviceBackend(CanDeviceBackend &&) = delete;
    CanDeviceBackend &operator=(CanDeviceBackend &&) = delete;

    bool open(const QString &deviceType, int deviceIndex, QString *errorMessage);
    bool startChannel(int channelIndex, const QString &bitrate, const QString &workMode, QString *errorMessage);
    bool transmitFrame(int channelIndex, unsigned int frameId, bool extendedFrame, bool remoteFrame, const QByteArray &data,
                       QString *errorMessage);
    bool receiveFrames(int channelIndex, int maxFrames, QVector<CanRxFrame> *frames, QString *errorMessage);
    void close();
    bool isOpen() const;

private:
    using DeviceHandle = void *;
    using ChannelHandle = void *;

    struct DeviceProfile
    {
        unsigned int deviceType = 0;
        int channelCount = 0;
        bool canFd = false;
    };

    struct ZcanDeviceInfo
    {
        unsigned short hwVersion = 0;
        unsigned short fwVersion = 0;
        unsigned short drVersion = 0;
        unsigned short inVersion = 0;
        unsigned short irqNum = 0;
        unsigned char canNum = 0;
        unsigned char serialNum[20] = {};
        unsigned char hardwareType[40] = {};
        unsigned short reserved[4] = {};
    };

    struct ZcanClassicConfig
    {
        unsigned int accCode;
        unsigned int accMask;
        unsigned int reserved;
        unsigned char filter;
        unsigned char timing0;
        unsigned char timing1;
        unsigned char mode;
    };

    struct ZcanCanFdConfig
    {
        unsigned int accCode;
        unsigned int accMask;
        unsigned int abitTiming;
        unsigned int dbitTiming;
        unsigned int brp;
        unsigned char filter;
        unsigned char mode;
        unsigned short pad;
        unsigned int reserved;
    };

    union ZcanChannelConfig
    {
        ZcanClassicConfig can;
        ZcanCanFdConfig canfd;
    };

    struct ZcanChannelInitConfig
    {
        unsigned int canType;
        ZcanChannelConfig config;
    };

    struct ZcanCanFrame
    {
        unsigned int canId;
        unsigned char canDlc;
        unsigned char pad;
        unsigned char reserved0;
        unsigned char reserved1;
        unsigned char data[8];
    };

    struct ZcanTransmitData
    {
        ZcanCanFrame frame;
        unsigned int transmitType;
    };

    struct ZcanReceiveData
    {
        ZcanCanFrame frame;
        quint64 timestamp;
    };

    struct ZcanReceiveBuffer
    {
        ZcanReceiveData data;
        unsigned char sdkPadding[64];
    };

    using SetValueFunc = int (*)(const char *path, const char *value);
    using GetValueFunc = const char *(*)(const char *path);
    using GetPropertysFunc = const void *(*)();

    struct IProperty
    {
        SetValueFunc setValue = nullptr;
        GetValueFunc getValue = nullptr;
        GetPropertysFunc getPropertys = nullptr;
    };

    using ZcanOpenDevice = DeviceHandle(__stdcall *)(unsigned int deviceType, unsigned int deviceIndex, unsigned int reserved);
    using ZcanCloseDevice = unsigned int(__stdcall *)(DeviceHandle deviceHandle);
    using ZcanGetDeviceInfo = unsigned int(__stdcall *)(DeviceHandle deviceHandle, ZcanDeviceInfo *deviceInfo);
    using ZcanInitCan = ChannelHandle(__stdcall *)(DeviceHandle deviceHandle, unsigned int canIndex, ZcanChannelInitConfig *initConfig);
    using ZcanStartCan = unsigned int(__stdcall *)(ChannelHandle channelHandle);
    using ZcanResetCan = unsigned int(__stdcall *)(ChannelHandle channelHandle);
    using ZcanTransmit = unsigned int(__stdcall *)(ChannelHandle channelHandle, ZcanTransmitData *transmitData, unsigned int length);
    using ZcanReceive = unsigned int(__stdcall *)(ChannelHandle channelHandle, ZcanReceiveData *receiveData, unsigned int length,
                                                 int waitTime);
    using GetIProperty = IProperty *(__stdcall *)(DeviceHandle deviceHandle);
    using ReleaseIProperty = unsigned int(__stdcall *)(IProperty *property);

    bool ensureLoaded(QString *errorMessage);
    DeviceProfile resolveDeviceProfile(const QString &deviceType, bool *ok) const;
    bool fillClassicConfig(const QString &bitrate, const QString &workMode, ZcanChannelInitConfig *config, QString *errorMessage) const;
    bool fillCanFdConfig(const QString &bitrate, const QString &workMode, ZcanChannelInitConfig *config, QString *errorMessage) const;
    bool setPropertyValue(int channelIndex, const QByteArray &name, const QByteArray &value, QString *errorMessage);
    void resetChannel(int channelIndex);

    DeviceHandle invokeOpenDevice(unsigned int deviceType, unsigned int deviceIndex, bool *accessViolation) const;
    unsigned int invokeCloseDevice(DeviceHandle deviceHandle, bool *accessViolation) const;
    unsigned int invokeGetDeviceInfo(DeviceHandle deviceHandle, ZcanDeviceInfo *deviceInfo, bool *accessViolation) const;
    ChannelHandle invokeInitCan(unsigned int channelIndex, ZcanChannelInitConfig *config, bool *accessViolation) const;
    unsigned int invokeStartCan(ChannelHandle channelHandle, bool *accessViolation) const;
    unsigned int invokeResetCan(ChannelHandle channelHandle, bool *accessViolation) const;
    unsigned int invokeTransmit(ChannelHandle channelHandle, ZcanTransmitData *transmitData, unsigned int length,
                                bool *accessViolation) const;
    unsigned int invokeReceive(ChannelHandle channelHandle, ZcanReceiveData *receiveData, unsigned int length, int waitTime,
                               bool *accessViolation) const;
    IProperty *invokeGetIProperty(bool *accessViolation) const;
    unsigned int invokeReleaseIProperty(IProperty *property, bool *accessViolation) const;
    int invokeSetProperty(IProperty *property, const char *path, const char *value, bool *accessViolation) const;

    QLibrary library_;
    ZcanOpenDevice openDevice_ = nullptr;
    ZcanCloseDevice closeDevice_ = nullptr;
    ZcanGetDeviceInfo getDeviceInfo_ = nullptr;
    ZcanInitCan initCan_ = nullptr;
    ZcanStartCan startCan_ = nullptr;
    ZcanResetCan resetCan_ = nullptr;
    ZcanTransmit transmit_ = nullptr;
    ZcanReceive receive_ = nullptr;
    GetIProperty getIProperty_ = nullptr;
    ReleaseIProperty releaseIProperty_ = nullptr;
    DeviceHandle handle_ = nullptr;
    DeviceProfile profile_;
    int channelCount_ = 0;
    std::array<ChannelHandle, 2> channelHandles_ = {nullptr, nullptr};
};
