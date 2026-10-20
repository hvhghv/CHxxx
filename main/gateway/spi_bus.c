/********************************** (C) COPYRIGHT *******************************
 * File Name          : spi_bus.c
 * Description        : SPI 多设备总线管理实现
 *******************************************************************************/

#include "ch32fun.h"
#include "spi_bus.h"
#include "pinmux.h"
#include "periph.h"
#include "settings.h"
#include "framelink.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * 长度前缀协议缓冲上限
 * -------------------------------------------------------------------------
 * 协议：[LEN_H][LEN_L][PAYLOAD]
 * 从机发送缓冲 = 前缀 2 + 负载上限（需容纳一帧 framelink 最大帧）
 * 主机接收缓冲 = 前缀 2 + 负载上限
 * 可通过 -DSPIBUS_SLAVE_TX_MAX=N 等覆盖以节省 RAM。
 * ------------------------------------------------------------------------- */
#ifndef SPIBUS_SLAVE_TX_MAX
#define SPIBUS_SLAVE_TX_MAX   (FRAMELINK_MAX_PAYLOAD + 16)
#endif
#ifndef SPIBUS_SLAVE_RX_MAX
#define SPIBUS_SLAVE_RX_MAX   (FRAMELINK_MAX_PAYLOAD + 16)
#endif
#ifndef SPIBUS_MSTR_TX_MAX
#define SPIBUS_MSTR_TX_MAX    (FRAMELINK_MAX_PAYLOAD + 16)
#endif
#ifndef SPIBUS_MSTR_RX_MAX
#define SPIBUS_MSTR_RX_MAX    (FRAMELINK_MAX_PAYLOAD + 16)
#endif

/* 主机收发缓冲（master_poll 与 master_send 共用，主循环串行调用互斥） */
#if GW_SPI_ROLE_MASTER
static uint8_t m_mstr_txbuf[SPIBUS_MSTR_TX_MAX + 2];
static uint8_t m_mstr_rxbuf[SPIBUS_MSTR_RX_MAX + 2];
#endif

/* ---------------------------------------------------------------------------
 * 设备表
 * ------------------------------------------------------------------------- */
typedef struct {
	int8_t cs_pin;      /* CS 引脚（紧凑编号，-1=无） */
	int8_t int_pin;     /* INT 引脚（紧凑编号，-1=无） */
	uint8_t int_high;   /* INT 有效电平（1=高有效） */
	uint8_t used;
} spibus_dev_t;

static spibus_dev_t m_dev[GW_SPI_MAX_DEV];
static int m_is_slave = 0;      /* 0=主机，1=从机 */
static int m_dev_count = 0;

/* ---------------------------------------------------------------------------
 * 扫描引脚配置，建立设备表
 * ------------------------------------------------------------------------- */
void spibus_reload(void)
{
	memset(m_dev, 0, sizeof(m_dev));
	m_is_slave = 0;
	m_dev_count = 0;

	/* 先确定总线角色（看 SCK 引脚 flags bit1） */
	for (int i = 0; i < PIN_COUNT; i++) {
		const pin_cfg_t *c = &g_pin_cfg[i];
		if (c->func == PIN_FUNC_SPI && c->param2 == PIN_SPI_SCK) {
			m_is_slave = (c->flags & PIN_FLAG_SPI_SLAVE) ? 1 : 0;
			break;
		}
	}

	/* 收集 CS / INT 引脚（按 param1 设备号归位） */
	for (int i = 0; i < PIN_COUNT; i++) {
		const pin_cfg_t *c = &g_pin_cfg[i];
		if (c->func != PIN_FUNC_SPI) continue;
		uint8_t dev = c->param1;
		if (dev >= GW_SPI_MAX_DEV) continue;

		if (c->param2 == PIN_SPI_CS) {
			m_dev[dev].cs_pin = (int8_t)i;
			m_dev[dev].used = 1;
		} else if (c->param2 == PIN_SPI_INT) {
			m_dev[dev].int_pin = (int8_t)i;
			m_dev[dev].int_high = (c->flags & PIN_FLAG_SPI_INT_HIGH) ? 1 : 0;
			m_dev[dev].used = 1;
		}
	}

	/* 统计有效设备（至少有一个 CS 或 INT） */
	for (int d = 0; d < GW_SPI_MAX_DEV; d++) {
		if (m_dev[d].used) m_dev_count++;
	}
}

void spibus_init(void)
{
	spibus_reload();
}

