/**
 * @file    hal_types.h
 * @brief   通用层基础类型、错误码、设备基类
 *
 * 所有 HAL 外设接口都基于这里的定义。本文件不包含任何芯片相关头文件，
 * 保证通用层可跨芯片编译。
 */
#ifndef HAL_TYPES_H
#define HAL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 错误码
 * =========================================================================== */
typedef enum {
    HAL_OK          =  0,   /**< 成功 */
    HAL_ERR_PARAM   = -1,   /**< 参数非法 */
    HAL_ERR_TIMEOUT = -2,   /**< 超时 */
    HAL_ERR_BUSY    = -3,   /**< 忙（上次操作未完成） */
    HAL_ERR_NOMEM   = -4,   /**< 无内存 / 缓冲不足 */
    HAL_ERR_IO      = -5,   /**< 硬件错误 */
    HAL_ERR_NOINIT  = -6,   /**< 未初始化 */
    HAL_ERR_UNSUP   = -7,   /**< 芯片不支持该功能 */
    HAL_ERR_FULL    = -8,   /**< 队列/表已满 */
    HAL_ERR_EMPTY   = -9,   /**< 队列为空 */
} hal_err_t;

/* ===========================================================================
 * 设备基类
 * =========================================================================== */
typedef struct {
    const char *name;        /**< 实例名，如 "uart0" / "ble" / "i2c0" */
    uint8_t     initialized; /**< 0 = 未初始化，1 = 已初始化 */
} hal_dev_t;

/* ===========================================================================
 * 引脚编码
 * ---------------------------------------------------------------------------
 * 统一用 (port << 4) | pin 编码，port 0=A 1=B 2=C 3=D ...
 * 例如 PA13 = (0 << 4) | 13 = 13
 *      PB22 = (1 << 4) | 22 = 38
 * 这样在 CH59x（只有 A/B 口）和 CH32（A/B/C/D）上都能表达。
 * =========================================================================== */
#define HAL_PIN(port, pin)   ((uint8_t)(((port) << 4) | ((pin) & 0x0F)))
#define HAL_PIN_PORT(p)      ((uint8_t)((p) >> 4))
#define HAL_PIN_NUM(p)       ((uint8_t)((p) & 0x0F))
#define HAL_PIN_NONE         0xFF

/* 常用引脚简写（CH59x 可用引脚） */
#define HAL_PA8   HAL_PIN(0, 8)
#define HAL_PA9   HAL_PIN(0, 9)
#define HAL_PA12  HAL_PIN(0, 12)
#define HAL_PA13  HAL_PIN(0, 13)
#define HAL_PA14  HAL_PIN(0, 14)
#define HAL_PA15  HAL_PIN(0, 15)
#define HAL_PB10  HAL_PIN(1, 10)
#define HAL_PB11  HAL_PIN(1, 11)
#define HAL_PB22  HAL_PIN(1, 22)
#define HAL_PB23  HAL_PIN(1, 23)

/* ===========================================================================
 * 通用辅助宏
 * =========================================================================== */
#define HAL_ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define HAL_UNUSED(x)       ((void)(x))

#ifndef HAL_MIN
#define HAL_MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif
#ifndef HAL_MAX
#define HAL_MAX(a, b)       (((a) > (b)) ? (a) : (b))
#endif

/* 编译期断言 */
#define HAL_STATIC_ASSERT(cond, msg)  typedef char hal_sa_##msg[(cond) ? 1 : -1]

#ifdef __cplusplus
}
#endif

#endif /* HAL_TYPES_H */
