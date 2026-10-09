/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_ble_profile.h
 * Description        : USB ↔ BLE 透传 GATT 服务
 *
 * 服务定义：
 *   Service UUID        : 0xFFF0
 *   RX 特征 (0xFFF1)    : Write / WriteWithoutResponse  (浏览器 → 设备 → USB)
 *   TX 特征 (0xFFF2)    : Notify                        (USB → 设备 → 浏览器)
 *   CCCD                : 通知使能
 *******************************************************************************/

#ifndef _USB_BLE_PROFILE_H
#define _USB_BLE_PROFILE_H

/* 按芯片选择官方 BLE 库头文件（与 sdk/common/cmake/sdk.cmake 的芯片宏一致） */
#if defined(CH57x)
#include "CH572BLEPeri_LIB.h"
#elif defined(CH59x)
#include "CH59xBLE_LIB.h"
#else
#error "未定义芯片宏（CH57x / CH59x）"
#endif

/* 服务与特征 UUID */
#define USB_BLE_SERV_UUID      0xFFF0
#define USB_BLE_CHAR_RX_UUID   0xFFF1   /* 浏览器写入 → 转发到 USB */
#define USB_BLE_CHAR_TX_UUID   0xFFF2   /* USB 数据 → 通知给浏览器 */

/* 数据最大长度（BLE 单包）
 * 默认 MTU 23 - 3 = 20，这里用 12 节省 RAM */
#define USB_BLE_MAX_LEN        12

/* 回调：收到浏览器写入的数据 */
typedef void (*usb_ble_rx_cb_t)(const uint8_t *data, uint16_t len);

/* 注册接收回调 */
void usb_ble_profile_set_rx_cb(usb_ble_rx_cb_t cb);

/* 初始化并注册 GATT 服务（返回 0 成功） */
bStatus_t usb_ble_profile_init(void);

/* 通过 TX 特征发送通知（USB → 浏览器）
 * 返回 0 成功，非 0 失败（未连接/未使能通知） */
bStatus_t usb_ble_profile_notify(const uint8_t *data, uint16_t len);

/* 当前是否有客户端使能了通知 */
int usb_ble_profile_notify_enabled(void);

#endif /* _USB_BLE_PROFILE_H */
