/********************************** (C) COPYRIGHT *******************************
 * File Name          : sys_nosys.c
 * Description        : LWIP NO_SYS（裸机）适配 - 仅提供 sys_now() 与随机数
 *
 * NO_SYS=1 时 LWIP 不需要信号量/邮箱/线程，只需：
 *   - sys_now()      ：毫秒时间（用于超时）
 *   - LWIP_RAND()    ：随机数（用于端口/序列号）
 *
 * 时间基准：SysTick 计数器（funSysTick32）。这里维护一个 1ms 溢出计数，
 * 由主循环定期调用 sys_nosys_tick() 更新。
 *******************************************************************************/

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "ch32fun.h"

/* 毫秒计数（由 sys_nosys_tick 累加） */
static volatile u32_t g_ms = 0;

/* 记录上次 SysTick 值，用于计算增量 */
static u32_t g_last_tick = 0;

/* 由主循环调用：更新毫秒计数（基于 SysTick 差值） */
void sys_nosys_tick(void)
{
	u32_t now = funSysTick32();
	/* SysTick 以 HCLK 计数，DELAY_MS_TIME = HCLK/1000 */
	u32_t delta = now - g_last_tick;
	u32_t ms = delta / DELAY_MS_TIME;
	if (ms) {
		g_ms += ms;
		g_last_tick += ms * DELAY_MS_TIME;
	}
}

/* LWIP 时间接口 */
u32_t sys_now(void)
{
	return g_ms;
}

/* 随机数（LCG，-nostdlib 下无 libc rand） */
u32_t lwip_rand_impl(void)
{
	static u32_t seed = 0x12345678;
	seed = seed * 1103515245u + 12345u;
	return (seed >> 16) & 0x7FFF;
}
