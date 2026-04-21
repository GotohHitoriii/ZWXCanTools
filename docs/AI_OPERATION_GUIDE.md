# ZWXCanTools AI Operation Guide

本文档面向另一个 AI Agent。阅读本文后，AI 应该能够通过本地接口操作 ZWXCanTools，而不是通过模拟鼠标点击界面。

## 1. 操作入口

ZWXCanTools 启动后会自动开启本地 AI 接口。优先使用 TCP JSON Lines 接口；WebSocket 保留为兼容接口。

- 推荐地址：`127.0.0.1:17652`
- 推荐协议：TCP，每行一条 JSON 请求，每行一条 JSON 响应，换行符为 `\n`
- 兼容地址：`ws://127.0.0.1:17651`
- 兼容协议：WebSocket，发送 JSON 文本，接收 JSON 文本
- 连接范围：仅本机 `LocalHost`
- 主要用途：让 AI 操作软件，同时让人机界面同步更新

连接成功后，AI 应立即发送 `get_state` 查询当前状态：

```json
{
  "action": "get_state"
}
```

## 2. 状态模型

AI 应以 `state` 为唯一可信状态，不要根据界面文字猜测状态。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `deviceType` | string | 当前选择的设备类型 |
| `deviceIndex` | number | 当前设备索引 |
| `deviceOpen` | bool | 设备是否已打开 |
| `channel0Started` | bool | CAN0 是否已启动 |
| `channel1Started` | bool | CAN1 是否已启动 |
| `bitrate` | string | 最近一次启动通道使用的波特率 |
| `workMode` | string | 最近一次启动通道使用的工作模式 |

当前 AI 桥采用一问一答模式。TCP JSON Lines 和 WebSocket 都不会主动推送状态。状态变化后，AI 需要再次调用 `get_state` 获取同步状态：

```json
{
  "action": "get_state"
}
```

AI 在执行任何硬件相关动作前，应先调用 `get_state`。

## 3. 通用请求/响应规则

请求格式：

```json
{
  "action": "命令名",
  "参数名": "参数值"
}
```

成功响应通常包含：

```json
{
  "ok": true,
  "state": {
    "...": "..."
  }
}
```

失败响应通常包含：

```json
{
  "ok": false,
  "state": {
    "...": "..."
  }
}
```

如果命令名不存在，软件返回：

```json
{
  "ok": false,
  "error": "unknown_action"
}
```

如果发送的不是 JSON 对象，软件返回：

```json
{
  "ok": false,
  "error": "invalid_json"
}
```

## 4. 已实现命令

### 4.1 查询状态：`get_state`

用途：获取当前设备、通道和波特率状态。

请求：

```json
{
  "action": "get_state"
}
```

响应：

```json
{
  "ok": true,
  "state": {
    "deviceType": "USBCAN-II",
    "deviceIndex": 0,
    "deviceOpen": false,
    "channel0Started": false,
    "channel1Started": false,
    "bitrate": "250kbps",
    "workMode": "正常模式"
  }
}
```

### 4.2 设置设备：`set_device`

用途：设置 CAN 分析仪类型和索引。只能在打开设备前设置；如果设备已经打开，建议先执行 `close_device`。

请求：

```json
{
  "action": "set_device",
  "deviceType": "USBCAN-II",
  "deviceIndex": 0
}
```

参数：

| 字段 | 类型 | 必填 | 推荐值/范围 |
| --- | --- | --- | --- |
| `deviceType` | string | 否 | `USBCAN-I`、`USBCAN-II`、`CANFD-200U` |
| `deviceIndex` | number | 否 | `0` 到 `3` |

响应：

```json
{
  "ok": true,
  "state": {
    "...": "..."
  }
}
```

注意：

- 当前默认设备类型是 `USBCAN-II`。
- 当前默认设备索引是 `0`。
- 如果用户没有明确说明，AI 应优先使用 `USBCAN-II` 和索引 `0`。

### 4.3 打开设备：`open_device`

用途：打开当前 `deviceType` 和 `deviceIndex` 对应的 CAN 分析仪。

请求：

```json
{
  "action": "open_device"
}
```

响应：

```json
{
  "ok": true,
  "state": {
    "deviceOpen": true
  }
}
```

失败时：

```json
{
  "ok": false,
  "state": {
    "deviceOpen": false
  }
}
```

注意：

- 如果 `ok` 为 `false`，不要继续启动通道。
- 失败常见原因：设备未连接、驱动未安装、设备索引不对、DLL 位数不匹配。
- 打开设备成功后，人机界面会同步显示“已打开”。

### 4.4 关闭设备：`close_device`

用途：关闭当前设备，并清除通道启动状态。

请求：

```json
{
  "action": "close_device"
}
```

响应：

```json
{
  "ok": true,
  "state": {
    "deviceOpen": false,
    "channel0Started": false,
    "channel1Started": false
  }
}
```

注意：

