# CH5xx / CH32 多芯片 SDK

支持 CH572 / CH585 / CH584 / CH592 / CH591 / CH32V003 / CH32X035 / CH32L103 /
CH32V203 的轻量级 GCC SDK，共享公共代码，芯片差异隔离。

---

## 目录结构

```
sdk/
├── CMakeLists.txt          # SDK 顶层（按 CHIP_SDK 选择芯片目录）
├── common/                 # ★ 所有芯片共享
│   ├── prebuilt/           #   RISC-V GCC 工具链（xPack 15.2.0）
│   ├── cmake/
│   │   ├── riscv.cmake     #   工具链配置
│   │   ├── sdk.cmake       #   构建逻辑（ch572_add_executable 等）
│   │   └── gen_linker_script.cmake
│   ├── core/               #   ch32fun.c / ch32fun.h（芯片无关）
│   └── extralibs/          #   fsusb（USB 设备栈）、flash、rtc 等
├── CH572_DEV/              # ★ CH572 专属
│   ├── core/
│   │   ├── ch5xxhw.h       #   CH57x/58x/59x 寄存器定义
│   │   └── ch32fun.ld      #   链接脚本（含所有芯片的 RAM 配置）
│   └── ble/
│       ├── lib/            #   libCH572BLE_PERI.a + CH572BLEPeri_LIB.h
│       ├── hal/            #   ble_shim.c / ble_hal.c / ble_config.h
│       └── vendor/inc/     #   CH572SFR.h / CH57x_*.h
└── CH592_DEV/              # ★ CH592/CH591 专属
    ├── core/
    │   ├── ch5xxhw.h
    │   └── ch32fun.ld
    ├── ble/
    │   ├── lib/            #   libCH59xBLE.a + CH59xBLE_LIB.h
    │   ├── hal/            #   ble_shim.c / ble_hal.c / ble_config.h
    │   └── vendor/inc/     #   CH592SFR.h / CH59x_*.h
    └── freertos/           #   FreeRTOS（CH592/CH591 专用）
        ├── kernel/         #   内核源码 + heap_4.c
        ├── port/           #   WCH 移植层（port.c / portASM.S / shim）
        ├── config/         #   FreeRTOSConfig.h
        ├── freertos.ld     #   链接脚本
        └── Startup_CH592_FreeRTOS.S
```

> `CH585_DEV/`（CH585/CH584）结构与 `CH592_DEV/` 类似，但使用
> `libCH58xBLE.a` + `libISP585.a`、`CH58x_*.h` 头文件，BLE 5.4（无 USBHS 的 CH584 去掉了 USBHS/SPI1）。
>
> CH32 专属目录 `CH32V003_DEV/`、`CH32X035_DEV/`、`CH32L103_DEV/`、`CH32V20x_DEV/`
> 只有 `core/`（寄存器头 + 链接脚本），无 `ble/` / `freertos/` 子目录。

---

## 芯片选择

修改项目根的 `default_target.txt`（推荐）或 `sdk_config.cmake`：

```cmake
set(CHIP_SDK "CH572")    # 见下方芯片差异对照表
```

或命令行覆盖：

```bash
cmake -B build -DCHIP_SDK=CH592 ...
```

---

## 芯片差异对照

> 📖 **完整的按型号（含封装后缀）参数对照见 [`docs/CHIP_COMPARISON.md`](../docs/CHIP_COMPARISON.md)**，
> 包含内核/存储、无线/USB、通信外设、定时器、模拟外设、人机接口、电源、封装、
> SDK 构建参数以及按需求的选型速查表。

### 系列级总览

