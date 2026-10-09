/********************************** (C) COPYRIGHT *******************************
 * File Name          : netroute.h
 * Description        : 网络路由（IP 包 → 串口/SPI 帧转发）
 *
 * 功能：
 *   - 静态路由表：目标 IP 网段 → 出站帧链路通道
 *   - 出站拦截：本机发出的 IP 包若匹配路由表，封装为 NET_FWD 帧转发
 *   - 入站注入：从帧链路收到 NET_FWD 帧，解出 IP 包注入 LWIP
 *
 * 帧转发 payload 格式（FRAME_TYPE_NET_FWD）：
 *   [IP 包原始字节]（含 IP 头，不含以太网头）
 *******************************************************************************/

#ifndef _NETROUTE_H
#define _NETROUTE_H

#include <stdint.h>
#include "framelink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETROUTE_MAX_ENTRIES  4

/* 路由表项：目标网段 → 出站通道 */
typedef struct {
	uint32_t dest;       /* 目标网络（小端，如 10.0.0.0） */
	uint32_t mask;       /* 子网掩码 */
	uint8_t  ch;         /* 出站 framelink 通道 */
	uint8_t  enabled;
} netroute_entry_t;

/* 初始化 */
void netroute_init(void);

/* 添加/更新路由（返回 0 成功） */
int netroute_add(uint32_t dest, uint32_t mask, uint8_t ch);

/* 删除路由 */
int netroute_del(uint32_t dest, uint32_t mask);

/* 清空路由表 */
void netroute_clear(void);

/* 查找路由：返回出站通道（-1=无匹配，走默认） */
int netroute_lookup(uint32_t dest_ip);

/* 获取路由表（供 HTTP/终端查询） */
const netroute_entry_t *netroute_table(int *count);

/* 出站拦截：由 netif->output 调用
 * 返回 1=已转发（不继续走默认路径），0=未匹配（走默认） */
int netroute_output(const uint8_t *ip_pkt, uint32_t len, uint32_t dest_ip);

/* 入站注入：从 NET_FWD 帧收到 IP 包，注入 LWIP
 * 返回 0 成功 */
int netroute_input(const uint8_t *ip_pkt, uint32_t len);

/* 统计 */
uint32_t netroute_tx_count(void);
uint32_t netroute_rx_count(void);

#ifdef __cplusplus
}
#endif

#endif /* _NETROUTE_H */
