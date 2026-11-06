/**
 * @file    hal_time.h
 * @brief   延时、系统 tick、软件定时器
 */
#ifndef HAL_TIME_H
#define HAL_TIME_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 软件定时器回调 */
typedef void (*hal_timer_cb_t)(void *user);

typedef struct hal_time hal_time_t;

struct hal_time {
    hal_dev_t dev;

    /** 初始化时基（通常在 main 最开始调用一次） */
    hal_err_t (*init)(hal_time_t *t);

    /** 毫秒/微秒延时（忙等或 tick 等待，由 port 层决定） */
    void (*delay_ms)(hal_time_t *t, uint32_t ms);
    void (*delay_us)(hal_time_t *t, uint32_t us);

    /** 获取自启动以来的毫秒 tick（32 位，约 49 天回绕） */
    uint32_t (*get_tick_ms)(hal_time_t *t);

    /** 获取微秒级时间戳（用于精确测量） */
    uint32_t (*get_tick_us)(hal_time_t *t);

    /**
     * 注册一个单次/周期软件定时器。
     * @param period_ms  周期（毫秒）
     * @param repeat     1 = 周期触发，0 = 单次
     * @param cb         回调（在中断上下文执行，应尽量短）
     * @param user       用户数据
     * @return 定时器 ID（>=0），失败返回负错误码
     */
    int (*timer_create)(hal_time_t *t, uint32_t period_ms, uint8_t repeat,
                        hal_timer_cb_t cb, void *user);
    hal_err_t (*timer_start)(hal_time_t *t, int id);
    hal_err_t (*timer_stop) (hal_time_t *t, int id);
    hal_err_t (*timer_delete)(hal_time_t *t, int id);

    /** 由 port 层在 tick 中断中调用，驱动软件定时器 */
    void (*tick_isr)(hal_time_t *t);
};

hal_time_t *hal_time_get(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIME_H */
