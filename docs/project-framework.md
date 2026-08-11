# 智能环境监测与联动控制系统 — 完整项目框架

## Context

在已有的 Bootloader + FreeRTOS + ESP32-OTA 底层基础上，扩展传感器层和上层控制接口。目标：打造一个**协议全覆盖、外设用到极致、多层控制接口**的简历级嵌入式项目。

### 当前实机 checkpoint（2026-08-11）

- 已完成：SSD1306 本地菜单、EC11 旋转/确认、BH1750、HC-SR501、AHT20+BMP280 温湿度气压、两路继电器、有源蜂鸣器和 WS2812B 自动调光。
- 当前 AHT20 与 BMP280 是一块组合模块，因此和 OLED、BH1750 一起共用 I2C1 `PB6/PB7 @ 100kHz`；下面的 I2C2 分组仍是后续目标架构。
- 当前本地 UI、传感器采样、WS2812B 调光和湿度→继电器1联动集成在已有 `vAppTask` 中；独立 SensorTask/DisplayTask、更多执行器联动、Web 仪表盘和 MQTT 仍待实现。

---

## 一、硬件总接线图（面包板）

### 引脚分配表

| STM32 引脚 | 外设功能 | 连接模块 | 模块引脚 | 备注 |
|------------|----------|----------|----------|------|
| **I2C1 总线（当前 100kHz — 4 地址）** |||||
| PB6 | I2C1_SCL | AHT20+BMP280 / BH1750 / SSD1306 | SCL | 温湿度气压+光照+显示 |
| PB7 | I2C1_SDA | 同上 | SDA | 地址: 0x38/0x76(或0x77)/0x23/0x3C |
| **SPI2（当前 16MHz，GMT020-02 ST7789）** |||||
| PB13 | SPI2_SCK | 2.0-inch TFT | SCL | SPI 时钟 |
| PB15 | SPI2_MOSI | 2.0-inch TFT | SDA | 单向像素/命令数据 |
| PB12 | GPIO output | 2.0-inch TFT | CS | 低电平选中 |
| PB14 | GPIO output | 2.0-inch TFT | DC | 命令/数据选择 |
| PA8 | GPIO output | 2.0-inch TFT | RST | 屏幕硬件复位 |
| **GPIO bit-bang（WS2812B 单线编码）** |||||
| PB5 | GPIO output | 15× WS2812B | DIN | 64MHz 时序，DOUT 实机验证 |
| **I2C2 总线（后续规划 400kHz）** |||||
| PB10 | I2C2_SCL | MPU6050 / VL53L0X | SCL | IMU+ToF |
| PB11 | I2C2_SDA | 同上 | SDA | 地址: 0x68/0x29 |

> 为什么分两条总线：(1) 6 个模块挂在一条总线上电容过大，400kHz 可能不稳；(2) 简历上两条 I2C 总线比一条更有说服力；(3) 慢速组 100kHz，快速组 400kHz，各取所需。两组地址无冲突。
| **USART1（ESP32 通信，当前接线）** |||||
| PA9 | USART1_TX | ESP32 | GPIO16 (RX) | STM32→ESP32 |
| PA10 | USART1_RX | ESP32 | GPIO17 (TX) | ESP32→STM32 |
| **SWD（程序烧录/调试）** |||||
| PA13 | SWDIO | ST-Link | SWDIO | SWD 数据 |
| PA14 | SWCLK | ST-Link | SWCLK | SWD 时钟 |
| **1-Wire** |||||
| PA0 | GPIO | DHT11 | DATA | 单总线，bit-banging |
| **Timer 2 Encoder Mode** |||||
| PA0 | TIM2_CH1 | 旋转编码器 | A | 硬件正交解码（PA0 冲突，换 PA6）|
| PA1 | TIM2_CH2 | 旋转编码器 | B | |

> ⚠️ DHT11 和编码器都想要 PA0，冲突。改：DHT11 → PA4，编码器 → PA6( TIM3_CH1) + PA7(TIM3_CH2)

**修正后：**

