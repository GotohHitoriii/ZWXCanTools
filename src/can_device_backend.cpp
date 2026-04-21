#include "can_device_backend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#if defined(_MSC_VER)
#include <excpt.h>
#endif

namespace
{
constexpr unsigned int kStatusOk = 1;
constexpr unsigned int kTypeCan = 0;
constexpr unsigned int kTypeCanFd = 1;
constexpr unsigned int kCanEffFlag = 0x80000000U;
constexpr unsigned int kCanRtrFlag = 0x40000000U;
constexpr unsigned int kCanEffMask = 0x1FFFFFFFU;
constexpr unsigned int kCanSffMask = 0x000007FFU;

void setAccessViolation(bool *accessViolation, bool value)
{
    if (accessViolation != nullptr) {
        *accessViolation = value;
    }
}
} // namespace

CanDeviceBackend::~CanDeviceBackend()
{
    close();
}

bool CanDeviceBackend::open(const QString &deviceType, int deviceIndex, QString *errorMessage)
{
    if (handle_ != nullptr) {
        return true;
    }

    bool deviceTypeOk = false;
    const DeviceProfile profile = resolveDeviceProfile(deviceType, &deviceTypeOk);
    if (!deviceTypeOk) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不支持的设备类型：%1").arg(deviceType);
        }
        return false;
    }

    if (deviceIndex < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("设备索引不能为负数");
        }
        return false;
    }

    if (deviceIndex > 3) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("设备索引范围暂限制为 0-3");
        }
        return false;
    }

    if (!ensureLoaded(errorMessage)) {
        return false;
    }

    bool accessViolation = false;
    handle_ = invokeOpenDevice(profile.deviceType, static_cast<unsigned int>(deviceIndex), &accessViolation);
    if (accessViolation) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("打开设备时触发了 SDK 访问异常，请检查驱动、位数和硬件连接");
        }
        handle_ = nullptr;
        return false;
    }

    if (handle_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("打开设备返回空句柄，请检查设备索引、硬件连接和驱动");
        }
        return false;
    }

    ZcanDeviceInfo deviceInfo;
    const unsigned int infoResult = invokeGetDeviceInfo(handle_, &deviceInfo, &accessViolation);
    if (accessViolation || infoResult != kStatusOk || deviceInfo.canNum == 0) {
        const QString detail = accessViolation
            ? QStringLiteral("读取设备信息时触发了 SDK 访问异常")
            : QStringLiteral("读取设备信息失败或通道数为 0");
        close();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1，请确认索引 %2 上确实有设备").arg(detail).arg(deviceIndex);
        }
        return false;
    }

    profile_ = profile;
    channelCount_ = qMin(profile.channelCount, static_cast<int>(deviceInfo.canNum));
    return true;
}

bool CanDeviceBackend::startChannel(int channelIndex, const QString &bitrate, const QString &workMode, QString *errorMessage)
{
    if (handle_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("设备尚未打开");
        }
        return false;
    }

    if (channelIndex < 0 || channelIndex >= channelCount_ || channelIndex >= static_cast<int>(channelHandles_.size())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("通道 %1 不存在，当前设备通道数为 %2").arg(channelIndex).arg(channelCount_);
        }
        return false;
    }

    ZcanChannelInitConfig config = {};
    const bool configOk = profile_.canFd
        ? fillCanFdConfig(bitrate, workMode, &config, errorMessage)
        : fillClassicConfig(bitrate, workMode, &config, errorMessage);
    if (!configOk) {
        return false;
    }

    resetChannel(channelIndex);

    if (profile_.canFd) {
        setPropertyValue(channelIndex, QByteArrayLiteral("canfd_standard"), QByteArrayLiteral("0"), nullptr);
        setPropertyValue(channelIndex, QByteArrayLiteral("initenal_resistance"), QByteArrayLiteral("1"), nullptr);
    } else {
        QByteArray baudRate;
        if (bitrate == QStringLiteral("125kbps")) {
            baudRate = QByteArrayLiteral("125000");
        } else if (bitrate == QStringLiteral("250kbps")) {
            baudRate = QByteArrayLiteral("250000");
        } else if (bitrate == QStringLiteral("500kbps")) {
            baudRate = QByteArrayLiteral("500000");
        } else if (bitrate == QStringLiteral("1Mbps")) {
            baudRate = QByteArrayLiteral("1000000");
        }
        if (!baudRate.isEmpty()) {
            setPropertyValue(channelIndex, QByteArrayLiteral("baud_rate"), baudRate, nullptr);
        }
    }

    bool accessViolation = false;
    ChannelHandle channelHandle = invokeInitCan(static_cast<unsigned int>(channelIndex), &config, &accessViolation);
    if (accessViolation) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("初始化通道时触发了 SDK 访问异常");
        }
        return false;
    }
    if (channelHandle == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("初始化通道返回空通道句柄");
        }
        return false;
    }

    const unsigned int startResult = invokeStartCan(channelHandle, &accessViolation);
    if (accessViolation || startResult != kStatusOk) {
        if (accessViolation) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("启动通道时触发了 SDK 访问异常");
            }
        } else if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("启动通道返回失败");
        }
        bool resetAccessViolation = false;
        invokeResetCan(channelHandle, &resetAccessViolation);
        return false;
    }

    channelHandles_.at(static_cast<size_t>(channelIndex)) = channelHandle;
    return true;
}

