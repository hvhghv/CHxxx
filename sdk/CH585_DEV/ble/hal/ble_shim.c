/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_shim.c
 * Description        : CH585/CH584 Lite SDK - BLE HAL 适配层
 *
 * 本文件用 ch32fun 的寄存器定义重新实现官方 StdPeriphDriver 中被 BLE HAL
 * 依赖的函数，从而避免编译官方 StdPeriphDriver 下的所有 .c 文件。
 *
 * 与 CH592_DEV/ble/hal/ble_shim.c 的差异（CH58x 特有）：
 *   - RTC 计数寄存器为 R32_RTC_CNT_32K（同 CH59x），但 32K 使能位在
 *     R8_CK32K_CONFIG（CH58x 无 R8_LSI_CONFIG）
 *   - 时钟配置寄存器为 R16_CLK_SYS_CFG（16 位）
 *   - USB 引脚使能在 R16_PIN_CONFIG（CH59x 为 R16_PIN_ANALOG_IE）
 *   - ROM 配置区地址与命令码同 CH59x（ISP585.h）
 *
 * 设计原则：
 *   1. 本文件只包含 ch32fun 的头（ch32fun.h），不包含官方 CH585SFR.h，
 *      以避免寄存器宏重定义冲突。
 *   2. 官方 BLE 库 (libCH58xBLE.a) 和 CH58xBLE_LIB.h 直接使用，不做修改。
 *   3. 所有寄存器地址和位定义均来自 ch32fun 的 ch5xxhw.h（与官方一致）。
 *******************************************************************************/

#include "ch32fun.h"
#include "ch5xx_flash.h"
#include "ble_types.h"
#include <string.h>

/* ===========================================================================
 * 1. 安全访问模式
 * =========================================================================== */

void sys_safe_access_enable(void)
{
	R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
	R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
	__asm__ volatile("fence.i");
}

void sys_safe_access_disable(void)
{
	R8_SAFE_ACCESS_SIG = 0;
	__asm__ volatile("fence.i");
}

/* ===========================================================================
 * 2. 系统时钟计数（BLE 库用作随机种子）
 * ===========================================================================
 * CH58x 的 SysTick 是 32 位（CNT 寄存器），直接读取即可。
 */

uint32_t SYS_GetSysTickCnt(void)
{
	return SysTick->CNT;
}

/* ===========================================================================
 * 3. 全局中断开关
 * ===========================================================================
 * 保存/恢复 mstatus 的 MIE 位。
 */

uint32_t SYS_DisableAllIrq(uint32_t *pirqv)
{
	uint32_t mstatus;
	__asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
	uint32_t saved = mstatus;          /* 保存调用前的 mstatus */
	mstatus &= ~(1u << 3);             /* 清 MIE，关全局中断 */
	__asm__ volatile("csrw mstatus, %0" : : "r"(mstatus));
	if (pirqv) *pirqv = saved;
	return saved;
}

void SYS_RecoverIrq(uint32_t irq_status)
{
	uint32_t mstatus;
	__asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
	mstatus = (mstatus & ~(1u << 3)) | (irq_status & (1u << 3));
	__asm__ volatile("csrw mstatus, %0" : : "r"(mstatus));
}

/* ===========================================================================
 * 4. PFIC 中断控制
 * ===========================================================================
 * ch32fun 只定义了 PFIC 结构体和 NVIC_EnableIRQ，这里补齐官方 HAL 需要的
 * 其余函数。寄存器布局与官方 core_riscv.h 完全一致。
 */

void PFIC_SetPriority(int IRQn, uint8_t priority)
{
	/* 官方实现：PFIC->IPRIOR[IRQn] = priority ? 0x80 : 0; */
	NVIC->IPRIOR[(uint32_t)IRQn] = priority ? 0x80 : 0;
}

void PFIC_SetPendingIRQ(int IRQn)
{
	NVIC->IPSR[((uint32_t)IRQn) >> 5] = (1 << ((uint32_t)IRQn & 0x1F));
}

void PFIC_ClearPendingIRQ(int IRQn)
{
	NVIC->IPRR[((uint32_t)IRQn) >> 5] = (1 << ((uint32_t)IRQn & 0x1F));
}

