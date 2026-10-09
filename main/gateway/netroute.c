/********************************** (C) COPYRIGHT *******************************
 * File Name          : netroute.c
 * Description        : 网络路由实现（IP 包 → 串口/SPI 帧转发）
 *******************************************************************************/

#include "ch32fun.h"
#include "netroute.h"
#include "frame.h"
#include "framelink.h"
#include "rndis.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/prot/ip4.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * 路由表
 * ------------------------------------------------------------------------- */
static netroute_entry_t routes[NETROUTE_MAX_ENTRIES];
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;

/* 外部 netif（用于注入） */
extern struct netif rndis_netif;

/* 转发缓冲（复用，省 RAM；与 RNDIS MTU 一致） */
#define NETROUTE_FWD_MAX  RNDIS_MTU
static uint8_t fwd_buf[NETROUTE_FWD_MAX];

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void netroute_init(void)
{
	memset(routes, 0, sizeof(routes));
	tx_count = 0;
	rx_count = 0;
}

/* ---------------------------------------------------------------------------
 * 路由表操作
 * ------------------------------------------------------------------------- */
int netroute_add(uint32_t dest, uint32_t mask, uint8_t ch)
{
	/* 先找是否已存在（同 dest/mask 则更新） */
	for (int i = 0; i < NETROUTE_MAX_ENTRIES; i++) {
		if (routes[i].enabled &&
		    routes[i].dest == dest && routes[i].mask == mask) {
			routes[i].ch = ch;
			return 0;
		}
	}
	/* 新增 */
	for (int i = 0; i < NETROUTE_MAX_ENTRIES; i++) {
		if (!routes[i].enabled) {
			routes[i].dest = dest;
			routes[i].mask = mask;
			routes[i].ch = ch;
			routes[i].enabled = 1;
			return 0;
		}
	}
	return -1;   /* 表满 */
}

int netroute_del(uint32_t dest, uint32_t mask)
{
	for (int i = 0; i < NETROUTE_MAX_ENTRIES; i++) {
		if (routes[i].enabled &&
		    routes[i].dest == dest && routes[i].mask == mask) {
			routes[i].enabled = 0;
			return 0;
		}
	}
	return -1;
}

void netroute_clear(void)
{
	memset(routes, 0, sizeof(routes));
}

/* ---------------------------------------------------------------------------
 * 查找路由
 * ------------------------------------------------------------------------- */
int netroute_lookup(uint32_t dest_ip)
{
	for (int i = 0; i < NETROUTE_MAX_ENTRIES; i++) {
		if (!routes[i].enabled) continue;
		if ((dest_ip & routes[i].mask) == (routes[i].dest & routes[i].mask)) {
			return routes[i].ch;
		}
	}
	return -1;
}

const netroute_entry_t *netroute_table(int *count)
{
	if (count) {
		int n = 0;
		for (int i = 0; i < NETROUTE_MAX_ENTRIES; i++) {
			if (routes[i].enabled) n++;
		}
		*count = n;
	}
	return routes;
}

/* ---------------------------------------------------------------------------
 * 出站拦截：把 IP 包封装为 NET_FWD 帧转发
 * ------------------------------------------------------------------------- */
int netroute_output(const uint8_t *ip_pkt, uint32_t len, uint32_t dest_ip)
{
	int ch = netroute_lookup(dest_ip);
	if (ch < 0) return 0;   /* 无匹配，走默认路径 */

	if (len > NETROUTE_FWD_MAX) return 0;   /* 太大，无法单帧转发 */

	/* 构造 NET_FWD 帧并发送 */
	uint32_t n = frame_build_single(fwd_buf, FRAME_TYPE_NET_FWD, 0,
	                                ip_pkt, (uint16_t)len);
	if (n == 0) return 0;

	if (framelink_send((framelink_ch_t)ch, fwd_buf, n) > 0) {
		tx_count++;
		return 1;   /* 已转发 */
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * 入站注入：从 NET_FWD 帧解出 IP 包注入 LWIP
 * ------------------------------------------------------------------------- */
int netroute_input(const uint8_t *ip_pkt, uint32_t len)
{
	if (len < 20 || len > NETROUTE_FWD_MAX) return -1;

	/* 分配 pbuf 并注入 */
	struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (!p) return -2;
	pbuf_take(p, ip_pkt, (u16_t)len);

	/* 注入 LWIP（IP 层） */
	if (ip4_input(p, &rndis_netif) != ERR_OK) {
		pbuf_free(p);
		return -3;
	}
	rx_count++;
	return 0;
}

uint32_t netroute_tx_count(void) { return tx_count; }
uint32_t netroute_rx_count(void) { return rx_count; }