int spibus_is_slave(void) { return m_is_slave; }
int spibus_dev_count(void) { return m_dev_count; }

int spibus_dev_info(int dev, int *cs_pin, int *int_pin)
{
	if (dev < 0 || dev >= GW_SPI_MAX_DEV || !m_dev[dev].used) return -1;
	if (cs_pin)  *cs_pin  = m_dev[dev].cs_pin;
	if (int_pin) *int_pin = m_dev[dev].int_pin;
	return 0;
}

/* ---------------------------------------------------------------------------
 * 主机模式：读取 INT 线是否有效
 * ------------------------------------------------------------------------- */
#if GW_SPI_ROLE_MASTER
static int int_active(int dev)
{
	const spibus_dev_t *d = &m_dev[dev];
	if (d->int_pin < 0) return 0;
	int lv = pinmux_gpio_read(d->int_pin);
	if (lv < 0) return 0;
	return d->int_high ? (lv != 0) : (lv == 0);
}

/* ---------------------------------------------------------------------------
 * 主机模式：向设备传输（自动 CS）
 * ------------------------------------------------------------------------- */
int spibus_master_xfer(int dev, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	if (dev < 0 || dev >= GW_SPI_MAX_DEV || !m_dev[dev].used) return -1;
	if (!tx || !rx || len == 0) return -1;

	int8_t cs = m_dev[dev].cs_pin;
	if (cs >= 0) pinmux_gpio_write(cs, 0);      /* CS 拉低（有效） */
	int r = periph_spi_transfer(tx, rx, len);
	if (cs >= 0) pinmux_gpio_write(cs, 1);      /* CS 释放 */
	return r;
}

/* 主机模式：发送数据（自动长度前缀 + 自动 CS）
 * 协议：[LEN_H][LEN_L][PAYLOAD]，一次性完成（CS 保持低）
 * 注意：复用 master_poll 的静态缓冲，避免额外 2×N 栈/静态占用 */
int spibus_master_send(int dev, const uint8_t *data, uint32_t len)
{
	if (!data || len == 0 || len > 0xFFFF) return -1;
	if (dev < 0 || dev >= GW_SPI_MAX_DEV || !m_dev[dev].used) return -1;
	if (len > SPIBUS_MSTR_TX_MAX) return -1;

	/* 复用主机收发缓冲（与 master_poll 互斥：都在主循环串行调用） */
	uint8_t *tx = m_mstr_txbuf;
	uint8_t *rx = m_mstr_rxbuf;
	uint32_t total = len + 2;

	tx[0] = (uint8_t)(len >> 8);
	tx[1] = (uint8_t)(len & 0xFF);
	memcpy(tx + 2, data, len);

	int r = spibus_master_xfer(dev, tx, rx, total);
	return (r == (int)total) ? (int)len : -1;
}

/* ---------------------------------------------------------------------------
 * 主机模式：轮询 INT，自动 CS + 长度前缀解析 + 读满整帧
 * -------------------------------------------------------------------------
 * 流程：
 *   ① 检测 INT 有效
 *   ② CS 拉低，读 2 字节长度前缀
 *   ③ 按 LEN 连续读满 PAYLOAD（从机 FIFO 8 字节，需多次读，从机自动续传）
 *   ④ CS 释放，回调 on_data(dev, payload, LEN)
 * ------------------------------------------------------------------------- */
int spibus_master_poll(spibus_rx_cb_t on_data)
{
	if (m_is_slave) return 0;

	uint8_t *txbuf = m_mstr_txbuf;
	uint8_t *rxbuf = m_mstr_rxbuf;
	int handled = 0;

	for (int d = 0; d < GW_SPI_MAX_DEV; d++) {
		if (!m_dev[d].used) continue;
		if (!int_active(d)) continue;           /* 无中断 */

		int8_t cs = m_dev[d].cs_pin;
		int ok = 0;

		if (cs >= 0) pinmux_gpio_write(cs, 0);  /* CS 拉低，整帧保持 */

		/* ② 读 2 字节长度前缀 */
		memset(txbuf, 0xFF, 2);
		if (periph_spi_transfer(txbuf, rxbuf, 2) == 2) {
			uint32_t plen = ((uint32_t)rxbuf[0] << 8) | rxbuf[1];

			if (plen == 0) {
				ok = 1;                         /* 空帧：合法，无数据 */
			} else if (plen <= SPIBUS_MSTR_RX_MAX) {
				/* ③ 读满 PAYLOAD */
				memset(txbuf, 0xFF, plen);
				if (periph_spi_transfer(txbuf, rxbuf, plen) == (int)plen) {
					if (on_data) on_data(d, rxbuf, plen);
					ok = 1;
				}
			}
			/* plen 超限 → 丢弃（ok=0，不回调） */
		}

		if (cs >= 0) pinmux_gpio_write(cs, 1);  /* CS 释放 */
		if (ok) handled++;
	}
	return handled;
}
#else /* !GW_SPI_ROLE_MASTER —— 仅从机模式：主机接口空实现 */
int spibus_master_xfer(int dev, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
	(void)dev; (void)tx; (void)rx; (void)len;
	return -1;
}
int spibus_master_poll(spibus_rx_cb_t on_data)
{
	(void)on_data;
	return 0;
}
#endif /* GW_SPI_ROLE_MASTER */

