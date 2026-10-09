/********************************** (C) COPYRIGHT *******************************
 * File Name          : dhcps.h
 * Description        : 极简 DHCP 服务器（RNDIS 主机自动获取 IP）
 *
 * 功能：
 *   - 监听 UDP 67，响应 DISCOVER/REQUEST
 *   - 派发 IP + 子网掩码 + 租期
 *   - **不派发网关（router 选项）**、不派发 DNS
 *   - 单客户端（适配 RNDIS 单主机场景）
 *
 * 配置：DHCP 地址池由调用方指定（起始 IP + 掩码）。
 *******************************************************************************/

#ifndef _DHCPS_H
#define _DHCPS_H

#include <stdint.h>
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 DHCP 服务器
 *   netif     : 服务网卡（RNDIS）
 *   server_ip : 服务器自身 IP（如 192.168.7.1）
 *   client_ip : 分配给客户端的 IP（如 192.168.7.2）
 *   mask      : 子网掩码（如 255.255.255.0）
 * 返回 0 成功 */
int dhcps_start(struct netif *netif, uint32_t server_ip,
                uint32_t client_ip, uint32_t mask);

/* 运行时更新派发的 IP 与掩码（服务器 IP 不变） */
void dhcps_config(uint32_t client_ip, uint32_t mask);

/* 获取当前配置（任一指针可为 NULL） */
void dhcps_get_config(uint32_t *server_ip, uint32_t *client_ip, uint32_t *mask);

/* 停止 DHCP 服务器 */
void dhcps_stop(void);

/* 已分配 IP 的客户端数量（0 或 1） */
int dhcps_client_count(void);

/* 最近分配的客户端 IP（0=无） */
uint32_t dhcps_client_ip(void);

#ifdef __cplusplus
}
#endif

#endif /* _DHCPS_H */