| STM32 引脚 | 功能 | 连接模块 | 模块引脚 |
|------------|------|----------|----------|
| **1-Wire（修正）** ||||
| PA4 | GPIO bit-bang | DHT11 | DATA |
| **EC11 旋转编码器（当前 GPIO 双边沿实现）** ||||
| PA6 | GPIO EXTI6 | 旋转编码器 | A (DT) |
| PA7 | GPIO EXTI7 | 旋转编码器 | B (CLK) |
| PA1 | GPIO input | 独立确认按键 | KEY |
| 3.3V/GND | 电源 | 编码器 | VCC/GND |
| **TIM2 CH1 PWM — SG90 舵机** ||||
| PA0 | TIM2_CH1 | SG90 | 信号线(橙) |
| 5V/GND | 电源 | SG90 | 红/棕 |
| **SPI2 TFT（当前接线）** ||||
| PB13/PB15 | SPI2 SCK/MOSI | GMT020-02 | SCL/SDA |
| PB12/PB14/PA8 | GPIO output | GMT020-02 | CS/DC/RST |
| **GPIO 中断输入** ||||
| PB0 | GPIO input | HC-SR501 PIR | OUT | 当前轮询 HIGH/LOW，含 30 秒预热 |
| PB10 | GPIO_EXTI10 | 对射式红外 | OUT | 上升/下降沿，门窗检测 |
| **GPIO 数字输出（当前接线）** ||||
| PA2/PA3 | GPIO OUT | 两路继电器 | IN1/IN2 | 低电平触发 |
| PB1 | GPIO OUT | 有源蜂鸣器 | IN | 低电平触发 |
| TBD | GPIO OUT | 雾化片 | EN | PB12-PB15 已被 TFT 占用 |
| **ADC 输入** ||||
| PA5 | ADC12_IN5 | MQ-2 烟雾传感器 | AO | 模拟量 |
| **TIM4 CH1 Input Capture — HC-SR04 超声波** ||||
| PB8 | TIM4_CH3 | HC-SR04 | ECHO | 输入捕获 |
| PB9 | GPIO OUT | HC-SR04 | TRIG | 触发脉冲 |
| **I2C 地址汇总（6 设备，两条总线，无冲突）** ||||
| I2C1 | PB6/PB7 | AHT20+BMP280 | 0x38 + 0x76/0x77 | 温湿度+气压组合板 |
| I2C1 | PB6/PB7 | BH1750 | 0x23 | 光照 |
| I2C1 | PB6/PB7 | SSD1306 OLED | 0x3C | 128×64 显示 |
| SPI2 | PB13/PB15 | GMT020-02 TFT | CS=PB12 | 240×320 ST7789 竖屏彩色显示 |
| GPIO bit-bang | PB5 | 15× WS2812B | DIN | BH1750 反向联动白光照明 |
| I2C2 | PB10/PB11 | MPU6050 | 0x68 | 6轴IMU |
| I2C2 | PB10/PB11 | VL53L0X ToF | 0x29 | 激光测距 |

### 电源分配

```
Blue Pill 5V ──┬── HC-SR04 (5V)
               ├── SG90 舵机 (5V)
               ├── MQ-2 (5V 加热)
               ├── 继电器 VCC (5V)
               ├── 雾化片驱动 (5V)
               └── 面包板 5V 轨

Blue Pill 3.3V ─┬── 面包板 3.3V 轨 ──┬── AHT20+BMP280 / BH1750
                 │                    ├── SSD1306 OLED
                 │                    ├── MPU6050
                 │                    ├── DHT11
                 │                    ├── 编码器
                 │                    ├── PIR HC-SR501
                 │                    ├── 对射红外
                 │                    └── VL53L0X

GND ──── 面包板 GND 轨 ──── 所有模块 GND（共地）

外部稳压 5V ── 1N4001 ── WS2812B +5V
PB5 ── 220~470Ω ── WS2812B DIN
```

WS2812B 的 1N4001 串在正电源线上：二极管无条纹端接外部 5V，带白色条纹端接灯带 `+5V`，把灯带供电降到约 4.2V，便于 3.3V DATA 达到高电平门限。不要把二极管串在 DATA 上。灯带与 STM32 必须共地，建议灯带入口并联 470~1000µF 电解电容。

