/********************************** (C) COPYRIGHT *******************************
 * File Name          : periph.h
 * Description        : 外设驱动（PWM / ADC / UART）
 *
 * 用 ch32fun 的寄存器定义实现，避免引入官方 StdPeriphDriver。
 *******************************************************************************/

#ifndef _PERIPH_H
#define _PERIPH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * PWM（PWMX，通道 PWM4~PWM11）
 * ------------------------------------------------------------------------- */
typedef struct {
	uint8_t  channel;    /* PWM 通道（1=PWM4 ... 0x80=PWM11） */
	uint8_t  polar;      /* 0=高电平有效, 1=低电平有效 */
	uint16_t period;     /* 周期（16 位模式） */
	uint16_t duty;       /* 占空比 */
	uint8_t  enabled;
} pwm_cfg_t;

void periph_pwm_init(void);
void periph_pwm_set(pwm_cfg_t *cfg);      /* 配置并启动 */
void periph_pwm_start(uint8_t channel);   /* 启动 */
void periph_pwm_stop(uint8_t channel);    /* 停止 */
void periph_pwm_set_duty(uint8_t channel, uint16_t duty);

/* ---------------------------------------------------------------------------
 * ADC（12 通道）
 * ------------------------------------------------------------------------- */
void     periph_adc_init(void);
int      periph_adc_read(uint8_t channel);   /* 返回 0..4095，<0 错误 */
uint16_t periph_adc_read_raw(uint8_t channel);

/* ---------------------------------------------------------------------------
 * UART（UART0~UART3）
 * ------------------------------------------------------------------------- */
typedef struct {
	uint8_t  uart;       /* UART 编号 0..3 */
	uint32_t baud;
	uint8_t  data_bits;  /* 8 */
	uint8_t  parity;     /* 0=无, 1=奇, 2=偶 */
	uint8_t  stop_bits;  /* 1 或 2 */
} uart_cfg_t;

void periph_uart_init(uart_cfg_t *cfg);
int  periph_uart_write(uint8_t uart, const uint8_t *data, uint32_t len);
int  periph_uart_read(uint8_t uart, uint8_t *data, uint32_t max_len);  /* 返回读取字节数 */

/* ---------------------------------------------------------------------------
 * SPI0（帧通道用）
 * ------------------------------------------------------------------------- */
typedef enum {
	SPI_ROLE_MASTER = 0,
	SPI_ROLE_SLAVE,
} spi_role_t;

/* 初始化 SPI0，clock_div: 分频系数（越大越慢） */
void periph_spi_init(spi_role_t role, uint8_t clock_div);

/* 主模式：全双工收发（同时发送 tx 并接收 rx），返回实际接收字节数 */
int periph_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t len);

/* 从模式：非阻塞读取 FIFO 中的数据，返回读取字节数 */
int periph_spi_slave_read(uint8_t *data, uint32_t max_len);

/* 从模式：预装发送数据到 FIFO */
int periph_spi_slave_write(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* _PERIPH_H */
