# Project Structure / 工程结构

## 中文

本文档面向开发者和 AI Agent，用于快速理解 ZWXCanTools 的工程边界。

ZWXCanTools 专为 AI 协同而生。工程结构中单独保留了 `ai_command_bridge.*`，让 AI Agent 可以通过稳定的本地 API 操作软件，而不是模拟鼠标点击。

### 顶层目录

| 路径 | 作用 |
| --- | --- |
| `src/` | C++/Qt 应用源码 |
| `resources/` | Qt 资源文件和图标 |
| `docs/` | 工程文档和 AI 操作文档 |
| `reference/` | 本地开发参考资料目录，整个目录不提交 |
| `build/` | 本地构建产物，Git 忽略 |

### 源码模块

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 应用启动、主窗口创建、AI 桥启动 |
| `src/main_window.*` | 人机界面、发送/接收页面、发送帧持久化 |
| `src/device_controller.*` | 设备状态和 CAN 操作编排 |
| `src/can_device_backend.*` | 第三方 CAN SDK 动态加载、设备/通道/收发调用 |
| `src/ai_command_bridge.*` | AI 本地命令接口，支持 TCP JSON Lines 和 WebSocket |

### 不提交内容

- `build/`
- CMake 生成文件
- Qt 部署到构建目录的 DLL 和插件
- 调试符号，如 `*.pdb`、`*.ilk`
- IDE 元数据，如 `.vs/`、`.vscode/`、`.idea/`
- `reference/` 中的本地参考资料、第三方 SDK、驱动库、示例、手册和未授权参考图片

## English

This document helps developers and AI agents understand the project boundaries of ZWXCanTools.

ZWXCanTools is born for AI collaboration. The project keeps `ai_command_bridge.*` as an explicit module so AI agents can operate the software through a stable local API instead of mouse-click simulation.

### Top-Level Directories

| Path | Purpose |
| --- | --- |
| `src/` | C++/Qt application source code |
| `resources/` | Qt resource file and icons |
| `docs/` | Project documentation and AI operation guide |
| `reference/` | Local development reference directory, ignored entirely |
| `build/` | Local generated build output, ignored by Git |

### Source Modules

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Application startup, main window creation, AI bridge startup |
| `src/main_window.*` | Human UI, send/receive pages, persistent transmit rows |
| `src/device_controller.*` | Device state and CAN operation orchestration |
| `src/can_device_backend.*` | Third-party CAN SDK dynamic loading and device/channel/transmit/receive calls |
| `src/ai_command_bridge.*` | Local AI command API over TCP JSON Lines and WebSocket |

### Do Not Commit

- `build/`
- CMake generated files
- Qt DLLs and plugins deployed into build directories
- Debug symbols such as `*.pdb` and `*.ilk`
- IDE metadata such as `.vs/`, `.vscode/`, `.idea/`
- Local reference materials, third-party SDK files, driver libraries, examples, manuals, and unlicensed reference images under `reference/`