bool CanDeviceBackend::transmitFrame(int channelIndex,
                                     unsigned int frameId,
                                     bool extendedFrame,
                                     bool remoteFrame,
                                     const QByteArray &data,
                                     QString *errorMessage)
{
    if (handle_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("设备尚未打开");
        }
        return false;
    }
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channelHandles_.size())
        || channelHandles_.at(static_cast<size_t>(channelIndex)) == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("通道 %1 尚未启动").arg(channelIndex);
        }
        return false;
    }
    if (transmit_ == nullptr && !ensureLoaded(errorMessage)) {
        return false;
    }
    if (transmit_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CAN驱动库缺少发送函数");
        }
        return false;
    }
    if (data.size() > 8) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("经典CAN数据长度不能超过8字节");
        }
        return false;
    }
    if (extendedFrame) {
        frameId &= kCanEffMask;
    } else {
        frameId &= kCanSffMask;
    }

    ZcanTransmitData transmitData = {};
    transmitData.frame.canId = frameId | (extendedFrame ? kCanEffFlag : 0U) | (remoteFrame ? kCanRtrFlag : 0U);
    transmitData.frame.canDlc = static_cast<unsigned char>(data.size());
    for (int i = 0; i < data.size(); ++i) {
        transmitData.frame.data[i] = static_cast<unsigned char>(data.at(i));
    }
    transmitData.transmitType = 0;

    bool accessViolation = false;
    const unsigned int transmitResult =
        invokeTransmit(channelHandles_.at(static_cast<size_t>(channelIndex)), &transmitData, 1, &accessViolation);
    if (accessViolation || transmitResult != 1) {
        if (errorMessage != nullptr) {
            *errorMessage = accessViolation ? QStringLiteral("发送帧时触发了 SDK 访问异常")
                                            : QStringLiteral("发送帧返回失败");
        }
        return false;
    }
    return true;
}

bool CanDeviceBackend::receiveFrames(int channelIndex, int maxFrames, QVector<CanRxFrame> *frames, QString *errorMessage)
{
    if (frames == nullptr) {
        return false;
    }
    frames->clear();
    if (maxFrames <= 0) {
        return true;
    }
    if (handle_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("设备尚未打开");
        }
        return false;
    }
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channelHandles_.size())
        || channelHandles_.at(static_cast<size_t>(channelIndex)) == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("通道 %1 尚未启动").arg(channelIndex);
        }
        return false;
    }
    if (receive_ == nullptr && !ensureLoaded(errorMessage)) {
        return false;
    }
    if (receive_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CAN驱动库缺少接收函数");
        }
        return false;
    }

    const int frameLimit = qMin(maxFrames, 99);
    frames->reserve(frameLimit);
    for (int i = 0; i < frameLimit; ++i) {
        ZcanReceiveBuffer rawBuffer = {};
        bool accessViolation = false;
        const unsigned int received =
            invokeReceive(channelHandles_.at(static_cast<size_t>(channelIndex)), &rawBuffer.data, 1, 0, &accessViolation);
        if (accessViolation) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("接收帧时触发了 SDK 访问异常");
            }
            return false;
        }
        if (received == 0) {
            break;
        }

        const auto &raw = rawBuffer.data;
        const unsigned int rawId = raw.frame.canId;
        CanRxFrame frame;
        frame.extendedFrame = (rawId & kCanEffFlag) != 0;
        frame.remoteFrame = (rawId & kCanRtrFlag) != 0;
        frame.frameId = rawId & (frame.extendedFrame ? kCanEffMask : kCanSffMask);
        frame.timestampUs = raw.timestamp;
        const int dataLength = qBound(0, static_cast<int>(raw.frame.canDlc), 8);
        if (!frame.remoteFrame) {
            frame.data = QByteArray(reinterpret_cast<const char *>(raw.frame.data), dataLength);
        }
        frames->append(frame);
    }
    return true;
}

