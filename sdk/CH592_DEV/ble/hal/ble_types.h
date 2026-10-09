/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_types.h
 * Description        : CH592/CH591 Lite SDK - BLE 补充类型定义
 *
 * 官方 BLE 库头文件 CH59xBLE_LIB.h 只包含 stdint.h，不引入
 * CH59x_common.h（避免与 ch32fun 的 ch5xxhw.h 寄存器宏冲突）。
 * 因此官方 HAL 用到的一些类型需要在此手工补充。
 *
 * 这些定义与官方 CH59x_clk.h / ISP592.h 完全一致。
 *******************************************************************************/

#ifndef __BLE_TYPES_H
#define __BLE_TYPES_H

#include <stdint.h>

/* ===========================================================================
 * IRQ 名称桥接（ch32fun 与官方命名差异）
 * ===========================================================================
 * ch32fun 的 ch5xxhw.h 把 BLE 中断命名为 LLE_IRQn(BLEL,21)/BB_IRQn(BLEB,20)，
 * 官方 CH592SFR.h 命名为 BLEL_IRQn/BLEH_IRQn。这里提供别名。
 * 注意：需在 ch32fun.h 之后包含本头文件（ble_hal.c 中的包含顺序已满足）。
 */
#ifndef BLEL_IRQn
#define BLEL_IRQn   LLE_IRQn
#endif
#ifndef BLEH_IRQn
#define BLEH_IRQn   BB_IRQn
#endif

/* RTC 振荡器计数周期（对应官方 RTC_OSCCntTypeDef） */
typedef enum {
	Count_1 = 0,
	Count_2,
	Count_4,
	Count_32,
	Count_64,
	Count_128,
	Count_1024,
	Count_2047,
} RTC_OSCCntTypeDef;

/* HSE 偏置电流（对应官方 HSECurrentTypeDef） */
typedef enum {
	HSE_RCur_75 = 0,
	HSE_RCur_100,
	HSE_RCur_125,
	HSE_RCur_150,
} HSECurrentTypeDef;

/* LSI 校准精度等级（对应官方 CH59x_clk.h 的 Cali_LevelTypeDef） */
typedef enum {
	Level_1 = 0,
	Level_2,
	Level_4,
	Level_8,
	Level_16,
	Level_32,
	Level_64,
} Cali_LevelTypeDef;

/* RTC 最大计数值（与官方 CH59x_clk.h / ch32fun ch5xxhw.h 一致） */
#ifndef RTC_MAX_COUNT
#define RTC_MAX_COUNT   0xA8C00000
#endif

/* 官方 core_riscv.h 的 SysTick 重载值掩码。
 * ch32fun 的 ch5xxhw.h 已定义 SYSTICK_LOAD_RELOAD_MSK，这里提供官方名称别名。
 * 注意：CH59x 的 SysTick 为 32 位。 */
#ifndef SysTick_LOAD_RELOAD_Msk
#define SysTick_LOAD_RELOAD_Msk   0xFFFFFFFF
#endif

/* ===========================================================================
 * CH59x Data-Flash(EEPROM) 相关（对应官方 ISP592.h）
 * ===========================================================================
 * SNV（配对信息存储）使用 EEPROM 区域。
 * =========================================================================== */
#ifndef EEPROM_BLOCK_SIZE
#define EEPROM_BLOCK_SIZE   4096    /* 官方 ISP592.h：Data-Flash 擦写块大小 */
#endif
#ifndef ROM_CFG_VERISON
#define ROM_CFG_VERISON     0x7F010 /* 官方 ISP592.h：芯片版本配置寄存器 */
#endif
#ifndef ROM_CFG_MAC_ADDR
#define ROM_CFG_MAC_ADDR    0x7F018 /* 官方 ISP592.h：MAC 地址配置区 */
#endif
#ifndef DEF_CHIP_ID_CH592A
#define DEF_CHIP_ID_CH592A  9       /* 官方 ISP592.h：CH592A 芯片 ID */
#endif

/* EEPROM 命令码（同官方 ISP592.h） */
#ifndef CMD_EEPROM_READ
#define CMD_EEPROM_READ     0x0B
#endif
#ifndef CMD_EEPROM_WRITE
#define CMD_EEPROM_WRITE    0x0A
#endif
#ifndef CMD_EEPROM_ERASE
#define CMD_EEPROM_ERASE    0x09
#endif
#ifndef CMD_GET_ROM_INFO
#define CMD_GET_ROM_INFO    0x06    /* 读取 ROM 配置信息（如 MAC 地址） */
#endif

/* ===========================================================================
 * ble_shim.c 提供的函数声明（官方 StdPeriphDriver 等价实现）
 * ===========================================================================
 * 注意：IRQn 参数用 int 而非 IRQn_Type，以兼容 ch32fun（IRQn_Type）和
 * 官方（IRQn_Type）两种定义。调用方传枚举值会自动转换。
 * =========================================================================== */
uint32_t SYS_GetSysTickCnt(void);
void     sys_safe_access_enable(void);
void     sys_safe_access_disable(void);
uint32_t SYS_DisableAllIrq(uint32_t *pirqv);
void     SYS_RecoverIrq(uint32_t irq_status);
void     PFIC_SetPriority(int IRQn, uint8_t priority);
void     PFIC_SetPendingIRQ(int IRQn);
void     PFIC_EnableIRQ(int IRQn);
void     PFIC_DisableIRQ(int IRQn);
void     PFIC_ClearPendingIRQ(int IRQn);
uint32_t RTC_InitClock(uint32_t cnt);
void     RTC_InitTime(uint16_t y, uint16_t mon, uint16_t d,
                      uint16_t h, uint16_t m, uint16_t s);
uint32_t RTC_GetCycleLSI(void);
void     RTC_SetCycleLSI(uint32_t cyc);
void     LowPower_Sleep(uint16_t rm);
void     HSECFG_Current(HSECurrentTypeDef c);
void     GetMACAddress(uint8_t *Buffer);
void     FLASH_ROM_READ(uint32_t StartAddr, void *Buffer, uint32_t Length);
void     FLASH_ROM_WRITE(uint32_t StartAddr, void *Buffer, uint32_t Length);
void     FLASH_ROM_ERASE(uint32_t StartAddr, uint32_t Length);
uint32_t __SysTick_Config(uint32_t ticks);
void     mDelayuS(uint16_t t);
void     mDelaymS(uint16_t t);

/* CH59x 专有：EEPROM(Data-Flash) 操作 —— 供 ble_hal.c 的 SNV 回调使用 */
uint32_t EEPROM_Read(uint32_t StartAddr, void *Buffer, uint32_t Length);
uint32_t EEPROM_Write(uint32_t StartAddr, void *Buffer, uint32_t Length);
uint32_t EEPROM_Erase(uint32_t StartAddr, uint32_t Length);

/* 官方 ISP592.h 用大写宏名 EEPROM_READ/WRITE/ERASE，ble_hal.c 也用它 */
#define EEPROM_READ(StartAddr, Buffer, Length)  EEPROM_Read((StartAddr), (Buffer), (Length))
#define EEPROM_WRITE(StartAddr, Buffer, Length) EEPROM_Write((StartAddr), (Buffer), (Length))
#define EEPROM_ERASE(StartAddr, Length)         EEPROM_Erase((StartAddr), (Length))

/* CH59x 专有：内部 32K 校准 与 温度传感器（供 ble_hal.c 的回调使用） */
void     Calibration_LSI(Cali_LevelTypeDef cali_Lv);
uint16_t ADC_ReadTempSensor(void);

#endif /* __BLE_TYPES_H */

