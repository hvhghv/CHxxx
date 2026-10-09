#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/* CH591/CH592 网关 - ch32fun 配置 */

/* 时钟：60MHz PLL */
#define FUNCONF_USE_HSI           0
#define FUNCONF_USE_HSE           1
#define CLK_SOURCE_CH5XX          CLK_SOURCE_PLL_60MHz
#define FUNCONF_SYSTEM_CORE_CLOCK 60 * 1000 * 1000

#define FUNCONF_DEBUG_HARDFAULT   0

/* CH59x 无 RCC 外设，必须关闭时钟安全系统 */
#define FUNCONF_USE_CLK_SEC       0

/* printf 未使用（输出走 USB CDC / HTTP），关闭以省 Flash */
#define FUNCONF_USE_DEBUGPRINTF   0
#define FUNCONF_USE_UARTPRINTF    0

#endif