void PFIC_DisableIRQ(int IRQn)
{
	NVIC->IRER[((uint32_t)IRQn) >> 5] = (1 << ((uint32_t)IRQn & 0x1F));
	__asm__ volatile("fence.i");
}

/* ch32fun 已提供 NVIC_EnableIRQ，这里提供官方名称的别名 */
void PFIC_EnableIRQ(int IRQn)
{
	NVIC_EnableIRQ((IRQn_Type)IRQn);
}

/* ===========================================================================
 * 5. RTC 时钟
 * ===========================================================================
 * BLE 库用 RTC（LSI 32K）作为 TMOS 系统时基。
 * 官方 RTC_InitClock 会校准 LSI 频率，这里做简化实现：
 * 直接使能 LSI、配置 RTC 计数模式，返回标称 32000 Hz。
 *
 * 注意：CH58x 的 32K 使能位在 R8_CK32K_CONFIG（CH59x 为 R8_LSI_CONFIG）。
 */

uint32_t RTC_InitClock(uint32_t cnt)
{
	(void)cnt;

	/* 使能 LSI（32K 内部振荡器） */
	SYS_SAFE_ACCESS(
		R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;
	);

	/* 配置 RTC 为触发/计数模式 */
	SYS_SAFE_ACCESS(
		R8_RTC_MODE_CTRL |= RB_RTC_TRIG_EN;
	);

	/* 返回 LSI 标称频率（Hz）。
	 * 官方会实测校准（24~42KHz），这里用标称值 32000。
	 * 精度影响 BLE 连接时序，如需高精度可参考官方校准算法。 */
	return 32000;
}

void RTC_InitTime(uint16_t y, uint16_t mon, uint16_t d,
                  uint16_t h, uint16_t m, uint16_t s)
{
	(void)y; (void)mon; (void)d; (void)h; (void)m; (void)s;
	/* BLE 应用只需要 RTC 作为单调计数器，不需要绝对日历时间。 */
}

uint32_t RTC_GetCycleLSI(void)
{
	uint32_t c1, c2;
	do {
		c1 = R32_RTC_CNT_32K;
		c2 = R32_RTC_CNT_32K;
	} while (c1 != c2); /* 双读校验，避免读到跳变中的值 */
	return c1;
}

void RTC_SetCycleLSI(uint32_t cyc)
{
	SYS_SAFE_ACCESS(
		R32_RTC_TRIG = cyc;
	);
}

/* ===========================================================================
 * 6. 低功耗
 * ===========================================================================
 * 官方 LowPower_Sleep 进入 Sleep 模式，由 RTC 唤醒。
 */

void LowPower_Sleep(uint16_t rm)
{
	/* 配置外设掉电保留域（对应官方 RB_PWR_* 位） */
	SYS_SAFE_ACCESS(
		R8_SLP_POWER_CTRL = (uint8_t)(rm & 0xFF);
	);

	/* 进入 Sleep（WFI），由 RTC 中断唤醒 */
	__asm__ volatile("wfi");
}

void HSECFG_Current(HSECurrentTypeDef c)
{
	(void)c;
	/* HSE 偏置电流配置。CH58x 使用 HSE 32MHz + PLL，
	 * 常规应用不需要调整，保留空实现。 */
}

/* ===========================================================================
 * 7. MAC 地址读取
 * ===========================================================================
 * 官方 GetMACAddress 通过 ROM 命令 CMD_GET_ROM_INFO 从 ROM 配置区读 6 字节
 * MAC（ISP585.h: FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_MAC_ADDR, Buffer, 0)）。
 */

/* ROM 中的 ISP 命令入口（与官方 ISP585.h 一致） */
extern uint32_t FLASH_EEPROM_CMD(uint8_t cmd, uint32_t StartAddr, void *Buffer, uint32_t Length);

void GetMACAddress(uint8_t *Buffer)
{
	FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_MAC_ADDR, Buffer, 0);
}

