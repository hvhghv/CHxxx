/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_profile.h
 * Description        : BLE 透传 GATT 服务
 *
 * 服务定义：
 *   Service UUID        : 0xFFF0
 *   RX 特征 (0xFFF1)    : Write / WriteWithoutResponse  (浏览器 → 设备)
 *   TX 特征 (0xFFF2)    : Notify                        (设备 → 浏览器)
 *   CCCD                : 通知使能
 *******************************************************************************/

#ifndef _BLE_PROFILE_H
#define _BLE_PROFILE_H

/* 按芯片选择官方 BLE 库头文件（与 sdk/common/cmake/sdk.cmake 的芯片宏一致） */
#if defined(CH57x)
#include "CH572BLEPeri_LIB.h"
#elif defined(CH59x)
#include "CH59xBLE_LIB.h"
#else
#error "未定义芯片宏（CH57x / CH59x）"
#endif

/* 服务与特征 UUID */
#define BLE_SERV_UUID      0xFFF0
#define BLE_CHAR_RX_UUID   0xFFF1   /* 浏览器写入 → 设备 */
#define BLE_CHAR_TX_UUID   0xFFF2   /* 设备 → 通知给浏览器 */

/* 单包最大长度（BLE 默认 MTU 23 - 3 = 20） */
#define BLE_MAX_LEN        20

/* 回调：收到浏览器写入的数据 */
typedef void (*ble_rx_cb_t)(const uint8_t *data, uint16_t len);

/* 注册接收回调 */
void ble_profile_set_rx_cb(ble_rx_cb_t cb);

/* 初始化并注册 GATT 服务 */
bStatus_t ble_profile_init(void);

/* 通过 TX 特征发送通知（设备 → 浏览器）
 * 返回 SUCCESS 成功，其他失败（未连接/未使能通知） */
bStatus_t ble_profile_notify(const uint8_t *data, uint16_t len);

/* 当前连接是否已使能通知 */
int ble_profile_notify_enabled(void);

#endif /* _BLE_PROFILE_H */
