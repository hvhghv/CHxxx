/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_profile.c
 * Description        : BLE 透传 GATT 服务实现
 *
 * 服务结构（Service UUID 0xFFF0）：
 *   ┌─ Primary Service Declaration (0xFFF0)
 *   ├─ Char RX Declaration  (0xFFF1, Write | WriteWithoutResponse)
 *   │    └─ RX Value  ← 浏览器/手机写入
 *   ├─ Char TX Declaration  (0xFFF2, Notify)
 *   │    ├─ TX Value  → 通知给浏览器/手机
 *   │    └─ CCCD      ← 客户端写 0x0001 使能通知
 *******************************************************************************/

#include "ble_profile.h"
#include <string.h>

/* ===========================================================================
 * UUID 定义
 * =========================================================================== */
static const uint8_t servUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(BLE_SERV_UUID), HI_UINT16(BLE_SERV_UUID)
};
static const uint8_t charRXUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(BLE_CHAR_RX_UUID), HI_UINT16(BLE_CHAR_RX_UUID)
};
static const uint8_t charTXUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(BLE_CHAR_TX_UUID), HI_UINT16(BLE_CHAR_TX_UUID)
};

static const gattAttrType_t servDecl = { ATT_BT_UUID_SIZE, servUUID };

/* ===========================================================================
 * 属性变量
 * =========================================================================== */
static uint8_t rxProps = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t rxValue[BLE_MAX_LEN] = { 0 };

static uint8_t txProps = GATT_PROP_NOTIFY;
static uint8_t txValue[BLE_MAX_LEN] = { 0 };
static gattCharCfg_t txCCCD[1];

/* 属性表索引（供 notify 取 handle） */
#define ATTR_IDX_TX_VALUE   4

static gattAttribute_t attrTbl[] = {
	/* 0: Primary Service */
	{ { ATT_BT_UUID_SIZE, primaryServiceUUID }, GATT_PERMIT_READ, 0, (uint8_t *)&servDecl },
	/* 1: RX 特征声明 */
	{ { ATT_BT_UUID_SIZE, characterUUID }, GATT_PERMIT_READ, 0, &rxProps },
	/* 2: RX 特征值 */
	{ { ATT_BT_UUID_SIZE, charRXUUID }, GATT_PERMIT_WRITE, 0, rxValue },
	/* 3: TX 特征声明 */
	{ { ATT_BT_UUID_SIZE, characterUUID }, GATT_PERMIT_READ, 0, &txProps },
	/* 4: TX 特征值 */
	{ { ATT_BT_UUID_SIZE, charTXUUID }, 0, 0, txValue },
	/* 5: TX CCCD */
	{ { ATT_BT_UUID_SIZE, clientCharCfgUUID },
	  GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)txCCCD },
};

/* ===========================================================================
 * 回调
 * =========================================================================== */
static ble_rx_cb_t rx_cb = NULL;

void ble_profile_set_rx_cb(ble_rx_cb_t cb)
{
	rx_cb = cb;
}

/* ===========================================================================
 * 读回调
 * =========================================================================== */
static bStatus_t readCB(uint16_t connHandle, gattAttribute_t *pAttr,
                        uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                        uint16_t maxLen, uint8_t method)
{
	(void)connHandle; (void)pAttr; (void)pValue; (void)pLen;
	(void)offset; (void)maxLen; (void)method;
	return ATT_ERR_READ_NOT_PERMITTED;
}

/* ===========================================================================
 * 写回调
 * =========================================================================== */
static bStatus_t writeCB(uint16_t connHandle, gattAttribute_t *pAttr,
                         uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method)
{
	(void)method;

	/* CCCD 写入（通知使能） */
	if (pAttr->type.len == ATT_BT_UUID_SIZE &&
	    pAttr->type.uuid[0] == LO_UINT16(GATT_CLIENT_CHAR_CFG_UUID) &&
	    pAttr->type.uuid[1] == HI_UINT16(GATT_CLIENT_CHAR_CFG_UUID)) {
		uint16_t value = BUILD_UINT16(pValue[0], pValue[1]);
		return GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
		                                      offset, value);
	}

	/* RX 特征写入 → 回调 */
	if (pAttr->type.len == ATT_BT_UUID_SIZE &&
	    pAttr->type.uuid[0] == LO_UINT16(BLE_CHAR_RX_UUID) &&
	    pAttr->type.uuid[1] == HI_UINT16(BLE_CHAR_RX_UUID)) {
		if (offset + len > BLE_MAX_LEN) return ATT_ERR_INVALID_VALUE_SIZE;
		if (rx_cb && len > 0) rx_cb(pValue, len);
		return SUCCESS;
	}

	return ATT_ERR_ATTR_NOT_FOUND;
}

static gattServiceCBs_t servCBs = { readCB, writeCB, NULL };

/* ===========================================================================
 * 初始化
 * =========================================================================== */
bStatus_t ble_profile_init(void)
{
	/* 初始化 CCCD（默认不使能通知） */
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, txCCCD);

	/* 注册服务 */
	return GATTServApp_RegisterService(attrTbl, GATT_NUM_ATTRS(attrTbl),
	                                   GATT_MAX_ENCRYPT_KEY_SIZE, &servCBs);
}

/* ===========================================================================
 * 通知
 * =========================================================================== */
int ble_profile_notify_enabled(void)
{
	uint16_t cfg = GATTServApp_ReadCharCfg(0, txCCCD);
	return (cfg & GATT_CLIENT_CFG_NOTIFY) ? 1 : 0;
}

bStatus_t ble_profile_notify(const uint8_t *data, uint16_t len)
{
	attHandleValueNoti_t noti;

	if (!data || len == 0 || len > BLE_MAX_LEN) return FAILURE;

	memcpy(txValue, data, len);

	noti.len = len;
	noti.pValue = txValue;
	noti.handle = attrTbl[ATTR_IDX_TX_VALUE].handle;

	return GATT_Notification(0, &noti, FALSE);
}