/* ===========================================================================
 * 8. Flash ROM 操作
 * ===========================================================================
 * 官方通过 ISP 命令 (FLASH_EEPROM_CMD) 完成。ch32fun 的 ch5xx_flash.h
 * 提供了等价的 ROM 调用封装，这里转发过去。
 *
 * 注意：这些函数仅供 BLE 库的 SNV（配对信息存储）回调使用。
 */

void FLASH_ROM_READ(uint32_t StartAddr, void *Buffer, uint32_t Length)
{
	ch5xx_flash_cmd_read(StartAddr, (uint8_t *)Buffer, (int)Length);
}

void FLASH_ROM_WRITE(uint32_t StartAddr, void *Buffer, uint32_t Length)
{
	ch5xx_flash_cmd_write(StartAddr, (uint8_t *)Buffer, (int)Length);
}

void FLASH_ROM_ERASE(uint32_t StartAddr, uint32_t Length)
{
	ch5xx_flash_cmd_erase(StartAddr, (int)Length);
}

/* ===========================================================================
 * 9. 延时函数（官方 HAL 使用）
 * =========================================================================== */

void mDelayuS(uint16_t t)
{
	Delay_Us(t);
}

void mDelaymS(uint16_t t)
{
	Delay_Ms(t);
}

/* ===========================================================================
 * 10. SysTick 配置（官方 __SysTick_Config 的等价实现）
 * ===========================================================================
 * CH58x 的 SysTick 是 32 位（CNT/CMP 均为 32 位）。
 */

#ifndef SysTick_LOAD_RELOAD_Msk
#define SysTick_LOAD_RELOAD_Msk   SYSTICK_LOAD_RELOAD_MSK
#endif

uint32_t __SysTick_Config(uint32_t ticks)
{
	if ((uint64_t)(ticks - 1) > (uint64_t)SysTick_LOAD_RELOAD_Msk) {
		return 1;   /* 超出范围 */
	}

	SysTick->CTLR = 0;
	SysTick->CNT  = 0;
	SysTick->CMP  = ticks - 1;
	/* 使能计数、中断、HCLK 时钟源 */
	SysTick->CTLR = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
	NVIC_EnableIRQ(SysTick_IRQn);
	return 0;
}

/* ===========================================================================
 * 11. EEPROM (Data-Flash) 操作 —— CH58x 专有
 * ===========================================================================
 * CH58x 有独立的 Data-Flash(EEPROM) 区域，用于存储 BLE SNV（配对信息）。
 * 通过 ROM 中的 FLASH_EEPROM_CMD 完成（与官方 ISP585.h 一致）。
 */

uint32_t EEPROM_Read(uint32_t StartAddr, void *Buffer, uint32_t Length)
{
	return FLASH_EEPROM_CMD(CMD_EEPROM_READ, StartAddr, Buffer, Length);
}

uint32_t EEPROM_Write(uint32_t StartAddr, void *Buffer, uint32_t Length)
{
	return FLASH_EEPROM_CMD(CMD_EEPROM_WRITE, StartAddr, Buffer, Length);
}

uint32_t EEPROM_Erase(uint32_t StartAddr, uint32_t Length)
{
	return FLASH_EEPROM_CMD(CMD_EEPROM_ERASE, StartAddr, NULL, Length);
}

/* ===========================================================================
 * 12. 内部 32K 时钟校准 —— CH58x 专有
 * ===========================================================================
 * BLE 库的 rcCB 回调需要此函数，用于校准内部 32K RC 振荡器。
 */

void Calibration_LSI(Cali_LevelTypeDef cali_Lv)
{
	(void)cali_Lv;

	/* 使能 LSI */
	SYS_SAFE_ACCESS(
		R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;
	);

	/* 等待 LSI 稳定（约 500us ~ 1ms） */
	{
		volatile uint32_t i;
		for (i = 0; i < 60000; i++);
	}
}

/* ===========================================================================
 * 13. 内部温度传感器 —— CH58x 专有
 * ===========================================================================
 * BLE 库的 tsCB 回调需要此函数，用于读取芯片内部温度（RF 温漂补偿）。
 * 简化实现：返回常温标称值。
 */

uint16_t ADC_ReadTempSensor(void)
{
	return 25;
}
