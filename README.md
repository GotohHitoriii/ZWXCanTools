# ZWXCanTools

[中文](#中文) | [English](#english)

## 中文

ZWXCanTools 是一款 Windows 平台 CAN 工具，使用 C++17、Qt 6 和 CMake 开发。

这个工程专为 AI 协同而生：软件不是只给人点击使用，也为 AI Agent 预留了稳定、可脚本化、可同步 UI 状态的本地控制接口。人可以在界面上操作 CAN，AI 也可以通过本地 API 操作同一套设备状态，两边看到的状态保持一致。

### 核心特点

- 现代桌面人机界面：设备连接、CAN0/CAN1 发送、CAN0/CAN1 接收。
- AI 原生协同接口：推荐使用 `127.0.0.1:17652` TCP JSON Lines。
- WebSocket 兼容接口：`ws://127.0.0.1:17651`。
- CAN 功能：打开/关闭设备、启动 CAN0/CAN1、发送经典 CAN 帧、接收和过滤最近帧。
- 发送帧记忆：用户添加的发送帧会持久化，重启软件后自动恢复。

### 依赖环境

- Windows 10/11 x64
- Visual Studio 2022 C++ 工具链
- CMake 3.24 或更新版本
- Ninja
- Qt 6，包含以下模块：
  - Widgets
  - Network
  - WebSockets
  - Svg
- 本机安装第三方 CAN 设备驱动和 SDK

建议设置 `QT_DIR` 环境变量，例如：

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
```

### 公开仓库说明

本仓库公开发布，因此不上传第三方 SDK、驱动库、示例代码、手册或其他授权资料。

本地开发时，请自行从设备供应商渠道获取 SDK，并放到默认路径：

```text
third_party/can_sdk/x64
```

也可以在配置 CMake 时指定本机 SDK 路径和驱动库路径：

```powershell
cmake --preset windows-msvc-debug -DCAN_SDK_DIR="D:/path/to/can_sdk/x64" -DCAN_SDK_DRIVER_DLL="D:/path/to/driver.dll"
```

如果没有本地 SDK，项目仍可编译 UI 和 AI 协同部分，但真实 CAN 硬件访问需要设备驱动库和驱动程序。

### 构建

在 Visual Studio Developer PowerShell 中运行：

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Release 构建：

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

### 目录结构

```text
ZWXCANTools/
  CMakeLists.txt
  CMakePresets.json
  README.md
  docs/
  resources/
  src/
  reference/        # 本地开发参考资料，整个目录被 .gitignore 忽略
```

### AI 协同

详细接口文档见 [docs/AI_OPERATION_GUIDE.md](docs/AI_OPERATION_GUIDE.md)。

推荐接口：

```text
TCP JSON Lines: 127.0.0.1:17652
```

每个请求是一行 JSON，每个响应也是一行 JSON，行尾为 `\n`。

### License

本项目使用 MIT License，详见 [LICENSE](LICENSE)。

## English

ZWXCanTools is a Windows CAN utility built with C++17, Qt 6, and CMake.

This project is born for AI collaboration. It is not only a human-operated desktop tool; it also exposes a stable, scriptable local control API for AI agents. Humans can operate CAN from the UI, while AI agents can operate the same device state through the local API, with both sides staying synchronized.

### Highlights

- Modern desktop interface for device connection, CAN0/CAN1 transmit, and CAN0/CAN1 receive.
- AI-native collaboration API: TCP JSON Lines on `127.0.0.1:17652`.
- WebSocket compatibility API: `ws://127.0.0.1:17651`.
- CAN features: open/close device, start CAN0/CAN1, send classic CAN frames, receive and filter recent frames.
- Persistent transmit rows: configured transmit frames are restored after restarting the application.

### Requirements

- Windows 10/11 x64
- Visual Studio 2022 C++ toolchain
- CMake 3.24 or newer
- Ninja
- Qt 6 with these modules:
  - Widgets
  - Network
  - WebSockets
  - Svg
- Locally installed third-party CAN device driver and SDK

Set the `QT_DIR` environment variable, for example:

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
```

### Public Repository Policy

This repository is public. Third-party SDK files, driver libraries, examples, manuals, and other licensed materials are not redistributed.

For local development, obtain the SDK from the device supplier and place it in the default path:

```text
third_party/can_sdk/x64
```

You can also configure CMake with a local SDK path and driver library path:

```powershell
cmake --preset windows-msvc-debug -DCAN_SDK_DIR="D:/path/to/can_sdk/x64" -DCAN_SDK_DRIVER_DLL="D:/path/to/driver.dll"
```

Without the local SDK, the UI and AI collaboration parts can still be built, but real CAN hardware access requires the device driver library and driver.

### Build

Run these commands in a Visual Studio Developer PowerShell:

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Release build:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

### Layout

```text
ZWXCANTools/
  CMakeLists.txt
  CMakePresets.json
  README.md
  docs/
  resources/
  src/
  reference/        # Local development references, ignored entirely by .gitignore
```

### AI Collaboration

See [docs/AI_OPERATION_GUIDE.md](docs/AI_OPERATION_GUIDE.md) for the full API guide.

Recommended API:

```text
TCP JSON Lines: 127.0.0.1:17652
```

Each request is one JSON object followed by `\n`, and each response follows the same format.

### License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
