/**
 * @file    blink.c
 * @brief   HAL 架构验证示例：PA13 闪灯 + UART 日志
 *
 * 目标：验证新架构（common + port + vendor）能编译、能运行。
 *
 * ★ 调试通道用 UART（PA9=TX, 115200），不用 EEPROM：
 *   EEPROM 作为调试通道不可靠（连续写入若干次后会失效），
 *   而 UART 是官方支持、实时、可靠的通道。
 *
 * 接线：PA9 -> USB-TTL 的 RX，GND -> GND，115200 8N1
 */
#include "hal_core.h"
/* ★ 不直接包含芯片头文件（CH59x_common.h / ch32x035.h ...），
 *   全部通过 hal_* 抽象访问，保证多芯片可编译。 */

/* 简易无符号十进制转字符串，返回写入长度 */
static int put_u32(char *buf, uint32_t v)
{
    char t[12];
    int  k = 0;
    int  n = 0;

    if (v == 0) { buf[n++] = '0'; return n; }
    while (v) { t[k++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (k) buf[n++] = t[--k];
    return n;
}

int main(void)
{
    hal_err_t      err;
    hal_gpio_t    *gpio;
    hal_time_t    *time;
    hal_uart_t    *uart;
    hal_uart_cfg_t cfg;
    uint32_t       hb = 0;
    uint32_t       last_tick = 0;

    /* 通用层初始化（时钟 + 时基 + GPIO） */
    err = hal_init();

    gpio = hal_gpio_get();
    time = hal_time_get();

    /* PA13 推挽输出 */
    gpio->init(gpio, HAL_PA13, HAL_GPIO_OUT_PP, 0);

    /* ---- UART0 日志：PA9=TX, PA8=RX, 115200 ---- */
    uart = hal_uart_get(0);
    cfg.baudrate  = 115200;
    cfg.data_bits = 8;
    cfg.stop_bits = 1;
    cfg.parity    = 0;
    cfg.tx_pin    = HAL_PA9;
    cfg.rx_pin    = HAL_PA8;
    cfg.use_dma   = 0;
    uart->init(uart, &cfg);

    uart->write(uart, (const uint8_t *)"\r\n=== HAL blink start ===\r\n", 27);

    if (err != HAL_OK) {
        uart->write(uart, (const uint8_t *)"hal_init FAILED\r\n", 17);
        while (1) { }
    }
    uart->write(uart, (const uint8_t *)"hal_init OK\r\n", 13);

    last_tick = time->get_tick_ms(time);

    while (1) {
        gpio->toggle(gpio, HAL_PA13);
        time->delay_ms(time, 500);

        hb++;

        /* 每 2 秒打印一次心跳 + tick，验证时基是否在走 */
        if ((hb & 0x03u) == 0) {
            uint32_t now = time->get_tick_ms(time);
            char     buf[64];
            int      n = 0;

            buf[n++] = 'h'; buf[n++] = 'b'; buf[n++] = '=';
            n += put_u32(&buf[n], hb / 4u);
            buf[n++] = ' '; buf[n++] = 't'; buf[n++] = 'i';
            buf[n++] = 'c'; buf[n++] = 'k'; buf[n++] = '=';
            n += put_u32(&buf[n], now);
            buf[n++] = ' '; buf[n++] = 'd'; buf[n++] = 't'; buf[n++] = '=';
            n += put_u32(&buf[n], now - last_tick);
            buf[n++] = '\r'; buf[n++] = '\n';
            last_tick = now;

            uart->write(uart, (const uint8_t *)buf, (uint32_t)n);
        }
    }
}
