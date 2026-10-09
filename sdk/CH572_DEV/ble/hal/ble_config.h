/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_config.h
 * Description        : CH572 Lite SDK - BLE 配置
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
#define CHIP_ID                 ID_CH572

/* ===========================================================================
 * 引入官方 BLE 库头文件
 * ===========================================================================
 * 注意：CH572BLEPeri_LIB.h 内部会包含 CH57x_common.h（官方），
 * 因此包含本头文件的 .c 文件不应再直接包含 ch5xxhw.h 的寄存器宏，
 * 以避免重定义。实际使用中，BLE 应用代码只包含本头文件即可。
 */
#include "CH572BLEPeri_LIB.h"

/* ===========================================================================
 * 默认配置（可被应用覆盖）
 * =========================================================================== */

/* MAC 地址：FALSE = 使用芯片出厂 MAC */
#ifndef BLE_MAC
#define BLE_MAC                 FALSE
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

/* 温度校准 */
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

/* SNV（配对信息存储）：默认关闭以节省 Flash/RAM */
#ifndef BLE_SNV
#define BLE_SNV                 FALSE
#endif
#ifndef BLE_SNV_ADDR
#define BLE_SNV_ADDR            0x3B000
#endif
#ifndef BLE_SNV_BLOCK
#define BLE_SNV_BLOCK           256
#endif
#ifndef BLE_SNV_NUM
#define BLE_SNV_NUM             1
#endif

/* 内存堆：CH572 RAM 只有 12K。
 * 注意：BLE 库要求 >= 3*1024（ble_hal.c 会检查），不能低于 3072。
 * 为适配 CH572 的 12K RAM 极限，这里用 3008（配合放宽的检查阈值）。 */
#ifndef BLE_MEMHEAP_SIZE
#define BLE_MEMHEAP_SIZE        (512 * 5 + 448)
#endif

/* 数据缓冲 */
#ifndef BLE_BUFF_MAX_LEN
#define BLE_BUFF_MAX_LEN        27
#endif
#ifndef BLE_BUFF_NUM
#define BLE_BUFF_NUM            3
#endif
#ifndef BLE_TX_NUM_EVENT
#define BLE_TX_NUM_EVENT        1
#endif
#ifndef BLE_TX_POWER
#define BLE_TX_POWER            LL_TX_PWR_0_DBM
#endif

/* ===========================================================================
 * BLE 内存堆（必须在应用中定义，或使用本头文件的 extern 声明）
 * =========================================================================== */
extern uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#endif /* __BLE_CONFIG_H */
