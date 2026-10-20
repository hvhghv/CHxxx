#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

/* CH32 系列（CH32X035 / CH32L103 / CH32V203）示例配置。
 * 完整可配置项见 ch32fun/ch32fun.h。
 * 目标芯片由构建系统通过 -DCH32X03x / -DCH32L103 / -DCH32V20x 指定。 */

/* CH32 系列没有 CH5xx 的时钟安全系统，统一关闭 */
#define FUNCONF_USE_CLK_SEC       0

/* 节省 Flash：不使用调试 printf */
#define FUNCONF_USE_DEBUGPRINTF   0
#define FUNCONF_USE_UARTPRINTF    0

#endif