/* ---------------------------------------------------------------------------
 * 从机模式：设置 INT 线（通知主机）
 * ------------------------------------------------------------------------- */
#if GW_SPI_ROLE_SLAVE
void spibus_slave_set_int(int dev, int level)
{
	if (dev < 0 || dev >= GW_SPI_MAX_DEV || !m_dev[dev].used) return;
	int8_t ip = m_dev[dev].int_pin;
	if (ip < 0) return;
	/* 按有效电平输出：level=1 表示"有数据"，输出有效电平 */
	int out = level ? (m_dev[dev].int_high ? 1 : 0)
	                : (m_dev[dev].int_high ? 0 : 1);
	pinmux_gpio_write(ip, out);
}

/* 从机模式：发送数据到指定设备（自动加长度前缀 + 拉高 INT 通知主机）
 * 流程：预装 [LEN_H][LEN_L][PAYLOAD] → 拉高 INT（通知主机来取）
 * 注意：SPI FIFO 仅 8 字节，超出部分由 spibus_slave_poll() 续传
 * 返回写入的负载字节数（不含前缀），<0 错误 */
static uint8_t  m_slave_tx_buf[SPIBUS_SLAVE_TX_MAX + 2];  /* 待续传数据（含前缀） */
static uint32_t m_slave_tx_len = 0;    /* 待续传字节数（含前缀） */
static uint32_t m_slave_tx_off = 0;    /* 已发送偏移 */
static int8_t   m_slave_tx_dev = -1;   /* 当前发送设备 */

int spibus_slave_send(int dev, const uint8_t *data, uint32_t len)
{
	if (!data || len == 0 || len > 0xFFFF) return -1;
	if (dev < 0 || dev >= GW_SPI_MAX_DEV || !m_dev[dev].used) {
		/* 无设备配置：退回默认设备 0（兼容单设备场景） */
		dev = 0;
		if (!m_dev[0].used) {
			/* 完全无设备：直接写 FIFO（无 INT 可用），仍带长度前缀 */
			uint8_t hdr[2];
			hdr[0] = (uint8_t)(len >> 8);
			hdr[1] = (uint8_t)(len & 0xFF);
			int n = periph_spi_slave_write(hdr, 2);
			if (n < 2) return -1;
			n = periph_spi_slave_write(data, len);
			return n;
		}
	}

	/* 组装 [LEN_H][LEN_L][PAYLOAD] 到续传缓冲 */
	if (len > SPIBUS_SLAVE_TX_MAX) len = SPIBUS_SLAVE_TX_MAX;
	m_slave_tx_buf[0] = (uint8_t)(len >> 8);
	m_slave_tx_buf[1] = (uint8_t)(len & 0xFF);
	memcpy(m_slave_tx_buf + 2, data, len);
	m_slave_tx_len = len + 2;
	m_slave_tx_off = 0;
	m_slave_tx_dev = (int8_t)dev;

	/* 先写 FIFO（最多 8 字节） */
	int n = periph_spi_slave_write(m_slave_tx_buf, m_slave_tx_len);
	if (n > 0) m_slave_tx_off = (uint32_t)n;

	spibus_slave_set_int(dev, 1);   /* 拉高 INT：有数据待取 */
	return (int)len;
}

/* ---------------------------------------------------------------------------
 * 从机模式：轮询
 *   - 发送 FIFO 空 → 续传剩余数据，或拉低 INT（主机已取走）
 *   - 接收 FIFO → 解析长度前缀，凑满整帧后回调 on_data(0, payload, len)
 * ------------------------------------------------------------------------- */
