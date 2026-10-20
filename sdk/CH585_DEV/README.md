# CH585 / CH584 精简 SDK

CH585/CH584 专属目录（`sdk/CH585_DEV/`），对应数据手册 **V1.6** 选型表。

## 芯片规格

| 型号 | Flash（用户+引导+数据） | RAM | 封装 | GPIO | 备注 |
|---|---|---|---|---|---|
| CH585M | 448+24+32 KB | 128 KB | QFN48 | 40 | USB FS + HS |
| CH585F | 448+24+32 KB | 128 KB | QFN32 | 24 | |
| CH585C | 448+24+32 KB | 128 KB | QFN26C3 | 17 | |
| CH585D | 448+24+32 KB | 128 KB | QFN20 | 12 | |
| CH584M | 448+24+32 KB | 96 KB | QFN48 | 40 | 去 USBHS + SPI1 |
| CH584X | 480+24+512 KB | 96 KB | QFN48 | 40 | 大 Flash 版本 |
| CH584F | 448+24+32 KB | 96 KB | QFN32 | 24 | |

- 内核：青稞 V4C，`rv32imac`
- BLE 5.4，USB FS（CH585 另有 USBHS）
- 芯片宏：`-DCH58x=1` + `-DCH5xx`

## 目录结构

```
CH585_DEV/
├── core/
│   ├── ch5xxhw.h       # CH57x/58x/59x 寄存器定义（ch32fun 上游）
│   └── ch32fun.ld      # 链接脚本（TARGET_MCU_LD=12 分支）
└── ble/
    ├── lib/
    │   ├── libCH58xBLE.a       # 官方 BLE 5.4 协议栈
    │   ├── libISP585.a         # ROM ISP（FLASH_EEPROM_CMD 等）
    │   ├── CH58xBLE_LIB.h
    │   └── CH58xBLE_ROM.h
    ├── hal/
    │   ├── ble_config.h        # BLE 配置（CHIP_ID / SNV / 堆大小）
    │   ├── ble_types.h         # 官方类型补充
    │   ├── ble_shim.c          # StdPeriphDriver 等价实现
    │   └── ble_hal.c           # CH58x_BLEInit / HAL_Init / HAL_ProcessEvent
    └── vendor/inc/             # CH585SFR.h / CH58x_*.h / ISP585.h
```

## 与 CH59x 的关键差异

| 项目 | CH59x | CH58x |
|---|---|---|
| BLE 库 | `libCH59xBLE.a` | `libCH58xBLE.a` |
| ISP 库 | `libISP592.a` | `libISP585.a` |
| 芯片 ID 宏 | `ID_CH592` | `ID_CH585` / `ID_CH584` |
| 32K 使能位 | `R8_LSI_CONFIG` | `R8_CK32K_CONFIG` |
| USB 引脚使能 | `R16_PIN_ANALOG_IE` | `R16_PIN_CONFIG` |
| 时钟配置 | `R32_CLK_SYS_CFG` | `R16_CLK_SYS_CFG` |
| HAL 入口 | `CH59x_BLEInit()` | `CH58x_BLEInit()` |
| `TARGET_MCU_LD` | 9 | 12 |
| `MCU_PACKAGE` | 1/2 | 4（96K）/ 5（128K） |

## 使用

```cmake
ch572_add_executable(myapp
    myapp.c
    BLE
    USB
)
```

应用代码中按芯片选择 HAL 入口：

```c
#if defined(CH58x)
void CH58x_BLEInit(void);
#define BLE_LIB_INIT()   CH58x_BLEInit()
#endif
```

> 注意：CH584 去掉了 USBHS 和 SPI1，`libCH58xBLE.a` 同时覆盖 CH585/CH584。