| 系列 | 型号（后缀） | 内核 | RAM | Flash | BLE | USB | 定位 |
|---|---|---|---|---|---|---|---|
| **CH57x** | CH572D / Q / R | RV32IMBC (V3C) | 12 KB | 240 KB | 5.0 | FS | 极低成本 BLE 外设 |
| **CH59x** | CH592X / F / A / D | V4C | 26 KB | 448 KB | 5.4 | FS 主/从 | BLE + USB 主力，支持 FreeRTOS/LWIP |
| **CH59x** | CH591F / D / R | V4C | 26 KB | 192 KB | 5.4 | FS 主/从 | 同 CH592，Flash 减半需裁剪 |
| **CH58x** | CH585M / F / C / D | V4C | **128 KB** | 448 KB | 5.4 | FS + **HS** | 大 RAM，BLE + USB 复合应用 |
| **CH58x** | CH584M / X / F | V4C | **96 KB** | 448 / 480 KB | 5.4 | FS | 同 CH585，去掉 USBHS + SPI1 |
| **CH32** | CH32V003F4P6 / F4U6 / A4M6 / J4M6 | **RV32EC (V2A)** | 2 KB | 16 KB | — | — | 超低成本通用 MCU |
| **CH32** | CH32X035R8T6 / C8T6 / G8U6 / G8R6 / F8U6 / F7P6 / D8U6 | V4C | 20 KB | 62 KB | — | FS + PDUSB | USB PD / Type-C 快充 |
| **CH32** | CH32L103C8T6 / K8U6 / K8U7 / G8R6 / F8U6 / F8P6 | V4C | 20 KB | 64 KB | — | FS + PDUSB | 低功耗 + USB PD + CAN |
| **CH32** | CH32V203F6P6 … RBT6 | V4B | 10/20/64 KB | 32/64/128 KB | — | 双 FS | 高性能双 USB + CAN |

### 型号 → SDK 参数速查

| 型号 | `CHIP_SDK` | 芯片宏 | `TARGET_MCU_LD` | `MCU_PACKAGE` | 架构 |
|---|---|---|---|---|---|
| CH572D / Q / R | `CH572` | `CH57x` | 10 | 2 | `rv32imac` / `ilp32` |
| CH592X / F / A / D | `CH592X` … `CH592D` | `CH59x` | 9 | 2 | `rv32imac` / `ilp32` |
| CH591F / D / R | `CH591F` … `CH591R` | `CH59x` | 9 | 1 | `rv32imac` / `ilp32` |
| CH585M / F / C / D | `CH585M` … `CH585D` | `CH58x` | 12 | 5 | `rv32imac` / `ilp32` |
| CH584M / X / F | `CH584M` … `CH584F` | `CH58x` | 12 | 4 | `rv32imac` / `ilp32` |
| CH32V003 全系 | `CH32V003` | `CH32V003` | 0 | 1 | **`rv32ec` / `ilp32e`** |
| CH32X035 全系 | `CH32X035` | `CH32X03x` | 4 | 1 | `rv32imac` / `ilp32` |
| CH32L103 全系 | `CH32L103` | `CH32L103` | 13 | 1 | `rv32imac` / `ilp32` |
| CH32V203 全系 | `CH32V203` | `CH32V20x` | 2 | 1 | `rv32imac` / `ilp32` |

> 兼容别名：`CH592` ≡ `CH592X`，`CH591` ≡ `CH591F`，`CH585` ≡ `CH585F`，`CH584` ≡ `CH584F`。
>
> CH32X035 / CH32L103 / CH32V203 的多个封装变体在 SDK 中合并为一个系列编译，
> 链接脚本按该系列的最大 Flash/RAM 配置；小容量型号需在 `funconfig.h` 中自行裁剪。

---

## 芯片差异隔离点

| 位置 | 差异 | 处理方式 |
|---|---|---|
| `sdk_config.cmake` | 芯片选择 | 用户配置 |
| `sdk/CMakeLists.txt` | 选择 `CHxxx_DEV` 目录 | 自动 |
| `sdk/common/cmake/sdk.cmake` | `TARGET_MCU_LD` / `MCU_PACKAGE` / BLE 库名 | 按 `CHIP_SDK` 分支 |
| `sdk/CHxxx_DEV/core/` | 链接脚本、寄存器头 | 各自独立 |
| `sdk/CHxxx_DEV/ble/` | BLE 库 + HAL | 各自独立 |

---

## 公共代码（`common/`）

以下文件**所有芯片共用**，无需修改：

| 文件 | 说明 |
|---|---|
| `core/ch32fun.c/h` | 启动、时钟、GPIO、延时、printf |
| `extralibs/fsusb.c/h` | 全速 USB 设备栈（CDC/HID） |
| `extralibs/usb_defines.h` | USB 描述符定义 |
| `extralibs/ch5xx_flash.h` | Flash 读写 |
| `extralibs/ch5xx_lowpower.h` | 低功耗 |
| `extralibs/rtc.h` | RTC |
| `cmake/riscv.cmake` | 工具链配置 |
| `cmake/sdk.cmake` | 构建逻辑 |
| `prebuilt/` | RISC-V GCC 15.2.0 |