void CanDeviceBackend::close()
{
    if (handle_ == nullptr || closeDevice_ == nullptr) {
        handle_ = nullptr;
        channelHandles_.fill(nullptr);
        channelCount_ = 0;
        return;
    }

    for (int i = 0; i < static_cast<int>(channelHandles_.size()); ++i) {
        resetChannel(i);
    }

    bool accessViolation = false;
    invokeCloseDevice(handle_, &accessViolation);
    handle_ = nullptr;
    channelCount_ = 0;
    profile_ = {};
}

bool CanDeviceBackend::isOpen() const
{
    return handle_ != nullptr;
}

bool CanDeviceBackend::ensureLoaded(QString *errorMessage)
{
    if (library_.isLoaded() && openDevice_ != nullptr && closeDevice_ != nullptr && getDeviceInfo_ != nullptr
        && initCan_ != nullptr && startCan_ != nullptr && resetCan_ != nullptr && transmit_ != nullptr
        && receive_ != nullptr) {
        return true;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("can_driver.dll")),
    };

    QStringList loadErrors;
    for (const QString &candidate : candidates) {
        if (!QFileInfo::exists(candidate)) {
            loadErrors.append(QStringLiteral("%1 不存在").arg(QDir::toNativeSeparators(candidate)));
            continue;
        }

        library_.setFileName(candidate);
        if (!library_.load()) {
            loadErrors.append(QStringLiteral("%1：%2").arg(QDir::toNativeSeparators(candidate), library_.errorString()));
            continue;
        }

        openDevice_ = reinterpret_cast<ZcanOpenDevice>(library_.resolve("ZCAN_OpenDevice"));
        closeDevice_ = reinterpret_cast<ZcanCloseDevice>(library_.resolve("ZCAN_CloseDevice"));
        getDeviceInfo_ = reinterpret_cast<ZcanGetDeviceInfo>(library_.resolve("ZCAN_GetDeviceInf"));
        initCan_ = reinterpret_cast<ZcanInitCan>(library_.resolve("ZCAN_InitCAN"));
        startCan_ = reinterpret_cast<ZcanStartCan>(library_.resolve("ZCAN_StartCAN"));
        resetCan_ = reinterpret_cast<ZcanResetCan>(library_.resolve("ZCAN_ResetCAN"));
        transmit_ = reinterpret_cast<ZcanTransmit>(library_.resolve("ZCAN_Transmit"));
        receive_ = reinterpret_cast<ZcanReceive>(library_.resolve("ZCAN_Receive"));
        getIProperty_ = reinterpret_cast<GetIProperty>(library_.resolve("GetIProperty"));
        releaseIProperty_ = reinterpret_cast<ReleaseIProperty>(library_.resolve("ReleaseIProperty"));
        if (openDevice_ == nullptr || closeDevice_ == nullptr || getDeviceInfo_ == nullptr
            || initCan_ == nullptr || startCan_ == nullptr || resetCan_ == nullptr || transmit_ == nullptr
            || receive_ == nullptr) {
            loadErrors.append(QStringLiteral("%1：缺少必要的 CAN SDK 函数").arg(QDir::toNativeSeparators(candidate)));
            library_.unload();
            openDevice_ = nullptr;
            closeDevice_ = nullptr;
            getDeviceInfo_ = nullptr;
            initCan_ = nullptr;
            startCan_ = nullptr;
            resetCan_ = nullptr;
            transmit_ = nullptr;
            receive_ = nullptr;
            getIProperty_ = nullptr;
            releaseIProperty_ = nullptr;
            continue;
        }

        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("无法加载CAN驱动库：%1").arg(loadErrors.join(QStringLiteral("; ")));
    }
    return false;
}

