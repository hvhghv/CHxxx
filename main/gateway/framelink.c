/********************************** (C) COPYRIGHT *******************************
 * File Name          : framelink.c
 * Description        : 帧链路管理实现
 *******************************************************************************/

#include "ch32fun.h"
#include "framelink.h"
#include "periph.h"
#include "spi_bus.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * 接收状态（帧同步）
 * ---------------------------------------------------------------------------
 * 新帧格式无分片字段（单包模型），故无需重组。
 * 每通道一个接收缓冲，大小 = 头 + 最大负载。
 * ------------------------------------------------------------------------- */
/* 接收缓冲 = 头 8 + 最大负载 + 余量。
 * 负载上限决定能接收的最大帧（定义见 framelink.h 的 FRAMELINK_MAX_PAYLOAD）。 */
#define RXBUF_SIZE   (FRAMELINK_MAX_PAYLOAD + 16)
#define MAX_HANDLERS 10

typedef struct {
	uint8_t  buf[RXBUF_SIZE];
	uint16_t len;             /* 已累积字节数 */
} framelink_rx_t;

static framelink_rx_t rx_state[FRAMELINK_ACTIVE_CH];

/* 处理器表 */
static struct {
	uint8_t type;
	frame_handler_t handler;
} handlers[MAX_HANDLERS];
static int handler_count = 0;

/* 发送缓冲 */
static uint8_t tx_buf[RXBUF_SIZE];

/* SPI 角色（0=主, 1=从） */
static uint8_t spi_is_slave = 0;

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void framelink_init(void)
{
	memset(rx_state, 0, sizeof(rx_state));
	handler_count = 0;
	spi_is_slave = 0;
}

void framelink_register(uint8_t type, frame_handler_t handler)
{
	if (handler_count < MAX_HANDLERS) {
		handlers[handler_count].type = type;
		handlers[handler_count].handler = handler;
		handler_count++;
	}
}

/* ---------------------------------------------------------------------------
 * 发送
 * ------------------------------------------------------------------------- */
int framelink_send(framelink_ch_t ch, const uint8_t *frame, uint32_t len)
{
	switch (ch) {
	case FRAMELINK_UART0: return periph_uart_write(0, frame, len);
	case FRAMELINK_UART1: return periph_uart_write(1, frame, len);
	case FRAMELINK_UART2: return periph_uart_write(2, frame, len);
	case FRAMELINK_UART3: return periph_uart_write(3, frame, len);
	case FRAMELINK_SPI:
#if GW_SPI_ROLE_MASTER && GW_SPI_ROLE_SLAVE
		if (spi_is_slave) {
			/* 从模式：加长度前缀 + 预装 FIFO + 自动拉高 INT 通知主机 */
			return spibus_slave_send(0, frame, len);
		} else {
			/* 主模式：加长度前缀 + 自动 CS 发送 */
			return spibus_master_send(0, frame, len);
		}
#elif GW_SPI_ROLE_SLAVE
		/* 仅从机：加长度前缀 + 预装 FIFO + 自动拉高 INT */
		return spibus_slave_send(0, frame, len);
#else
		/* 仅主机：加长度前缀 + 自动 CS 发送 */
		return spibus_master_send(0, frame, len);
#endif
	default: return -1;
	}
}

/* ---------------------------------------------------------------------------
 * SPI 角色配置
 * ------------------------------------------------------------------------- */
void framelink_spi_config(int is_slave, uint8_t clock_div)
{
	spi_is_slave = is_slave ? 1 : 0;
	periph_spi_init(spi_is_slave ? SPI_ROLE_SLAVE : SPI_ROLE_MASTER, clock_div);
}

/* ---------------------------------------------------------------------------
 * SPI 从模式轮询
 * -------------------------------------------------------------------------
 * 注意：SPI 接收已统一由 spibus_slave_poll() 处理（含长度前缀解析），
 *       本函数保留为空实现，避免两处竞争读取 FIFO。
 * ------------------------------------------------------------------------- */
void framelink_poll_spi(void)
{
}

void framelink_send_resp(framelink_ch_t ch, uint8_t type, uint32_t message_id,
                         const uint8_t *payload, uint16_t payload_len)
{
	/* 帧头 + 负载不能超过发送缓冲，否则丢弃（防溢出） */
	if ((uint32_t)frame_header_len() + payload_len > sizeof(tx_buf)) return;
	uint32_t n = frame_build_single(tx_buf, type, message_id, payload, payload_len);
	if (n) {
		framelink_send(ch, tx_buf, n);
	}
}

/* ---------------------------------------------------------------------------
 * 帧分发
 * ------------------------------------------------------------------------- */
static void framelink_dispatch(framelink_ch_t ch, const frame_t *f)
{
	for (int i = 0; i < handler_count; i++) {
		if (handlers[i].type == f->type) {
			uint8_t resp[512];
			uint32_t rlen = handlers[i].handler(ch, f, resp, sizeof(resp));
			if (rlen) {
				framelink_send_resp(ch, (uint8_t)(f->type | 0x80),
				                    f->message_id, resp, (uint16_t)rlen);
			}
			return;
		}
	}
	/* 无处理器 → 回错误 */
	{
		uint8_t err[1] = { 0x01 };
		framelink_send_resp(ch, FRAME_TYPE_ERR, f->message_id, err, 1);
	}
}

/* ---------------------------------------------------------------------------
 * 多包重组
 * ---------------------------------------------------------------------------
 * 新帧格式无分片字段（单包模型），无需重组，直接分发。
 * ------------------------------------------------------------------------- */
static void framelink_reasm(framelink_ch_t ch, const frame_t *f)
{
	framelink_dispatch(ch, f);
}

/* ---------------------------------------------------------------------------
 * 字节流帧提取（通过 codec 抽象：同步 → 求长度 → 解码）
 * ------------------------------------------------------------------------- */
void framelink_rx(framelink_ch_t ch, const uint8_t *data, uint32_t len)
{
	if (ch >= FRAMELINK_ACTIVE_CH) return;
	framelink_rx_t *rx = &rx_state[ch];

	for (uint32_t i = 0; i < len; i++) {
		/* 缓冲满则丢弃（防溢出） */
		if (rx->len >= RXBUF_SIZE) {
			rx->len = 0;
		}
		rx->buf[rx->len++] = data[i];

		/* 至少要有头 */
		if (rx->len < frame_header_len()) continue;

		/* 帧同步：找 magic 起始偏移 */
		int off = frame_sync(rx->buf, rx->len);
		if (off < 0) {
			/* 未找到 magic：保留最后 1 字节（可能是 magic 首字节） */
			if (rx->len > 1) {
				rx->buf[0] = rx->buf[rx->len - 1];
				rx->len = 1;
			}
			continue;
		}
		if (off > 0) {
			/* 丢弃 magic 之前的字节 */
			memmove(rx->buf, rx->buf + off, rx->len - off);
			rx->len -= off;
		}

		/* 求整帧长度 */
		uint32_t total = frame_frame_size(rx->buf, rx->len);
		if (total == 0) {
			/* 头部非法：丢弃首字节重新同步 */
			memmove(rx->buf, rx->buf + 1, rx->len - 1);
			rx->len--;
			continue;
		}
		if (rx->len < total) continue;   /* 还没收全 */

		/* 解码 */
		frame_t f;
		if (frame_decode(rx->buf, total, &f) == 0) {
			framelink_reasm(ch, &f);
		}

		/* 移除已处理的帧 */
		uint32_t remain = rx->len - total;
		if (remain) memmove(rx->buf, rx->buf + total, remain);
		rx->len = remain;
	}
}
