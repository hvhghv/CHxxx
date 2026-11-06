/**
 * @file    hal_uart_ch32x035.c
 * @brief   CH32X035 USART 实现
 *
 * ★ CH32X035 有 USART1~USART4（4 个），API 是 STM32 风格：
 *   USART_Init(USARTx, &init) / USART_SendData() / USART_ReceiveData()
 *   中断向量分别 CH32_IRQ_USART1(32) / CH32_IRQ_USART2(39) / CH32_IRQ_USART3(42) / CH32_IRQ_USART4(43)
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

#define UART_MAX          4
#define UART_RX_BUF_SIZE  256
#define UART_TX_BUF_SIZE  256

typedef struct {
    hal_uart_t base;

    uint8_t  tx_pin, rx_pin;
    uint8_t  initialized;

    uint8_t  rx_buf[UART_RX_BUF_SIZE];
    uint16_t rx_head, rx_tail;
    uint8_t  tx_buf[UART_TX_BUF_SIZE];
    uint16_t tx_head, tx_tail;

    hal_uart_rx_cb_t      rx_cb;
    void                 *rx_user;
    hal_uart_tx_done_cb_t tx_done_cb;
    void                 *tx_done_user;
} uart_ctx_t;

static uart_ctx_t s_uart[UART_MAX];

/* 索引 → USART 外设 */
static USART_TypeDef *uart_base(uint8_t idx)
{
    switch (idx) {
    case 0: return USART1;
    case 1: return USART2;
    case 2: return USART3;
    case 3: return CH32_USART4_PERIPH;
    default: return NULL;
    }
}

static IRQn_Type uart_irqn(uint8_t idx)
{
    switch (idx) {
    case 0: return CH32_IRQ_USART1;
    case 1: return CH32_IRQ_USART2;
    case 2: return CH32_IRQ_USART3;
    default: return CH32_IRQ_USART4;
    }
}

static uint32_t uart_clock(uint8_t idx)
{
    switch (idx) {
    case 0: return CH32_USART1_CLK;
    case 1: return CH32_USART2_CLK;
    case 2: return CH32_USART3_CLK;
    default: return CH32_USART4_CLK;
    }
}

/* ===========================================================================
 * 中断处理
 * =========================================================================== */
static void uart_irq_handler(hal_uart_t *u)
{
    uart_ctx_t     *c    = (uart_ctx_t *)u;
    USART_TypeDef  *base = uart_base((uint8_t)(c - s_uart));

    if (!base) return;

    /* 接收 */
    if (USART_GetITStatus(base, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(base);
        uint16_t next = (uint16_t)((c->rx_head + 1) % UART_RX_BUF_SIZE);
        if (next != c->rx_tail) {
            c->rx_buf[c->rx_head] = b;
            c->rx_head = next;
        }
        if (c->rx_cb) c->rx_cb(u, &b, 1, c->rx_user);
    }

    /* 发送 */
    if (USART_GetITStatus(base, USART_IT_TXE) != RESET) {
        if (c->tx_tail != c->tx_head) {
            USART_SendData(base, c->tx_buf[c->tx_tail]);
            c->tx_tail = (uint16_t)((c->tx_tail + 1) % UART_TX_BUF_SIZE);
        } else {
            USART_ITConfig(base, USART_IT_TXE, DISABLE);
            if (c->tx_done_cb) c->tx_done_cb(u, c->tx_done_user);
        }
    }
}

/* 各 UART 的中断入口（由 hal_irq 层分发） */
static void usart1_isr(void *user) { HAL_UNUSED(user); uart_irq_handler(&s_uart[0].base); }
static void usart2_isr(void *user) { HAL_UNUSED(user); uart_irq_handler(&s_uart[1].base); }
static void usart3_isr(void *user) { HAL_UNUSED(user); uart_irq_handler(&s_uart[2].base); }
static void usart4_isr(void *user) { HAL_UNUSED(user); uart_irq_handler(&s_uart[3].base); }

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t uart_init(hal_uart_t *u, const hal_uart_cfg_t *cfg)
{
    uart_ctx_t      *c    = (uart_ctx_t *)u;
    uint8_t          idx  = (uint8_t)(c - s_uart);
    USART_TypeDef   *base = uart_base(idx);
    USART_InitTypeDef ui;

    if (!cfg || cfg->baudrate == 0 || !base) return HAL_ERR_PARAM;

    c->tx_pin = cfg->tx_pin;
    c->rx_pin = cfg->rx_pin;
    c->rx_head = c->rx_tail = 0;
    c->tx_head = c->tx_tail = 0;

    /* 时钟 */
    if (idx == 0) CH32_APB2_CLK_CMD(uart_clock(idx), ENABLE);
    else          CH32_APB1_CLK_CMD(uart_clock(idx), ENABLE);

    /* 8N1 */
    USART_StructInit(&ui);
    ui.USART_BaudRate            = cfg->baudrate;
    ui.USART_WordLength          = USART_WordLength_8b;
    ui.USART_StopBits            = USART_StopBits_1;
    ui.USART_Parity              = USART_Parity_No;
    ui.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    ui.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(base, &ui);

    /* 使能 RXNE 中断 */
    USART_ITConfig(base, USART_IT_RXNE, ENABLE);
    USART_Cmd(base, ENABLE);

    c->initialized = 1;
    u->dev.initialized = 1;
    return HAL_OK;
}

static hal_err_t uart_deinit(hal_uart_t *u)
{
    uart_ctx_t     *c    = (uart_ctx_t *)u;
    uint8_t         idx  = (uint8_t)(c - s_uart);
    USART_TypeDef  *base = uart_base(idx);

    if (!base) return HAL_ERR_PARAM;

    hal_irq_disable(uart_irqn(idx));
    USART_Cmd(base, DISABLE);
    c->initialized = 0;
    u->dev.initialized = 0;
    return HAL_OK;
}

/* ===========================================================================
 * 发送
 * =========================================================================== */
static hal_err_t uart_write(hal_uart_t *u, const uint8_t *buf, uint16_t len)
{
    uart_ctx_t     *c    = (uart_ctx_t *)u;
    uint8_t         idx  = (uint8_t)(c - s_uart);
    USART_TypeDef  *base = uart_base(idx);
    uint16_t        i;

    if (!c->initialized || !buf || !base) return HAL_ERR_PARAM;

    for (i = 0; i < len; i++) {
        while (USART_GetFlagStatus(base, USART_FLAG_TXE) == RESET) { }
        USART_SendData(base, buf[i]);
    }
    while (USART_GetFlagStatus(base, USART_FLAG_TC) == RESET) { }
    return HAL_OK;
}

static int uart_write_async(hal_uart_t *u, const uint8_t *buf, uint16_t len)
{
    uart_ctx_t     *c    = (uart_ctx_t *)u;
    uint8_t         idx  = (uint8_t)(c - s_uart);
    USART_TypeDef  *base = uart_base(idx);
    uint16_t        i;

    if (!c->initialized || !buf || !base) return HAL_ERR_PARAM;

    for (i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((c->tx_head + 1) % UART_TX_BUF_SIZE);
        if (next == c->tx_tail) break;
        c->tx_buf[c->tx_head] = buf[i];
        c->tx_head = next;
    }

    /* 启动 TXE 中断驱动发送 */
    if (c->tx_tail != c->tx_head) {
        USART_ITConfig(base, USART_IT_TXE, ENABLE);
    }
    return (int)i;
}

/* ===========================================================================
 * 接收
 * =========================================================================== */
static int uart_read(hal_uart_t *u, uint8_t *buf, uint16_t len,
                     uint32_t timeout_ms)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uint16_t    n = 0;
    hal_time_t *t = hal_time_get();
    uint32_t    start = t->get_tick_ms(t);

    if (!c->initialized || !buf) return HAL_ERR_PARAM;

    while (n < len) {
        if (c->rx_tail != c->rx_head) {
            buf[n++] = c->rx_buf[c->rx_tail];
            c->rx_tail = (uint16_t)((c->rx_tail + 1) % UART_RX_BUF_SIZE);
            start = t->get_tick_ms(t);
        } else {
            if (timeout_ms != 0 &&
                (t->get_tick_ms(t) - start) >= timeout_ms) break;
            if (timeout_ms == 0 && n > 0) break;
        }
    }
    return (int)n;
}

