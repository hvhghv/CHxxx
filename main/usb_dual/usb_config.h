/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_config.h
 * Description        : CH572 双 USB 串口（复合 CDC-ACM）描述符
 *
 * 设备枚举为两个独立的 CDC-ACM 虚拟串口：
 *   串口 A (Interface 0/1) : 终端   —— 系统信息、蓝牙控制命令
 *   串口 B (Interface 2/3) : 蓝牙桥 —— 与 BLE 双向透传
 *
 * 两个串口在设备管理器中显示不同名称（IAD iFunction + 接口 iInterface）：
 *   A -> "CH572 Terminal (A)"
 *   B -> "CH572 BLE Bridge (B)"
 *
 * 端点分配：
 *   EP0      : 控制
 *   EP1 IN   : CDC-A 通知
 *   EP2 OUT  : CDC-A 数据接收
 *   EP3 IN   : CDC-A 数据发送
 *   EP4 OUT  : CDC-B 数据接收
 *   EP5 IN   : CDC-B 数据发送
 *
 * 注意：CH572 的 EP7 双向 DMA 与 RX 共用寄存器，不能同时收发；
 * 因此所有数据端点均为单向，每个 CDC 用独立的 IN/OUT 端点。
 *
 * 说明：因 CH572 只有 12KB RAM，而 BLE GAP 模块需 ~10.5KB，
 * 故只保留 2 个 CDC（去掉了调试口 C）。printf 输出改走串口 A。
 *******************************************************************************/

#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "funconfig.h"
#include "ch32fun.h"

/* -------------------------------------------------------------------------
 * 端点配置
 * ------------------------------------------------------------------------- */
/* 只用 EP1~EP3，把端点表缩小到 4 项（省 RAM） */
#define FUSB_MAX_EP_CNT       4
#define FUSB_BUFFERS_NUMBER   4
#define FUSB_EP1_MODE         USBFS_EP_MODE_TX   /* CDC-A notify IN */
#define FUSB_EP2_MODE         USBFS_EP_MODE_RX   /* CDC-A data OUT */
#define FUSB_EP3_MODE         USBFS_EP_MODE_TX   /* CDC-A data IN */

#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   0
#define FUSB_CURSED_TURBO_DMA 0
#define FUSB_HID_USER_REPORTS 0
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          0
#define FUSB_USER_HANDLERS    1
#define FUSB_USE_DMA7_COPY    0
#define FUSB_VDD_5V           FUNCONF_USE_5V_VDD
#define FUSB_FROM_RAM         0

#include "usb_defines.h"

/* -------------------------------------------------------------------------
 * 设备标识
 * ------------------------------------------------------------------------- */
#define FUSB_USB_VID 0x1209
#define FUSB_USB_PID 0x5722
#define FUSB_USB_REV 0x0001

#define FUSB_STR_MANUFACTURER u"CH572-Lite-SDK"
#define FUSB_STR_PRODUCT      u"CH572 BLE CDC"
#define FUSB_STR_SERIAL       u"0001"

/* 功能名（显示在设备管理器的接口名 / 串口名中）
 * 注意：Windows 串口名默认取 IAD 的 iFunction 字符串。 */
#define FUSB_STR_ITF_A        u"CH572 Terminal (A)"

/* -------------------------------------------------------------------------
 * 设备描述符
 * ------------------------------------------------------------------------- */
static const uint8_t device_descriptor[] = {
	18, 1, 0x00, 0x02,
	0xEF,   /* bDeviceClass: Miscellaneous (复合设备) */
	0x02,   /* bDeviceSubClass: Common Class */
	0x01,   /* bDeviceProtocol: Interface Association Descriptor */
	64,
	(uint8_t)(FUSB_USB_VID), (uint8_t)(FUSB_USB_VID >> 8),
	(uint8_t)(FUSB_USB_PID), (uint8_t)(FUSB_USB_PID >> 8),
	(uint8_t)(FUSB_USB_REV), (uint8_t)(FUSB_USB_REV >> 8),
	1, 2, 3, 1,
};

/* -------------------------------------------------------------------------
 * 配置描述符集（3 个 CDC 设备，共 6 个接口）
 *
 * 每个 CDC 需要 IAD + 控制接口 + 数据接口。
 * ------------------------------------------------------------------------- */
static const uint8_t config_descriptor[] = {
	/* ===== Configuration Descriptor ===== */
	0x09, 0x02,
	0x4B, 0x00,             /* wTotalLength = 75 */
	0x02,                   /* bNumInterfaces = 2 */
	0x01,                   /* bConfigurationValue */
	0x00,                   /* iConfiguration */
	0xC0,                   /* bmAttributes: 自供电 + 远程唤醒 */
	0x32,                   /* bMaxPower: 100mA */

	/* ===== CDC-A: Interface Association ===== */
	0x08, 0x0B,             /* bLength, bDescriptorType (IAD) */
	0x00,                   /* bFirstInterface = 0 */
	0x02,                   /* bInterfaceCount = 2 */
	0x02, 0x02, 0x01,       /* CDC / ACM / AT */
	0x04,                   /* iFunction = 4 ("CH572 Terminal (A)") */

	/* --- CDC-A Interface 0: Control --- */
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
	/* CDC Header */
	0x05, 0x24, 0x00, 0x10, 0x01,
	/* CDC Call Management */
	0x05, 0x24, 0x01, 0x00, 0x01,
	/* CDC ACM */
	0x04, 0x24, 0x02, 0x02,
	/* CDC Union */
	0x05, 0x24, 0x06, 0x00, 0x01,
	/* EP1 IN: notification */
	0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,

	/* --- CDC-A Interface 1: Data --- */
	0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x04,
	/* EP2 OUT: data */
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
	/* EP3 IN: data */
	0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,
};

/* -------------------------------------------------------------------------
 * 字符串描述符
 * ------------------------------------------------------------------------- */
struct usb_string_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wString[64];
};

static const struct usb_string_descriptor string0 = {
	.bLength = 4, .bDescriptorType = 3, .wString = { 0x0409 }
};
static const struct usb_string_descriptor string1 = {
	.bLength = 4 + sizeof(FUSB_STR_MANUFACTURER) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_MANUFACTURER
};
static const struct usb_string_descriptor string2 = {
	.bLength = 4 + sizeof(FUSB_STR_PRODUCT) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_PRODUCT
};
static const struct usb_string_descriptor string3 = {
	.bLength = 4 + sizeof(FUSB_STR_SERIAL) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_SERIAL
};

/* 功能名（接口/串口名），索引 4/5/6 */
static const struct usb_string_descriptor string4 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_A) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_A
};

/* -------------------------------------------------------------------------
 * 描述符索引表
 * ------------------------------------------------------------------------- */
const static struct descriptor_list_struct {
	uint32_t        lIndexValue;
	const uint8_t  *addr;
	uint8_t         length;
} descriptor_list[] = {
	{0x00000100, device_descriptor, sizeof(device_descriptor)},
	{0x00000200, config_descriptor, sizeof(config_descriptor)},
	{0x00000300, (const uint8_t *)&string0, 4},
	{0x04090301, (const uint8_t *)&string1, string1.bLength},
	{0x04090302, (const uint8_t *)&string2, string2.bLength},
	{0x04090303, (const uint8_t *)&string3, string3.bLength},
	{0x04090304, (const uint8_t *)&string4, string4.bLength},
};

#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list)) / (sizeof(struct descriptor_list_struct)))

#endif /* _USB_CONFIG_H */
