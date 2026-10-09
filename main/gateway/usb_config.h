/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_config.h
 * Description        : CH591/CH592 网关 USB 复合设备描述符
 *
 * 阶段 2：4×CDC-ACM + 1×RNDIS
 *
 * 接口布局：
 *   CDC-A (Interface 0/1)   : 终端
 *   CDC-B (Interface 2/3)   : UART 转发 0
 *   CDC-C (Interface 4/5)   : UART 转发 1
 *   CDC-D (Interface 6/7)   : UART 转发 2
 *   RNDIS (Interface 8/9)   : 网络（控制 + 数据）
 *
 * 端点分配（CH59x：EP0-EP7，EP4 与 EP0 共用 DMA 不可用）：
 *   EP0      : 控制
 *   EP1 BDIR : CDC-A 数据（双向）
 *   EP2 BDIR : CDC-B 数据（双向）
 *   EP3 BDIR : CDC-C 数据（双向）
 *   EP4      : 不可用（与 EP0 共用 DMA）
 *   EP5 BDIR : CDC-D 数据（双向）
 *   EP6 IN   : RNDIS 数据发送
 *   EP7 OUT  : RNDIS 数据接收
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
#define FUSB_EP5_MODE         USBFS_EP_MODE_BDIR  /* CDC-D 数据（双向） */
#define FUSB_EP6_MODE         USBFS_EP_MODE_TX    /* RNDIS 数据 IN  */
#define FUSB_EP7_MODE         USBFS_EP_MODE_RX    /* RNDIS 数据 OUT */

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
#define FUSB_STR_ITF_B        u"CH591 UART0 (B)"
#define FUSB_STR_ITF_C        u"CH591 UART1 (C)"
#define FUSB_STR_ITF_D        u"CH591 UART2 (D)"
#define FUSB_STR_ITF_RNDIS    u"CH591 RNDIS"

/* -------------------------------------------------------------------------
 * CDC 数量（由 CMake 传入 GW_CDC_COUNT；缺省按芯片）
 *   3 = 终端 + 2 个 UART 转发（CDC-A/B/C）—— CH591 默认
 *   4 = 终端 + 3 个 UART 转发（CDC-A/B/C/D）—— CH592 默认
 * ------------------------------------------------------------------------- */
#ifndef GW_CDC_COUNT
#  if defined(CH591)
#    define GW_CDC_COUNT 3
#  else
#    define GW_CDC_COUNT 4
#  endif
#endif

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
 * GW_CDC_COUNT 个 CDC（各 2 接口）+ RNDIS（2 接口）
 *   CDC 数量 3 → 8 接口（CH591）；4 → 10 接口（CH592）
 * CDC 数据接口用 1 个 BDIR 端点（省端点）。
 *
 * 接口/端点编号规则（CDC 段）：
 *   CDC-i 控制接口 = 2*i，数据接口 = 2*i+1
 *   端点号：CDC-0→EP1, CDC-1→EP2, CDC-2→EP3, CDC-3→EP5（跳过 EP4）
 * RNDIS 段紧随 CDC 之后：控制接口 = 2*GW_CDC_COUNT，数据接口 = 2*GW_CDC_COUNT+1
 * ------------------------------------------------------------------------- */

/* 生成一个 CDC 块（IAD + 控制接口 + 数据接口 + BDIR 端点）
 *   i   : CDC 序号（0..3）
 *   ep  : 端点号（1/2/3/5）
 *   str : 接口字符串描述符索引 */
#define CDC_BLOCK(i, ep, str) \
	0x08, 0x0B, (uint8_t)(2*(i)), 0x02, 0x02, 0x02, 0x01, (str), \
	0x09, 0x04, (uint8_t)(2*(i)), 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, \
	0x05, 0x24, 0x00, 0x10, 0x01, \
	0x05, 0x24, 0x01, 0x00, 0x01, \
	0x04, 0x24, 0x02, 0x02, \
	0x05, 0x24, 0x06, (uint8_t)(2*(i)), (uint8_t)(2*(i)+1), \
	0x09, 0x04, (uint8_t)(2*(i)+1), 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00, \
	0x07, 0x05, (uint8_t)(0x80 | (ep)), 0x02, 0x40, 0x00, 0x00

