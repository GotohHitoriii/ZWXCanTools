# GitHub Preparation Checklist / GitHub 发布前检查

## 中文

本工程作为公开仓库发布。公开仓库中不得包含第三方 SDK、驱动库、示例代码、手册或其他授权资料。

ZWXCanTools 的定位需要在公开说明中保持清晰：这是一个专为 AI 协同而生的 CAN 工具，人和 AI 可以共同操作同一套软件状态。

### 应提交内容

- `CMakeLists.txt`
- `CMakePresets.json`
- `.gitignore`
- `.gitattributes`
- `README.md`
- `docs/`
- `resources/`
- `src/`

### 不应提交内容

- `build/`
- `reference/`
- 第三方 SDK、驱动库、LIB、示例、手册
- CMake 生成文件
- Qt 部署到构建目录的 DLL 和插件
- 调试符号：`*.pdb`、`*.ilk`
- IDE 元数据：`.vs/`、`.vscode/`、`.idea/`
- 运行日志和崩溃转储

### 第三方 SDK 策略

公开仓库只保留 SDK 放置位置和配置说明，不上传第三方文件。

开发者需要自行从设备供应商渠道获取 SDK，然后：

1. 放到默认路径 `third_party/can_sdk/x64`。
2. 或使用 `-DCAN_SDK_DIR="D:/path/to/can_sdk/x64"` 和 `-DCAN_SDK_DRIVER_DLL="D:/path/to/driver.dll"` 指定本机路径。

### 首次推送命令

```powershell
git status --ignored
git add .
git status
git commit -m "Initial public ZWXCanTools project"
git branch -M main
git remote add origin <your-github-repo-url>
git push -u origin main
```

提交前重点确认：`build/` 和 `reference/` 必须显示为 ignored，不能出现在 staged files 中。

### 构建检查

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

如果未配置本地第三方 SDK，构建可以继续，但真实 CAN 硬件访问不可用。

## English

This project is published as a public repository. The public repository must not include third-party SDK files, driver libraries, examples, manuals, or other licensed materials.

The public project description should keep the positioning clear: ZWXCanTools is a CAN tool born for AI collaboration, allowing humans and AI agents to operate the same software state together.

### Files To Commit

- `CMakeLists.txt`
- `CMakePresets.json`
- `.gitignore`
- `.gitattributes`
- `README.md`
- `docs/`
- `resources/`
- `src/`

### Files Not To Commit

- `build/`
- `reference/`
- Third-party SDK, driver libraries, LIB files, examples, and manuals
- CMake generated files
- Qt DLLs and plugins deployed into build directories
- Debug symbols: `*.pdb`, `*.ilk`
- IDE metadata: `.vs/`, `.vscode/`, `.idea/`
- Runtime logs and crash dumps

### Third-Party SDK Policy

The public repository only documents where the SDK should be placed and how to configure it. It does not redistribute third-party files.

Developers should obtain the SDK from the device supplier, then:

1. Place it under the default `third_party/can_sdk/x64` path.
2. Or configure CMake with `-DCAN_SDK_DIR="D:/path/to/can_sdk/x64"` and `-DCAN_SDK_DRIVER_DLL="D:/path/to/driver.dll"`.

### First Push Commands

```powershell
git status --ignored
git add .
git status
git commit -m "Initial public ZWXCanTools project"
git branch -M main
git remote add origin <your-github-repo-url>
git push -u origin main
```

Before committing, confirm that `build/` and `reference/` are ignored and do not appear in staged files.

### Build Smoke Test

```powershell
$env:QT_DIR = "D:\Qt\6.11.0\msvc2022_64"
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

If the local third-party SDK is not configured, the project can still build, but real CAN hardware access will not be available.
