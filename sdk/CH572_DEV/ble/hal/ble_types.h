/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_types.h
 * Description        : CH572 Lite SDK - BLE 补充类型定义
 *
 * 官方 BLE 库头文件 CH572BLEPeri_LIB.h 只包含 stdint.h，不引入
 * CH57x_common.h（避免与 ch32fun 的 ch5xxhw.h 寄存器宏冲突）。
 * 因此官方 HAL 用到的一些类型需要在此手工补充。
 *
 * 这些定义与官方 CH57x_clk.h 完全一致。
 *******************************************************************************/

#ifndef __BLE_TYPES_H
#define __BLE_TYPES_H

#include <stdint.h>

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

/* RTC 最大计数值（与官方 CH57x_clk.h / ch32fun ch5xxhw.h 一致） */
#ifndef RTC_MAX_COUNT
#define RTC_MAX_COUNT   0xA8C00000
#endif

/* 官方 core_riscv.h 的 SysTick 重载值掩码。
 * ch32fun 的 ch5xxhw.h 已定义 SYSTICK_LOAD_RELOAD_MSK，这里提供官方名称别名。
 * 注意：ch32fun 定义为 0xFFFFFFFFFFFFFFFF（64 位），CH572 实际为 32 位 SysTick。 */
#ifndef SysTick_LOAD_RELOAD_Msk
#define SysTick_LOAD_RELOAD_Msk   0xFFFFFFFF
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

#endif /* __BLE_TYPES_H */