CanDeviceBackend::DeviceProfile CanDeviceBackend::resolveDeviceProfile(const QString &deviceType, bool *ok) const
{
    if (ok != nullptr) {
        *ok = true;
    }

    if (deviceType == QStringLiteral("USBCAN-I")) {
        return {3, 1, false};
    }
    if (deviceType == QStringLiteral("USBCAN-II")) {
        return {4, 2, false};
    }
    if (deviceType == QStringLiteral("CANFD-200U")) {
        return {41, 2, true};
    }

    if (ok != nullptr) {
        *ok = false;
    }
    return {};
}

bool CanDeviceBackend::fillClassicConfig(const QString &bitrate,
                                         const QString &workMode,
                                         ZcanChannelInitConfig *config,
                                         QString *errorMessage) const
{
    if (config == nullptr) {
        return false;
    }

    config->canType = kTypeCan;
    config->config.can.accCode = 0;
    config->config.can.accMask = 0xFFFFFFFF;
    config->config.can.filter = 0;
    config->config.can.mode = workMode == QStringLiteral("只听模式") ? 1 : 0;

    if (bitrate == QStringLiteral("125kbps")) {
        config->config.can.timing0 = 0x03;
        config->config.can.timing1 = 0x1C;
    } else if (bitrate == QStringLiteral("250kbps")) {
        config->config.can.timing0 = 0x01;
        config->config.can.timing1 = 0x1C;
    } else if (bitrate == QStringLiteral("500kbps")) {
        config->config.can.timing0 = 0x00;
        config->config.can.timing1 = 0x1C;
    } else if (bitrate == QStringLiteral("1Mbps")) {
        config->config.can.timing0 = 0x00;
        config->config.can.timing1 = 0x14;
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不支持的波特率：%1").arg(bitrate);
        }
        return false;
    }

    return true;
}

bool CanDeviceBackend::fillCanFdConfig(const QString &bitrate,
                                       const QString &workMode,
                                       ZcanChannelInitConfig *config,
                                       QString *errorMessage) const
{
    if (config == nullptr) {
        return false;
    }

    config->canType = kTypeCanFd;
    config->config.canfd.accCode = 0;
    config->config.canfd.accMask = 0xFFFFFFFF;
    config->config.canfd.filter = 0;
    config->config.canfd.mode = workMode == QStringLiteral("只听模式") ? 1 : 0;
    config->config.canfd.dbitTiming = 0x0081830E; // 1Mbps data phase, matching the SDK demo default.

    if (bitrate == QStringLiteral("125kbps")) {
        config->config.canfd.abitTiming = 0x0041AFBE;
    } else if (bitrate == QStringLiteral("250kbps")) {
        config->config.canfd.abitTiming = 0x0001AFBE;
    } else if (bitrate == QStringLiteral("500kbps")) {
        config->config.canfd.abitTiming = 0x0001975E;
    } else if (bitrate == QStringLiteral("1Mbps")) {
        config->config.canfd.abitTiming = 0x0001845C;
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不支持的波特率：%1").arg(bitrate);
        }
        return false;
    }

    return true;
}

