/**
 * @file    hal_uart_ch585.c
 * @brief   CH585 UART 实现（基于官方 StdPeriphDriver）
 *
 * ★ 关键坑：必须置位 R8_UARTx_IER 的 RB_IER_TXD_EN(0x40)，
 *          否则 TXD 引脚无波形（TFC 仍读 0，看起来发送完成但实际没输出）。
 */
#include "hal_core.h"
#include "CH58x_common.h"
#include <string.h>

/* CH592 只有 26KB RAM，缓冲不能太大。
 * 4 个 UART 实例 × (128 rx + 128 tx) = 1KB，可接受。 */
#define UART_RX_BUF_SIZE   128
#define UART_TX_BUF_SIZE   128

typedef struct {
    hal_uart_t           base;
    uint8_t              index;
    volatile uint8_t     initialized;

    /* 环形接收缓冲 */
    volatile uint16_t    rx_head;
    volatile uint16_t    rx_tail;
    uint8_t              rx_buf[UART_RX_BUF_SIZE];

    /* 环形发送缓冲 */
    volatile uint16_t    tx_head;
    volatile uint16_t    tx_tail;
    uint8_t              tx_buf[UART_TX_BUF_SIZE];

    hal_uart_rx_cb_t     rx_cb;
    void                *rx_user;
    hal_uart_tx_done_cb_t tx_done_cb;
    void                *tx_done_user;

    /* 引脚（用于配置） */
    uint8_t              tx_pin;
    uint8_t              rx_pin;
} uart_ctx_t;

static uart_ctx_t s_uart[4];

/* ===========================================================================
 * 端口寄存器映射
 * =========================================================================== */
typedef struct {
    volatile uint8_t  *ier;      /* R8_UARTx_IER */
    volatile uint8_t  *fcr;      /* R8_UARTx_FCR */
    volatile uint8_t  *lcr;      /* R8_UARTx_LCR */
    volatile uint8_t  *iir;      /* R8_UARTx_IIR */
    volatile uint8_t  *thr;      /* R8_UARTx_THR */
    volatile uint8_t  *rbr;      /* R8_UARTx_RBR */
    volatile uint8_t  *lsr;      /* R8_UARTx_LSR */
} uart_regs_t;

static void uart_get_regs(uint8_t idx, uart_regs_t *r)
{
    switch (idx) {
    case 0:
        r->ier = &R8_UART0_IER; r->fcr = &R8_UART0_FCR;
        r->lcr = &R8_UART0_LCR; r->iir = &R8_UART0_IIR;
        r->thr = &R8_UART0_THR; r->rbr = &R8_UART0_RBR;
        r->lsr = &R8_UART0_LSR;
        break;
    case 1:
        r->ier = &R8_UART1_IER; r->fcr = &R8_UART1_FCR;
        r->lcr = &R8_UART1_LCR; r->iir = &R8_UART1_IIR;
        r->thr = &R8_UART1_THR; r->rbr = &R8_UART1_RBR;
        r->lsr = &R8_UART1_LSR;
        break;
    case 2:
        r->ier = &R8_UART2_IER; r->fcr = &R8_UART2_FCR;
        r->lcr = &R8_UART2_LCR; r->iir = &R8_UART2_IIR;
        r->thr = &R8_UART2_THR; r->rbr = &R8_UART2_RBR;
        r->lsr = &R8_UART2_LSR;
        break;
    default:
        r->ier = &R8_UART3_IER; r->fcr = &R8_UART3_FCR;
        r->lcr = &R8_UART3_LCR; r->iir = &R8_UART3_IIR;
        r->thr = &R8_UART3_THR; r->rbr = &R8_UART3_RBR;
        r->lsr = &R8_UART3_LSR;
        break;
    }
}

static IRQn_Type uart_irqn(uint8_t idx)
{
    switch (idx) {
    case 0:  return UART0_IRQn;
    case 1:  return UART1_IRQn;
    case 2:  return UART2_IRQn;
    default: return UART3_IRQn;
    }
}

