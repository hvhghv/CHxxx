/********************************** (C) COPYRIGHT *******************************
 * File Name          : bleapp.h
 * Description        : 网关 BLE 应用（GAP 广播 + GATT 透传服务）
 *
 * 提供：
 *   - BLE 初始化（GAP + GATT + 自定义透传服务）
 *   - bleapp_process()  主循环调用（TMOS 事件处理）
 *   - bleapp_notify()   向已连接主机发送通知
 *   - BLE 状态查询
 *   - 单角色分时切换：Peripheral（从机广播）<-> Central（主机扫描/连接）
 *
 * GATT 服务（与 usb_dual 一致）：
 *   Service 0xFFF0
 *     RX 0xFFF1 (Write)  ← 主机写入
 *     TX 0xFFF2 (Notify) → 通知主机
 *******************************************************************************/

#ifndef _BLEAPP_H
#define _BLEAPP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * BLE 角色模式（编译期宏，由 CMake 定义）
 *   BLE_ROLE_MODE_PERIPH  ：仅从机（省 Flash，CH591 可用）
 *   BLE_ROLE_MODE_CENTRAL ：仅主机
 *   BLE_ROLE_MODE_DUAL    ：双模式（从机 + 主机，默认）
 * ------------------------------------------------------------------------- */
#define BLE_ROLE_MODE_PERIPH   1
#define BLE_ROLE_MODE_CENTRAL  2
#define BLE_ROLE_MODE_DUAL     3

#ifndef BLE_ROLE_MODE
#define BLE_ROLE_MODE          BLE_ROLE_MODE_DUAL
#endif

/* 是否编译从机 / 主机代码 */
#if BLE_ROLE_MODE == BLE_ROLE_MODE_PERIPH
  #define BLE_ENABLE_PERIPH   1
  #define BLE_ENABLE_CENTRAL  0
#elif BLE_ROLE_MODE == BLE_ROLE_MODE_CENTRAL
  #define BLE_ENABLE_PERIPH   0
  #define BLE_ENABLE_CENTRAL  1
#else
  #define BLE_ENABLE_PERIPH   1
  #define BLE_ENABLE_CENTRAL  1
#endif

/* BLE 角色 */
typedef enum {
	BLE_ROLE_PERIPHERAL = 0,   /* 从机：广播，可被连接 */
	BLE_ROLE_CENTRAL    = 1,   /* 主机：扫描，主动连接 */
} bleapp_role_t;

/* 扫描到的设备信息 */
typedef struct {
	uint8_t addr[6];
	uint8_t addr_type;
	int8_t  rssi;
	char    name[20];
} bleapp_dev_t;

/* 初始化 BLE（GAP + GATT + 透传服务），默认从机广播 */
void bleapp_init(void);

/* 主循环调用：处理 TMOS 事件（必须周期调用） */
void bleapp_process(void);

/* 向已连接主机发送通知，返回 0 成功 */
int bleapp_notify(const uint8_t *data, uint16_t len);

/* 注册收到主机写入的回调 */
typedef void (*bleapp_rx_cb_t)(const uint8_t *data, uint16_t len);
void bleapp_set_rx_cb(bleapp_rx_cb_t cb);

/* 状态查询 */
int bleapp_is_connected(void);
int bleapp_is_advertising(void);
const char *bleapp_state_str(void);

/* ---------------------------------------------------------------------------
 * 单角色分时切换
 * ------------------------------------------------------------------------- */

/* 当前角色 */
bleapp_role_t bleapp_role(void);
const char *bleapp_role_str(void);

/* 切换到主机模式：停广播 → 断开现有连接 → CentralInit → 开始扫描 */
int bleapp_switch_central(void);

/* 切回从机模式：停止扫描/断开 → PeripheralInit → 恢复广播 */
int bleapp_switch_peripheral(void);

/* 主机扫描控制 */
int bleapp_central_scan(void);        /* 开始/重启扫描（清空列表） */
int bleapp_central_stop_scan(void);   /* 停止扫描 */

/* 扫描结果 */
int bleapp_scan_count(void);
const bleapp_dev_t *bleapp_scan_get(int idx);

/* 连接扫描列表中的第 idx 个设备，返回 0 成功 */
int bleapp_central_connect(int idx);

/* 断开当前连接（主机或从机） */
int bleapp_disconnect(void);

/* 主机是否处于扫描中 */
int bleapp_is_scanning(void);

/* ---------------------------------------------------------------------------
 * 动态 GATT 服务（供 blemgr 使用）
 * ---------------------------------------------------------------------------
 * 每个 owner 注册一个服务，含若干特征。服务/特征 UUID 由调用方提供，
 * 内部动态构建 attrTbl（常驻，协议栈持有指针）。
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * BLE UUID128 支持（编译期宏，默认关闭以省空间）
 *   0：仅支持 16bit UUID（省 RAM/Flash，帧负载更小）
 *   1：支持 16bit + 128bit UUID
 * 由 CMake 定义 BLE_UUID128_SUPPORT=1 开启。
 * ------------------------------------------------------------------------- */
#ifndef BLE_UUID128_SUPPORT
#define BLE_UUID128_SUPPORT   0
#endif

/* 特征描述（注册时传入） */
typedef struct {
	uint16_t uuid16;          /* 16bit UUID（0=使用 uuid128） */
	uint8_t  props;           /* BLE_PROP_* 位 */
#if BLE_UUID128_SUPPORT
	uint8_t  uuid128[16];     /* 128bit UUID（uuid16=0 时有效） */
#endif
} bleapp_feat_t;

/* 事件回调（连接/订阅/数据）：conn_handle 由 blemgr 映射到 logical_id */
typedef void (*bleapp_evt_cb_t)(int owner_id, int feat_idx, uint16_t conn_handle,
                                const uint8_t *data, uint16_t len);

/* 连接状态回调：up=1 建立, up=0 断开 */
typedef void (*bleapp_link_cb_t)(int up, uint16_t conn_handle);
void bleapp_set_link_cb(bleapp_link_cb_t cb);

/* CCCD 订阅变化回调：en=1 使能, en=0 禁用 */
typedef void (*bleapp_cccd_cb_t)(int owner_id, int feat_idx, uint16_t conn_handle, int en);
void bleapp_set_cccd_cb(bleapp_cccd_cb_t cb);

/* 注册服务：owner_id 唯一，feats 数组原子注册，返回 0 成功 */
int bleapp_register_service(int owner_id, const bleapp_feat_t *feats, int count);

/* 注销服务 */
int bleapp_unregister_service(int owner_id);

/* 发送数据到指定 owner 的特征（Notify），返回 0 成功 */
int bleapp_send(int owner_id, int feat_idx, uint16_t conn_handle,
                const uint8_t *data, uint16_t len);

/* 设置事件回调（连接/断开/订阅/数据） */
void bleapp_set_evt_cb(bleapp_evt_cb_t cb);

/* 查询某 owner 的特征是否已订阅 Notify */
int bleapp_is_notify_enabled(int owner_id, int feat_idx, uint16_t conn_handle);

#ifdef __cplusplus
}
#endif

#endif /* _BLEAPP_H */