- 关闭设备会同时使 CAN0/CAN1 状态变为未启动。
- AI 完成硬件操作后，除非用户明确要求保持连接，否则建议关闭设备。

### 4.5 启动通道：`start_channel`

用途：启动 CAN0 或 CAN1。

请求：

```json
{
  "action": "start_channel",
  "channel": 0,
  "bitrate": "250kbps",
  "workMode": "正常模式"
}
```

参数：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `channel` | number | 是 | `0` 表示 CAN0，`1` 表示 CAN1 |
| `bitrate` | string | 否 | `125kbps`、`250kbps`、`500kbps`、`1Mbps` |
| `workMode` | string | 否 | `正常模式`、`只听模式` |

响应：

```json
{
  "ok": true,
  "state": {
    "deviceOpen": true,
    "channel0Started": true,
    "bitrate": "250kbps",
    "workMode": "正常模式"
  }
}
```

注意：

- 启动通道前必须先成功执行 `open_device`。
- 如果要启动 CAN1，将 `channel` 设置为 `1`。
- 如果用户没有说明波特率，AI 应使用 `250kbps`。
- 如果用户没有说明工作模式，AI 应使用 `正常模式`。

### 4.6 发送单帧：`send_frame`

用途：通过指定通道发送一帧经典 CAN 数据。

请求：

```json
{
  "action": "send_frame",
  "channel": 0,
  "frameId": "18FF50E5",
  "frameType": "扩展帧",
  "frameFormat": "数据帧",
  "data": "11 22 33 44"
}
```

参数：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `channel` | number | 是 | `0` 表示 CAN0，`1` 表示 CAN1 |
| `frameId` | string | 是 | 十六进制 ID，标准帧最多 3 位，扩展帧最多 8 位 |
| `frameType` | string | 否 | `扩展帧` 或 `标准帧`，默认 `扩展帧` |
| `frameFormat` | string | 否 | `数据帧` 或 `远程帧`，默认 `数据帧` |
| `data` | string | 否 | 最多 8 个十六进制字节，例如 `11 22 33 44` |

响应：

```json
{
  "ok": true,
  "state": {
    "...": "..."
  }
}
```

注意：

- 发送前必须先成功打开设备并启动对应通道。
- `data` 会自动压缩为十六进制字节，`11 22` 和 `1122` 等价。
- 如果 `data` 中有效十六进制字符数量为奇数，软件会返回 `invalid_frame_data`。
- 如果 `frameId` 为空，软件会返回 `invalid_frame_id`。

### 4.7 读取接收帧：`get_rx_frames`

用途：从指定通道读取当前硬件接收缓冲区中的 CAN 帧。AI 可用它确认发送是否被另一个通道收到。

请求：

```json
{
  "action": "get_rx_frames",
  "channel": 1,
  "maxFrames": 99,
  "filterId": "18FF50E5"
}
```

参数：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `channel` | number | 是 | `0` 表示 CAN0，`1` 表示 CAN1 |
| `maxFrames` | number | 否 | 读取数量，范围 `1` 到 `99`，默认 `99` |
| `filterId` | string | 否 | 只返回指定 ID；留空返回全部 |

响应：

```json
{
  "ok": true,
  "frames": [
    {
      "frameId": "18FF50E5",
      "frameType": "扩展帧",
      "frameFormat": "数据帧",
      "dlc": 4,
      "data": "11 22 33 44",
      "timestampUs": "12345678"
    }
  ],
  "state": {
    "...": "..."
  }
}
```

注意：

- 读取前必须先成功打开设备并启动对应通道。
- 该命令读取的是硬件接收缓冲区，可能一次返回 0 到 `maxFrames` 条。
- 确认闭环发送时，AI 应循环读取几次，例如每 100 ms 读取一次，总等待 2 秒。

## 5. 推荐操作流程

### 5.1 打开设备并启动 CAN0

```json
{"action":"get_state"}
```

如果 `deviceOpen` 为 `false`：

```json
{"action":"set_device","deviceType":"USBCAN-II","deviceIndex":0}
```

```json
{"action":"open_device"}
```

如果 `open_device` 返回 `ok: true`：

```json
{"action":"start_channel","channel":0,"bitrate":"250kbps","workMode":"正常模式"}
```

### 5.2 打开设备并同时启动 CAN0/CAN1

```json
{"action":"set_device","deviceType":"USBCAN-II","deviceIndex":0}
```

```json
{"action":"open_device"}
```

```json
{"action":"start_channel","channel":0,"bitrate":"250kbps","workMode":"正常模式"}
```

```json
{"action":"start_channel","channel":1,"bitrate":"250kbps","workMode":"正常模式"}
```

### 5.3 安全关闭

```json
{"action":"get_state"}
```

如果 `deviceOpen` 为 `true`：

```json
{"action":"close_device"}
```

### 5.4 CAN0 发送一帧并确认 CAN1 收到

前提：CAN0 和 CAN1 在硬件上已经连接。

