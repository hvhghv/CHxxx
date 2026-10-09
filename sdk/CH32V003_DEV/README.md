# CH32V003 精简 SDK

CH32V003 专属目录（`sdk/CH32V003_DEV/`）。

## 芯片规格

| 项目 | 值 |
|---|---|
| 内核 | RV32EC（RISC-V2A，压缩指令 + 16 寄存器） |
| 架构 | `-march=rv32ec -mabi=ilp32e` |
| SRAM | 2 KB |
| Flash | 16 KB |
| GPIO | 18（PA/PC/PD） |
| 定时器 | TIM1 / TIM2 |
| 外设 | ADC、SPI、I2C、USART、OPA、CMP |
| BLE | 无 |
| USB | 无 |
| `TARGET_MCU_LD` | 0 |
| `MCU_PACKAGE` | 1（V003 固定） |
| 芯片宏 | `-DCH32V003=1` |

## 目录结构

```
CH32V003_DEV/
└── core/
    ├── ch32v003hw.h    # CH32V003 寄存器定义（来自 ch32fun 上游）
    └── ch32fun.ld      # 链接脚本（含所有芯片的 RAM/Flash 配置）
```

## 说明

- `ch32v003hw.h` 与 `ch32fun.ld` 直接取自 ch32fun 上游，未做修改。
- CH32V003 使用 `libgcc.a`（位于 `tmp/ch32fun/misc/`），因为标准工具链的
  libgcc 不支持 `rv32ec/ilp32e`。构建时由 `sdk.cmake` 自动加入。
- 本芯片**不支持** BLE / USB（`ch572_add_executable(... BLE/USB)` 不可用）。
