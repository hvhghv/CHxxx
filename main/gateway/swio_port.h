/********************************** (C) COPYRIGHT *******************************
 * File Name          : swio_port.h
 * Description        : SWIO 主机平台抽象层（CH591/CH592）
 *
 * 提供 SWIO 位操作所需的底层原语：
 *   - 单线开漏模拟（CH5xx 无 OD 模式，用「切方向」模型）
 *   - 周期级精确延时（基于 64 位 SysTick）
 *
 * 参考：cnlohr bitbang_rvswdio.h（MIT/NewBSD）
 *
 * 开漏模型（CH5xx 无 GPIO_ModeOut_OD）：
 *   拉低（输出 0）：DIR=1, OUT=0
 *   释放（高阻，外部上拉拉高）：DIR=0, PU=1
 *   预充电（输出高）：DIR=1, OUT=1（用于接收时的总线冲突检测）
 *   读线：PIN & bit
 *******************************************************************************/

#ifndef _SWIO_PORT_H
#define _SWIO_PORT_H

#include <stdint.h>
#include "ch32fun.h"

/* ---------------------------------------------------------------------------
 * 引脚端口描述（预解析寄存器指针，避免位操作中重复计算）
 * ------------------------------------------------------------------------- */
typedef struct {
	uint32_t           chfun;   /* ch32fun 引脚宏（PA0..PB15） */
	uint32_t           bit;     /* 位掩码 (1 << (pin & 0xF)) */
	volatile uint32_t *dir;     /* R32_PA_DIR  + GPIOB 偏移 */
	volatile uint32_t *out;     /* R32_PA_OUT  + GPIOB 偏移 */
	volatile uint32_t *set;     /* R32_PA_SET  + GPIOB 偏移 */
	volatile uint32_t *clr;     /* R32_PA_CLR  + GPIOB 偏移 */
	volatile uint32_t *pu;      /* R32_PA_PU   + GPIOB 偏移 */
	volatile uint32_t *pd_drv;  /* R32_PA_PD_DRV + GPIOB 偏移 */
	volatile uint32_t *pin;     /* R32_PA_PIN  + GPIOB 偏移 */
	int                valid;   /* 1 = 已配置 */
} swio_port_t;

/* ---------------------------------------------------------------------------
 * 平台原语（内联，最小延时）
 * ------------------------------------------------------------------------- */

/* 拉低（输出 0）：DIR=1, OUT=0 */
static inline void swio_port_drive_low(swio_port_t *p)
{
	*p->dir |= p->bit;          /* 输出方向 */
	*p->clr  = p->bit;          /* OUT = 0（单周期写） */
}

/* 释放（高阻 + 上拉）：DIR=0, PU=1 */
static inline void swio_port_release(swio_port_t *p)
{
	*p->pu  |= p->bit;          /* 上拉使能 */
	*p->dir &= ~p->bit;         /* 输入方向（高阻） */
}

/* 预充电（输出高）：DIR=1, OUT=1 */
static inline void swio_port_drive_high(swio_port_t *p)
{
	*p->dir |= p->bit;          /* 输出方向 */
	*p->set  = p->bit;          /* OUT = 1（单周期写） */
}

/* 读线电平（0/1） */
static inline int swio_port_read(swio_port_t *p)
{
	return (*p->pin & p->bit) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * 周期级精确延时（基于 64 位 SysTick）
 * ---------------------------------------------------------------------------
 * CH592 的 SysTick->CNT 是 64 位，60MHz 下每周期 16.7ns。
 * 轮询等待指定周期数，精度远优于 NOP 计数。
 * ------------------------------------------------------------------------- */
static inline void swio_port_delay_cycles(uint32_t cycles)
{
	uint64_t start = SysTick->CNT;
	while ((uint64_t)(SysTick->CNT - start) < (uint64_t)cycles) {
		/* busy wait */
	}
}

/* 微秒级延时（用于协议帧间间隔，非位时序） */
static inline void swio_port_delay_us(uint32_t us)
{
	swio_port_delay_cycles(us * (FUNCONF_SYSTEM_CORE_CLOCK / 1000000u));
}

/* ---------------------------------------------------------------------------
 * 关中断（位时序关键段）—— 复用 ch32fun 的 __disable_irq/__enable_irq
 * ------------------------------------------------------------------------- */
#define swio_port_irq_disable()  __disable_irq()
#define swio_port_irq_enable()   __enable_irq()

#endif /* _SWIO_PORT_H */
