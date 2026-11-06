/**
 * @file    hal_time_ch32x035.c
 * @brief   CH32X035 时基：延时、tick、软件定时器
 *
 * ★ 用 TIM2 作为 1ms 时基。
 *   CH32X035 有 SysTick（RV32 内核），但 TIM2 走 PFIC 中断表，
 *   与 HAL 的 hal_irq 影子副本机制配合更直接。
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

#include <string.h>

#define TIMER_MAX   8

typedef struct {
    uint32_t       period_ms;
    uint32_t       next_ms;
    uint8_t        repeat;
    uint8_t        active;
    hal_timer_cb_t cb;
    void          *user;
} soft_timer_t;

static volatile uint32_t s_tick_ms = 0;
static soft_timer_t      s_timers[TIMER_MAX];

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t time_init(hal_time_t *t)
{
    TIM_TimeBaseInitTypeDef tb;

    /* TIM2 时钟（APB1） */
    CH32_APB1_CLK_CMD(CH32_TIM2_CLK, ENABLE);

    /* 1ms 周期：预分频到 1MHz，计数 1000 次 = 1ms */
    tb.TIM_Prescaler         = (uint16_t)(SystemCoreClock / 1000000u - 1u);
    tb.TIM_CounterMode       = TIM_CounterMode_Up;
    tb.TIM_Period            = 1000u - 1u;
    tb.TIM_ClockDivision     = TIM_CKD_DIV1;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tb);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    t->dev.initialized = 1;
    return HAL_OK;
}

/* ===========================================================================
 * 延时
 * =========================================================================== */
static void time_delay_ms(hal_time_t *t, uint32_t ms)
{
    uint32_t loops_per_ms = SystemCoreClock / 1000u / 4u;
    volatile uint32_t n;

    HAL_UNUSED(t);
    while (ms--) {
        n = loops_per_ms;
        while (n--) {
            __asm__ volatile("nop");
        }
    }
}

static void time_delay_us(hal_time_t *t, uint32_t us)
{
    uint32_t loops_per_us = SystemCoreClock / 1000000u / 4u;
    volatile uint32_t n;

    HAL_UNUSED(t);
    if (loops_per_us == 0) loops_per_us = 1;
    while (us--) {
        n = loops_per_us;
        while (n--) {
            __asm__ volatile("nop");
        }
    }
}

/* ===========================================================================
 * tick 查询
 * =========================================================================== */
static uint32_t time_get_tick_ms(hal_time_t *t)
{
    HAL_UNUSED(t);
    return s_tick_ms;
}

static uint32_t time_get_tick_us(hal_time_t *t)
{
    HAL_UNUSED(t);
    /* 微秒精度用 TIM2 当前计数补充 */
    return s_tick_ms * 1000u + (uint32_t)TIM_GetCounter(TIM2);
}

/* ===========================================================================
 * 软件定时器
 * =========================================================================== */
static int time_timer_create(hal_time_t *t, uint32_t period_ms, uint8_t repeat,
                             hal_timer_cb_t cb, void *user)
{
    int i;
    HAL_UNUSED(t);

    if (!cb || period_ms == 0) return -1;

    for (i = 0; i < TIMER_MAX; i++) {
        if (!s_timers[i].active) {
            s_timers[i].period_ms = period_ms;
            s_timers[i].next_ms   = s_tick_ms + period_ms;
            s_timers[i].repeat    = repeat;
            s_timers[i].active    = 1;
            s_timers[i].cb        = cb;
            s_timers[i].user      = user;
            return i;
        }
    }
    return -1;      /* 表满 */
}

static hal_err_t time_timer_start(hal_time_t *t, int id)
{
    HAL_UNUSED(t);
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    s_timers[id].next_ms = s_tick_ms + s_timers[id].period_ms;
    s_timers[id].active  = 1;
    return HAL_OK;
}

static hal_err_t time_timer_stop(hal_time_t *t, int id)
{
    HAL_UNUSED(t);
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    s_timers[id].active = 0;
    return HAL_OK;
}

static hal_err_t time_timer_delete(hal_time_t *t, int id)
{
    HAL_UNUSED(t);
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    memset(&s_timers[id], 0, sizeof(s_timers[id]));
    return HAL_OK;
}

/* ===========================================================================
 * tick 中断处理
 * =========================================================================== */
static void time_tick_isr(hal_time_t *t)
{
    int i;
    HAL_UNUSED(t);

    s_tick_ms++;

    for (i = 0; i < TIMER_MAX; i++) {
        if (!s_timers[i].active) continue;
        if ((int32_t)(s_tick_ms - s_timers[i].next_ms) >= 0) {
            if (s_timers[i].repeat) {
                s_timers[i].next_ms += s_timers[i].period_ms;
            } else {
                s_timers[i].active = 0;
            }
            if (s_timers[i].cb) {
                s_timers[i].cb(s_timers[i].user);
            }
        }
    }
}

/* ===========================================================================
 * TIM2 中断：通过 hal_irq 层分发
 * =========================================================================== */
static void tim2_isr(void *user)
{
    HAL_UNUSED(user);

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        hal_time_get()->tick_isr(hal_time_get());
    }
}

/* 由 port_ch32x035.c 的 hal_init() 调用 */
void hal_time_attach_tick(void)
{
    hal_irq_attach(CH32_IRQ_TIM2_UP, tim2_isr, NULL);
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static hal_time_t s_time = {
    .dev = { .name = "time", .initialized = 0 },
    .init         = time_init,
    .delay_ms     = time_delay_ms,
    .delay_us     = time_delay_us,
    .get_tick_ms  = time_get_tick_ms,
    .get_tick_us  = time_get_tick_us,
    .timer_create = time_timer_create,
    .timer_start  = time_timer_start,
    .timer_stop   = time_timer_stop,
    .timer_delete = time_timer_delete,
    .tick_isr     = time_tick_isr,
};

hal_time_t *hal_time_get(void)
{
    return &s_time;
}
