#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/* CH572 双 USB 串口设备 - 时钟配置 */
#define FUNCONF_USE_HSI           0
#define FUNCONF_USE_HSE           1
#define CLK_SOURCE_CH5XX          CLK_SOURCE_PLL_60MHz
#define FUNCONF_SYSTEM_CORE_CLOCK 60 * 1000 * 1000

#define FUNCONF_DEBUG_HARDFAULT   0
#define FUNCONF_USE_CLK_SEC       0

/* printf 走 SWIO 调试口（USB 输出由各自的端点单独处理） */
#define FUNCONF_USE_DEBUGPRINTF   1
#define FUNCONF_USE_UARTPRINTF    0

#endif
