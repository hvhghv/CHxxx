/**
 * @file    hal_uart.h
 * @brief   UART 通用接口
 *
 * 用法：
 *   hal_uart_t *u = hal_uart_get(1);
 *   hal_uart_cfg_t cfg = { .baudrate=115200, .tx_pin=HAL_PA9, .rx_pin=HAL_PA8 };
 *   u->init(u, &cfg);
 *   u->write(u, (const uint8_t*)"hi\r\n", 4);
 */
#ifndef HAL_UART_H
#define HAL_UART_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_uart hal_uart_t;

/* 接收回调（在中断上下文执行，应尽量短） */
typedef void (*hal_uart_rx_cb_t)(hal_uart_t *u, const uint8_t *data,
                                 uint16_t len, void *user);
/* 发送完成回调 */
typedef void (*hal_uart_tx_done_cb_t)(hal_uart_t *u, void *user);

typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;     /**< 5..8，默认 8 */
    uint8_t  stop_bits;     /**< 1..2，默认 1 */
    uint8_t  parity;        /**< 0=none 1=odd 2=even */
    uint8_t  tx_pin;        /**< HAL_PIN 编码，HAL_PIN_NONE = 不用 */
    uint8_t  rx_pin;
    uint8_t  use_dma;       /**< 0=中断 1=DMA（未实现时忽略） */
} hal_uart_cfg_t;

struct hal_uart {
    hal_dev_t dev;

    hal_err_t (*init)  (hal_uart_t *u, const hal_uart_cfg_t *cfg);
    hal_err_t (*deinit)(hal_uart_t *u);

    /** 阻塞写：全部发出才返回（内部有超时保护） */
    hal_err_t (*write) (hal_uart_t *u, const uint8_t *buf, uint16_t len);
    /** 非阻塞写：返回实际入队字节数 */
    int       (*write_async)(hal_uart_t *u, const uint8_t *buf, uint16_t len);
    /** 阻塞读：读到 len 或超时才返回，返回实际读到的字节数 */
    int       (*read)  (hal_uart_t *u, uint8_t *buf, uint16_t len,
                        uint32_t timeout_ms);

    hal_err_t (*set_rx_cb)(hal_uart_t *u, hal_uart_rx_cb_t cb, void *user);
    hal_err_t (*set_tx_done_cb)(hal_uart_t *u, hal_uart_tx_done_cb_t cb,
                                void *user);

    /** 供 port 层内部使用：由 UARTx_IRQHandler 调用 */
    void      (*irq_handler)(hal_uart_t *u);
};

/**
 * 获取 UART 实例。
 * @param index 0..3（CH59x 有 UART0~UART3）
 * @return 实例指针，索引越界返回 NULL
 */
hal_uart_t *hal_uart_get(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