/* ===========================================================================
 * GPIO 复用配置
 * =========================================================================== */
static void uart_pin_init(uint8_t idx, uint8_t tx_pin, uint8_t rx_pin)
{
    /* 官方 UARTx_DefInit 内部会配置默认引脚。
     * 这里只处理非默认引脚的重映射（简化：仅支持 CH585 默认引脚）。 */
    HAL_UNUSED(idx);
    HAL_UNUSED(tx_pin);
    HAL_UNUSED(rx_pin);
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t uart_init(hal_uart_t *u, const hal_uart_cfg_t *cfg)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uart_regs_t r;

    if (!cfg || cfg->baudrate == 0) return HAL_ERR_PARAM;
    if (c->index > 3) return HAL_ERR_PARAM;

    uart_get_regs(c->index, &r);

    c->tx_pin = cfg->tx_pin;
    c->rx_pin = cfg->rx_pin;
    c->rx_head = c->rx_tail = 0;
    c->tx_head = c->tx_tail = 0;

    /* 用官方初始化（内部设置波特率、8N1、FIFO） */
    switch (c->index) {
    case 0: UART0_DefInit(); break;
    case 1: UART1_DefInit(); break;
    case 2: UART2_DefInit(); break;
    default: UART3_DefInit(); break;
    }

    /* 按需改波特率 */
    if (cfg->baudrate != 115200) {
        switch (c->index) {
        case 0: UART0_BaudRateCfg(cfg->baudrate); break;
        case 1: UART1_BaudRateCfg(cfg->baudrate); break;
        case 2: UART2_BaudRateCfg(cfg->baudrate); break;
        default: UART3_BaudRateCfg(cfg->baudrate); break;
        }
    }

    /* ★ 关键：TXD 引脚使能，否则无输出 */
    *r.ier = RB_IER_TXD_EN;
    /* 接收中断使能 */
    *r.ier |= RB_IER_RECV_RDY;

    uart_pin_init(c->index, cfg->tx_pin, cfg->rx_pin);

    hal_irq_attach(uart_irqn(c->index), (hal_isr_t)u->irq_handler, u);
    hal_irq_enable(uart_irqn(c->index));

    c->initialized = 1;
    u->dev.initialized = 1;
    return HAL_OK;
}

static hal_err_t uart_deinit(hal_uart_t *u)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uart_regs_t r;
    uart_get_regs(c->index, &r);
    *r.ier = 0;
    hal_irq_disable(uart_irqn(c->index));
    c->initialized = 0;
    u->dev.initialized = 0;
    return HAL_OK;
}

/* ===========================================================================
 * 发送
 * =========================================================================== */
static hal_err_t uart_write(hal_uart_t *u, const uint8_t *buf, uint16_t len)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uart_regs_t r;
    uint16_t i;
    uint32_t guard = 0;

    if (!buf || len == 0) return HAL_ERR_PARAM;
    if (!c->initialized) return HAL_ERR_NOINIT;

    uart_get_regs(c->index, &r);

    for (i = 0; i < len; i++) {
        /* 等 THR 空（LSR bit7 = TEMT） */
        guard = 0;
        while ((*r.lsr & 0x80) == 0) {
            if (++guard > 2000000u) return HAL_ERR_TIMEOUT;
        }
        *r.thr = buf[i];
    }
    return HAL_OK;
}