static uint8_t  m_slave_rx_buf[SPIBUS_SLAVE_RX_MAX + 2];
static uint32_t m_slave_rx_len = 0;    /* 已收字节数（含前缀） */
static uint32_t m_slave_rx_need = 0;   /* 本帧总长度（含前缀），0=未知 */

int spibus_slave_poll(spibus_rx_cb_t on_data)
{
	if (!m_is_slave) return 0;

	int did = 0;

	/* 发送 FIFO 已空 → 续传剩余数据，或清除 INT（数据已被主机取走） */
	if (periph_spi_slave_tx_empty()) {
		if (m_slave_tx_len > 0 && m_slave_tx_off < m_slave_tx_len) {
			/* 有剩余数据：继续写入 FIFO */
			uint32_t rem = m_slave_tx_len - m_slave_tx_off;
			int n = periph_spi_slave_write(m_slave_tx_buf + m_slave_tx_off, rem);
			if (n > 0) m_slave_tx_off += (uint32_t)n;
			/* INT 保持有效（仍有数据） */
			did = 1;
		} else if (m_slave_tx_len > 0) {
			/* 全部发完 → 清状态 + 拉低 INT */
			m_slave_tx_len = 0; m_slave_tx_off = 0; m_slave_tx_dev = -1;
			for (int d = 0; d < GW_SPI_MAX_DEV; d++) {
				if (m_dev[d].used && m_dev[d].int_pin >= 0)
					spibus_slave_set_int(d, 0);
			}
		} else {
			/* 无待发数据 → 确保 INT 已拉低 */
			for (int d = 0; d < GW_SPI_MAX_DEV; d++) {
				if (m_dev[d].used && m_dev[d].int_pin >= 0)
					spibus_slave_set_int(d, 0);
			}
		}
	}

	/* 接收：读 FIFO，累积到 m_slave_rx_buf，按长度前缀切帧 */
	uint8_t tmp[32];
	int n = periph_spi_slave_read(tmp, sizeof(tmp));
	for (int i = 0; i < n; i++) {
		if (m_slave_rx_len < sizeof(m_slave_rx_buf))
			m_slave_rx_buf[m_slave_rx_len++] = tmp[i];

		/* 前缀未收齐 */
		if (m_slave_rx_need == 0) {
			if (m_slave_rx_len < 2) continue;
			uint32_t plen = ((uint32_t)m_slave_rx_buf[0] << 8) | m_slave_rx_buf[1];
			if (plen > SPIBUS_SLAVE_RX_MAX) {
				/* 长度非法：丢弃首字节重新同步 */
				memmove(m_slave_rx_buf, m_slave_rx_buf + 1, m_slave_rx_len - 1);
				m_slave_rx_len--;
				continue;
			}
			m_slave_rx_need = plen + 2;
			if (m_slave_rx_need == 2) {
				/* 空帧：直接消费 */
				m_slave_rx_len = 0;
				m_slave_rx_need = 0;
				did = 1;
				continue;
			}
		}

		/* 整帧收齐 */
		if (m_slave_rx_len >= m_slave_rx_need) {
			uint32_t plen = m_slave_rx_need - 2;
			if (on_data) on_data(0, m_slave_rx_buf + 2, plen);
			m_slave_rx_len = 0;
			m_slave_rx_need = 0;
			did = 1;
		}
	}

	return (n > 0 || did) ? 1 : 0;
}
#else /* !GW_SPI_ROLE_SLAVE —— 仅主机模式：从机接口空实现 */
void spibus_slave_set_int(int dev, int level)
{
	(void)dev; (void)level;
}
int spibus_slave_send(int dev, const uint8_t *data, uint32_t len)
{
	(void)dev; (void)data; (void)len;
	return -1;
}
int spibus_slave_poll(spibus_rx_cb_t on_data)
{
	(void)on_data;
	return 0;
}
#endif /* GW_SPI_ROLE_SLAVE */

/* ---------------------------------------------------------------------------
 * 主循环统一轮询：收到的数据喂给 framelink（FRAMELINK_SPI 通道）
 * ------------------------------------------------------------------------- */
static void spi_bus_rx_cb(int dev, const uint8_t *data, uint32_t len)
{
	(void)dev;
	framelink_rx(FRAMELINK_SPI, data, len);
}

void spi_bus_poll(void)
{
	if (m_dev_count == 0) return;

	if (m_is_slave) {
		spibus_slave_poll(spi_bus_rx_cb);
	} else {
		spibus_master_poll(spi_bus_rx_cb);
	}
}
