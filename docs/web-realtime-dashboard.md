# Web 实时仪表盘实现说明

## 现在实现了什么

ESP32 内置页面会显示 STM32 的真实温度、湿度、气压、光照、PIR、两路继电器、蜂鸣器、自动模式和 WS2812B 亮度。页面和 OTA 共用 `http://192.168.4.1`，不需要外部 CDN，也不占用用于暂存固件的 SPIFFS。

数据链路：

```text
AHT20/BMP280/BH1750/PIR/执行器
             │
             ▼
STM32 vAppTask ── 200ms 更新 SensorSnapshot_t
             │
             │ CMD_GET_SENSOR_SNAPSHOT / CMD_SENSOR_SNAPSHOT_RSP
             ▼
ESP32 sensor_poll ── 1s 查询一次并缓存最新快照
             │
             │ GET /api/sensors
             ▼
浏览器 ── 1s 轮询并局部更新页面
```

这是请求/响应加缓存，不是 STM32 主动持续推流。好处是 UART 负载固定，浏览器请求不会直接占住串口，OTA 期间也能暂停查询。

## UART 快照协议

共享定义位于 `shared/protocol.h`：

| 方向 | 命令 | ID | Payload |
|------|------|----|---------|
| ESP32 → STM32 | `CMD_GET_SENSOR_SNAPSHOT` | `0x32` | 空 |
| STM32 → ESP32 | `CMD_SENSOR_SNAPSHOT_RSP` | `0x87` | 18B `SensorSnapshot_t` |

`SensorSnapshot_t` 全部采用定点整数，STM32 不进行浮点或 JSON 格式化：

| 字段 | 类型 | 单位/含义 |
|------|------|-----------|
| `uptime_ms` | `uint32_t` | STM32 启动时间，毫秒 |
| `temperature_centi_c` | `int16_t` | 摄氏度 ×100 |
| `humidity_centi_percent` | `uint16_t` | `%RH` ×100 |
| `pressure_pa` | `uint32_t` | Pa |
| `light_lux` | `uint16_t` | lux |
| `flags` | `uint16_t` | 有效位、PIR 和执行器状态 |
| `led_brightness` | `uint8_t` | WS2812B 原始通道值 0..255 |
| `led_percent` | `uint8_t` | 相对当前最大亮度 160 的百分比 |

`flags` 当前定义：

| Bit | 含义 |
|-----|------|
| 0 | 温湿度气压有效 |
| 1 | BH1750 光照有效 |
| 2 | PIR 驱动就绪 |
| 3 | PIR 已完成 30 秒预热 |
| 4 | 检测到人体 |
| 5 / 6 | 继电器 1 / 2 开启 |
| 7 | 自动模式开启 |
| 8 | 蜂鸣器开启 |

当前两颗 MCU 都是小端，因此结构体按 1 字节对齐后直接作为 payload 传输，并用编译期断言固定为 18B。UART 外层帧仍带长度和 IEEE CRC-32。

## ESP32 缓存与并发

ESP32 的 `sensor_poll` 任务每 1 秒请求一次快照。收到且长度正确后，写入受 Mutex 保护的静态缓存；HTTP Handler 只复制缓存，不直接访问 UART。

传感器查询、蓝牙 `VERSION`/继电器控制和 OTA 共用 `g_ota_mutex`，保证任何时刻只有一个 UART 请求/响应事务。OTA 优先：查询任务拿不到锁就跳过本轮，缓存超过 5 秒未更新时 `/api/sensors` 返回 `online:false`，页面清空数值并显示“数据超时”。

没有使用动态分配保存传感器数据，也没有在 STM32 上新增任务。STM32 的共享快照通过短临界区复制，避免 `vAppTask` 更新到一半时 `vCommTask` 读到撕裂数据。

## REST API

### `GET /api/sensors`

正常响应：

```json
{
  "online": true,
  "age_ms": 24,
  "uptime_ms": 128430,
  "environment_valid": true,
  "light_valid": true,
  "temperature": 26.30,
  "humidity": 61.00,
  "pressure": 1012.40,
  "lux": 428,
  "pir_ready": true,
  "pir_warmed_up": true,
  "pir": false,
  "relay1": true,
  "relay2": false,
  "auto_mode": true,
  "buzzer": false,
  "led_brightness": 67,
  "led_percent": 42
}
```

