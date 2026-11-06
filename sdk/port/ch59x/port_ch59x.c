/**
 * @file    port_ch59x.c
 * @brief   CH59x 移植层总入口：hal_init() 实现
 *
 * 说明：所有中断入口都在 hal_irq_trampoline_ch59x.S 中定义，
 *      由汇编 trampoline 统一分发到 hal_irq_dispatch()。
 *      这里只负责初始化顺序与 GPIO 中断的二级分发注册。
 */
#include "hal_core.h"
#include "CH59x_common.h"

/* GPIO 中断的二级分发：由 hal_irq_attach 注册到 hal_irq 层 */
static void gpio_a_isr(void *user)
{
    (void)user;
    hal_gpio_get()->irq_dispatch(hal_gpio_get(), 0);
}

static void gpio_b_isr(void *user)
{
    (void)user;
    hal_gpio_get()->irq_dispatch(hal_gpio_get(), 1);
}

/* 由 hal_time_ch59x.c 提供 */
extern void hal_time_attach_tick(void);

hal_err_t hal_init(void)
{
    hal_sys_t  *sys  = hal_sys_get();
    hal_time_t *time = hal_time_get();
    hal_gpio_t *gpio = hal_gpio_get();
    hal_err_t   err;

    /* 1. 系统时钟（BLE 要求 60MHz PLL）
     * 诊断开关：HAL_SKIP_SYSCLK=1 时跳过，用于定位时钟配置问题 */
#ifndef HAL_SKIP_SYSCLK
    err = sys->init(sys);
    if (err != HAL_OK) return err;
#else
    sys->dev.initialized = 1;
#endif

    /* 2. ★ 先注册中断处理函数，再使能中断！
     *    time->init() 内部会 hal_irq_enable(TMR0_IRQn)，
     *    如果此时处理函数还没注册，中断一触发就跳到 NULL 死机。
     *    （入口在 hal_irq_trampoline_ch59x.S） */
    hal_time_attach_tick();
    hal_irq_attach(GPIO_A_IRQn, gpio_a_isr, NULL);
    hal_irq_attach(GPIO_B_IRQn, gpio_b_isr, NULL);

    /* 3. 时基（1ms TMR0）
     * ★ 不需要再调 hal_irq_global_enable()：
     *   官方 startup_CH592.S 的 handle_reset 已经设了 mstatus = 0x88（MIE=1），
     *   PFIC_EnableIRQ 之后中断即可响应。重复设 mstatus 反而会破坏 MPIE/MPP。 */
    err = time->init(time);
    if (err != HAL_OK) return err;

    /* 4. GPIO */
    gpio->dev.initialized = 1;

    return HAL_OK;
}