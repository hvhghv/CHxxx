/**
 * @file    hal_gpio_ch32x035.c
 * @brief   CH32X035 GPIO 实现
 *
 * ★ CH32X035 与 CH5xx 的差异：
 *   - 用 GPIO_TypeDef* 指针（GPIOA/GPIOB/GPIOC），不是寄存器宏
 *   - 模式用 GPIO_Mode_xxx + GPIO_Speed_xxx + GPIO_Init()
 *   - 中断走 EXTI 线（EXTI7_0 / EXTI15_8 / EXTI25_16）
 *   - 引脚号 0~15 对应 GPIO_Pin_0~15
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

#define GPIO_PINS_PER_PORT  16

static hal_gpio_irq_cb_t s_irq_cb[3][GPIO_PINS_PER_PORT];
static void             *s_irq_user[3][GPIO_PINS_PER_PORT];

/* 端口号 → GPIO_TypeDef* */
static GPIO_TypeDef *port_base(uint8_t port)
{
    switch (port) {
    case 0: return GPIOA;
    case 1: return GPIOB;
    case 2: return GPIOC;
    default: return NULL;
    }
}

/* 端口号 → RCC 时钟位 */
static uint32_t port_clock(uint8_t port)
{
    switch (port) {
    case 0: return CH32_GPIOA_CLK;
    case 1: return CH32_GPIOB_CLK;
    case 2: return CH32_GPIOC_CLK;
    default: return 0;
    }
}

/* ===========================================================================
 * 初始化 / 反初始化
 * =========================================================================== */
static hal_err_t gpio_init(hal_gpio_t *g, uint8_t pin, hal_gpio_mode_t mode,
                           uint8_t pull)
{
    uint8_t        port = HAL_PIN_PORT(pin);
    uint8_t        num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef  *base = port_base(port);
    GPIO_InitTypeDef gi;
    uint32_t       clk;

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT) return HAL_ERR_PARAM;

    clk = port_clock(port);
    CH32_APB2_CLK_CMD(clk, ENABLE);

    gi.GPIO_Pin   = (uint16_t)(1u << num);
    gi.GPIO_Speed = GPIO_Speed_50MHz;

    switch (mode) {
    case HAL_GPIO_IN:
        gi.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        break;
    case HAL_GPIO_IN_PULLUP:
        gi.GPIO_Mode = GPIO_Mode_IPU;
        break;
    case HAL_GPIO_IN_PULLDOWN:
        gi.GPIO_Mode = GPIO_Mode_IPD;
        break;
    case HAL_GPIO_IN_ANALOG:
        gi.GPIO_Mode = GPIO_Mode_AIN;
        break;
    case HAL_GPIO_OUT_PP:
        gi.GPIO_Mode = GPIO_Mode_Out_PP;
        break;
    case HAL_GPIO_OUT_OD:
        /* ★ CH32X035 不支持开漏输出，退化为推挽 */
        gi.GPIO_Mode = GPIO_Mode_Out_PP;
        break;
    case HAL_GPIO_OUT_AF_PP:
        gi.GPIO_Mode = GPIO_Mode_AF_PP;
        break;
    case HAL_GPIO_OUT_AF_OD:
        /* ★ CH32X035 不支持复用开漏，退化为复用推挽 */
        gi.GPIO_Mode = GPIO_Mode_AF_PP;
        break;
    default:
        return HAL_ERR_PARAM;
    }

    HAL_UNUSED(pull);   /* pull 已由 mode 决定 */
    GPIO_Init(base, &gi);
    return HAL_OK;
}

static hal_err_t gpio_deinit(hal_gpio_t *g, uint8_t pin)
{
    uint8_t       port = HAL_PIN_PORT(pin);
    uint8_t       num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT) return HAL_ERR_PARAM;

    GPIO_InitTypeDef gi = { 0 };
    gi.GPIO_Pin  = (uint16_t)(1u << num);
    gi.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(base, &gi);
    return HAL_OK;
}

/* ===========================================================================
 * 读写
 * =========================================================================== */
static void gpio_write(hal_gpio_t *g, uint8_t pin, uint8_t level)
{
    uint8_t       port = HAL_PIN_PORT(pin);
    uint8_t       num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT) return;

    if (level) GPIO_SetBits(base, (uint16_t)(1u << num));
    else       GPIO_ResetBits(base, (uint16_t)(1u << num));
}

