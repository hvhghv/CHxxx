/**
 * @file    hal_uart_ch572.c
 * @brief   CH572 UART 实现
 *
 * ★ CH572 与 CH59x 的重大差异：
 *   - 只有 **1 个 UART**（R8_UART_*，无编号），不是 4 个
 *   - 官方 API 无编号：UART_DefInit() / UART_BaudRateCfg() / UART_INTCfg()
 *   - 中断向量只有一个 UART_IRQHandler（UART_IRQn = 27）
 *
 * 因此 hal_uart_get(0) 返回唯一实例，index > 0 返回 NULL。
 */
#include "hal_core.h"
#include "CH57x_common.h"
#include <string.h>

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
#ifndef HAL_UART_RX_BUF_SIZE
#define HAL_UART_RX_BUF_SIZE 256
#endif
#ifndef HAL_UART_TX_BUF_SIZE
#define HAL_UART_TX_BUF_SIZE 256
#endif
#define UART_RX_BUF_SIZE   HAL_UART_RX_BUF_SIZE
#define UART_TX_BUF_SIZE   HAL_UART_TX_BUF_SIZE

typedef struct {
    hal_uart_t base;

    uint8_t  tx_pin, rx_pin;
    uint8_t  initialized;

    /* 环形缓冲 */
    uint8_t  rx_buf[UART_RX_BUF_SIZE];
    uint16_t rx_head, rx_tail;
    uint8_t  tx_buf[UART_TX_BUF_SIZE];
    uint16_t tx_head, tx_tail;

    hal_uart_rx_cb_t      rx_cb;
    void                 *rx_user;
    hal_uart_tx_done_cb_t tx_done_cb;
    void                 *tx_done_user;
} uart_ctx_t;

static uart_ctx_t s_uart;

/* ===========================================================================
 * 中断处理
 * =========================================================================== */
static void uart_irq_handler(hal_uart_t *u)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uint8_t iir = R8_UART_IIR;

    if (iir & RB_IIR_NO_INT) return;

    {
        uint8_t type = (uint8_t)((iir & 0x0E) >> 1);

        if (type == 2 || type == 6) {          /* RX 数据可用 / RX 超时 */
            uint8_t last = 0;
            uint8_t got  = 0;
            while (R8_UART_LSR & RB_LSR_DATA_RDY) {
                uint8_t b = R8_UART_RBR;
                uint16_t next = (uint16_t)((c->rx_head + 1) % UART_RX_BUF_SIZE);
                if (next != c->rx_tail) {
                    c->rx_buf[c->rx_head] = b;
                    c->rx_head = next;
                }
                last = b;
                got  = 1;
            }
            if (got && c->rx_cb) {
                c->rx_cb(u, &last, 1, c->rx_user);
            }
        } else if (type == 1) {                /* THR 空 */
            while (c->tx_tail != c->tx_head) {
                if (R8_UART_TFC >= 8) break;   /* FIFO 满 */
                R8_UART_THR = c->tx_buf[c->tx_tail];
                c->tx_tail = (uint16_t)((c->tx_tail + 1) % UART_TX_BUF_SIZE);
            }
            if (c->tx_tail == c->tx_head) {
                /* 发送完成，关 THR 空中断 */
                R8_UART_IER &= (uint8_t)~RB_IER_THR_EMPTY;
                if (c->tx_done_cb) {
                    c->tx_done_cb(u, c->tx_done_user);
                }
            }
        }
    }
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t uart_init(hal_uart_t *u, const hal_uart_cfg_t *cfg)
{
    uart_ctx_t *c = (uart_ctx_t *)u;

    if (!cfg || cfg->baudrate == 0) return HAL_ERR_PARAM;

    c->tx_pin = cfg->tx_pin;
    c->rx_pin = cfg->rx_pin;
    c->rx_head = c->rx_tail = 0;
    c->tx_head = c->tx_tail = 0;

    /* 官方初始化（内部设置默认引脚、8N1、FIFO） */
    UART_DefInit();

    /* 按需改波特率 */
    if (cfg->baudrate != 115200) {
        UART_BaudRateCfg(cfg->baudrate);
    }

    /* 使能 RX 数据可用 + RX 超时中断；THR 空中断在发送时才开 */
    R8_UART_IER |= (uint8_t)(RB_IER_RECV_RDY | RB_IER_RECV_RDY);
    R8_UART_IER &= (uint8_t)~RB_IER_THR_EMPTY;

    c->initialized = 1;
    u->dev.initialized = 1;
    return HAL_OK;
}

static hal_err_t uart_deinit(hal_uart_t *u)
{
    uart_ctx_t *c = (uart_ctx_t *)u;

    hal_irq_disable(UART_IRQn);
    R8_UART_IER = 0;
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
    uint16_t i;

    if (!c->initialized || !buf) return HAL_ERR_PARAM;

    /* 阻塞发送：直接写 THR，等待 FIFO 有空位 */
    for (i = 0; i < len; i++) {
        while (R8_UART_TFC >= 8) { }
        R8_UART_THR = buf[i];
    }
    return HAL_OK;
}

static int uart_write_async(hal_uart_t *u, const uint8_t *buf, uint16_t len)
{
    uart_ctx_t *c = (uart_ctx_t *)u;
    uint16_t i;

    if (!c->initialized || !buf) return HAL_ERR_PARAM;

    for (i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((c->tx_head + 1) % UART_TX_BUF_SIZE);
        if (next == c->tx_tail) return (int)i;      /* 缓冲满 */
        c->tx_buf[c->tx_head] = buf[i];
        c->tx_head = next;
    }

    /* 立刻启动发送 */
    while (c->tx_tail != c->tx_head && R8_UART_TFC < 8) {
        R8_UART_THR = c->tx_buf[c->tx_tail];
        c->tx_tail = (uint16_t)((c->tx_tail + 1) % UART_TX_BUF_SIZE);
    }
    if (c->tx_tail != c->tx_head) {
        R8_UART_IER |= RB_IER_THR_EMPTY;
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

    if (!c->initialized || !buf) return HAL_ERR_PARAM;

    while (n < len) {
        if (c->rx_tail != c->rx_head) {
            buf[n++] = c->rx_buf[c->rx_tail];
            c->rx_tail = (uint16_t)((c->rx_tail + 1) % UART_RX_BUF_SIZE);
            start = t->get_tick_ms(t);      /* 收到数据后重置超时 */
        } else {
            if (timeout_ms != 0 && (t->get_tick_ms(t) - start) >= timeout_ms) break;
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
static void uart_init_instance(void)
{
    s_uart.base.dev.name        = "uart0";
    s_uart.base.dev.initialized = 0;
    s_uart.base.init            = uart_init;
    s_uart.base.deinit          = uart_deinit;
    s_uart.base.write           = uart_write;
    s_uart.base.write_async     = uart_write_async;
    s_uart.base.read            = uart_read;
    s_uart.base.set_rx_cb       = uart_set_rx_cb;
    s_uart.base.set_tx_done_cb  = uart_set_tx_done_cb;
}

hal_uart_t *hal_uart_get(uint8_t index)
{
    static uint8_t installed = 0;
    if (!installed) {
        installed = 1;
        uart_init_instance();
    }
    /* ★ CH572 只有 1 个 UART */
    return (index == 0) ? &s_uart.base : NULL;
}

/* 供中断 trampoline 调用（UART_IRQHandler → hal_irq_dispatch → 这里） */
void hal_uart_irq_entry(void)
{
    if (s_uart.initialized) {
        uart_irq_handler(&s_uart.base);
    }
}