### I2C 总线接线示意（目标架构）

当前面包板只启用 I2C1，AHT20+BMP280 组合板、BH1750 和 SSD1306 并联在 PB6/PB7；I2C2 留给后续 MPU6050/VL53L0X。

```
        I2C1 (当前 100kHz)                         I2C2 (后续 400kHz)
        PB6(SCL) ───┬────────────┬────────┐         PB10(SCL) ───┬────────┐
                    │            │        │                      │        │
        PB7(SDA) ───┼────────────┼────────┼─4.7kΩ  PB11(SDA) ───┼────────┼─4.7kΩ
                    │            │        │                      │        │
              AHT20+BMP280    BH1750   SSD1306               MPU6050  VL53L0X
              (0x38+0x76/77)  (0x23)   (0x3C)                (0x68)   (0x29)
```

> **上拉电阻注意**：每个模块可能自带 4.7kΩ 上拉，6 个模块并联后等效上拉 < 1kΩ，会拉不上去。如果 I2C 通信不稳定，把多余的模块上拉电阻拆掉，每条总线只保留一对 4.7kΩ。

---

## 二、系统架构全景图

```
┌──────────────────────────────────────────────────────────────────┐
│                      控制终端层                                   │
│                                                                  │
│  ┌───────────────┐  ┌───────────────┐  ┌──────────────────┐    │
│  │  Web 仪表盘   │  │ 手机蓝牙 APP  │  │  Python 桌面端   │    │
│  │ (ESP32 提供)  │  │ (SPP 串口助手)│  │ (ota_sender.py   │    │
│  │               │  │               │  │  + 控制面板)     │    │
│  │ 实时数据+控制  │  │ 命令+OTA固件  │  │ 调试+固件+控制   │    │
│  └───────┬───────┘  └───────┬───────┘  └────────┬─────────┘    │
│          │ WebSocket        │ BT SPP              │ UART        │
│          │ + HTTP REST      │                     │             │
└──────────┼──────────────────┼─────────────────────┼─────────────┘
           │                  │                     │
┌──────────┴──────────────────┴─────────────────────┴─────────────┐
│                       ESP32 通信桥                               │
│                                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │WebSocket │ │ HTTP     │ │ BT SPP   │ │ MQTT Client      │   │
│  │Server    │ │REST API  │ │Server    │ │ (阿里云IoT/EMQX) │   │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────────┬─────────┘   │
│       └─────────────┴────────────┴───────────────┘             │
│                          │                                       │
│               ┌──────────┴──────────┐                           │
│               │   协议转换 & 路由     │                           │
│               │  JSON ↔ UART Frame   │                           │
│               └──────────┬──────────┘                           │
│                          │ UART 自定义帧协议                      │
└──────────────────────────┼──────────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────────┐
│                    STM32 Blue Pill (FreeRTOS)                    │
│                                                                  │
│  ┌─────────────────── FreeRTOS 任务 ───────────────────────┐   │
│  │                                                          │   │
│  │  SensorTask (P=3, 768w)  ← 1秒定时采集                   │   │
│  │  ├─ I2C 轮询: AHT20, BMP280, BH1750, MPU6050, VL53L0X  │   │
│  │  ├─ 1-Wire: DHT11                                        │   │
│  │  └─ ADC: MQ-2 烟雾                                       │   │
│  │                                                          │   │
│  │  EventTask (P=4, 256w)   ← ISR 驱动                      │   │
│  │  ├─ PIR EXTI → TaskNotify → 人员活动标记                 │   │
│  │  └─ 对射红外 EXTI → 门窗开关标记                         │   │
│  │                                                          │   │
│  │  ControlTask (P=3, 512w) ← 200ms 周期                    │   │
│  │  ├─ 湿度 < 40% → 雾化片 ON / 继电器1                     │   │
│  │  ├─ 温度 > 30°C → 继电器2 ON（风扇）                     │   │
│  │  ├─ 烟雾超阈值 → 蜂鸣器告警                              │   │
│  │  ├─ PIR 无人 5min → 自动关闭所有执行器                   │   │
│  │  └─ 旋转编码器 → 菜单选择/模式切换                       │   │
│  │                                                          │   │
│  │  DisplayTask (P=1, 256w) ← 1s 刷新                       │   │
│  │  └─ SSD1306 轮显: [温湿度] → [气压光照] → [PM状态] →    │   │
│  │     [系统信息] → 循环                                     │   │
│  │                                                          │   │
│  │  CommTask (P=3, 768w)  ← 已有，扩展传感器数据上报        │   │
│  │  └─ USART1 ↔ ESP32: JSON 传感器数据 + 命令分发          │   │
│  │                                                          │   │
│  │  MonitorTask (P=1, 256w) ← 已有                          │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌── Bootloader (0x08000000, 8KB) ─────────────────────────┐   │
│  │  OTA 状态机 | Flash 擦写(.ramfunc) | CRC-32 | 应用跳转   │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

---

## 三、数据流

```
传感器原始数据 (STM32)
  │
  ├─→ 本地 OLED 显示（每秒刷新）
  │
  ├─→ ControlTask 阈值判断
  │     └─→ 继电器/蜂鸣器/舵机/雾化片 动作
  │
  └─→ CommTask JSON 打包
        │
        └─→ UART → ESP32
                    │
                    ├─→ WebSocket → Web 仪表盘（实时推送）
                    ├─→ MQTT Publish → 云平台（持久化）
                    ├─→ BT SPP → 手机 APP（按需查询）
                    └─→ HTTP REST → GET /api/sensors（轮询）


