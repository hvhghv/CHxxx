# CH32X035 / CH32L103 / CH32V20x 精简 SDK

CH32 系列（青稞 V4 内核）专属目录，与 CH5xx 共享 `sdk/common/`。

## 芯片规格

| 目录 | 型号 | 内核 | Flash | RAM | USB | BLE | `TARGET_MCU_LD` |
|---|---|---|---|---|---|---|---|
| `CH32X035_DEV/` | CH32X035 / X033 | V4C `rv32imac` | 62 KB | 20 KB | FS + PDUSB | — | 4 |
| `CH32L103_DEV/` | CH32L103 / M103 | V4C `rv32imac` | 64 KB | 20 KB | FS + PDUSB | — | 13 |
| `CH32V20x_DEV/` | CH32V203 | V4B `rv32imac` | 32/64/128 KB | 10/20/64 KB | 双 FS | — | 2 |

## 目录结构

```
CH32X035_DEV/            CH32L103_DEV/            CH32V20x_DEV/
├── core/                ├── core/                ├── core/
│   ├── ch32x03xhw.h     │   ├── ch32l103hw.h     │   ├── ch32v20xhw.h
│   └── ch32fun.ld       │   └── ch32fun.ld       │   └── ch32fun.ld
└── (无 ble/)            └── (无 ble/)            └── lib/libgcc.a
```

## 芯片宏

| 芯片 | 宏 |
|---|---|
| CH32X035 | `-DCH32X03x=1` |
| CH32L103 | `-DCH32L103=1` |
| CH32V203 | `-DCH32V20x=1`（默认 `CH32V20x_D6`） |

## 说明

- 链接脚本直接取自 ch32fun 上游，已补充 `TARGET_MCU_LD == 13`（CH32L103）分支。
- CH32V203 的 32K Flash / 10K RAM 型号需自行在 `funconfig.h` 中裁剪；
  链接脚本默认按 64K Flash + 20K RAM 配置。
- 上述芯片均**不支持 BLE**；`ch572_add_executable(... BLE)` 会报错。
- USB（`fsusb`）已支持上述三款芯片的 USB FS 设备模式。
- 示例固件：`main/blink_ch32/`。