---

## 添加新芯片

1. 创建 `sdk/CHxxx_DEV/`，复制 `core/`（链接脚本 + 寄存器头）和 `ble/`（BLE 库 + HAL）
2. 在 `sdk_config.cmake` 加入新选项（含 `CHIP_IS_XXX` / `CHIP_HAS_BLE` / `CHIP_HAS_USB` 判定）
3. 在 `sdk/CMakeLists.txt` 加入目录映射
4. 在 `sdk/common/cmake/sdk.cmake` 加入 `TARGET_MCU_LD` / `MCU_PACKAGE` / 架构分支
5. 在 `ch32fun.ld` 确认该芯片的 RAM/Flash 配置存在（必要时新增 `TARGET_MCU_LD` 分支）
6. 若芯片架构特殊（如 CH32V003 的 `rv32ec`），在 `sdk.cmake` 中设置对应 `MARCH`/`MABI`，
   并按需提供专用 `libgcc.a`

---

## CH58x（CH585 / CH584）说明

| 项目 | 说明 |
|---|---|
| 芯片宏 | `-DCH58x=1`（同时定义 `-DCH5xx`） |
| `MCU_PACKAGE` | 5 = CH585（128K RAM）/ 4 = CH584（96K RAM） |
| BLE 库 | `libCH58xBLE.a` + `libISP585.a`（SNV 需要 EEPROM） |
| HAL 入口 | `CH58x_BLEInit()` / `HAL_Init()`（与 CH59x 接口一致） |
| 时钟 | 默认 `CLK_SOURCE_PLL_60MHz`；CH58x 不支持精确 60MHz 时会自动切到 62.4MHz |
| 特殊差异 | 32K 使能位在 `R8_CK32K_CONFIG`（CH59x 为 `R8_LSI_CONFIG`） |

---

## CH32 系列（X035 / L103 / V203）说明

| 项目 | 说明 |
|---|---|
| 芯片宏 | `-DCH32X03x` / `-DCH32L103` / `-DCH32V20x` |
| 架构 | 全部 `rv32imac/ilp32`（与 CH5xx 相同） |
| BLE | 不支持（`ch572_add_executable(... BLE)` 会报错） |
| USB | `fsusb` 已支持 X035 / L103 / V203 的 USB FS |
| 示例固件 | `main/blink_ch32/` |

---

## CH32V003 特别说明

CH32V003 与 CH5xx 差异较大，集成时注意：

| 项目 | 说明 |
|---|---|
| 架构 | `-march=rv32ec -mabi=ilp32e`（压缩指令 + 16 寄存器） |
| 芯片宏 | `-DCH32V003=1`（**无** `-DCH5xx`） |
| libgcc | 标准工具链 libgcc 不支持 `rv32ec/ilp32e`，使用 `sdk/CH32V003_DEV/lib/libgcc.a` |
| BLE / USB | 不支持（`ch572_add_executable(... BLE/USB)` 不可用） |
| 示例固件 | `main/blink_v003/` |

---

## FreeRTOS 支持（CH592 / CH591）

`ch572_add_executable(... FREERTOS)` 启用官方 WCH FreeRTOS 移植
（FreeRTOS-Kernel V11.1.0，Qingke V4C 内核）。

```cmake
ch572_add_executable(myapp
    myapp.c
    FREERTOS
)
```

`FREERTOS` 会自动：
- 加入内核（tasks/queue/list/timers/...）+ WCH 移植层 + 启动文件
- 使用 `sdk/CH592_DEV/freertos/freertos.ld`（`.vector` 段入 RAM）
- 定义 `FUNCONF_OVERRIDE_STARTUP=1`（禁用 ch32fun 启动代码）
- MARCH 提升为 `rv32imac_zicsr_zifencei`

> 详见 `sdk/CH592_DEV/freertos/README.md`。示例：`main/freertos_demo/`。

| 固件 | 芯片 | FLASH | RAM |
|---|---|---|---|
| `freertos_demo` | CH592 | 2.29% | 36.21% |
| `freertos_demo` | CH591 | 5.35% | 36.21% |

---

## 构建

```bash
# 配置（工具链路径）
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=sdk/common/cmake/riscv.cmake

# 编译
cmake --build build

# 烧录
cmake --build build --target flash
```

或使用项目根快捷脚本：`.\build`、`.\build flash`
