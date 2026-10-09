/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_ble_profile.c
 * Description        : USB ↔ BLE 透传 GATT 服务实现
 *
 * 服务结构（Service UUID 0xFFF0）：
 *   ┌─ Primary Service Declaration (0xFFF0)
 *   ├─ Char RX Declaration  (0xFFF1, Write | WriteWithoutResponse)
 *   │    └─ RX Value  ← 浏览器写入，转发到 USB
 *   ├─ Char TX Declaration  (0xFFF2, Notify)
 *   │    ├─ TX Value  → USB 数据通知给浏览器
 *   │    └─ CCCD      ← 浏览器写 0x0001 使能通知
 *******************************************************************************/

#include "usb_ble_profile.h"
#include <string.h>

/* ===========================================================================
 * UUID 定义
 * =========================================================================== */
static const uint8_t servUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(USB_BLE_SERV_UUID), HI_UINT16(USB_BLE_SERV_UUID)
};
static const uint8_t charRXUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(USB_BLE_CHAR_RX_UUID), HI_UINT16(USB_BLE_CHAR_RX_UUID)
};
static const uint8_t charTXUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(USB_BLE_CHAR_TX_UUID), HI_UINT16(USB_BLE_CHAR_TX_UUID)
};

/* ===========================================================================
 * 属性变量
 * =========================================================================== */
static const gattAttrType_t servDecl = { ATT_BT_UUID_SIZE, servUUID };

/* --- RX 特征（浏览器 → USB）--- */
static uint8_t rxProps = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t rxValue[USB_BLE_MAX_LEN] = { 0 };

/* --- TX 特征（USB → 浏览器）--- */
static uint8_t txProps = GATT_PROP_NOTIFY;
static uint8_t txValue[USB_BLE_MAX_LEN] = { 0 };
static gattCharCfg_t txCCCD[1];   /* 每个连接一个实例 */

/* ===========================================================================
 * 回调
 * =========================================================================== */
static usb_ble_rx_cb_t rx_cb = NULL;

void usb_ble_profile_set_rx_cb(usb_ble_rx_cb_t cb)
{
	rx_cb = cb;
}

/* ===========================================================================
 * 属性表
 * =========================================================================== */
static gattAttribute_t attrTbl[] = {
	/* Primary Service */
	{
		{ ATT_BT_UUID_SIZE, primaryServiceUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&servDecl
	},

	/* ---- RX 特征声明 ---- */
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		&rxProps
	},
	/* RX 特征值（可写） */
	{
		{ ATT_BT_UUID_SIZE, charRXUUID },
		GATT_PERMIT_WRITE,
		0,
		rxValue
	},

	/* ---- TX 特征声明 ---- */
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		&txProps
	},
	/* TX 特征值（通知） */
	{
		{ ATT_BT_UUID_SIZE, charTXUUID },
		0,
		0,
		txValue
	},
	/* TX CCCD（客户端配置） */
	{
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		(uint8_t *)txCCCD
	},
};

/* ===========================================================================
 * 读回调
 * =========================================================================== */
static bStatus_t rxReadCB(uint16_t connHandle, gattAttribute_t *pAttr,
                          uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                          uint16_t maxLen, uint8_t method)
{
	(void)connHandle; (void)pAttr; (void)pValue; (void)pLen;
	(void)offset; (void)maxLen; (void)method;
	return ATT_ERR_READ_NOT_PERMITTED;   /* RX 只写 */
}

/* ===========================================================================
 * 写回调
 * =========================================================================== */
static bStatus_t rxWriteCB(uint16_t connHandle, gattAttribute_t *pAttr,
                           uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method)
{
	(void)connHandle; (void)method;

	/* CCCD 写入（通知使能） */
	if (pAttr->type.len == ATT_BT_UUID_SIZE &&
	    pAttr->type.uuid[0] == LO_UINT16(GATT_CLIENT_CHAR_CFG_UUID) &&
	    pAttr->type.uuid[1] == HI_UINT16(GATT_CLIENT_CHAR_CFG_UUID)) {
		uint16_t value = BUILD_UINT16(pValue[0], pValue[1]);
		return GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
		                                      offset, value);
	}

	/* RX 特征值写入 → 转发到 USB */
	if (pAttr->type.len == ATT_BT_UUID_SIZE &&
	    pAttr->type.uuid[0] == LO_UINT16(USB_BLE_CHAR_RX_UUID) &&
	    pAttr->type.uuid[1] == HI_UINT16(USB_BLE_CHAR_RX_UUID)) {
		if (offset + len > USB_BLE_MAX_LEN) return ATT_ERR_INVALID_VALUE_SIZE;
		if (rx_cb && len > 0) {
			rx_cb(pValue, len);
		}
		return SUCCESS;
	}

	return ATT_ERR_ATTR_NOT_FOUND;
}

/* ===========================================================================
 * 服务回调表
 * =========================================================================== */
static gattServiceCBs_t servCBs = {
	rxReadCB,    /* 读 */
	rxWriteCB,   /* 写 */
	NULL         /* 授权 */
};

/* ===========================================================================
 * 初始化
 * =========================================================================== */
bStatus_t usb_ble_profile_init(void)
{
	bStatus_t status;

	/* 初始化 CCCD（每个连接默认不使能通知） */
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, txCCCD);

	/* 注册服务 */
	status = GATTServApp_RegisterService(attrTbl, GATT_NUM_ATTRS(attrTbl),
	                                     GATT_MAX_ENCRYPT_KEY_SIZE, &servCBs);
	return status;
}

/* ===========================================================================
 * 发送通知
 * =========================================================================== */
int usb_ble_profile_notify_enabled(void)
{
	/* 读取 CCCD 值（0 = 未使能，1 = 通知，2 = 指示） */
	uint16_t cfg = GATTServApp_ReadCharCfg(0, txCCCD);
	return (cfg & GATT_CLIENT_CFG_NOTIFY) ? 1 : 0;
}

bStatus_t usb_ble_profile_notify(const uint8_t *data, uint16_t len)
{
	attHandleValueNoti_t noti;

	if (len == 0 || len > USB_BLE_MAX_LEN) return FAILURE;
	if (!usb_ble_profile_notify_enabled()) return FAILURE;

	memcpy(txValue, data, len);

	noti.len = len;
	noti.pValue = txValue;

	/* TX 特征值的 handle：属性表索引 4
	 * [0]=Service [1]=RX Decl [2]=RX Value [3]=TX Decl [4]=TX Value [5]=TX CCCD */
	noti.handle = attrTbl[4].handle;

	return GATT_Notification(0, &noti, FALSE);
}
