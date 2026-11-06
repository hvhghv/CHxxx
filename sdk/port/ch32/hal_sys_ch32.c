/**
 * @file    hal_sys_ch32x035.c
 * @brief   CH32X035 系统控制：时钟、复位、UID、低功耗
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

/* ===========================================================================
 * 时钟
 * =========================================================================== */
static hal_err_t sys_init(hal_sys_t *s)
{
    /* CH32X035 默认 HSI 8MHz；配置到 48MHz（PLL）
     * 官方 SystemInit() 已在 startup 里调用，这里只确保时钟正确。 */
    SystemCoreClockUpdate();

    s->dev.initialized = 1;
    return HAL_OK;
}

static uint32_t sys_get_clock_hz(hal_sys_t *s)
{
    HAL_UNUSED(s);
    return SystemCoreClock;
}

static hal_err_t sys_set_clock_hz(hal_sys_t *s, uint32_t hz)
{
    HAL_UNUSED(s);
    /* CH32X035 时钟切换需要重配 PLL，暂只支持查询。
     * 需要时在应用层直接调 RCC 驱动。 */
    return (hz == SystemCoreClock) ? HAL_OK : HAL_ERR_UNSUP;
}

/* ===========================================================================
 * 复位
 * =========================================================================== */
static void sys_reset(hal_sys_t *s)
{
    HAL_UNUSED(s);
    NVIC_SystemReset();
}

/* ===========================================================================
 * UID
 * =========================================================================== */
static void sys_get_uid(hal_sys_t *s, uint8_t uid[8])
{
    HAL_UNUSED(s);
    /* CH32X035 的 UID 在 0x1FFFF7E8（96 位 = 12 字节，这里取前 8 字节） */
    const uint8_t *p = (const uint8_t *)0x1FFFF7E8;
    int i;
    for (i = 0; i < 8; i++) uid[i] = p[i];
}

/* ===========================================================================
 * 低功耗（简化实现）
 * =========================================================================== */
static uint32_t sys_sleep(hal_sys_t *s, const hal_sleep_cfg_t *cfg)
{
    HAL_UNUSED(s);
    HAL_UNUSED(cfg);
    /* TODO: 实现 WFI 低功耗 */
    return 0;
}

static uint32_t sys_get_wake_reason(hal_sys_t *s)
{
    HAL_UNUSED(s);
    return 0;
}

static void sys_clear_wake_reason(hal_sys_t *s)
{
    HAL_UNUSED(s);
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static hal_sys_t s_sys = {
    .dev = { .name = "sys", .initialized = 0 },
    .init              = sys_init,
    .get_clock_hz      = sys_get_clock_hz,
    .set_clock_hz      = sys_set_clock_hz,
    .reset             = sys_reset,
    .get_uid           = sys_get_uid,
    .sleep             = sys_sleep,
    .get_wake_reason   = sys_get_wake_reason,
    .clear_wake_reason = sys_clear_wake_reason,
};

hal_sys_t *hal_sys_get(void)
{
    return &s_sys;
}