static int uart_write_async(hal_uart_t *u, const uint8_t *buf, uint16_t len)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uint16_t i, next;

    if (!buf || len == 0 || !c->initialized) return 0;

    for (i = 0; i < len; i++) {
        next = (uint16_t)((c->tx_head + 1) % UART_TX_BUF_SIZE);
        if (next == c->tx_tail) break;      /* 满 */
        c->tx_buf[c->tx_head] = buf[i];
        c->tx_head = next;
    }

    /* 使能发送空中断，由 ISR 继续发送 */
    {
        uart_regs_t r;
        uart_get_regs(c->index, &r);
        *r.ier |= RB_IER_THR_EMPTY;
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
    uint16_t n = 0;
    hal_time_t *t = hal_time_get();
    uint32_t start = t->get_tick_ms(t);

    if (!buf || len == 0 || !c->initialized) return 0;

    while (n < len) {
        if (c->rx_tail != c->rx_head) {
            buf[n++] = c->rx_buf[c->rx_tail];
            c->rx_tail = (uint16_t)((c->rx_tail + 1) % UART_RX_BUF_SIZE);
        } else {
            if ((t->get_tick_ms(t) - start) >= timeout_ms) break;
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
 * 中断处理
 * =========================================================================== */
static void uart_irq_handler(hal_uart_t *u)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uart_regs_t r;
    uint8_t iir;

    uart_get_regs(c->index, &r);

    for (;;) {
        iir = *r.iir;
        if (iir & 0x01) break;              /* 无中断 */

        switch (iir & 0x0E) {
        case 0x02:                          /* THR 空 */
            if (c->tx_tail != c->tx_head) {
                *r.thr = c->tx_buf[c->tx_tail];
                c->tx_tail = (uint16_t)((c->tx_tail + 1) % UART_TX_BUF_SIZE);
            } else {
                *r.ier &= (uint8_t)~RB_IER_THR_EMPTY;
                if (c->tx_done_cb) c->tx_done_cb(u, c->tx_done_user);
            }
            break;

        case 0x04:                          /* 接收数据可用 */
            while (*r.lsr & 0x01) {         /* LSR bit0 = DR */
                uint8_t ch = *r.rbr;
                uint16_t next = (uint16_t)((c->rx_head + 1) % UART_RX_BUF_SIZE);
                if (next != c->rx_tail) {
                    c->rx_buf[c->rx_head] = ch;
                    c->rx_head = next;
                }
            }
            break;

        case 0x06:                          /* 接收线路状态 */
            (void)*r.lsr;
            break;

        case 0x0C:                          /* 接收超时 */
            while (*r.lsr & 0x01) {
                uint8_t ch = *r.rbr;
                uint16_t next = (uint16_t)((c->rx_head + 1) % UART_RX_BUF_SIZE);
                if (next != c->rx_tail) {
                    c->rx_buf[c->rx_head] = ch;
                    c->rx_head = next;
                }
            }
            break;

        default:
            break;
        }
    }

    /* 有完整数据时回调 */
    if (c->rx_cb && c->rx_tail != c->rx_head) {
        uint8_t tmp[64];
        uint16_t n = 0;
        while (n < sizeof(tmp) && c->rx_tail != c->rx_head) {
            tmp[n++] = c->rx_buf[c->rx_tail];
            c->rx_tail = (uint16_t)((c->rx_tail + 1) % UART_RX_BUF_SIZE);
        }
        if (n) c->rx_cb(u, tmp, n, c->rx_user);
    }
}

/* ===========================================================================
 * 实例表
 * ---------------------------------------------------------------------------
 * 注意：hal_uart_t 必须是 uart_ctx_t 的第一个成员，这样 (uart_ctx_t*)u
 *      和 &s_uart[n].base 可以互相转换。
 * =========================================================================== */
static void uart_init_instances(void)
{
    uint8_t i;
    static const char *names[4] = { "uart0", "uart1", "uart2", "uart3" };

    for (i = 0; i < 4; i++) {
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
        s_uart[i].index                = i;
    }
}

hal_uart_t *hal_uart_get(uint8_t index)
{
    static uint8_t inited = 0;

    if (index > 3) return NULL;
    if (!inited) {
        uart_init_instances();
        inited = 1;
    }
    return &s_uart[index].base;
}
