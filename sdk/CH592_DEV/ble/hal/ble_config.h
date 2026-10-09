/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_config.h
 * Description        : CH592/CH591 Lite SDK - BLE 配置
 *
 * 本头文件定义 BLE 库所需的配置宏，等价于官方 HAL/include/CONFIG.h，
 * 但去掉了对官方 StdPeriphDriver 的依赖。
 *
 * 使用方式：
 *   1. 在应用工程里定义 BLE_MEMHEAP_SIZE / BLE_BUFF_MAX_LEN 等（可选，有默认值）
 *   2. 定义 MEM_BUF 数组（BLE 库的内存堆）
 *   3. 调用 ble_init() 完成初始化
 *******************************************************************************/

#ifndef __BLE_CONFIG_H
#define __BLE_CONFIG_H

#include "ch32fun.h"

/* ===========================================================================
 * 芯片标识
 * =========================================================================== */
#define CHIP_ID                 ID_CH592

/* ===========================================================================
 * 引入官方 BLE 库头文件
 * ===========================================================================
 * 注意：CH59xBLE_LIB.h 内部会包含 CH59x_common.h（官方），
 * 因此包含本头文件的 .c 文件不应再直接包含 ch5xxhw.h 的寄存器宏，
 * 以避免重定义。实际使用中，BLE 应用代码只包含本头文件即可。
 */
#include "CH59xBLE_LIB.h"

/* ===========================================================================
 * 芯片级常量与 IRQ 别名（ch32fun 与官方命名差异的桥接）
 * ===========================================================================
 * ch32fun 的 ch5xxhw.h 把 BLE 中断命名为 LLE_IRQn(BLEL,21)/BB_IRQn(BLEB,20)，
 * 官方 CH592SFR.h 命名为 BLEL_IRQn/BLEH_IRQn。这里提供别名，供 ble_hal.c
 * 使用官方名称。
 */
#ifndef FLASH_ROM_MAX_SIZE
#define FLASH_ROM_MAX_SIZE      0x070000    /* CH592 Flash 448KB */
#endif

/* HAL 任务事件（对应官方 HAL/include/HAL.h） */
#ifndef HAL_KEY_EVENT
#define HAL_KEY_EVENT           0x0002
#endif
#ifndef HAL_REG_INIT_EVENT
#define HAL_REG_INIT_EVENT      0x2000      /* 定时校准事件 */
#endif
#ifndef HAL_TEST_EVENT
#define HAL_TEST_EVENT          0x4000
#endif

/* ===========================================================================
 * 默认配置（可被应用覆盖）
 * =========================================================================== */

/* MAC 地址：FALSE = 使用芯片出厂 MAC */
#ifndef BLE_MAC
#define BLE_MAC                 FALSE
#endif

/* DCDC：默认关闭 */
#ifndef DCDC_ENABLE
#define DCDC_ENABLE             FALSE
#endif

/* 睡眠管理：默认关闭 */
#ifndef HAL_SLEEP
#define HAL_SLEEP               FALSE
#endif

#ifndef SLEEP_RTC_MIN_TIME
#define SLEEP_RTC_MIN_TIME      US_TO_RTC(1000)
#endif
#ifndef SLEEP_RTC_MAX_TIME
#define SLEEP_RTC_MAX_TIME      (RTC_MAX_COUNT - 1000 * 1000 * 30)
#endif
#ifndef WAKE_UP_RTC_MAX_TIME
#define WAKE_UP_RTC_MAX_TIME    US_TO_RTC(1600)
#endif

/* LED / KEY：默认关闭 */
#ifndef HAL_KEY
#define HAL_KEY                 FALSE
#endif
#ifndef HAL_LED
#define HAL_LED                 FALSE
#endif

/* 温度校准（CH59x 内置温度传感器，用于 RF 温漂补偿） */
#ifndef TEM_SAMPLE
#define TEM_SAMPLE              TRUE
#endif

/* 定时校准 */
#ifndef BLE_CALIBRATION_ENABLE
#define BLE_CALIBRATION_ENABLE  TRUE
#endif
#ifndef BLE_CALIBRATION_PERIOD
#define BLE_CALIBRATION_PERIOD  120000
#endif

/* SNV（配对信息存储）：默认开启，使用 Data-Flash(EEPROM) */
#ifndef BLE_SNV
#define BLE_SNV                 TRUE
#endif
#ifndef BLE_SNV_ADDR
/* CH592: Flash 448KB(0x70000) -> SNV 放在 0x77000 之前
 * CH591: Flash 192KB(0x30000) -> 由 FLASH_ROM_MAX_SIZE 自动适配 */
#define BLE_SNV_ADDR            (0x77000 - FLASH_ROM_MAX_SIZE)
#endif
#ifndef BLE_SNV_BLOCK
#define BLE_SNV_BLOCK           256
#endif
#ifndef BLE_SNV_NUM
#define BLE_SNV_NUM             1
#endif

/* RTC 时钟源：0=外部32K, 1=内部32K(32000Hz), 2=内部32K(32768Hz) */
#ifndef CLK_OSC32K
#define CLK_OSC32K              1
#endif

/* 内存堆：CH592/CH591 RAM 为 26K，官方建议 >= 6K */
#ifndef BLE_MEMHEAP_SIZE
#define BLE_MEMHEAP_SIZE        (1024 * 6)
#endif

/* 数据缓冲 */
#ifndef BLE_BUFF_MAX_LEN
#define BLE_BUFF_MAX_LEN        27
#endif
#ifndef BLE_BUFF_NUM
#define BLE_BUFF_NUM            5
#endif
#ifndef BLE_TX_NUM_EVENT
#define BLE_TX_NUM_EVENT        1
#endif
#ifndef BLE_TX_POWER
#define BLE_TX_POWER            LL_TX_PWR_0_DBM
#endif

/* 多连接配置（CH59x 支持多连接） */
#ifndef PERIPHERAL_MAX_CONNECTION
#define PERIPHERAL_MAX_CONNECTION   1
#endif
#ifndef CENTRAL_MAX_CONNECTION
#define CENTRAL_MAX_CONNECTION      3
#endif

/* ===========================================================================
 * BLE 内存堆（必须在应用中定义，或使用本头文件的 extern 声明）
 * =========================================================================== */
extern uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#endif /* __BLE_CONFIG_H */