static uint8_t gpio_read(hal_gpio_t *g, uint8_t pin)
{
    uint8_t       port = HAL_PIN_PORT(pin);
    uint8_t       num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT) return 0;

    return GPIO_ReadInputDataBit(base, (uint16_t)(1u << num)) ? 1 : 0;
}

static void gpio_toggle(hal_gpio_t *g, uint8_t pin)
{
    uint8_t       port = HAL_PIN_PORT(pin);
    uint8_t       num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT) return;

    if (GPIO_ReadOutputDataBit(base, (uint16_t)(1u << num))) {
        GPIO_ResetBits(base, (uint16_t)(1u << num));
    } else {
        GPIO_SetBits(base, (uint16_t)(1u << num));
    }
}

static void gpio_write_port(hal_gpio_t *g, uint8_t port, uint32_t value)
{
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base) return;

    GPIO_Write(base, (uint16_t)value);
}

static uint32_t gpio_read_port(hal_gpio_t *g, uint8_t port)
{
    GPIO_TypeDef *base = port_base(port);

    HAL_UNUSED(g);
    if (!base) return 0;

    return (uint32_t)GPIO_ReadInputData(base);
}

/* ===========================================================================
 * 中断
 * =========================================================================== */
static hal_err_t gpio_set_irq(hal_gpio_t *g, uint8_t pin, hal_gpio_irq_t edge,
                              hal_gpio_irq_cb_t cb, void *user)
{
    uint8_t       port = HAL_PIN_PORT(pin);
    uint8_t       num  = HAL_PIN_NUM(pin);
    GPIO_TypeDef *base = port_base(port);
    EXTI_InitTypeDef ei;
    uint8_t       port_src;

    HAL_UNUSED(g);
    if (!base || num >= GPIO_PINS_PER_PORT || port >= 3) return HAL_ERR_PARAM;

    s_irq_cb[port][num]   = cb;
    s_irq_user[port][num] = user;

    if (edge == HAL_GPIO_IRQ_NONE) return HAL_OK;

    /* EXTI 线号 = 引脚号，端口源选择 */
    port_src = (uint8_t)port;   /* 0=PA 1=PB 2=PC */
    GPIO_EXTILineConfig(port_src, num);

    ei.EXTI_Line    = (uint32_t)(1u << num);
    ei.EXTI_Mode    = EXTI_Mode_Interrupt;
    ei.EXTI_Trigger = (edge == HAL_GPIO_IRQ_RISING)  ? EXTI_Trigger_Rising :
                      (edge == HAL_GPIO_IRQ_FALLING) ? EXTI_Trigger_Falling :
                                                       EXTI_Trigger_Rising_Falling;
    ei.EXTI_LineCmd = ENABLE;
    EXTI_Init(&ei);

    /* 使能对应的 EXTI 中断线（7_0 / 15_8 / 25_16） */
    {
        IRQn_Type irqn = (num < 8)  ? CH32_IRQ_EXTI_LOW :
                         (num < 16) ? CH32_IRQ_EXTI_MID : CH32_IRQ_EXTI_HIGH;
        hal_irq_enable(irqn);
    }
    return HAL_OK;
}

static void gpio_irq_dispatch(hal_gpio_t *g, uint8_t port)
{
    int i;
    HAL_UNUSED(g);

    if (port >= 3) return;

    for (i = 0; i < GPIO_PINS_PER_PORT; i++) {
        if (EXTI_GetITStatus((uint32_t)(1u << i)) != RESET) {
            EXTI_ClearITPendingBit((uint32_t)(1u << i));
            if (s_irq_cb[port][i]) {
                s_irq_cb[port][i](HAL_PIN(port, i), s_irq_user[port][i]);
            }
        }
    }
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static hal_gpio_t s_gpio = {
    .dev = { .name = "gpio", .initialized = 0 },
    .init         = gpio_init,
    .deinit       = gpio_deinit,
    .write        = gpio_write,
    .read         = gpio_read,
    .toggle       = gpio_toggle,
    .write_port   = gpio_write_port,
    .read_port    = gpio_read_port,
    .set_irq      = gpio_set_irq,
    .irq_dispatch = gpio_irq_dispatch,
};

hal_gpio_t *hal_gpio_get(void)
{
    return &s_gpio;
}
