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
 * 串口波特率：波特率 ↔ 16 位分频值 DL（DIV 固定为 1）
 * 公式（数据手册 9.3.1）：baud = Fsys * 2 / DIV / 16 / DL
 *   DIV = R8_UARTx_DIV 固定 1（数据手册：通常写入 1）；DL = R16_UARTx_DL（16 位）
 * Fsys=60MHz：baud = 7.5M / DL，最高 7.5Mbps（DL=1），最低 114bps（DL=65535）。
 * 注：高频波特率受整数分频限制（如 2M/3M 无法精确，实际 2.5M/3.75M）。
 * ------------------------------------------------------------------------- */
#define UART_FSYS  FUNCONF_SYSTEM_CORE_CLOCK

uint16_t uart_baud_to_div(uint32_t baud)
{
	if (baud == 0) baud = UART_BAUD_DEFAULT;
	uint32_t dl = (uint32_t)(((uint64_t)UART_FSYS * 2) / 16 / baud);
	if (dl < 1) dl = 1;
	if (dl > 65535) dl = 65535;
	return (uint16_t)dl;
}

uint32_t uart_div_to_baud(uint16_t dl)
{
	if (dl == 0) dl = uart_baud_to_div(UART_BAUD_DEFAULT);
	return (uint32_t)(((uint64_t)UART_FSYS * 2) / 16 / dl);
}

/* ---------------------------------------------------------------------------
 * 引脚映射表：紧凑编号 → 物理引脚号
 *   编码：PA0-15 = 0..15；PB0-23 = 0x20 | n（bit5 标记 PB）
 * 每芯片不同（CH591 QFN28 引脚少于 CH592 QFN32），由 GW_PIN_COUNT 控制。
 * 用 uint8_t 存（物理引脚号 ≤ 0x37），最省 Flash。
 * ------------------------------------------------------------------------- */
#define PM_PB(n)   (0x20 | (n))   /* PB 编码 */

#if defined(CH591)
/* CH591F QFN28：20 个 GPIO（据数据手册引脚图） */
static const uint8_t g_pin_map[PIN_COUNT] = {
	4, 5, 8, 9, 10, 11, 12, 13, 14, 15,    /* PA4/5/8/9/10/11/12/13/14/15 */
	PM_PB(4), PM_PB(7), PM_PB(10), PM_PB(11), PM_PB(12), PM_PB(13), PM_PB(14), PM_PB(15),  /* PB4/7/10-15 */
	PM_PB(22), PM_PB(23),                   /* PB22-23 */
};
#else
/* CH592X QFN32：24 个 GPIO（据数据手册引脚图）
 * 左：PA4-15；右：PB0/4/6/7/10-15/22-23 */
static const uint8_t g_pin_map[PIN_COUNT] = {
	4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,    /* PA4-15 */
	PM_PB(0), PM_PB(4), PM_PB(6), PM_PB(7),      /* PB0/4/6/7 */
	PM_PB(10), PM_PB(11), PM_PB(12), PM_PB(13), PM_PB(14), PM_PB(15),  /* PB10-15 */
	PM_PB(22), PM_PB(23),                        /* PB22-23 */
};
#endif

/* 紧凑编号 → ch32fun 引脚号（越界返回 0xFFFFFFFF） */
static u32 pin_to_ch32fun(int pin)
{
	if (pin < 0 || pin >= PIN_COUNT) return 0xFFFFFFFF;
	uint8_t m = g_pin_map[pin];
	return (m & 0x20) ? (PB | (u32)(m & 0x1F)) : (u32)m;
}

/* 紧凑编号 → 引脚名（如 "PA0" / "PB10"） */
static const char *pin_name_of(int pin)
{
	if (pin < 0 || pin >= PIN_COUNT) return "??";
	uint8_t m = g_pin_map[pin];
	static char buf[8];
	int n = 0;
	uint8_t num = m & 0x1F;
	buf[n++] = 'P';
	buf[n++] = (m & 0x20) ? 'B' : 'A';
	if (num >= 10) buf[n++] = (char)('0' + num / 10);
	buf[n++] = (char)('0' + num % 10);
	buf[n] = '\0';
	return buf;
}

/* ---------------------------------------------------------------------------
 * 名称
 * ------------------------------------------------------------------------- */
const char *pinmux_pin_name(int pin)
{
	return pin_name_of(pin);
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
		/* TX 引脚负责初始化整个 UART（波特率/校验/停止位）
		 * param2 编码：bit0=模式, bit1-2=校验, bit3=停止位
		 * uart_dl = 波特率分频值（0=默认 115200） */
		{
			uart_cfg_t uc;
			uc.uart      = c->param1;                       /* UART 号 */
			uc.baud      = uart_div_to_baud(c->uart_dl);    /* 分频值 → 波特率 */
			uc.data_bits = 8;
			uc.parity    = (uint8_t)((c->param2 >> 1) & 0x03);     /* bit1-2 校验 */
			uc.stop_bits = (uint8_t)((c->param2 >> 3) & 0x01) + 1; /* bit3 停止位 */
			periph_uart_init(&uc);
		}
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