static const uint8_t config_descriptor[] = {
	/* ===== Configuration Descriptor ===== */
	0x09, 0x02,
	(uint8_t)(9 + 52 * GW_CDC_COUNT + 45), 0x00,   /* wTotalLength */
	(uint8_t)(2 * GW_CDC_COUNT + 2),               /* bNumInterfaces */
	0x01,                   /* bConfigurationValue */
	0x00,                   /* iConfiguration */
	0xC0,                   /* bmAttributes */
	0x32,                   /* bMaxPower: 100mA */

	/* ===== CDC-A（终端）===== */
	CDC_BLOCK(0, 1, 4),
#if GW_CDC_COUNT >= 2
	/* ===== CDC-B（UART0）===== */
	CDC_BLOCK(1, 2, 5),
#endif
#if GW_CDC_COUNT >= 3
	/* ===== CDC-C（UART1）===== */
	CDC_BLOCK(2, 3, 6),
#endif
#if GW_CDC_COUNT >= 4
	/* ===== CDC-D（UART2）===== */
	CDC_BLOCK(3, 5, 7),
#endif

	/* ===== RNDIS ===== */
	0x08, 0x0B, (uint8_t)(2*GW_CDC_COUNT), 0x02, 0xE0, 0x01, 0x03, (uint8_t)(GW_CDC_COUNT + 4),   /* IAD: Wireless/RNDIS */
	/* Control interface 8 */
	/* Control interface = 2*GW_CDC_COUNT */
	0x09, 0x04, (uint8_t)(2*GW_CDC_COUNT), 0x00, 0x01, 0xE0, 0x01, 0x03, 0x00,
	/* RNDIS class descriptor (5 bytes) */
	0x05, 0x24, 0x00, 0x01, 0x00,
	/* Data interface = 2*GW_CDC_COUNT + 1 */
	0x09, 0x04, (uint8_t)(2*GW_CDC_COUNT+1), 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	/* EP7 OUT: bulk data */
	0x07, 0x05, 0x07, 0x02, 0x40, 0x00, 0x00,
	/* EP6 IN: bulk data */
	0x07, 0x05, 0x86, 0x02, 0x40, 0x00, 0x00,
};

/* 编译期断言：配置描述符长度必须与 wTotalLength 公式一致 */
typedef char _config_desc_len_check[
	(sizeof(config_descriptor) == (9 + 52 * GW_CDC_COUNT + 45)) ? 1 : -1
];

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
	.bLength = 4 + sizeof(FUSB_STR_ITF_D) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_D
};
static const struct usb_string_descriptor string8 = {
	.bLength = 4 + sizeof(FUSB_STR_ITF_RNDIS) - 2, .bDescriptorType = 3,
	.wString = FUSB_STR_ITF_RNDIS
};

/* -------------------------------------------------------------------------
 * 描述符索引表
 * ------------------------------------------------------------------------- */
const static struct descriptor_list_struct {
	uint32_t        lIndexValue;
	const uint8_t  *addr;
	uint16_t        length;   /* 配置描述符 >255 字节，需 uint16 */
} descriptor_list[] = {
	{0x00000100, device_descriptor, sizeof(device_descriptor)},
	{0x00000200, config_descriptor, sizeof(config_descriptor)},
	{0x00000300, (const uint8_t *)&string0, 4},
	{0x04090301, (const uint8_t *)&string1, string1.bLength},
	{0x04090302, (const uint8_t *)&string2, string2.bLength},
	{0x04090303, (const uint8_t *)&string3, string3.bLength},
	/* CDC 接口字符串：索引 4..(3+GW_CDC_COUNT) */
	{0x04090304, (const uint8_t *)&string4, string4.bLength},
#if GW_CDC_COUNT >= 2
	{0x04090305, (const uint8_t *)&string5, string5.bLength},
#endif
#if GW_CDC_COUNT >= 3
	{0x04090306, (const uint8_t *)&string6, string6.bLength},
#endif
#if GW_CDC_COUNT >= 4
	{0x04090307, (const uint8_t *)&string7, string7.bLength},
#endif
	/* RNDIS 接口字符串：索引 4+GW_CDC_COUNT */
	{0x04090300 | (4 + GW_CDC_COUNT), (const uint8_t *)&string8, string8.bLength},
};

#define DESCRIPTOR_LIST_ENTRIES ((sizeof(descriptor_list)) / (sizeof(struct descriptor_list_struct)))

#endif /* _USB_CONFIG_H */
