/********************************** (C) COPYRIGHT *******************************
 * File Name          : freertos_shim.h
 * Description        : CH59x FreeRTOS 适配层 - 补充 ch32fun 缺失的官方接口
 *
 * 官方 FreeRTOS 移植（port.c / portmacro.h）依赖 core_riscv.h / CH59x_common.h
 * 提供的一些函数与宏。ch32fun 的 ch32fun.h 已提供大部分（SetVTFIRQ、
 * FunctionalState、__enable_irq/__disable_irq、NVIC_EnableIRQ 等），
 * 本头文件只补齐仍缺失的部分：
 *   PFIC_SetPriority()  - 设置中断优先级（官方名称）
 *   PFIC_EnableIRQ()    - 使能中断（别名 NVIC_EnableIRQ）
 *   PFIC_DisableIRQ()   - 关闭中断（别名 NVIC_DisableIRQ）
 *   SysTick_Config()    - 配置 SysTick（CH59x 为 64 位）
 *   SWI_IRQn            - 软件中断编号（别名 Software_IRQn）
 *******************************************************************************/

#ifndef __FREERTOS_SHIM_H
#define __FREERTOS_SHIM_H

#include "ch32fun.h"

/* ---------------------------------------------------------------------------
 * 中断编号别名（ch32fun 用 Software_IRQn，官方用 SWI_IRQn）
 * ------------------------------------------------------------------------- */
#ifndef SWI_IRQn
#define SWI_IRQn    Software_IRQn
#endif

/* ---------------------------------------------------------------------------
 * 小写 nop 别名（官方 core_riscv.h 用 __nop，ch32fun 用 __NOP）
 * ------------------------------------------------------------------------- */
#ifndef __nop
#define __nop()    __NOP()
#endif

/* ---------------------------------------------------------------------------
 * PFIC 中断控制（官方名称，映射到 ch32fun 的 NVIC 接口）
 * ------------------------------------------------------------------------- */
static inline void PFIC_SetPriority(IRQn_Type IRQn, uint8_t priority)
{
	/* 官方：PFIC->IPRIOR[IRQn] = priority ? 0x80 : 0; */
	NVIC->IPRIOR[(uint32_t)IRQn] = priority ? 0x80 : 0;
}

static inline void PFIC_EnableIRQ(IRQn_Type IRQn)
{
	NVIC_EnableIRQ(IRQn);
}

static inline void PFIC_DisableIRQ(IRQn_Type IRQn)
{
	NVIC_DisableIRQ(IRQn);
}

static inline void PFIC_SetPendingIRQ(IRQn_Type IRQn)
{
	NVIC_SetPendingIRQ(IRQn);
}

static inline void PFIC_ClearPendingIRQ(IRQn_Type IRQn)
{
	NVIC_ClearPendingIRQ(IRQn);
}

/* ---------------------------------------------------------------------------
 * SysTick_Config - 配置 SysTick（CH59x 为 64 位计数器）
 * ------------------------------------------------------------------------- */
static inline uint32_t SysTick_Config(uint64_t ticks)
{
	if ((ticks - 1) > SYSTICK_LOAD_RELOAD_MSK)
		return 1;   /* 重载值超范围 */

	SysTick->CMP  = ticks - 1;
	SysTick->CNT  = 0;
	PFIC_EnableIRQ(SysTick_IRQn);
	SysTick->CTLR = SYSTICK_CTLR_INIT | SYSTICK_CTLR_STRE |
	                SYSTICK_CTLR_STCLK | SYSTICK_CTLR_STE;
	return 0;
}

#endif /* __FREERTOS_SHIM_H */
