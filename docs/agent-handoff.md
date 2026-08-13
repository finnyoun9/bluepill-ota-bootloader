# Agent 交接：v1.0 收口

> 基线：2026-08-13。详细范围和简历边界见 [resume-roadmap.md](resume-roadmap.md)。

## 当前基线

- Bootloader 构建通过：RAM 2260 B / 20 KB，Flash 7040 B / 64 KB。
- Application 构建通过：RAM 18032 B / 20 KB（88.0%），Flash 38916 B / 64 KB（59.4%）。
- ESP32 构建通过：RAM 65184 B / 320 KB，Flash 1374725 B / 1835008 B。
- 新版 OLED/TFT、中文字体、返回键和 Web 灯带控制已进入工作区；最终实机回归尚未形成完整证据。
- 灯带使用 Relay 2 / PA3 / NO2；Relay 1 / PA2 当前空置。加湿器暂停。

## P0 任务

1. 烧录 STM32 Application 与 ESP32，回归 OLED 状态页、TFT 菜单、返回键、语言切换。
2. 回归灯带 Power、AUTO/MANUAL、手动亮度和 BH1750 自动调光；确认 Web 与 TFT 状态同步。
3. 打印各任务 `uxTaskGetStackHighWaterMark()` 和最小剩余 heap。Application RAM 已到 88%，先测量再缩栈。
4. 完成 20 次控制循环、3 次 OTA 和 24 h 稳定运行，保存串口日志和版本号。
5. 整理截图、接线图和 WS2812B 波形，提交后再打 `v1.0`。

## 构建基线

```powershell
pio run -e bluepill
pio run -e app
pio run -d esp32-comm-bridge
```

## 红线

- STM32 UART 是 RXNE 中断 + StreamBuffer，不写 DMA + IDLE。
- Bootloader 是单 Application 分区，不写 A/B 自动回滚。
- Web 使用 REST 轮询，不写 WebSocket 或历史数据已完成。
- 未烧录回归的新 UI 只能写“构建通过/待实机验证”。
