/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_config.h
 * Description        : CH591/CH592 网关 USB 复合设备描述符
 *
 * 阶段 2：3×CDC-ACM + 1×RNDIS
 *
 * 接口布局：
 *   CDC-A (Interface 0/1) : 终端
 *   CDC-B (Interface 2/3) : 调试
 *   CDC-C (Interface 4/5) : 数据
 *   RNDIS (Interface 6/7) : 网络（控制 + 数据）
 *
 * 端点分配（CH59x：EP0-EP7，EP4 与 EP0 共用 DMA 不可用）：
 *   EP0      : 控制
 *   EP1 BDIR : CDC-A 数据（双向）
 *   EP2 BDIR : CDC-B 数据（双向）
 *   EP3 BDIR : CDC-C 数据（双向）
 *   EP5 OUT  : RNDIS 数据接收
 *   EP6 IN   : RNDIS 数据发送
 *   EP7 IN   : RNDIS 中断（状态通知）
 *******************************************************************************/

#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "funconfig.h"
#include "ch32fun.h"

/* -------------------------------------------------------------------------
 * 端点配置
 * ------------------------------------------------------------------------- */
#define FUSB_MAX_EP_CNT       8
#define FUSB_BUFFERS_NUMBER   9

#define FUSB_EP1_MODE         USBFS_EP_MODE_BDIR  /* CDC-A 数据（双向） */
#define FUSB_EP2_MODE         USBFS_EP_MODE_BDIR  /* CDC-B 数据（双向） */
#define FUSB_EP3_MODE         USBFS_EP_MODE_BDIR  /* CDC-C 数据（双向） */
#define FUSB_EP4_MODE         0                   /* 保留（与 EP0 共用 DMA） */
#define FUSB_EP5_MODE         USBFS_EP_MODE_RX    /* RNDIS 数据 OUT */
#define FUSB_EP6_MODE         USBFS_EP_MODE_TX    /* RNDIS 数据 IN  */
#define FUSB_EP7_MODE         USBFS_EP_MODE_TX    /* RNDIS 中断 IN  */

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
#define FUSB_USB_PID 0x5911
#define FUSB_USB_REV 0x0001

#define FUSB_STR_MANUFACTURER u"CH59x-Lite-SDK"
#define FUSB_STR_PRODUCT      u"CH591 Gateway"
#define FUSB_STR_SERIAL       u"0001"

#define FUSB_STR_ITF_A        u"CH591 Terminal (A)"
#define FUSB_STR_ITF_B        u"CH591 Debug (B)"
#define FUSB_STR_ITF_C        u"CH591 Data (C)"
#define FUSB_STR_ITF_RNDIS    u"CH591 RNDIS"

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
 * 配置描述符集
 *
 * 3×CDC（各 2 接口）+ RNDIS（2 接口）= 8 接口
 * CDC 数据接口用 1 个 BDIR 端点（省端点）。
 * ------------------------------------------------------------------------- */
static const uint8_t config_descriptor[] = {
	/* ===== Configuration Descriptor ===== */
	0x09, 0x02,
	0xC4, 0x00,             /* wTotalLength = 196 */
	0x08,                   /* bNumInterfaces = 8 */
	0x01,                   /* bConfigurationValue */
	0x00,                   /* iConfiguration */
	0xC0,                   /* bmAttributes */
	0x32,                   /* bMaxPower: 100mA */

	/* ===== CDC-A ===== */
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x04,   /* IAD */
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,  /* Control iface */
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x01, 0x00, 0x01,
	0x04, 0x24, 0x02, 0x02,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,  /* Data iface */
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,   /* EP1 BDIR */

	/* ===== CDC-B ===== */
	0x08, 0x0B, 0x02, 0x02, 0x02, 0x02, 0x01, 0x05,   /* IAD */
	0x09, 0x04, 0x02, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x01, 0x00, 0x01,
	0x04, 0x24, 0x02, 0x02,
	0x05, 0x24, 0x06, 0x02, 0x03,
	0x09, 0x04, 0x03, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,   /* EP2 BDIR */

	/* ===== CDC-C ===== */
	0x08, 0x0B, 0x04, 0x02, 0x02, 0x02, 0x01, 0x06,   /* IAD */
	0x09, 0x04, 0x04, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x01, 0x00, 0x01,
	0x04, 0x24, 0x02, 0x02,
	0x05, 0x24, 0x06, 0x04, 0x05,
	0x09, 0x04, 0x05, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00,   /* EP3 BDIR */

	/* ===== RNDIS ===== */
	0x08, 0x0B, 0x06, 0x02, 0xE0, 0x01, 0x03, 0x07,   /* IAD: Wireless/RNDIS */
	/* Control interface 6 */
	0x09, 0x04, 0x06, 0x00, 0x01, 0xE0, 0x01, 0x03, 0x00,
	/* RNDIS class descriptor (5 bytes) */
	0x05, 0x24, 0x00, 0x01, 0x00,
	/* EP7 IN: interrupt (notify) */
	0x07, 0x05, 0x87, 0x03, 0x08, 0x00, 0x10,
	/* Data interface 7 */
	0x09, 0x04, 0x07, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	/* EP5 OUT: bulk data */
	0x07, 0x05, 0x05, 0x02, 0x40, 0x00, 0x00,
	/* EP6 IN: bulk data */
	0x07, 0x05, 0x86, 0x02, 0x40, 0x00, 0x00,
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
static const struct usb_string_descriptor string4 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_A) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_A
};
static const struct usb_string_descriptor string5 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_B) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_B
};
static const struct usb_string_descriptor string6 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_C) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_C
};
static const struct usb_string_descriptor string7 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_RNDIS) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_RNDIS
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
	{0x04090305, (const uint8_t *)&string5, string5.bLength},
	{0x04090306, (const uint8_t *)&string6, string6.bLength},
	{0x04090307, (const uint8_t *)&string7, string7.bLength},
};

#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list)) / (sizeof(struct descriptor_list_struct)))

#endif /* _USB_CONFIG_H */
