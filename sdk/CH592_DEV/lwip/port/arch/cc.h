/********************************** (C) COPYRIGHT *******************************
 * File Name          : cc.h
 * Description        : LWIP 编译器抽象层（RISC-V GCC + FreeRTOS）
 *******************************************************************************/

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 字节序：RISC-V 小端 */
#define LWIP_LITTLE_ENDIAN  1234
#define BYTE_ORDER          LWIP_LITTLE_ENDIAN

/* 诊断输出
 * 默认关闭 printf（省格式化引擎 ~750B Flash）。
 * 需要诊断时定义 LWIP_DIAG_PRINTF=1 并确保 printf 可用。 */
#ifndef LWIP_DIAG_PRINTF
#define LWIP_DIAG_PRINTF  0
#endif

#if LWIP_DIAG_PRINTF
#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { \
	printf("LWIP ASSERT: %s @ %s:%d\r\n", x, __FILE__, __LINE__); \
	while (1); } while (0)
#else
/* 无 printf：DIAG 丢弃；ASSERT 仅死循环（保留故障停机语义） */
#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { while (1); } while (0)
#endif

/* 随机数（用于端口/序列号）—— -nostdlib 下无 libc rand，用简单 LCG */
uint32_t lwip_rand_impl(void);
#define LWIP_RAND()   lwip_rand_impl()

/* 结构体打包 */
#define PACK_STRUCT_USE_INCLUDES

#endif /* LWIP_ARCH_CC_H */