static hal_err_t uart_set_rx_cb(hal_uart_t *u, hal_uart_rx_cb_t cb, void *user)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    c->rx_cb = cb;
    c->rx_user = user;
    return HAL_OK;
}

static hal_err_t uart_set_tx_done_cb(hal_uart_t *u, hal_uart_tx_done_cb_t cb,
                                     void *user)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    c->tx_done_cb = cb;
    c->tx_done_user = user;
    return HAL_OK;
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static void uart_init_instances(void)
{
    static const char *names[UART_MAX] = { "usart1", "usart2", "usart3", "usart4" };
    int i;

    for (i = 0; i < UART_MAX; i++) {
        s_uart[i].base.dev.name        = names[i];
        s_uart[i].base.dev.initialized = 0;
        s_uart[i].base.init            = uart_init;
        s_uart[i].base.deinit          = uart_deinit;
        s_uart[i].base.write           = uart_write;
        s_uart[i].base.write_async     = uart_write_async;
        s_uart[i].base.read            = uart_read;
        s_uart[i].base.set_rx_cb       = uart_set_rx_cb;
        s_uart[i].base.set_tx_done_cb  = uart_set_tx_done_cb;
        s_uart[i].base.irq_handler     = uart_irq_handler;
    }
}

hal_uart_t *hal_uart_get(uint8_t index)
{
    static uint8_t installed = 0;
    static uint8_t irq_registered = 0;

    if (!installed) {
        installed = 1;
        uart_init_instances();
    }
    if (index >= UART_MAX) return NULL;

    /* 首次访问时注册中断处理函数 */
    if (!irq_registered) {
        irq_registered = 1;
        hal_irq_attach(CH32_IRQ_USART1, usart1_isr, NULL);
        hal_irq_attach(CH32_IRQ_USART2, usart2_isr, NULL);
        hal_irq_attach(CH32_IRQ_USART3, usart3_isr, NULL);
        hal_irq_attach(CH32_IRQ_USART4, usart4_isr, NULL);
        hal_irq_enable(CH32_IRQ_USART1);
        hal_irq_enable(CH32_IRQ_USART2);
        hal_irq_enable(CH32_IRQ_USART3);
        hal_irq_enable(CH32_IRQ_USART4);
    }
    return &s_uart[index].base;
}