bool CanDeviceBackend::setPropertyValue(int channelIndex,
                                        const QByteArray &name,
                                        const QByteArray &value,
                                        QString *errorMessage)
{
    if (handle_ == nullptr || getIProperty_ == nullptr || releaseIProperty_ == nullptr) {
        return false;
    }

    bool accessViolation = false;
    IProperty *property = invokeGetIProperty(&accessViolation);
    if (accessViolation || property == nullptr || property->setValue == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = accessViolation ? QStringLiteral("GetIProperty 触发了 SDK 访问异常")
                                            : QStringLiteral("GetIProperty 返回空属性接口");
        }
        return false;
    }

    const QByteArray path = QByteArray::number(channelIndex) + "/" + name;
    const int result = invokeSetProperty(property, path.constData(), value.constData(), &accessViolation);
    bool releaseAccessViolation = false;
    invokeReleaseIProperty(property, &releaseAccessViolation);

    if (accessViolation || result != 1) {
        if (errorMessage != nullptr) {
            *errorMessage = accessViolation ? QStringLiteral("设置属性时触发了 SDK 访问异常")
                                            : QStringLiteral("设置属性失败：%1").arg(QString::fromLatin1(path));
        }
        return false;
    }

    return true;
}

void CanDeviceBackend::resetChannel(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channelHandles_.size())) {
        return;
    }

    ChannelHandle &channelHandle = channelHandles_.at(static_cast<size_t>(channelIndex));
    if (channelHandle == nullptr || resetCan_ == nullptr) {
        channelHandle = nullptr;
        return;
    }

    bool accessViolation = false;
    invokeResetCan(channelHandle, &accessViolation);
    channelHandle = nullptr;
}

CanDeviceBackend::DeviceHandle CanDeviceBackend::invokeOpenDevice(unsigned int deviceType,
                                                                  unsigned int deviceIndex,
                                                                  bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return openDevice_(deviceType, deviceIndex, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return nullptr;
    }
#else
    return openDevice_(deviceType, deviceIndex, 0);
#endif
}

unsigned int CanDeviceBackend::invokeCloseDevice(DeviceHandle deviceHandle, bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return closeDevice_(deviceHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return closeDevice_(deviceHandle);
#endif
}

unsigned int CanDeviceBackend::invokeGetDeviceInfo(DeviceHandle deviceHandle,
                                                   ZcanDeviceInfo *deviceInfo,
                                                   bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return getDeviceInfo_(deviceHandle, deviceInfo);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return getDeviceInfo_(deviceHandle, deviceInfo);
#endif
}

CanDeviceBackend::ChannelHandle CanDeviceBackend::invokeInitCan(unsigned int channelIndex,
                                                                ZcanChannelInitConfig *config,
                                                                bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return initCan_(handle_, channelIndex, config);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return nullptr;
    }
#else
    return initCan_(handle_, channelIndex, config);
#endif
}

unsigned int CanDeviceBackend::invokeStartCan(ChannelHandle channelHandle, bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return startCan_(channelHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return startCan_(channelHandle);
#endif
}

unsigned int CanDeviceBackend::invokeResetCan(ChannelHandle channelHandle, bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return resetCan_(channelHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return resetCan_(channelHandle);
#endif
}

unsigned int CanDeviceBackend::invokeTransmit(ChannelHandle channelHandle,
                                              ZcanTransmitData *transmitData,
                                              unsigned int length,
                                              bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return transmit_(channelHandle, transmitData, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return transmit_(channelHandle, transmitData, length);
#endif
}

unsigned int CanDeviceBackend::invokeReceive(ChannelHandle channelHandle,
                                             ZcanReceiveData *receiveData,
                                             unsigned int length,
                                             int waitTime,
                                             bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return receive_(channelHandle, receiveData, length, waitTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return receive_(channelHandle, receiveData, length, waitTime);
#endif
}

CanDeviceBackend::IProperty *CanDeviceBackend::invokeGetIProperty(bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return getIProperty_(handle_);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return nullptr;
    }
#else
    return getIProperty_(handle_);
#endif
}

unsigned int CanDeviceBackend::invokeReleaseIProperty(IProperty *property, bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return releaseIProperty_(property);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return releaseIProperty_(property);
#endif
}

int CanDeviceBackend::invokeSetProperty(IProperty *property,
                                        const char *path,
                                        const char *value,
                                        bool *accessViolation) const
{
    setAccessViolation(accessViolation, false);
#if defined(_MSC_VER)
    __try {
        return property->setValue(path, value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        setAccessViolation(accessViolation, true);
        return 0;
    }
#else
    return property->setValue(path, value);
#endif
}
