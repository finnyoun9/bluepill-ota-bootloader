# Logic-analyzer captures

项目里的所有逻辑分析仪文件统一放在这里，仓库根目录不再散放抓包。

```text
captures/
├── uart/                 # STM32 ↔ ESP32 UART 联调的 PulseView .sr 会话
└── ws2812/
    ├── evidence/         # 最终结论使用的两份关键 VCD
    └── intermediate/     # 接线、探头和 PB5 排障过程中的中间抓包
```

## WS2812B 关键证据

- `ws2812/evidence/spi-din-not-accepted.vcd`：旧 SPI1 4MHz / 5-bit
  编码在灯带 DIN 处的波形。数据可以解码，但第一颗灯珠没有输出有效
  DOUT，因此不能证明灯珠真正接收。
- `ws2812/evidence/bitbang-dout-confirmed.vcd`：最终 64MHz GPIO
  bit-bang 驱动在第一颗 DOUT 与第二颗 DIN 之间的波形。每帧 336 bit，
  等于后 14 颗灯珠的 `14 × 24 bit`，证明第一颗已接收并转发。

`ws2812/intermediate/` 只保留调试过程，不作为最终时序依据。用 PulseView 或
其他支持 VCD/sigrok session 的工具打开；关键 VCD 的数据通道是 D0。