控制指令 (上层→底层)
  │
  用户 → Web按钮 / 蓝牙命令 / MQTT / Python
        │
        └─→ ESP32 路由 → UART CMD Frame → STM32 CommTask
                                            │
                                            └─→ ControlTask 执行动作
```

---

## 四、上层控制接口设计

### 4.1 Web 仪表盘（ESP32 直接提供）

当前已实现并实机验证的是独立的手机 Web OTA：ESP32 常驻 SoftAP `STM32-OTA-Bridge`，浏览器访问 `http://192.168.4.1`，页面从固件只读段提供，SPIFFS 专门用于暂存 STM32 Application。接口为 `GET /api/status`、`POST /api/upload?version=<N>`、`POST /api/start`。

下面的传感器仪表盘、WebSocket 和控制接口仍是 Phase 4 规划，不应理解为已经实现：

```
ESP32 启动后：
  - HTTP Server (port 80) → 提供 index.html
  - WebSocket Server (port 81) → 实时 JSON push
  - REST API:
      GET  /api/sensors     → 最新传感器数据 JSON
      POST /api/control     → {"relay1": true, "relay2": false}
      GET  /api/ota/status  → OTA 状态
```

**Web 页面功能**：
- 传感器仪表盘（温度表、湿度环、光照、气压）
- 实时曲线（用 Chart.js，最近 5 分钟历史）
- 开关控制按钮（继电器1/2、雾化片、蜂鸣器静音）
- 模式切换（手动/自动）
- OTA 固件上传按钮
- 暗色主题，响应式布局（手机上也能看）

技术栈：纯 HTML + CSS + JS（一个 index.html，约 500 行），Chart.js CDN。

### 4.2 手机端

**方案 A（当前已实现）**：手机连接 `STM32-OTA-Bridge` 热点并打开 `http://192.168.4.1`，可完成 STM32 Application OTA。传感器仪表盘仍待 Phase 4。

**方案 B（蓝牙）**：手机装一个 "Serial Bluetooth Terminal" APP，连接 ESP32 蓝牙 SPP，发文本命令：
```
STATUS          → 返回 JSON 传感器数据
RELAY1 ON       → 开继电器1
RELAY2 OFF      → 关继电器2
MIST ON         → 开雾化
BUZZER OFF      → 关蜂鸣器
AUTO            → 自动模式
MANUAL          → 手动模式
```

**方案 C（原生 APP，加分项）**：React Native 或 Flutter 写一个极简 APP（3 个页面：仪表盘、控制面板、OTA）。这个可以作为后续扩展，不是必选项。

