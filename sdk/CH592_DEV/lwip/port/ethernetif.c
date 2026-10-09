/********************************** (C) COPYRIGHT *******************************
 * File Name          : ethernetif.c
 * Description        : LWIP netif ↔ RNDIS 适配层
 *
 * 把 RNDIS 收到的以太网帧注入 LWIP，把 LWIP 发出的帧通过 RNDIS 发给主机。
 *******************************************************************************/

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "rndis.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * netif 输出：LWIP → RNDIS
 * ------------------------------------------------------------------------- */
static err_t rndis_netif_output(struct netif *netif, struct pbuf *p)
{
	(void)netif;

	/* RNDIS 需要连续缓冲，这里把 pbuf 链拷到连续内存 */
	static uint8_t txframe[RNDIS_MAX_FRAME];
	if (p->tot_len > sizeof(txframe)) {
		return ERR_MEM;
	}
	pbuf_copy_partial(p, txframe, p->tot_len, 0);

	if (rndis_send_frame(txframe, p->tot_len) != 0) {
		return ERR_IF;
	}
	return ERR_OK;
}

/* ---------------------------------------------------------------------------
 * netif 初始化
 * ------------------------------------------------------------------------- */
err_t rndis_netif_init(struct netif *netif)
{
	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->output = etharp_output;
	netif->linkoutput = rndis_netif_output;
	netif->mtu = RNDIS_MTU;   /* 方案 B：降 MTU 匹配 USB 缓冲 */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->hwaddr_len = ETH_HWADDR_LEN;
	rndis_get_mac(netif->hwaddr);
	return ERR_OK;
}

/* ---------------------------------------------------------------------------
 * 从 RNDIS 接收帧并注入 LWIP（由 usb 任务调用）
 * ------------------------------------------------------------------------- */
void rndis_netif_input(struct netif *netif, const uint8_t *frame, uint32_t len)
{
	struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
	if (p == NULL) {
		return;
	}
	pbuf_take(p, frame, (u16_t)len);

	if (netif->input(p, netif) != ERR_OK) {
		pbuf_free(p);
	}
}
