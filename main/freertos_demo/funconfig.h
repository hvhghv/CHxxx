#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/* CH592/CH591 FreeRTOS 示例配置。
 * 完整可配置项见 ch32fun/ch32fun.h。
 * 目标芯片由构建系统通过 -DCH59x 指定。
 * FreeRTOS 构建会自动定义 FUNCONF_OVERRIDE_STARTUP=1。 */

/* CH59x 无 RCC 外设，必须关闭时钟安全系统（否则 ch32fun.c 编译报错） */
#define FUNCONF_USE_CLK_SEC       0

/* printf 输出到调试口（SWIO） */
#define FUNCONF_USE_DEBUGPRINTF   1
#define FUNCONF_USE_UARTPRINTF    0

#endif