### 4.3 Python 桌面控制面板

在现有 `tools/ota_sender.py` 基础上，加一个 `control_panel.py`：

```
功能：
  - 串口直连 STM32 UART（不经过 ESP32）
  - 实时传感器数据显示（tkinter 或 纯 terminal curses）
  - 继电器/蜂鸣器开关控制
  - OTA 固件上传（已有）
  - CSV 数据记录（每 1s 一行）
```

---

## 五、STM32 FreeRTOS 任务通信图

```
                    ┌─────────────┐
    PIR EXTI ──────→│ EventTask   │──TaskNotify──┐
    红外 EXTI ──────→│ (事件检测)   │              │
                    └─────────────┘              │
                                                 ▼
┌──────────────┐   xQueue   ┌──────────────┐  ┌──────────────┐
│ SensorTask   │───────────→│ ControlTask  │  │  CommTask    │
│ (1s 定时采集) │            │ (200ms 阈值) │  │  (ESP32通信)  │
│              │            │              │  │              │
│ I2C Mutex ◄──┼────────────┼──────────────┤  │ USART1 ↔ JSON│
│ 保护5个设备   │            │ 执行器:       │  │              │
└──────┬───────┘            │ 继电器/蜂鸣器 │  └──────┬───────┘
       │                    │ 舵机/雾化片   │         │
       │                    └──────┬───────┘         │
       ▼                           │                 │
┌──────────────┐                   │                 │
│ DisplayTask  │                   │                 │
│ (1s OLED刷新)│                   │                 │
└──────────────┘                   │                 │
                                   ▼                 │
                          ┌──────────────┐          │
   旋转编码器 ──────────→│ 菜单输入处理  │          │
  (TIM3 Encoder)        │ (模式/阈值)   │          │
                          └──────────────┘          │
                                                    │
                          I2C1 总线                  │
                    ┌──────┴──────┐                 │
                    │  Mutex      │                 │
                    └──────┬──────┘                 │
           ┌───────────────┼───────────────┐        │
           │               │               │        │
        SensorTask    DisplayTask      其他任务     │
        (读传感器)     (写OLED)       (读MPU6050)   │
```

---

## 六、目录结构（扩展后）

