/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_shim.c
 * Description        : CH572 Lite SDK - BLE HAL 适配层
 *
 * 本文件用 ch32fun 的寄存器定义重新实现官方 StdPeriphDriver 中被 BLE HAL
 * 依赖的函数，从而避免编译官方 StdPeriphDriver 下的所有 .c 文件。
 *
 * 设计原则：
 *   1. 本文件只包含 ch32fun 的头（ch32fun.h），不包含官方 CH572SFR.h，
 *      以避免寄存器宏重定义冲突。
 *   2. 官方 BLE 库 (libCH572BLE_PERI.a) 和 CH572BLEPeri_LIB.h 直接使用，
 *      不做任何修改。
 *   3. 所有寄存器地址和位定义均来自 ch32fun 的 ch5xxhw.h（与官方一致）。
 *
 * 对应关系：
 *   sys_safe_access_enable/disable  -> ch32fun 的 SYS_SAFE_ACCESS 宏
 *   SYS_GetSysTickCnt               -> SysTick->CNT
 *   SYS_DisableAllIrq/RecoverIrq    -> mstatus 中断开关
 *   PFIC_*                          -> ch32fun 的 NVIC 封装
 *   RTC_InitClock/RTC_InitTime      -> 直接操作 RTC 寄存器
 *   RTC_GetCycleLSI                 -> R32_RTC_CNT_32K
 *   LowPower_Sleep                  -> 直接操作 PWR 寄存器
 *   HSECFG_Current                  -> 直接操作 HSE 寄存器
 *   GetMACAddress                   -> 读 ROM 配置区
 *   FLASH_ROM_READ/WRITE/ERASE      -> 调用 ROM 中的 ISP 函数
 *******************************************************************************/

#include "ch32fun.h"
#include "ch5xx_flash.h"
#include "ble_types.h"
#include <string.h>

/* ===========================================================================
 * 1. 安全访问模式
 * ===========================================================================
 * 官方实现：
 *   #define sys_safe_access_enable()  ... R8_SAFE_ACCESS_SIG = SIG1; = SIG2;
 *   #define sys_safe_access_disable() R8_SAFE_ACCESS_SIG = 0;
 * ch32fun 已提供等价的 SYS_SAFE_ACCESS 宏（见 ch5xxhw.h）。
 * 这里提供函数形式，供官方 HAL 调用。
 */

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
 * CH572 的 SysTick 是 32 位（CNT 寄存器），直接读取即可。
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
	/* BLE 应用只需要 RTC 作为单调计数器，不需要绝对日历时间。
	 * 这里留空；若需要日历功能，可在此实现年月日到计数值的换算。 */
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
 * ch32fun 的 ch5xx_lowpower.h 提供了类似能力，这里给出精简实现。
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
	/* HSE 偏置电流配置。官方用于低功耗后恢复 HSE 驱动能力。
	 * CH572 使用内部 PLL，通常不需要调整，保留空实现。 */
}

/* ===========================================================================
 * 7. MAC 地址读取
 * ===========================================================================
 * 官方 GetMACAddress 从 ROM 配置区读 6 字节 MAC。
 */

#ifndef ROM_CFG_MAC_ADDR
#define ROM_CFG_MAC_ADDR   0x0003f018
#endif

void GetMACAddress(uint8_t *Buffer)
{
	const uint8_t *p = (const uint8_t *)ROM_CFG_MAC_ADDR;
	for (int i = 0; i < 6; i++) {
		Buffer[i] = p[i];
	}
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
 * 官方 core_riscv.h 的 __SysTick_Config 使用 64 位 SysTick 的 CMP 寄存器；
 * CH572 的 SysTick 是 32 位（CNT/CMP 均为 32 位），这里按 CH572 实现。
 */

/* 官方 core_riscv.h 定义（CH572 的 SysTick 为 32 位）
 * 注意：ch32fun 的 ch5xxhw.h 已定义 SYSTICK_LOAD_RELOAD_MSK，
 * 这里提供官方名称的别名。
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
