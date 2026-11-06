/**
 * @file    uart_demo.c
 * @brief   阶段 3 验收：UART + 可切换日志
 *
 * 验证：
 *   1. UART1 (PA9=TXD, PA8=RXD) 能输出
 *   2. 日志后端可运行时切换（UART -> RAM -> UART）
 *   3. 接收回显
 *
 * CH59x 的 UART1 默认引脚：PA9 = TXD1, PA8 = RXD1
 */
#include "hal_core.h"

static hal_uart_t *g_uart;

/* 接收回调：原样回显 */
static void on_rx(hal_uart_t *u, const uint8_t *data, uint16_t len, void *user)
{
    u->write(u, data, len);
}

int main(void)
{
    hal_err_t      err;
    hal_uart_t    *u;
    hal_uart_cfg_t cfg;
    uint32_t       cnt = 0;

    err = hal_init();
    if (err != HAL_OK) {
        while (1) { }
    }

    /* ---- UART1 初始化 ---- */
    u = hal_uart_get(1);
    if (!u) while (1) { }

    cfg.baudrate  = 115200;
    cfg.data_bits = 8;
    cfg.stop_bits = 1;
    cfg.parity    = 0;
    cfg.tx_pin    = HAL_PA9;
    cfg.rx_pin    = HAL_PA8;
    cfg.use_dma   = 0;

    err = u->init(u, &cfg);
    if (err != HAL_OK) {
        while (1) { }
    }
    g_uart = u;

    /* ---- 日志后端切到 UART1 ---- */
    hal_log_init();
    hal_log_set_sink(HAL_LOG_SINK_UART, u);
    hal_log_set_level(HAL_LOG_LVL_DEBUG);

    LOG_I("\r\n=== CH59x HAL UART Demo ===\r\n");
    LOG_I("clock = %u Hz\r\n",
          hal_sys_get()->get_clock_hz(hal_sys_get()));
    LOG_I("uart1 ready, baud=115200\r\n");

    u->set_rx_cb(u, on_rx, NULL);

    while (1) {
        cnt++;
        LOG_I("[%u] tick=%u\r\n", cnt,
              hal_time_get()->get_tick_ms(hal_time_get()));

        /* 每 5 次演示一次后端切换：UART -> RAM -> UART */
        if ((cnt % 5) == 0) {
            uint8_t tmp[64];
            uint16_t n;

            LOG_I("-- switch to RAM sink --\r\n");
            hal_log_set_sink(HAL_LOG_SINK_RAM, NULL);
            LOG_I("this line goes to RAM buffer\r\n");

            n = hal_log_ram_read(tmp, sizeof(tmp));
            hal_log_set_sink(HAL_LOG_SINK_UART, u);
            LOG_I("-- back to UART, %u bytes were buffered --\r\n", n);
            if (n) u->write(u, tmp, n);
        }

        hal_time_get()->delay_ms(hal_time_get(), 1000);
    }
}
