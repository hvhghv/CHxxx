/********************************** (C) COPYRIGHT *******************************
 * File Name          : pinmux.c
 * Description        : 引脚复用管理器实现
 *******************************************************************************/

#include "ch32fun.h"
#include "pinmux.h"
#include "settings.h"
#include "periph.h"
#include <string.h>

/* 引脚配置表：直接使用统一配置中的 pins 字段（避免重复存储） */
#define g_pin_cfg   (g_cfg.pins)

/* ---------------------------------------------------------------------------
 * 引脚编号 ↔ ch32fun 引脚宏
 *   0..15  → PA0..PA15
 *   16..39 → PB0..PB23
 * ------------------------------------------------------------------------- */
static u32 pin_to_ch32fun(int pin)
{
	if (pin < 16) {
		return (u32)pin;              /* PA0..PA15 */
	} else if (pin < 40) {
		return PB | (u32)(pin - 16);  /* PB0..PB23 */
	}
	return 0xFFFFFFFF;
}

/* ---------------------------------------------------------------------------
 * 名称
 * ------------------------------------------------------------------------- */
const char *pinmux_pin_name(int pin)
{
	static char buf[8];
	int n = 0;
	if (pin < 16) {
		buf[n++] = 'P'; buf[n++] = 'A';
	} else if (pin < 40) {
		buf[n++] = 'P'; buf[n++] = 'B';
		pin -= 16;
	} else {
		buf[0] = '?'; buf[1] = '?'; buf[2] = '\0';
		return buf;
	}
	/* 十进制（最多 2 位） */
	if (pin >= 10) buf[n++] = (char)('0' + pin / 10);
	buf[n++] = (char)('0' + pin % 10);
	buf[n] = '\0';
	return buf;
}

const char *pinmux_func_name(int func)
{
	switch (func) {
	case PIN_FUNC_NONE:     return "NONE";
	case PIN_FUNC_GPIO_IN:  return "GPIO_IN";
	case PIN_FUNC_GPIO_OUT: return "GPIO_OUT";
	case PIN_FUNC_UART_TX:  return "UART_TX";
	case PIN_FUNC_UART_RX:  return "UART_RX";
	case PIN_FUNC_PWM:      return "PWM";
	case PIN_FUNC_ADC:      return "ADC";
	case PIN_FUNC_SPI:      return "SPI";
	case PIN_FUNC_I2C:      return "I2C";
	case PIN_FUNC_SWIO:     return "SWIO";
	default:                return "?";
	}
}

/* ---------------------------------------------------------------------------
 * 应用单个引脚配置
 * ------------------------------------------------------------------------- */
int pinmux_apply(int pin)
{
	if (pin < 0 || pin >= PIN_COUNT) return -1;
	u32 p = pin_to_ch32fun(pin);
	if (p == 0xFFFFFFFF) return -1;

	pin_cfg_t *c = &g_pin_cfg[pin];

	switch (c->func) {
	case PIN_FUNC_NONE:
		funPinMode(p, GPIO_ModeIN_Floating);
		break;

	case PIN_FUNC_GPIO_IN:
		switch (c->param1) {
		case PIN_GPIO_IN_PU:  funPinMode(p, GPIO_ModeIN_PU); break;
		case PIN_GPIO_IN_PD:  funPinMode(p, GPIO_ModeIN_PD); break;
		default:              funPinMode(p, GPIO_ModeIN_Floating); break;
		}
		break;

	case PIN_FUNC_GPIO_OUT:
		funPinMode(p, GPIO_ModeOut_PP_5mA);
		funDigitalWrite(p, c->arg1 ? FUN_HIGH : FUN_LOW);
		break;

	case PIN_FUNC_ADC:
		/* ADC 引脚配置为浮空输入（模拟） */
		funPinMode(p, GPIO_ModeIN_Floating);
		break;

	case PIN_FUNC_UART_TX:
		funPinMode(p, GPIO_ModeOut_PP_5mA);
		break;

	case PIN_FUNC_UART_RX:
		funPinMode(p, GPIO_ModeIN_PU);
		break;

	case PIN_FUNC_PWM:
		funPinMode(p, GPIO_ModeOut_PP_5mA);
		break;

	case PIN_FUNC_SWIO:
		/* SWIO 空闲时释放（高阻上拉），烧录时由 swio.c 驱动 */
		funPinMode(p, GPIO_ModeIN_PU);
		break;

	case PIN_FUNC_SPI:
	case PIN_FUNC_I2C:
		/* 阶段 4+ 实现具体外设初始化 */
		break;

	default:
		break;
	}
	return 0;
}