```
bluepill-ota/
├── shared/                       # 共享协议（已有）
│   ├── protocol.h
│   ├── protocol.c
│   ├── ota_config.h
│   └── ota_config.c
│
├── bootloader/                   # Bootloader（已有）
│   └── ...
│
├── application/                  # STM32 应用（扩展）
│   ├── Core/
│   │   ├── Inc/
│   │   │   ├── app_tasks.h       # 任务定义（已有，需更新优先级）
│   │   │   ├── uart_comm.h       # UART 驱动（已有）
│   │   │   ├── cmd_handler.h     # 命令处理（已有，扩展传感器命令）
│   │   │   ├── sensors/          # 🆕 传感器驱动层
│   │   │   │   ├── sensor_bus.h  # I2C 总线管理 + Mutex
│   │   │   │   ├── aht20.h       # AHT20 驱动
│   │   │   │   ├── bmp280.h      # BMP280 驱动
│   │   │   │   ├── bh1750.h      # BH1750 驱动
│   │   │   │   ├── mpu6050.h     # MPU6050 驱动
│   │   │   │   ├── vl53l0x.h     # VL53L0X ToF 驱动
│   │   │   │   ├── dht11.h       # DHT11 1-Wire 驱动
│   │   │   │   └── mq2.h         # MQ-2 ADC 驱动
│   │   │   ├── actuators/        # 🆕 执行器层
│   │   │   │   ├── relay.h       # 继电器控制
│   │   │   │   ├── buzzer.h      # 蜂鸣器控制
│   │   │   │   ├── servo.h       # 舵机 PWM 控制
│   │   │   │   └── mist.h        # 雾化片控制
│   │   │   ├── inputs/           # 🆕 输入设备层
│   │   │   │   ├── rotary.h      # 旋转编码器（Timer Encoder）
│   │   │   │   ├── pir.h         # PIR 中断管理
│   │   │   │   ├── beam_ir.h     # 对射红外中断管理
│   │   │   │   └── hcsr04.h      # HC-SR04 超声波（Timer Capture）
│   │   │   └── display/          # 🆕 显示层
│   │   │       └── oled_ui.h     # SSD1306 界面管理
│   │   └── Src/
│   │       ├── main.c            # 入口（已有，扩展任务创建）
│   │       ├── uart_comm.c       # UART 驱动（已有）
│   │       ├── sensors/          # 🆕 驱动实现
│   │       │   ├── sensor_bus.c
│   │       │   ├── aht20.c
│   │       │   ├── bmp280.c
│   │       │   ├── bh1750.c
│   │       │   ├── mpu6050.c
│   │       │   ├── vl53l0x.c
│   │       │   ├── dht11.c
│   │       │   └── mq2.c
│   │       ├── actuators/        # 🆕 执行器实现
│   │       │   ├── relay.c
│   │       │   ├── buzzer.c
│   │       │   ├── servo.c
│   │       │   └── mist.c
│   │       ├── inputs/           # 🆕 输入设备实现
│   │       │   ├── rotary.c
│   │       │   ├── pir.c
│   │       │   ├── beam_ir.c
│   │       │   └── hcsr04.c
│   │       └── display/          # 🆕 显示实现
│   │           └── oled_ui.c
│   ├── application.ld            # 链接脚本（已有）
│   └── FreeRTOSConfig.h          # FreeRTOS 配置（已有）
│
├── esp32-comm-bridge/            # ESP32 固件（扩展）
│   ├── src/
│   │   ├── main.cpp              # 入口（已有，扩展 Web/WebSocket）
│   │   ├── bt_spp.cpp            # 蓝牙 SPP（已有）
│   │   ├── wifi_ota.cpp          # WiFi OTA 下载（已有）
│   │   ├── stm32_protocol.cpp    # STM32 协议（已有）
│   │   ├── websocket_server.cpp  # 🆕 WebSocket 实时推送
│   │   ├── http_server.cpp       # 🆕 HTTP REST API + Web 页面
│   │   ├── mqtt_client.cpp       # 🆕 MQTT 客户端
│   │   └── config_manager.cpp    # 🆕 NVS 配置管理
│   ├── data/                     # 🆕 SPIFFS Web 文件
│   │   └── index.html            # Web 仪表盘（单页）
│   └── platformio.ini            # 配置（需更新）
│
├── web-dashboard/                # 🆕 Web 仪表盘源文件
│   └── index.html                # 独立的 Web 页面（可脱离 ESP32 开发）
│
├── tools/                        # PC 工具（扩展）
│   ├── ota_sender.py             # OTA 发送（已有）
│   └── control_panel.py          # 🆕 Python 桌面控制面板
│
└── docs/
    └── architecture.md           # 架构文档
```

---

## 七、协议扩展

UART 协议新增传感器数据上报命令：

```c
#define CMD_SENSOR_DATA      0x21  // STM32→ESP32: JSON 传感器数据
#define CMD_CONTROL_CMD      0x22  // ESP32→STM32: 控制指令
#define CMD_CONTROL_ACK      0x86  // STM32→ESP32: 控制确认
```

STM32 每 1 秒发送一次：
```json
{"t":26.3,"h":58.2,"p":1013.2,"lx":342,"smoke":120,"ax":0.1,"ay":0.0,"az":1.0,"dist":45,"pir":1,"door":0,"relay1":0,"relay2":1,"mist":1,"buzz":0,"fw":3}
```

ESP32→STM32 控制指令：
```json
{"relay1":1,"relay2":0,"mist":1,"mode":"auto","thresh_temp":30}
```

---

## 八、简历关键词增量

在已有的 BOOTLOADER / FreeRTOS / OTA / UART 协议 基础上，传感器扩展新增：

