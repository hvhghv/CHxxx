/********************************** (C) COPYRIGHT *******************************
 * File Name          : periph.c
 * Description        : 外设驱动实现（PWM / ADC / UART）
 *******************************************************************************/

#include "ch32fun.h"
#include "periph.h"
#include <string.h>

/* ===========================================================================
 * PWM（PWMX，16 位模式）
 * =========================================================================== */
void periph_pwm_init(void)
{
	/* 使能 PWMX 时钟（CH59x 默认开启，这里配置时钟分频） */
	R8_PWM_CLOCK_DIV = 1;
	/* 16 位周期模式 */
	R8_PWM_CONFIG = (R8_PWM_CONFIG & 0xf0) | (3 << 2);
}

void periph_pwm_set(pwm_cfg_t *cfg)
{
	if (!cfg) return;

	/* 16 位周期 */
	R32_PWM_REG_CYCLE = cfg->period;

	/* 极性 */
	if (cfg->polar) {
		R8_PWM_POLAR |= cfg->channel;
	} else {
		R8_PWM_POLAR &= ~cfg->channel;
	}

	/* 占空比：限制 duty ≤ period（防异常输出） */
	uint16_t duty = cfg->duty;
	if (cfg->period && duty > cfg->period) duty = cfg->period;
	periph_pwm_set_duty(cfg->channel, duty);

	/* 使能输出 */
	if (cfg->enabled) {
		R8_PWM_OUT_EN |= cfg->channel;
	} else {
		R8_PWM_OUT_EN &= ~cfg->channel;
	}
}

void periph_pwm_start(uint8_t channel)
{
	R8_PWM_OUT_EN |= channel;
}

void periph_pwm_stop(uint8_t channel)
{
	R8_PWM_OUT_EN &= ~channel;
}

void periph_pwm_set_duty(uint8_t channel, uint16_t duty)
{
	/* channel 是位掩码：bit0=PWM4 ... bit7=PWM11
	 * 寄存器映射（据 CH592 手册）：
	 *   PWM4/5 → R32_PWM4_7_DATA(0x40005004) 低/高 16 位
	 *   PWM6/7 → R32_PWM4_7_DATA 的 +4/+6（即 0x40005008 低/高 16 位）
	 *   PWM8/9 → R32_PWM_REG_DATA8(0x40005010) 低/高 16 位
	 *   PWM10/11 → 仅 8 位（R8_PWM10_DATA/0x4000500A、R8_PWM11_DATA/0x4000500B） */
	for (int i = 0; i < 8; i++) {
		if (!((channel >> i) & 1)) continue;
		if (i < 4) {
			/* PWM4-7：0x40005004 起的 4 个 16 位槽 */
			*((volatile uint16_t *)0x40005004 + i) = duty;
		} else if (i < 6) {
			/* PWM8-9：0x40005010 起的 2 个 16 位槽 */
			*((volatile uint16_t *)0x40005010 + (i - 4)) = duty;
		} else {
			/* PWM10-11：仅 8 位寄存器 */
			*((volatile uint8_t *)(0x4000500A + (i - 6))) = (uint8_t)duty;
		}
	}
}

/* ===========================================================================
 * ADC（单端，12 位）
 * =========================================================================== */
void periph_adc_init(void)
{
	/* 关闭 TouchKey 电源 */
	R8_TKEY_CFG &= ~0x04;   /* RB_TKEY_PWR_ON */

	/* ADC 上电 + 输入缓冲 + 单端 + 时钟 4MHz */
	R8_ADC_CFG = RB_ADC_POWER_ON | RB_ADC_BUF_EN | (3 << 6);

	/* 延时稳定 */
	for (volatile int i = 0; i < 1000; i++);
}

uint16_t periph_adc_read_raw(uint8_t channel)
{
	R8_ADC_CHANNEL = channel;

	/* 启动转换 */
	R8_ADC_CONVERT |= RB_ADC_START;
	while (R8_ADC_CONVERT & RB_ADC_START);

	/* 再读一次（丢弃首次） */
	R8_ADC_CONVERT |= RB_ADC_START;
	while (R8_ADC_CONVERT & RB_ADC_START);

	return R16_ADC_DATA & RB_ADC_DATA;
}

int periph_adc_read(uint8_t channel)
{
	if (channel > 13) return -1;   /* 仅外部通道 0..13 */
	return (int)periph_adc_read_raw(channel);
}

/* ===========================================================================
 * UART（UART0~UART3）
 * ===========================================================================
 * 寄存器基址：UART0=0x40003000，每个 +0x400
 * =========================================================================== */
#define UART_BASE(u)   (0x40003000u + (u) * 0x400u)

static volatile uint8_t *uart_reg(uint8_t uart, uint32_t off)
{
	return (volatile uint8_t *)(UART_BASE(uart) + off);
}