```json
{"action":"set_device","deviceType":"USBCAN-II","deviceIndex":0}
```

```json
{"action":"open_device"}
```

```json
{"action":"start_channel","channel":0,"bitrate":"250kbps","workMode":"正常模式"}
```

```json
{"action":"start_channel","channel":1,"bitrate":"250kbps","workMode":"正常模式"}
```

```json
{"action":"send_frame","channel":0,"frameId":"18FF50E5","frameType":"扩展帧","frameFormat":"数据帧","data":"11 22 33 44"}
```

然后循环读取 CAN1，直到看到同 ID 和同数据，或超时：

```json
{"action":"get_rx_frames","channel":1,"maxFrames":99,"filterId":"18FF50E5"}
```

收到的目标帧应满足：

- `frameId` 等于 `18FF50E5`
- `frameType` 等于 `扩展帧`
- `frameFormat` 等于 `数据帧`
- `data` 等于 `11 22 33 44`

## 6. AI 行为约束

AI 必须遵守以下规则：

1. 不要绕过 AI 接口去直接调用设备驱动库。
2. 不要模拟鼠标点击来替代已有 AI 命令。
3. 不要在 `deviceOpen=false` 时启动通道。
4. 不要在 `open_device` 返回 `ok=false` 后继续执行 `start_channel`。
5. 不要假设设备索引一定存在；打开失败后应询问用户确认硬件连接和索引。
6. 如果用户没有指定波特率，默认使用 `250kbps`。
7. 如果用户没有指定工作模式，默认使用 `正常模式`。
8. 完成任务后，除非用户要求保持打开，建议执行 `close_device`。
9. 发送后确认接收时，应使用 `get_rx_frames` 循环读取，不要只读一次就判定失败。

## 7. 当前未开放给 AI 的能力

以下能力在界面中已经存在或部分存在，但当前 AI 桥尚未开放对应命令：

| 能力 | 当前状态 | 建议未来命令 |
| --- | --- | --- |
| 添加发送帧行 | 人工界面可操作 | `add_tx_frame` |
| 停止周期发送 | 人工界面可操作 | `stop_tx_frame` |
| 清除接收表格 | 人工界面可操作 | `clear_rx_frames` |
| 设置接收 ID 过滤 | 人工界面可操作 | `set_rx_filter` |

未来扩展时，应保持一个原则：AI 通过命令改变的内容，界面必须同步更新；人在界面中改变的内容，AI 也应能通过 `get_state` 或事件得知。

## 8. 建议的未来命令格式

本节是约定草案，当前版本不一定支持。AI 不应直接调用这些命令，除非软件后续明确实现。

### 8.1 清除接收帧：`clear_rx_frames`

```json
{
  "action": "clear_rx_frames",
  "channel": 0
}
```

## 9. 最小可用 AI 客户端示例

下面是一个 Python 示例，用于通过推荐的 TCP JSON Lines 接口连接 ZWXCanTools 并启动 CAN0。此示例只使用 Python 标准库。

```python
import json
import socket

sock = socket.create_connection(("127.0.0.1", 17652), timeout=3)
reader = sock.makefile("r", encoding="utf-8", newline="\n")

def call(command):
    payload = json.dumps(command, ensure_ascii=False).encode("utf-8") + b"\n"
    sock.sendall(payload)
    return json.loads(reader.readline())

print(call({"action": "get_state"}))
print(call({"action": "set_device", "deviceType": "USBCAN-II", "deviceIndex": 0}))
open_result = call({"action": "open_device"})
print(open_result)

if open_result.get("ok"):
    print(call({
        "action": "start_channel",
        "channel": 0,
        "bitrate": "250kbps",
        "workMode": "正常模式"
    }))

reader.close()
sock.close()
```

## 10. 故障处理策略

当命令失败时，AI 应优先读取响应中的 `ok` 和 `state`。

推荐处理：

- `unknown_action`：说明软件版本不支持该命令，停止继续调用同类命令。
- `invalid_json`：修正 JSON 格式后重试一次。
- `open_device` 返回 `ok=false`：不要启动通道，询问用户检查硬件、驱动、索引和设备类型。
- `start_channel` 返回 `ok=false`：不要发送 CAN 帧，询问用户确认设备是否打开、通道是否存在、波特率是否正确。
- TCP 连接失败：确认 ZWXCanTools 是否正在运行，确认端口 `17652` 是否被占用。
- WebSocket 连接失败：改用推荐的 TCP JSON Lines 接口；如仍需使用 WebSocket，确认端口 `17651` 是否被占用。

## 11. 版本备注

本文档对应当前开发版 ZWXCanTools。

当前 AI 桥的实现位置：

- `src/ai_command_bridge.h`
- `src/ai_command_bridge.cpp`
- `src/device_controller.h`
- `src/device_controller.cpp`

AI 接口扩展时，优先修改 `AiCommandBridge::execute()`，并确保所有命令最终通过 `DeviceController` 操作业务状态，以保证界面与 AI 状态同步。
