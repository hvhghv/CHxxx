/********************************** (C) COPYRIGHT *******************************
 * File Name          : framehdl.c
 * Description        : 帧命令处理器（GPIO / ADC / PWM / UART / 网络转发）
 *
 * 帧命令仅可「临时配置/读取」外设功能，**无引脚复用修改权限**。
 *******************************************************************************/

#include "ch32fun.h"
#include "framelink.h"
#include "frame.h"
#include "pinmux.h"
#include "periph.h"
#include "netroute.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * 小端读写
 * ------------------------------------------------------------------------- */
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

/* ---------------------------------------------------------------------------
 * PING：回显
 * ------------------------------------------------------------------------- */
static uint32_t hdl_ping(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	uint32_t n = f->payload_len < max ? f->payload_len : max;
	memcpy(resp, f->payload, n);
	return n;
}

/* ---------------------------------------------------------------------------
 * GPIO 读：payload = [pin(1)]，resp = [pin(1), val(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_gpio_read(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 1 || max < 2) return 0;
	uint8_t pin = f->payload[0];
	int val = pinmux_gpio_read(pin);
	resp[0] = pin;
	resp[1] = (uint8_t)(val > 0 ? 1 : 0);
	return 2;
}

/* ---------------------------------------------------------------------------
 * GPIO 写：payload = [pin(1), val(1)]，resp = [pin(1), val(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_gpio_write(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 2 || max < 2) return 0;
	uint8_t pin = f->payload[0];
	uint8_t val = f->payload[1];
	pinmux_gpio_write(pin, val);
	resp[0] = pin;
	resp[1] = val;
	return 2;
}

/* ---------------------------------------------------------------------------
 * ADC 读：payload = [ch(1)]，resp = [ch(1), val_lo(1), val_hi(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_adc_read(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 1 || max < 3) return 0;
	uint8_t adc_ch = f->payload[0];
	int val = periph_adc_read(adc_ch);
	resp[0] = adc_ch;
	wr16(resp + 1, (uint16_t)(val < 0 ? 0 : val));
	return 3;
}

/* ---------------------------------------------------------------------------
 * PWM 配置：payload = [ch(1), period_lo(1), period_hi(1), duty_lo(1), duty_hi(1), en(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_pwm_set(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 6 || max < 1) return 0;
	pwm_cfg_t cfg;
	cfg.channel = f->payload[0];
	cfg.period  = rd16(f->payload + 1);
	cfg.duty    = rd16(f->payload + 3);
	cfg.enabled = f->payload[5];
	cfg.polar   = 0;
	periph_pwm_set(&cfg);
	resp[0] = 0;   /* OK */
	return 1;
}

/* ---------------------------------------------------------------------------
 * PWM 启停：payload = [ch(1), on(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_pwm_start(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 1 || max < 1) return 0;
	periph_pwm_start(f->payload[0]);
	resp[0] = 0;
	return 1;
}

static uint32_t hdl_pwm_stop(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 1 || max < 1) return 0;
	periph_pwm_stop(f->payload[0]);
	resp[0] = 0;
	return 1;
}

/* ---------------------------------------------------------------------------
 * UART 配置：payload = [uart(1), baud(4), parity(1), stop(1)]
 * ------------------------------------------------------------------------- */
static uint32_t hdl_uart_cfg(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 7 || max < 1) return 0;
	uart_cfg_t cfg;
	cfg.uart      = f->payload[0];
	cfg.baud      = rd32(f->payload + 1);
	cfg.parity    = f->payload[5];
	cfg.stop_bits = f->payload[6];
	cfg.data_bits = 8;
	periph_uart_init(&cfg);
	resp[0] = 0;
	return 1;
}

/* ---------------------------------------------------------------------------
 * 网络帧转发：payload = [IP 包原始字节]，注入 LWIP
 * ------------------------------------------------------------------------- */
static uint32_t hdl_net_fwd(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	(void)resp; (void)max;
	if (f->payload_len < 20) return 0;
	netroute_input(f->payload, f->payload_len);
	return 0;   /* 无响应 */
}

/* ---------------------------------------------------------------------------
 * 注册所有处理器
 * ------------------------------------------------------------------------- */
void framehdl_init(void)
{
	framelink_register(FRAME_TYPE_PING,       hdl_ping);
	framelink_register(FRAME_TYPE_GPIO_READ,  hdl_gpio_read);
	framelink_register(FRAME_TYPE_GPIO_WRITE, hdl_gpio_write);
	framelink_register(FRAME_TYPE_ADC_READ,   hdl_adc_read);
	framelink_register(FRAME_TYPE_PWM_SET,    hdl_pwm_set);
	framelink_register(FRAME_TYPE_PWM_START,  hdl_pwm_start);
	framelink_register(FRAME_TYPE_PWM_STOP,   hdl_pwm_stop);
	framelink_register(FRAME_TYPE_UART_CFG,   hdl_uart_cfg);
	framelink_register(FRAME_TYPE_NET_FWD,    hdl_net_fwd);
}