void periph_uart_init(uart_cfg_t *cfg)
{
	if (!cfg || cfg->uart > 3) return;

	uint8_t u = cfg->uart;

	/* 复位 FIFO */
	*uart_reg(u, 0x02) = 0x07;   /* FCR: enable + clear */

	/* 波特率：baud = Fsys * 2 / DIV / 16 / DL（数据手册 9.3.1）
	 * DIV 固定 1（数据手册：通常写入 1）；DL = Fsys*2/16/baud（最高 7.5Mbps @60MHz） */
	uint32_t dl = (uint32_t)(((uint64_t)FUNCONF_SYSTEM_CORE_CLOCK * 2) / 16 / cfg->baud);
	if (dl < 1) dl = 1;
	if (dl > 65535) dl = 65535;
	*uart_reg(u, 0x03) = 0x80;   /* LCR: DLAB=1 */
	*uart_reg(u, 0x0C) = (uint8_t)(dl & 0xFF);        /* DLL */
	*uart_reg(u, 0x0D) = (uint8_t)((dl >> 8) & 0xFF); /* DLM */
	*uart_reg(u, 0x0E) = 1;      /* DIV: 预分频固定 1 */

	/* LCR: 8 位数据 + 奇偶 + 停止位 */
	uint8_t lcr = 0x03;          /* 8 data bits */
	if (cfg->parity == 1) lcr |= 0x08;        /* odd */
	else if (cfg->parity == 2) lcr |= 0x18;   /* even */
	if (cfg->stop_bits == 2) lcr |= 0x04;     /* 2 stop bits */
	*uart_reg(u, 0x03) = lcr;    /* DLAB=0 */
}

int periph_uart_write(uint8_t uart, const uint8_t *data, uint32_t len)
{
	if (uart > 3 || !data) return -1;
	for (uint32_t i = 0; i < len; i++) {
		/* 等待发送 FIFO 有空间（TFC 是计数值，FIFO 深度 8） */
		uint32_t timeout = 100000;
		while (*uart_reg(uart, 0x0B) >= 8 && --timeout);   /* TFC >= 8 = 满 */
		*uart_reg(uart, 0x08) = data[i];   /* THR */
	}
	return (int)len;
}

int periph_uart_read(uint8_t uart, uint8_t *data, uint32_t max_len)
{
	if (uart > 3 || !data) return -1;
	uint32_t n = 0;
	while (n < max_len) {
		/* 检查接收 FIFO 是否有数据（RFC） */
		if (*uart_reg(uart, 0x0A) == 0) break;
		data[n++] = *uart_reg(uart, 0x08);  /* RBR */
	}
	return (int)n;
}

/* ===========================================================================
 * SPI0（帧通道）
 * ===========================================================================
 * 主模式：SCK/MOSI 输出，MISO 输入
 * 从模式：MISO 输出，SCK/MOSI 输入
 * =========================================================================== */
void periph_spi_init(spi_role_t role, uint8_t clock_div)
{
	/* 清空 FIFO */
	R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;

	if (role == SPI_ROLE_MASTER) {
		R8_SPI0_CLOCK_DIV = clock_div;
		/* 主模式：MOSI + SCK 输出使能 */
		R8_SPI0_CTRL_MOD = RB_SPI_MOSI_OE | RB_SPI_SCK_OE;
	} else {
		/* 从模式：MISO 输出使能 + slave */
		R8_SPI0_CTRL_MOD = RB_SPI_MISO_OE | RB_SPI_MODE_SLAVE;
	}

	/* 使能 BUFFER/FIFO 自动清 IF_BYTE_END 标志 */
	R8_SPI0_CTRL_CFG |= RB_SPI_AUTO_IF;
	/* 关闭 DMA */
	R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE;
}

/* 主模式全双工收发 */
int periph_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	if (!tx || !rx || len == 0) return -1;

	/* 发送：FIFO 方向 = 输出 */
	R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
	R16_SPI0_TOTAL_CNT = (uint16_t)len;
	R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;

	for (uint32_t i = 0; i < len; i++) {
		while (R8_SPI0_FIFO_COUNT >= 8);   /* 等 FIFO 有空间 */
		R8_SPI0_FIFO = tx[i];
	}
	while (R8_SPI0_FIFO_COUNT != 0);       /* 等发送完成 */

	/* 接收：FIFO 方向 = 输入 */
	R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
	R16_SPI0_TOTAL_CNT = (uint16_t)len;
	R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;

	for (uint32_t i = 0; i < len; i++) {
		while (R8_SPI0_FIFO_COUNT == 0);
		rx[i] = R8_SPI0_FIFO;
	}
	return (int)len;
}

/* 从模式：读 FIFO */
int periph_spi_slave_read(uint8_t *data, uint32_t max_len)
{
	if (!data) return -1;
	R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
	uint32_t n = 0;
	while (n < max_len && R8_SPI0_FIFO_COUNT > 0) {
		data[n++] = R8_SPI0_FIFO;
	}
	return (int)n;
}

/* 从模式：预装发送数据 */
int periph_spi_slave_write(const uint8_t *data, uint32_t len)
{
	if (!data) return -1;
	R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
	uint32_t n = 0;
	for (uint32_t i = 0; i < len; i++) {
		if (R8_SPI0_FIFO_COUNT >= 8) break;   /* FIFO 满 */
		R8_SPI0_FIFO = data[i];
		n++;
	}
	return (int)n;
}