void pinmux_apply_all(void)
{
	for (int i = 0; i < PIN_COUNT; i++) {
		pinmux_apply(i);
	}
}

/* ---------------------------------------------------------------------------
 * 设置功能
 * ------------------------------------------------------------------------- */
int pinmux_set_func(int pin, pin_func_t func)
{
	if (pin < 0 || pin >= PIN_COUNT) return -1;
	memset(&g_pin_cfg[pin], 0, sizeof(pin_cfg_t));
	g_pin_cfg[pin].func = (uint8_t)func;
	return 0;
}

/* ---------------------------------------------------------------------------
 * GPIO 读写
 * ------------------------------------------------------------------------- */
int pinmux_gpio_read(int pin)
{
	if (pin < 0 || pin >= PIN_COUNT) return -1;
	u32 p = pin_to_ch32fun(pin);
	if (p == 0xFFFFFFFF) return -1;
	return funDigitalRead(p) ? 1 : 0;
}

int pinmux_gpio_write(int pin, int level)
{
	if (pin < 0 || pin >= PIN_COUNT) return -1;
	u32 p = pin_to_ch32fun(pin);
	if (p == 0xFFFFFFFF) return -1;
	g_pin_cfg[pin].arg1 = level ? 1 : 0;
	funDigitalWrite(p, level ? FUN_HIGH : FUN_LOW);
	return 0;
}

/* ---------------------------------------------------------------------------
 * ADC 读取（引脚需配置为 PIN_FUNC_ADC，param1 = ADC 通道号）
 * ------------------------------------------------------------------------- */
int pinmux_adc_read(int pin)
{
	if (pin < 0 || pin >= PIN_COUNT) return -1;
	if (g_pin_cfg[pin].func != PIN_FUNC_ADC) return -1;
	return periph_adc_read(g_pin_cfg[pin].param1);
}

/* ---------------------------------------------------------------------------
 * 默认配置（仅重置引脚部分，不动 DHCP 等其它配置）
 * ------------------------------------------------------------------------- */
void pinmux_reset_default(void)
{
	memset(g_pin_cfg, 0, sizeof(g_cfg.pins));

	/* 默认：全部为未使用（浮空输入） */
	for (int i = 0; i < PIN_COUNT; i++) {
		g_pin_cfg[i].func = PIN_FUNC_NONE;
	}

	/* 示例默认配置（可按需修改）：
	 *   PA0  → GPIO 输出（LED）
	 *   PA1  → GPIO 输入（按键，上拉）
	 */
	g_pin_cfg[PIN_PA(0)].func = PIN_FUNC_GPIO_OUT;
	g_pin_cfg[PIN_PA(1)].func = PIN_FUNC_GPIO_IN;
	g_pin_cfg[PIN_PA(1)].param1 = PIN_GPIO_IN_PU;
}

/* ---------------------------------------------------------------------------
 * 配置存储（统一保存/加载整个 gateway_cfg_t）
 * ------------------------------------------------------------------------- */
int pinmux_save(void)
{
	return settings_save();
}

int pinmux_load(void)
{
	return settings_load();
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void pinmux_init(void)
{
	settings_load();       /* 加载统一配置（含引脚 + DHCP） */
	pinmux_apply_all();
}
