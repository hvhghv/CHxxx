/**
 * @file    port_ch32x035.c
 * @brief   CH32X035 移植层总入口：hal_init() 实现
 *
 * ★ CH32X035 与 CH5xx 的差异：
 *   - 时钟/外设 API 是 STM32 风格（RCC_xxx / GPIO_xxx / TIM_xxx）
 *   - 中断向量名不同（CH32_IRQ_USART1 / TIM1_UP_IRQn / CH32_IRQ_EXTI_LOW ...）
 *   - GPIO 按端口分组（GPIOA / GPIOB / GPIOC），用 EXTI 线做中断
 */
#include "hal_core.h"
/* ===========================================================================
 * 芯片头文件（由 CHIP_DEFINES 选择）
 * ---------------------------------------------------------------------------
 * 本 port 层同时支持 CH32X035 / CH32L103 / CH32V20x。
 * =========================================================================== */
#if defined(CH32X035)
  #include "ch32x035.h"
  #define CH32_FAMILY_X035   1
#elif defined(CH32L103)
  #include "ch32l103.h"
  #define CH32_FAMILY_L103   1
#elif defined(CH32V20x)
  #include "ch32v20x.h"
  #define CH32_FAMILY_V20X   1
#else
  #error "未定义 CH32 芯片宏（CH32X035 / CH32L103 / CH32V20x）"
#endif

#if defined(CH32_FAMILY_X035)
  #define CH32_IRQ_TIM2_UP       TIM2_UP_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI7_0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI15_8_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI25_16_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        USART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_APB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_APB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_APB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_APB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_APB2Periph_USART1
  #define CH32_USART2_CLK        RCC_APB1Periph_USART2
  #define CH32_USART3_CLK        RCC_APB1Periph_USART3
  #define CH32_USART4_CLK        RCC_APB1Periph_USART4
  #define CH32_APB1_CLK_CMD      RCC_APB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_APB2PeriphClockCmd
  #define CH32_USART4_PERIPH     USART4
#elif defined(CH32_FAMILY_L103)
  #define CH32_IRQ_TIM2_UP       TIM2_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI9_5_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI15_10_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        USART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_PB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_PB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_PB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_PB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_PB2Periph_USART1
  #define CH32_USART2_CLK        RCC_PB1Periph_USART2
  #define CH32_USART3_CLK        RCC_PB1Periph_USART3
  #define CH32_USART4_CLK        RCC_PB1Periph_USART4
  #define CH32_APB1_CLK_CMD      RCC_PB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_PB2PeriphClockCmd
  #define CH32_USART4_PERIPH     USART4
#elif defined(CH32_FAMILY_V20X)
  #define CH32_IRQ_TIM2_UP       TIM2_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI9_5_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI15_10_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        UART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_APB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_APB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_APB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_APB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_APB2Periph_USART1
  #define CH32_USART2_CLK        RCC_APB1Periph_USART2
  #define CH32_USART3_CLK        RCC_APB1Periph_USART3
  #define CH32_USART4_CLK        RCC_APB1Periph_UART4
  #define CH32_APB1_CLK_CMD      RCC_APB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_APB2PeriphClockCmd
  #define CH32_USART4_PERIPH     UART4
#endif

/* GPIO EXTI 中断的二级分发
 * ★ CH32X035 用 EXTI7_0 / EXTI15_8 / EXTI25_16 三条线，
 *   这里统一转发给 hal_gpio 层，由它按引脚号区分。 */
static void exti_isr(void *user)
{
    (void)user;
    hal_gpio_get()->irq_dispatch(hal_gpio_get(), 0);
}

/* 由 hal_time_ch32x035.c 提供 */
extern void hal_time_attach_tick(void);

hal_err_t hal_init(void)
{
    hal_sys_t  *sys  = hal_sys_get();
    hal_time_t *time = hal_time_get();
    hal_gpio_t *gpio = hal_gpio_get();
    hal_err_t   err;

    /* 1. 系统时钟
     * 诊断开关：HAL_SKIP_SYSCLK=1 时跳过，用于定位时钟配置问题 */
#ifndef HAL_SKIP_SYSCLK
    err = sys->init(sys);
    if (err != HAL_OK) return err;
#else
    sys->dev.initialized = 1;
#endif

    /* 2. ★ 先注册中断处理函数，再使能中断！
     *    time->init() 内部会 hal_irq_enable(CH32_IRQ_TIM2_UP)，
     *    如果此时处理函数还没注册，中断一触发就跳到 NULL 死机。
     *    （入口在 hal_irq_trampoline_ch32x035.S） */
    hal_time_attach_tick();
    hal_irq_attach(CH32_IRQ_EXTI_LOW, exti_isr, NULL);

    /* 3. 时基（1ms TIM2）
     * ★ 不需要再调 hal_irq_global_enable()：
     *   官方 startup 的 handle_reset 已经设了 mstatus = 0x88（MIE=1）。 */
    err = time->init(time);
    if (err != HAL_OK) return err;

    /* 4. GPIO */
    gpio->dev.initialized = 1;

    return HAL_OK;
}
