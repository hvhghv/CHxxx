/**
 * @file    hal_sys.h
 * @brief   系统级接口：时钟、复位、唯一 ID、低功耗
 */
#ifndef HAL_SYS_H
#define HAL_SYS_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 低功耗
 * =========================================================================== */
typedef enum {
    HAL_SLEEP_MODE_SLEEP    = 0,  /**< 仅关 CPU 时钟，外设继续运行 */
    HAL_SLEEP_MODE_STANDBY  = 1,  /**< 深度睡眠，RAM 保持 */
    HAL_SLEEP_MODE_SHUTDOWN = 2,  /**< 最低功耗，RAM 丢失（唤醒等同复位） */
} hal_sleep_mode_t;

/* 唤醒源位掩码 */
#define HAL_WAKE_GPIO    0x0001u
#define HAL_WAKE_RTC     0x0002u
#define HAL_WAKE_USB     0x0004u
#define HAL_WAKE_BLE     0x0008u
#define HAL_WAKE_UART    0x0010u
#define HAL_WAKE_TIMER   0x0020u

typedef struct {
    hal_sleep_mode_t mode;
    uint32_t         wake_sources;      /**< HAL_WAKE_* 位或 */
    uint32_t         rtc_wake_ms;       /**< RTC 定时唤醒毫秒数，0 = 不定时 */
    uint8_t          gpio_wake_pin;     /**< GPIO 唤醒引脚编码，HAL_PIN_NONE = 不用 */
    uint8_t          gpio_wake_level;   /**< 0 = 低电平唤醒，1 = 高电平唤醒 */
    uint8_t          gpio_wake_port;    /**< 唤醒端口（0=A 1=B），部分芯片需要 */
} hal_sleep_cfg_t;

typedef struct hal_sys hal_sys_t;

struct hal_sys {
    hal_dev_t dev;

    /** 系统初始化（时钟树、必要外设上电） */
    hal_err_t (*init)(hal_sys_t *s);

    /** 时钟 */
    uint32_t  (*get_clock_hz)(hal_sys_t *s);
    hal_err_t (*set_clock_hz)(hal_sys_t *s, uint32_t hz);

    /** 复位 */
    void (*reset)(hal_sys_t *s);

    /** 芯片唯一 ID（8 字节） */
    void (*get_uid)(hal_sys_t *s, uint8_t uid[8]);

    /**
     * 进入低功耗，被唤醒后返回。
     * @return 唤醒原因位掩码（HAL_WAKE_*）
     */
    uint32_t (*sleep)(hal_sys_t *s, const hal_sleep_cfg_t *cfg);

    /** 查询上次唤醒原因（不进入睡眠） */
    uint32_t (*get_wake_reason)(hal_sys_t *s);

    /** 清除唤醒原因 */
    void (*clear_wake_reason)(hal_sys_t *s);
};

hal_sys_t *hal_sys_get(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYS_H */