| 类别 | 关键词 |
|------|--------|
| **外设协议** | I2C 多设备总线、1-Wire bit-banging、Timer Encoder 硬件解码、Timer Input Capture、PWM、ADC |
| **传感器** | AHT20, BMP280, BH1750, MPU6050 6轴, VL53L0X ToF, DHT11, MQ-2, HC-SR04 |
| **系统设计** | 6+ 任务架构、ISR→Task 异步投递、Mutex 总线保护、阈值联动控制 |
| **通信** | MQTT IoT 云接入、WebSocket 实时推送、HTTP REST API、蓝牙 SPP |
| **上位机** | ESP32 内置 Web 仪表盘（SPA）、Python 桌面控制面板、蓝牙文本命令 |
| **闭环控制** | 传感器→决策→执行器→反馈 完整控制闭环 |

---

## 九、实现顺序

| 阶段 | 内容 | 预计 |
|------|------|------|
| **Phase 0** | 面包板接线，I2C 总线扫描，确认第一批设备地址 | 已完成 |
| **Phase 1** | STM32 当前批次传感器（AHT20/BMP280/BH1750/PIR） | 暂告一段落 |
| **Phase 2** | 继电器/蜂鸣器已完成；加湿器联动等待模块到货 | 等待硬件 |
| **Phase 3** | FreeRTOS 任务整合 + 联动控制逻辑（湿度继电器、光照灯带已完成） | 进行中 |
| **Phase 4** | ESP32 WebSocket + HTTP + Web 仪表盘 | 下一阶段 |
| **Phase 5** | ESP32 MQTT 上云 | 代码 |
| **Phase 6** | Python 桌面控制面板 | 代码 |
| **Phase 7** | 整体联调 + OTA 验证 | 联调 |

---

## 十、电源注意事项

- **5V 设备**（HC-SR04, SG90, MQ-2, 继电器, 雾化片），VCC 接 Blue Pill 的 **5V** 引脚或外部 5V 电源
- 所有 **信号线** 电平必须 ≤ 3.3V（Blue Pill GPIO 不耐 5V！）
- MQ-2 模拟输出最大 5V → 用电阻分压（10k+20k）降到 3.3V 再进 ADC
- HC-SR501 PIR 输出最大 3.3V → 直连安全
- 继电器模块光耦隔离 → IN 信号 3.3V 安全
- 总电流估算：MQ-2(~150mA) + SG90(~200mA) + 继电器×2(~150mA) + 雾化片(~300mA) ≈ 800mA。Blue Pill USB 供电最大 500mA，如果全部同时工作，需要**外接 5V 2A 电源**到面包板

---

## 十一、几个实战注意事项

### 1. 不用浮点数（STM32F103 没有 FPU）

所有传感器数据在 STM32 侧用**定点数**表示，温度用 `×10` 整数（如 263 = 26.3°C），气压用 `Pa`，距离用 `mm`。JSON 转换在 ESP32 侧做（有 FPU）。

### 2. VL53L0X 驱动太大（约 20KB+）

ST 官方驱动非常庞大。在 54KB Flash 里放不下。两种方案：
- 用社区精简版 API（只测距，不用校准/ROI 等高级功能）→ 约 3-5KB
- 通过编译宏 `#ifdef ENABLE_VL53L0X` 做成可选模块

### 3. 传感器驱动抽象层

所有传感器驱动用统一接口：

```c
typedef struct {
    const char *name;
    bool (*init)(void);
    bool (*read)(void *out);
    uint32_t period_ms;
} Sensor_t;
```

SensorTask 里用一张注册表轮询，新增传感器只需加一行。面试的时候可以展开聊"表驱动设计模式"。

### 4. I2C 上拉电阻问题

每个模块可能自带 4.7kΩ 上拉电阻。6 个模块全部并上去后等效上拉 < 1kΩ，I2C 信号拉不上去。如果通信不稳定，把多余的模块上拉电阻拆掉（用烙铁或小刀断开模块上的上拉电阻焊点），每条总线只保留一对 4.7kΩ。

### 5. HC-SR04 ECHO 是 5V 电平

ECHO 输出 5V 电平，直连 STM32 的 3.3V 引脚会烧。需要串联 1kΩ + 对地 2kΩ 分压（分压后 3.3V）再进 TIM4_CH3。
