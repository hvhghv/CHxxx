/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_proto.h
 * Description        : BLE 帧命令协议（应用芯片 <-> CH592 BLE 物理层）
 *
 * 架构：
 *   - CH592 统一管理 BLE 协议栈（物理层）
 *   - 应用芯片通过帧命令注册服务/特征 UUID
 *   - 连接/数据事件通过帧命令回推给应用芯片
 *   - 数据透传：应用芯片指定 UUID + 连接对象发送
 *
 * 帧命令 type（复用 frame.h 的 type 字段）：
 *   0x60  BLE_REG_SVC     注册服务（owner + 特征数组，原子）
 *   0x61  BLE_UNREG_SVC   注销服务
 *   0x62  BLE_SEND        发送数据（owner + logical_id + 特征索引）
 *   0x63  BLE_LINK_ACK    应用 ACK LINK_DOWN
 *   0x70  BLE_LINK_UP     事件：连接建立（→ 应用）
 *   0x71  BLE_LINK_DOWN   事件：连接断开（→ 应用）
 *   0x72  BLE_NOTIFY_EN   事件：CCCD 订阅使能（→ 应用）
 *   0x73  BLE_NOTIFY_DIS  事件：CCCD 订阅禁用（→ 应用）
 *   0x74  BLE_DATA_RX     事件：收到写入数据（→ 应用，裸透传）
 *
 * 错误码（响应 payload[0]）：
 *   0x00 OK
 *   0x01 EINVAL   参数非法
 *   0x02 EBUSY    UUID 已被占用 / 资源不足
 *   0x03 ENODEV   owner 未注册
 *   0x04 ESTALE   logical_id 已失效（ACK 前可发，ACK 后失效）
 *   0x05 EAGAIN   NOTIFY 未订阅 / 无法发送
 *   0x06 ENOENT   特征不存在
 *******************************************************************************/

#ifndef _BLE_PROTO_H
#define _BLE_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 帧命令 type
 * ------------------------------------------------------------------------- */
#define BLE_CMD_REG_SVC     0x60
#define BLE_CMD_UNREG_SVC   0x61
#define BLE_CMD_SEND        0x62
#define BLE_CMD_LINK_ACK    0x63

#define BLE_EVT_LINK_UP     0x70
#define BLE_EVT_LINK_DOWN   0x71
#define BLE_EVT_NOTIFY_EN   0x72
#define BLE_EVT_NOTIFY_DIS  0x73
#define BLE_EVT_DATA_RX     0x74

/* ---------------------------------------------------------------------------
 * 错误码
 * ------------------------------------------------------------------------- */
#define BLE_OK          0x00
#define BLE_EINVAL      0x01
#define BLE_EBUSY       0x02
#define BLE_ENODEV      0x03
#define BLE_ESTALE      0x04
#define BLE_EAGAIN      0x05
#define BLE_ENOENT      0x06

/* ---------------------------------------------------------------------------
 * 特征属性位（props）
 * ------------------------------------------------------------------------- */
#define BLE_PROP_READ       0x01
#define BLE_PROP_WRITE      0x02
#define BLE_PROP_WRITE_NR   0x04    /* Write Without Response */
#define BLE_PROP_NOTIFY     0x08

/* ---------------------------------------------------------------------------
 * 连接事件通知模式（link_notify_mode）
 * ------------------------------------------------------------------------- */
#define BLE_LINK_EAGER      0x00    /* 连接建立即通知 */
#define BLE_LINK_LAZY       0x01    /* 手机首次写入该 owner 时才通知 */

/* ---------------------------------------------------------------------------
 * 容量上限
 * ------------------------------------------------------------------------- */
#define BLE_MAX_OWNERS      2       /* 最多 2 个应用 owner */
#define BLE_MAX_FEATURES    6       /* 每 owner 最多 6 个特征 */

/* ---------------------------------------------------------------------------
 * BLE_REG_SVC 请求 payload 布局
 * ---------------------------------------------------------------------------
 *   [0]      owner_id
 *   [1]      feat_count (1..BLE_MAX_FEATURES)
 *   [2]      link_notify_mode (EAGER/LAZY)
 *   [3..]    特征描述数组，每项：
 *              [0-1]  uuid16 (小端)
 *              [2]    props
 *              [3..18] uuid128（仅 BLE_UUID128_SUPPORT=1 且 uuid16=0 时）
 *   变长：每特征 3 字节（无 128bit）或 19 字节（含 128bit）
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * BLE_SEND 请求 payload 布局
 * ---------------------------------------------------------------------------
 *   [0]      owner_id
 *   [1]      feat_idx（特征索引）
 *   [2-5]    logical_id (uint32 小端)
 *   [6..]    数据（原样透传）
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * BLE_LINK_ACK 请求 payload 布局
 * ---------------------------------------------------------------------------
 *   [0]      owner_id
 *   [1-4]    logical_id (uint32 小端)
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * 事件 payload 布局（CH592 → 应用）
 * ---------------------------------------------------------------------------
 * BLE_EVT_LINK_UP / LINK_DOWN:
 *   [0]      owner_id
 *   [1-4]    logical_id (uint32 小端)
 *
 * BLE_EVT_NOTIFY_EN / NOTIFY_DIS:
 *   [0]      owner_id
 *   [1]      feat_idx
 *   [2-5]    logical_id (uint32 小端)
 *
 * BLE_EVT_DATA_RX:
 *   [0]      owner_id
 *   [1]      feat_idx
 *   [2-5]    logical_id (uint32 小端)
 *   [6..]    数据（原样透传）
 * ------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* _BLE_PROTO_H */
