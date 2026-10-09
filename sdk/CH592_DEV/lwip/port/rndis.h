/********************************** (C) COPYRIGHT *******************************
 * File Name          : rndis.h
 * Description        : RNDIS (Remote NDIS) USB 类设备
 *
 * 实现 RNDIS 类协议，使 CH591/CH592 通过 USB 呈现为一张以太网网卡，
 * 供 LWIP 使用。参考 MS-RNDIS 规范。
 *******************************************************************************/

#ifndef _RNDIS_H
#define _RNDIS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * MTU 配置（方案 B：降低 MTU 以匹配 USB 缓冲，省 RAM）
 * ---------------------------------------------------------------------------
 * RNDIS 的 USB 收发缓冲有限，故把 IP MTU 降到 500。
 * 主机侧会据此调整 TCP MSS，自动分段，无需 IP 分片。
 * 若需标准 1500 MTU，把 RNDIS_MTU 改为 1500 并增大 RNDIS_BUF_SIZE。
 * ------------------------------------------------------------------------- */
#define RNDIS_MTU            500                     /* IP 层 MTU */
#define RNDIS_MAX_FRAME      (RNDIS_MTU + 14)        /* 以太网帧（+14 头） */
#define RNDIS_BUF_SIZE       (RNDIS_MAX_FRAME + 64)  /* USB 缓冲（+RNDIS 头 44 + 余量） */

/* RNDIS 消息类型 */
#define RNDIS_MSG_INITIALIZE            0x00000002
#define RNDIS_MSG_INITIALIZE_CMPLT      0x80000002
#define RNDIS_MSG_HALT                  0x00000003
#define RNDIS_MSG_QUERY                 0x00000004
#define RNDIS_MSG_QUERY_CMPLT           0x80000004
#define RNDIS_MSG_SET                   0x00000005
#define RNDIS_MSG_SET_CMPLT             0x80000005
#define RNDIS_MSG_RESET                 0x00000006
#define RNDIS_MSG_RESET_CMPLT           0x80000006
#define RNDIS_MSG_INDICATE              0x00000007
#define RNDIS_MSG_KEEPALIVE             0x00000008
#define RNDIS_MSG_KEEPALIVE_CMPLT       0x80000008

/* RNDIS OID */
#define OID_GEN_SUPPORTED_LIST          0x00010101
#define OID_GEN_HARDWARE_STATUS         0x00010102
#define OID_GEN_MEDIA_SUPPORTED         0x00010103
#define OID_GEN_MEDIA_IN_USE            0x00010104
#define OID_GEN_MAXIMUM_FRAME_SIZE      0x00010106
#define OID_GEN_LINK_SPEED              0x00010107
#define OID_GEN_TRANSMIT_BLOCK_SIZE     0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE      0x0001010B
#define OID_GEN_VENDOR_ID               0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION      0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER   0x0001010E
#define OID_GEN_MAXIMUM_TOTAL_SIZE      0x00010111
#define OID_GEN_MEDIA_CONNECT_STATUS    0x00010114
#define OID_GEN_PHYSICAL_MEDIUM         0x00010202
#define OID_802_3_PERMANENT_ADDRESS     0x01010101
#define OID_802_3_CURRENT_ADDRESS       0x01010102
#define OID_802_3_MULTICAST_LIST        0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE     0x01010104
#define OID_802_3_RCV_ERROR_ALIGNMENT   0x01020101
#define OID_802_3_XMIT_ONE_COLLISION    0x01020102
#define OID_802_3_XMIT_MORE_COLLISIONS  0x01020103

/* RNDIS 状态 */
#define RNDIS_STATUS_SUCCESS            0x00000000
#define RNDIS_STATUS_NOT_SUPPORTED      0xC00000BB
#define RNDIS_STATUS_INVALID_DATA       0xC0010015

/* 数据包过滤 */
#define RNDIS_PACKET_TYPE_DIRECTED      0x00000001
#define RNDIS_PACKET_TYPE_MULTICAST     0x00000002
#define RNDIS_PACKET_TYPE_BROADCAST     0x00000004
#define RNDIS_PACKET_TYPE_ALL_MULTICAST 0x00000008
#define RNDIS_PACKET_TYPE_PROMISCUOUS   0x00000020

/* 初始化 */
void rndis_init(void);

/* 处理来自主机的 RNDIS 控制消息（EP0 数据阶段） */
int rndis_control_message(const uint8_t *data, uint32_t len, uint8_t *reply, uint32_t *reply_len);

/* 处理来自主机的 RNDIS 数据包（EP 数据 OUT），返回以太网帧指针与长度 */
int rndis_data_out(const uint8_t *data, uint32_t len, const uint8_t **frame, uint32_t *frame_len);

/* ---------------------------------------------------------------------------
 * USB 多包传输（CH59x 端点 64B，需累积/分块）
 * ---------------------------------------------------------------------------
 * 主机把一个 RNDIS 帧拆成多个 64B USB 包发送，需累积到完整帧；
 * 发送时也需把帧拆成多个 64B 包。
 * ------------------------------------------------------------------------- */

/* 接收累积：每次 USB OUT 中断调用，传入本包数据
 * 返回：1=已收完整 RNDIS 数据帧（frame/frame_len 有效），0=继续等待，-1=错误 */
int rndis_usb_rx(const uint8_t *data, uint32_t len,
                 const uint8_t **frame, uint32_t *frame_len);

/* 发送分块：把 RNDIS 报文拆成 64B 包通过 USB 发送
 * send_fn: 发送单包的回调（返回 0 成功）
 * 返回 0 成功 */
int rndis_usb_tx(const uint8_t *data, uint32_t len,
                 int (*send_fn)(const uint8_t *pkt, uint32_t pkt_len));

/* 发送以太网帧到主机（由 LWIP netif 调用） */
int rndis_send_frame(const uint8_t *frame, uint32_t len);

/* 主机是否已初始化（可收发数据） */
int rndis_is_ready(void);

/* 获取本机 MAC 地址 */
void rndis_get_mac(uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif /* _RNDIS_H */