- `age_ms` 是 ESP32 缓存距本次 HTTP 请求的时间，不是传感器采样周期。
- 单个传感器读失败时，对应有效位为 `false`，数值返回 `null`。
- 从未收到快照时 `age_ms:null`；UART 中断超过 5 秒后 `online:false`。
- 温湿度和气压在 ESP32 用整数拆分成十进制 JSON，未启用浮点 `printf`。

现有 OTA API 保持不变：

| API | 用途 |
|-----|------|
| `GET /api/status` | OTA 暂存、写入进度与结果 |
| `POST /api/upload?version=<N>` | 流式上传 Application `.bin` |
| `POST /api/start` | 启动 STM32 OTA |
| `POST /api/control` | 下发灯带电源或蜂鸣器控制 |

当前灯带接在 Relay 2 的 `NO2` 触点，Web 使用语义字段而不是暴露接线编号：

```json
{"light": true}
```

ESP32 将它转换为 `RELAY2 ON`，经 `CMD_APP_MSG` 发给 STM32；STM32 回传两路继电器状态，页面再用 `relay2` 同步开关。加湿器已移除，自动湿度联动保持关闭。灯带亮度仍由 BH1750 的 5~1000 lux 映射控制，这一步 Web 只控制整条灯带的 VCC 通断。

## 构建与实机检查

```powershell
gcc -std=c11 -Wall -Wextra -Werror -Ishared `
  tools/protocol_smoke_test.c shared/protocol.c -o protocol_smoke_test.exe
.\protocol_smoke_test.exe
pio run -e app
pio run -d esp32-comm-bridge
```

## 灯带与语言同步

- `POST /api/control {"light":true}`：继电器 2 / NO2 灯带电源。
- `POST /api/control {"light_auto":true|false}`：AUTO/MANUAL 模式。
- `POST /api/control {"brightness":1..100}`：MANUAL 亮度设定。
- `POST /api/control {"ui_chinese":true|false}`：TFT 中文/English。
- STM32 是状态源；ESP32 每 200ms 缓存快照，Web 每 1s 刷新。TFT 本地旋钮操作会出现在下一次 Web 刷新，Web 操作也通过同一命令通道立即更新 TFT。

烧录两端新固件后，手机或电脑连接 `STM32-OTA-Bridge`：

```powershell
curl.exe http://192.168.4.1/api/sensors
```

也可以通过 Bluetooth SPP 下发 `WIFI <ssid>,<password>`，让 ESP32 把配置保存到 NVS，再从家庭局域网访问其 DHCP 地址。2026-08-11 实机回归时，ESP32 位于 `192.168.0.104`，连续 4 次读取均为 `online=true`，`age_ms` 为 201~871ms；温湿度、光照和网页更新时间持续变化。该地址由 DHCP 分配，只是本次测试证据，不能写死到客户端。

最新一次真机读数（页面切到真机模式后持续刷新）：**24.9°C、51.9%RH、81 lux**，STM32 链路缓存 `age_ms` 约 **345ms**，页面更新时间持续跳动，确认数据链是活的而非静态演示。

本地预览脚本既能展示演示数据，也能把只读 API 转发到真机：

```powershell
# 静态演示
python tools/preview_web_ui.py --port 8765

# 真机实时数据；浏览器打开 http://127.0.0.1:8765/
python tools/preview_web_ui.py --port 8765 --device http://192.168.0.104
```

`--device` 代理两个只读接口和 `POST /api/control`，不会转发固件上传或 OTA 写入。实机页面截图见 [web-realtime-dashboard-live.png](images/web-realtime-dashboard-live.png)。

检查顺序：

1. `online` 应为 `true`，`age_ms` 通常低于 1000。
2. 遮挡 BH1750，确认 `lux` 上下变化，灯带百分比反向变化。
3. PIR 预热结束后在传感器前移动，确认 `pir` 变化。
4. 在 Web 控制页切换灯带电源，确认 Relay 2 吸合/释放、NO2 通断和页面状态在约 1 秒内一致。
5. 临时断开 PA9/PA10 任一 UART 线，约 5 秒后确认页面显示数据超时；恢复后应自动上线。
6. 再跑一次 Web OTA，确认传感器轮询不会抢占 OTA UART 事务。

## 当前边界

- 已实现：实时状态卡片、1 秒 REST 轮询、离线检测、灯带电源/蜂鸣器 Web 控制、OTA 页面。
- 未实现：Web 灯带亮度控制、WebSocket 推送、历史数据持久化、MQTT/Node-RED。
- 浏览器当前只显示最新快照，不保存历史曲线；历史数据计划由 Pi5 Mosquitto + Node-RED/FlowFuse Dashboard 负责。
