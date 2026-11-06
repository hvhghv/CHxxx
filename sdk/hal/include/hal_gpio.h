/**
 * @file    hal_gpio.h
 * @brief   GPIO 通用接口
 *
 * 用法：
 *   hal_gpio_t *g = hal_gpio_get();
 *   g->init(g, HAL_PA13, HAL_GPIO_OUT_PP, HAL_GPIO_PULL_NONE);
 *   g->write(g, HAL_PA13, 1);
 */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 方向 + 输出类型 */
typedef enum {
    HAL_GPIO_IN         = 0,   /**< 浮空输入 */
    HAL_GPIO_IN_PULLUP  = 1,   /**< 上拉输入 */
    HAL_GPIO_IN_PULLDOWN= 2,   /**< 下拉输入 */
    HAL_GPIO_IN_ANALOG  = 3,   /**< 模拟输入（ADC 用） */
    HAL_GPIO_OUT_PP     = 4,   /**< 推挽输出 */
    HAL_GPIO_OUT_OD     = 5,   /**< 开漏输出 */
    HAL_GPIO_OUT_AF_PP  = 6,   /**< 复用推挽 */
    HAL_GPIO_OUT_AF_OD  = 7,   /**< 复用开漏 */
} hal_gpio_mode_t;

/* 中断触发边沿 */
typedef enum {
    HAL_GPIO_IRQ_NONE    = 0,
    HAL_GPIO_IRQ_RISING  = 1,
    HAL_GPIO_IRQ_FALLING = 2,
    HAL_GPIO_IRQ_BOTH    = 3,
} hal_gpio_irq_t;

/* 引脚中断回调 */
typedef void (*hal_gpio_irq_cb_t)(uint8_t pin, void *user);

typedef struct hal_gpio hal_gpio_t;

struct hal_gpio {
    hal_dev_t dev;

    hal_err_t (*init)   (hal_gpio_t *g, uint8_t pin, hal_gpio_mode_t mode,
                         uint8_t pull);   /* pull: 0=none 1=up 2=down */
    hal_err_t (*deinit) (hal_gpio_t *g, uint8_t pin);

    void      (*write)  (hal_gpio_t *g, uint8_t pin, uint8_t level);
    uint8_t   (*read)   (hal_gpio_t *g, uint8_t pin);
    void      (*toggle) (hal_gpio_t *g, uint8_t pin);

    /* 批量写整个端口（port 0=A 1=B ...），value 为 32 位端口值 */
    void      (*write_port)(hal_gpio_t *g, uint8_t port, uint32_t value);
    uint32_t  (*read_port) (hal_gpio_t *g, uint8_t port);

    /* 中断 */
    hal_err_t (*set_irq)(hal_gpio_t *g, uint8_t pin, hal_gpio_irq_t edge,
                         hal_gpio_irq_cb_t cb, void *user);
    /* 供 port 层内部使用：由 GPIOA/B_IRQHandler 调用 */
    void      (*irq_dispatch)(hal_gpio_t *g, uint8_t port);
};

/** 获取 GPIO 实例（全局唯一） */
hal_gpio_t *hal_gpio_get(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
