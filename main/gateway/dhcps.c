/********************************** (C) COPYRIGHT *******************************
 * File Name          : dhcps.c
 * Description        : 极简 DHCP 服务器实现
 *
 * 协议要点：
 *   - 监听 UDP 67；广播回复到 255.255.255.255:68
 *   - DISCOVER → OFFER；REQUEST → ACK
 *   - 选项：53(类型) 54(服务器ID) 1(掩码) 51(租期) —— 不含 3(网关)
 *******************************************************************************/

#include "ch32fun.h"
#include "dhcps.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include <string.h>

/* DHCP 端口 */
#define DHCP_SERVER_PORT  67
#define DHCP_CLIENT_PORT  68

/* 消息类型 */
#define DHCP_DISCOVER     1
#define DHCP_OFFER        2
#define DHCP_REQUEST      3
#define DHCP_ACK          5

/* 选项码 */
#define OPT_SUBNET_MASK   1
#define OPT_ROUTER        3
#define OPT_REQ_IP        50
#define OPT_LEASE_TIME    51
#define OPT_MSG_TYPE      53
#define OPT_SERVER_ID     54
#define OPT_END           255

/* DHCP 报文（固定部分 240 字节 + 选项） */
#define DHCP_FIXED_LEN    240
#define DHCP_MIN_LEN      (DHCP_FIXED_LEN + 64)

static struct udp_pcb *dhcps_pcb = NULL;
static struct netif   *dhcps_netif = NULL;
static uint32_t dhcps_server_ip = 0;
static uint32_t dhcps_offer_ip = 0;   /* 分配给客户端的 IP */
static uint32_t dhcps_mask = 0;
static uint32_t dhcps_lease = 3600;   /* 租期 1 小时 */
static volatile int dhcps_assigned = 0;   /* 是否已分配 */

/* 静态发送缓冲（避免 PBUF_RAM 堆分配，省 LWIP 堆压力） */
static uint8_t dhcps_txbuf[DHCP_MIN_LEN];

/* 大端读写 */
static inline uint32_t rd32be(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wr32be(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* 构建回复报文（OFFER/ACK），返回长度 */
static uint16_t dhcps_build(uint8_t *out, const uint8_t *req, uint8_t msg_type)
{
	memset(out, 0, DHCP_MIN_LEN);

	out[0] = 2;                 /* op = BOOTREPLY */
	out[1] = req[1];            /* htype */
	out[2] = req[2];            /* hlen */
	/* xid */
	memcpy(out + 4, req + 4, 4);
	/* flags：保留广播标志 */
	out[10] = req[10]; out[11] = req[11];
	/* yiaddr = 分配的客户端 IP（大端） */
	wr32be(out + 16, dhcps_offer_ip);
	/* siaddr = 服务器 IP */
	wr32be(out + 20, dhcps_server_ip);
	/* chaddr（客户端 MAC） */
	memcpy(out + 28, req + 28, 16);
	/* magic cookie */
	out[236] = 99; out[237] = 130; out[238] = 83; out[239] = 99;

	uint16_t n = DHCP_FIXED_LEN;
	/* 选项 53：消息类型 */
	out[n++] = OPT_MSG_TYPE; out[n++] = 1; out[n++] = msg_type;
	/* 选项 54：服务器标识 */
	out[n++] = OPT_SERVER_ID; out[n++] = 4; wr32be(out + n, dhcps_server_ip); n += 4;
	/* 选项 1：子网掩码 */
	out[n++] = OPT_SUBNET_MASK; out[n++] = 4; wr32be(out + n, dhcps_mask); n += 4;
	/* 选项 51：租期 */
	out[n++] = OPT_LEASE_TIME; out[n++] = 4; wr32be(out + n, dhcps_lease); n += 4;
	/* 不含选项 3（网关）——按要求不派发 */
	out[n++] = OPT_END;
	return n;
}

/* UDP 接收回调 */
static void dhcps_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
	(void)arg; (void)addr; (void)port;
	if (!p) return;
	if (p->tot_len < DHCP_MIN_LEN) { pbuf_free(p); return; }

	uint8_t req[DHCP_MIN_LEN];
	pbuf_copy_partial(p, req, DHCP_MIN_LEN, 0);
	pbuf_free(p);

	/* 校验 magic cookie */
	if (req[236] != 99 || req[237] != 130 || req[238] != 83 || req[239] != 99) return;

	/* 解析选项，找消息类型 */
	uint8_t msg_type = 0;
	uint16_t i = DHCP_FIXED_LEN;
	while (i < DHCP_MIN_LEN) {
		uint8_t opt = req[i];
		if (opt == OPT_END) break;
		if (opt == 0) { i++; continue; }   /* pad */
		if (i + 1 >= DHCP_MIN_LEN) break;
		uint8_t len = req[i + 1];
		if (opt == OPT_MSG_TYPE && len >= 1) { msg_type = req[i + 2]; }
		i += 2 + len;
	}

	uint8_t reply_type;
	if (msg_type == DHCP_DISCOVER)      reply_type = DHCP_OFFER;
	else if (msg_type == DHCP_REQUEST)  reply_type = DHCP_ACK;
	else return;

	/* 构建回复到静态缓冲（避免 PBUF_RAM 堆分配） */
	uint16_t n = dhcps_build(dhcps_txbuf, req, reply_type);

	/* 广播回复到 255.255.255.255:68
	 * 用 PBUF_REF 引用静态缓冲，不拷贝、不占堆。
	 * udp_sendto 内部会同步发送（NO_SYS 模式），发送后缓冲即可复用。 */
	struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_REF);
	if (!r) return;
	r->payload = dhcps_txbuf;

	ip_addr_t bcast;
	IP4_ADDR(&bcast, 255, 255, 255, 255);
	udp_sendto(pcb, r, &bcast, DHCP_CLIENT_PORT);
	pbuf_free(r);

	if (reply_type == DHCP_ACK) dhcps_assigned = 1;
}

int dhcps_start(struct netif *netif, uint32_t server_ip,
                uint32_t client_ip, uint32_t mask)
{
	if (dhcps_pcb) return 0;   /* 已启动 */

	dhcps_netif = netif;
	dhcps_server_ip = server_ip;
	dhcps_offer_ip = client_ip;
	dhcps_mask = mask;
	dhcps_assigned = 0;

	dhcps_pcb = udp_new();
	if (!dhcps_pcb) return -1;

	err_t e = udp_bind(dhcps_pcb, IP_ADDR_ANY, DHCP_SERVER_PORT);
	if (e != ERR_OK) {
		udp_remove(dhcps_pcb);
		dhcps_pcb = NULL;
		return -1;
	}
	udp_recv(dhcps_pcb, dhcps_recv, NULL);
	return 0;
}

void dhcps_stop(void)
{
	if (dhcps_pcb) {
		udp_remove(dhcps_pcb);
		dhcps_pcb = NULL;
	}
	dhcps_assigned = 0;
}

void dhcps_config(uint32_t client_ip, uint32_t mask)
{
	if (client_ip) dhcps_offer_ip = client_ip;
	if (mask)      dhcps_mask = mask;
}

void dhcps_get_config(uint32_t *server_ip, uint32_t *client_ip, uint32_t *mask)
{
	if (server_ip) *server_ip = dhcps_server_ip;
	if (client_ip) *client_ip = dhcps_offer_ip;
	if (mask)      *mask = dhcps_mask;
}

int dhcps_client_count(void) { return dhcps_assigned ? 1 : 0; }
uint32_t dhcps_client_ip(void) { return dhcps_assigned ? dhcps_offer_ip : 0; }
