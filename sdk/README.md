# CH5xx / CH32V003 多芯片 SDK

支持 CH572 / CH592 / CH591 / CH32V003 的轻量级 GCC SDK，共享公共代码，芯片差异隔离。

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

> CH32V003 专属目录 `CH32V003_DEV/`（`core/ch32v003hw.h` + `lib/libgcc.a`）结构类似，
> 无 `ble/` / `freertos/` 子目录（V003 无 BLE/USB/FreeRTOS）。

---

## 芯片选择

修改项目根的 `sdk_config.cmake`：

```cmake
set(CHIP_SDK "CH572")    # 可选：CH572 / CH592 / CH591 / CH32V003
```

或命令行覆盖：

```bash
cmake -B build -DCHIP_SDK=CH592 ...
```

---

## 芯片差异对照

| 项目 | CH572 | CH591 | CH592 | CH32V003 |
|---|---|---|---|---|
| 内核 | RV32IMAC (V3C) | RV32IMAC (V4C) | RV32IMAC (V4C) | **RV32EC (V2A)** |
| SRAM | 12 KB | **26 KB** | **26 KB** | 2 KB |
| Flash | 240 KB | 192 KB | 448 KB | 16 KB |
| EEPROM | — | 32 KB | 32 KB | — |
| BLE | 5.0 | 5.4 | 5.4 | — |
| USB | Device | Host/Device | Host/Device | — |
| UART | 1 | 4 | 4 | 1 |
| GPIO | 12 | 24 | 24 | 18 |
| `TARGET_MCU_LD` | 10 | 9 | 9 | 0 |
| `MCU_PACKAGE` | 2 | 1 | 2 | 1 |
| 架构 | `rv32imac/ilp32` | `rv32imac/ilp32` | `rv32imac/ilp32` | `rv32ec/ilp32e` |
| BLE 库 | `libCH572BLE_PERI.a` | `libCH59xBLE.a` | `libCH59xBLE.a` | — |

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
2. 在 `sdk_config.cmake` 加入新选项
3. 在 `sdk/CMakeLists.txt` 加入目录映射
4. 在 `sdk/common/cmake/sdk.cmake` 加入 `TARGET_MCU_LD` / `MCU_PACKAGE` / 架构分支
5. 在 `ch32fun.ld` 确认该芯片的 RAM/Flash 配置存在
6. 若芯片架构特殊（如 CH32V003 的 `rv32ec`），在 `sdk.cmake` 中设置对应 `MARCH`/`MABI`，
   并按需提供专用 `libgcc.a`

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
