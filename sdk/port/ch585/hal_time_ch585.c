/**
 * @file    hal_time_ch585.c
 * @brief   CH585 时基：延时、tick、软件定时器
 *
 * ★ 用 TMR0 作为 1ms 时基，不用 SysTick。
 *   原因：CH585 官方例程全部用 TMR0/TMR1/TMR2/TMR3 做定时，
 *   没有任何一个用 SysTick；SysTick 在 CH585 上主要留给 BLE 协议栈内部，
 *   用户态使用会与协议栈冲突，且实测在中断上下文里行为不稳定。
 */
#include "hal_core.h"
#include "CH58x_common.h"
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
    /* TMR0 1ms 周期：TMR0_TimerInit 的参数是「计数周期」，
     * 60MHz 下 1ms = 60000 个时钟。 */
    TMR0_TimerInit(FREQ_SYS / 1000u);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    hal_irq_enable(TMR0_IRQn);

    t->dev.initialized = 1;
    return HAL_OK;
}


/* ===========================================================================
 * 延时
 * =========================================================================== */
static void time_delay_ms(hal_time_t *t, uint32_t ms)
{
    /* 简单忙等延时，不依赖中断。
     * 这里按 HCLK 估算循环次数：60MHz 下每次循环约 4 个周期。 */
    uint32_t loops_per_ms = GetSysClock() / 1000u / 4u;
    volatile uint32_t n;

    while (ms--) {
        n = loops_per_ms;
        while (n--) {
            __asm__ volatile("nop");
        }
    }
}

static void time_delay_us(hal_time_t *t, uint32_t us)
{
    /* 简单忙等：60MHz 下每次循环约 0.1us，用 10 次近似 1us */
    volatile uint32_t n = us * 6u;
    while (n--) {
        __asm__ volatile("nop");
    }
}

/* ===========================================================================
 * tick
 * =========================================================================== */
static uint32_t time_get_tick_ms(hal_time_t *t)
{
    return s_tick_ms;
}

static uint32_t time_get_tick_us(hal_time_t *t)
{
    /* 用 SysTick 当前值补充微秒精度 */
    uint32_t cnt = SysTick->CNT;
    uint32_t reload = SysTick->CMP;
    uint32_t elapsed = reload - cnt;
    return s_tick_ms * 1000u + (elapsed / (GetSysClock() / 1000000u));
}

/* ===========================================================================
 * 软件定时器
 * =========================================================================== */
static int time_timer_create(hal_time_t *t, uint32_t period_ms, uint8_t repeat,
                             hal_timer_cb_t cb, void *user)
{
    int i;
    if (!cb || period_ms == 0) return (int)HAL_ERR_PARAM;

    for (i = 0; i < TIMER_MAX; i++) {
        if (!s_timers[i].active) {
            s_timers[i].period_ms = period_ms;
            s_timers[i].next_ms   = s_tick_ms + period_ms;
            s_timers[i].repeat    = repeat;
            s_timers[i].cb        = cb;
            s_timers[i].user      = user;
            s_timers[i].active    = 1;
            return i;
        }
    }
    return (int)HAL_ERR_FULL;
}

static hal_err_t time_timer_start(hal_time_t *t, int id)
{
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    if (!s_timers[id].cb) return HAL_ERR_PARAM;
    s_timers[id].next_ms = s_tick_ms + s_timers[id].period_ms;
    s_timers[id].active  = 1;
    return HAL_OK;
}

static hal_err_t time_timer_stop(hal_time_t *t, int id)
{
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    s_timers[id].active = 0;
    return HAL_OK;
}

static hal_err_t time_timer_delete(hal_time_t *t, int id)
{
    if (id < 0 || id >= TIMER_MAX) return HAL_ERR_PARAM;
    s_timers[id].active = 0;
    s_timers[id].cb     = NULL;
    s_timers[id].user   = NULL;
    return HAL_OK;
}

/* 由 SysTick 中断调用
 * ★ 必须放在 .highcode（RAM）：由 RAM 中的 systick_isr 直接调用，
 *   中断上下文下不能访问 Flash。 */
__attribute__((section(".highcode")))
static void time_tick_isr(hal_time_t *t)
{
    int i;
    s_tick_ms++;

    for (i = 0; i < TIMER_MAX; i++) {
        if (!s_timers[i].active || !s_timers[i].cb) continue;
        if ((int32_t)(s_tick_ms - s_timers[i].next_ms) >= 0) {
            if (s_timers[i].repeat) {
                s_timers[i].next_ms += s_timers[i].period_ms;
            } else {
                s_timers[i].active = 0;
            }
            s_timers[i].cb(s_timers[i].user);
        }
    }
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

/* ===========================================================================
 * SysTick 中断：通过 hal_irq 层分发（入口在 hal_irq_trampoline_ch585.S）
 * ---------------------------------------------------------------------------
/* ===========================================================================
 * TMR0 中断：通过 hal_irq 层分发（入口在 hal_irq_trampoline_ch585.S）
 * ---------------------------------------------------------------------------
 * ★ 必须放在 .highcode（RAM）：CH585 在中断上下文中访问 Flash 会失败/挂起。
 * =========================================================================== */
__attribute__((section(".highcode")))
static void tmr0_isr(void *user)
{
    (void)user;
    /* 清 TMR0 周期结束标志，否则中断会无限重入 */
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);

    hal_time_get()->tick_isr(hal_time_get());
}

/* 由 port_ch585.c 的 hal_init() 调用，注册 TMR0 处理函数 */
void hal_time_attach_tick(void)
{
    hal_irq_attach(TMR0_IRQn, tmr0_isr, NULL);
}
