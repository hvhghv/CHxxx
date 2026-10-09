/********************************** (C) COPYRIGHT *******************************
 * File Name          : bleapp.c
 * Description        : 网关 BLE 应用实现
 *
 * 单角色分时切换设计：
 *   - 默认 Peripheral（从机）：广播 + GATT 透传服务
 *   - 指令切换到 Central（主机）：停广播 → 断开 → 扫描 → 连接
 *   - 切回 Peripheral：停扫描/断开 → 恢复广播
 *   - 两种角色共用同一个 TMOS 任务，事件回调区分
 *******************************************************************************/

#include "ch32fun.h"
#include "bleapp.h"
#include <string.h>

/* BLE 库头（ble_config.h 会引入 CH59xBLE_LIB.h） */
#include "ble_config.h"

/* BLE HAL（由 sdk/CH592_DEV/ble/hal 提供） */
void CH59x_BLEInit(void);
void HAL_Init(void);

/* ===========================================================================
 * 状态
 * =========================================================================== */
static tmosTaskID ble_task_id;
static uint16_t    ble_conn_handle = GAP_CONNHANDLE_INIT;
static volatile int ble_connected = 0;
static volatile int ble_advertising = 0;
static volatile int ble_scanning = 0;
static volatile int ble_initialized = 0;
static bleapp_role_t ble_role = BLE_ROLE_PERIPHERAL;

static bleapp_rx_cb_t rx_cb = NULL;

void bleapp_set_rx_cb(bleapp_rx_cb_t cb) { rx_cb = cb; }
int  bleapp_is_connected(void)   { return ble_connected; }
int  bleapp_is_advertising(void) { return ble_advertising; }
int  bleapp_is_scanning(void)    { return ble_scanning; }
bleapp_role_t bleapp_role(void)  { return ble_role; }

const char *bleapp_role_str(void)
{
#if !BLE_ENABLE_CENTRAL
	return "peripheral";
#elif !BLE_ENABLE_PERIPH
	return "central";
#else
	return (ble_role == BLE_ROLE_CENTRAL) ? "central" : "peripheral";
#endif
}

const char *bleapp_state_str(void)
{
	if (ble_connected) return "connected";
	if (ble_advertising) return "advertising";
	if (ble_scanning) return "scanning";
	return "idle";
}

/* ===========================================================================
 * GATT 透传服务（0xFFF0）
 * =========================================================================== */
#define BLE_SERV_UUID   0xFFF0
#define BLE_CHAR_RX_UUID 0xFFF1
#define BLE_CHAR_TX_UUID 0xFFF2
#define BLE_MAX_LEN     20

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

static uint8_t rxProps = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t rxValue[BLE_MAX_LEN] = { 0 };

static uint8_t txProps = GATT_PROP_NOTIFY;
static uint8_t txValue[BLE_MAX_LEN] = { 0 };
static gattCharCfg_t txCCCD[1];

static gattAttribute_t attrTbl[] = {
	{ { ATT_BT_UUID_SIZE, primaryServiceUUID }, GATT_PERMIT_READ, 0, (uint8_t *)&servDecl },
	{ { ATT_BT_UUID_SIZE, characterUUID }, GATT_PERMIT_READ, 0, &rxProps },
	{ { ATT_BT_UUID_SIZE, charRXUUID }, GATT_PERMIT_WRITE, 0, rxValue },
	{ { ATT_BT_UUID_SIZE, characterUUID }, GATT_PERMIT_READ, 0, &txProps },
	{ { ATT_BT_UUID_SIZE, charTXUUID }, 0, 0, txValue },
	{ { ATT_BT_UUID_SIZE, clientCharCfgUUID }, GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, (uint8_t *)txCCCD },
};

static bStatus_t rxReadCB(uint16_t connHandle, gattAttribute_t *pAttr,
                          uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                          uint16_t maxLen, uint8_t method)
{
	(void)connHandle; (void)pAttr; (void)pValue; (void)pLen;
	(void)offset; (void)maxLen; (void)method;
	return ATT_ERR_READ_NOT_PERMITTED;
}

static bStatus_t rxWriteCB(uint16_t connHandle, gattAttribute_t *pAttr,
                           uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method)
{
	(void)method;

	/* CCCD 写入 */
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

static gattServiceCBs_t servCBs = { rxReadCB, rxWriteCB, NULL };

/* ===========================================================================
 * 动态 GATT 服务（owner 注册）
 * ===========================================================================
 * 每个 owner 一个服务，含若干特征。attrTbl 与 UUID 存储必须常驻
 * （协议栈持有指针），故用静态数组池。
 * ------------------------------------------------------------------------- */
#define BLEAPP_MAX_OWNERS   2
#define BLEAPP_MAX_FEATS    6
#define BLEAPP_MTU_DATA     20      /* 单特征 value 缓冲（对应默认 ATT_MTU 23） */

typedef struct {
	int      used;
	int      feat_count;

	/* 协议栈持有指针的常驻存储 */
	uint8_t  serv_uuid_bytes[2];
	uint8_t  feat_uuid_bytes[BLEAPP_MAX_FEATS][2];   /* 仅 16bit UUID */
	uint8_t  char_props[BLEAPP_MAX_FEATS];
	uint8_t  feat_value[BLEAPP_MAX_FEATS][BLEAPP_MTU_DATA];  /* 特征值（写入保存） */
	uint8_t  tx_buf[BLEAPP_MTU_DATA];                /* 通知发送独立缓冲（避免与写入冲突） */
	gattCharCfg_t cccd[BLEAPP_MAX_FEATS];
	gattAttribute_t attr_tbl[1 + BLEAPP_MAX_FEATS * 3];  /* serv + (decl,value,cccd)*N */
	int      attr_count;
	uint16_t serv_handle;
	int      registered;
} bleapp_owner_t;

static bleapp_owner_t owners[BLEAPP_MAX_OWNERS];

static bleapp_evt_cb_t evt_cb = NULL;
void bleapp_set_evt_cb(bleapp_evt_cb_t cb) { evt_cb = cb; }

static bleapp_link_cb_t link_cb = NULL;
void bleapp_set_link_cb(bleapp_link_cb_t cb) { link_cb = cb; }

static bleapp_cccd_cb_t cccd_cb = NULL;
void bleapp_set_cccd_cb(bleapp_cccd_cb_t cb) { cccd_cb = cb; }

/* 动态服务的读写回调：定位 owner/feat，回调事件 */
static bStatus_t dynReadCB(uint16_t connHandle, gattAttribute_t *pAttr,
                           uint8_t *pValue, uint16_t *pLen, uint16_t offset,
                           uint16_t maxLen, uint8_t method)
{
	(void)connHandle; (void)pValue; (void)offset; (void)maxLen; (void)method;
	/* 只支持读特征值本身 */
	*pLen = 0;
	(void)pAttr;
	return ATT_ERR_READ_NOT_PERMITTED;
}

static bStatus_t dynWriteCB(uint16_t connHandle, gattAttribute_t *pAttr,
                            uint8_t *pValue, uint16_t len, uint16_t offset, uint8_t method)
{
	(void)method;

	/* CCCD 写入 */
	if (pAttr->type.len == ATT_BT_UUID_SIZE &&
	    pAttr->type.uuid[0] == LO_UINT16(GATT_CLIENT_CHAR_CFG_UUID) &&
	    pAttr->type.uuid[1] == HI_UINT16(GATT_CLIENT_CHAR_CFG_UUID)) {
		uint16_t value = BUILD_UINT16(pValue[0], pValue[1]);
		bStatus_t st = GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
		                                              offset, value);
		if (cccd_cb) {
			for (int o = 0; o < BLEAPP_MAX_OWNERS; o++) {
				bleapp_owner_t *ow = &owners[o];
				if (!ow->used) continue;
				for (int f = 0; f < ow->feat_count; f++) {
					if (pAttr->pValue == (uint8_t *)&ow->cccd[f]) {
						cccd_cb(o, f, connHandle,
						        (value & GATT_CLIENT_CFG_NOTIFY) ? 1 : 0);
					}
				}
			}
		}
		return st;
	}

	/* 特征值写入 → 找到 owner/feat */
	for (int o = 0; o < BLEAPP_MAX_OWNERS; o++) {
		bleapp_owner_t *ow = &owners[o];
		if (!ow->used) continue;
		for (int f = 0; f < ow->feat_count; f++) {
			if (pAttr->pValue == ow->feat_value[f]) {
				/* 保存写入值（供后续读取/回显），限长防溢出 */
				uint16_t copy = len;
				if (offset >= BLEAPP_MTU_DATA) return ATT_ERR_INVALID_OFFSET;
				if (offset + copy > BLEAPP_MTU_DATA) copy = BLEAPP_MTU_DATA - offset;
				memcpy(ow->feat_value[f] + offset, pValue, copy);
				if (evt_cb) evt_cb(o, f, connHandle, pValue, len);
				return SUCCESS;
			}
		}
	}
	return ATT_ERR_ATTR_NOT_FOUND;
}

static gattServiceCBs_t dynServCBs = { dynReadCB, dynWriteCB, NULL };

/* 注册服务：构建 attrTbl 并调用协议栈 */
int bleapp_register_service(int owner_id, const bleapp_feat_t *feats, int count)
{
	if (owner_id < 0 || owner_id >= BLEAPP_MAX_OWNERS) return -1;
	if (count <= 0 || count > BLEAPP_MAX_FEATS) return -1;
	if (owners[owner_id].used) return -1;   /* 已占用 */

	bleapp_owner_t *ow = &owners[owner_id];
	memset(ow, 0, sizeof(*ow));

	/* 服务 UUID：用 owner_id 派生（0xFF00 + owner_id） */
	uint16_t serv_uuid = (uint16_t)(0xFF00 + owner_id);
	ow->serv_uuid_bytes[0] = LO_UINT16(serv_uuid);
	ow->serv_uuid_bytes[1] = HI_UINT16(serv_uuid);

	ow->feat_count = count;
	int ai = 0;

	/* 服务声明 */
	ow->attr_tbl[ai].type.len = ATT_BT_UUID_SIZE;
	ow->attr_tbl[ai].type.uuid = primaryServiceUUID;
	ow->attr_tbl[ai].permissions = GATT_PERMIT_READ;
	ow->attr_tbl[ai].handle = 0;
	ow->attr_tbl[ai].pValue = ow->serv_uuid_bytes;
	ai++;

	for (int f = 0; f < count; f++) {
		/* 特征声明 */
		ow->char_props[f] = 0;
		if (feats[f].props & 0x01) ow->char_props[f] |= GATT_PROP_READ;
		if (feats[f].props & 0x02) ow->char_props[f] |= GATT_PROP_WRITE;
		if (feats[f].props & 0x04) ow->char_props[f] |= GATT_PROP_WRITE_NO_RSP;
		if (feats[f].props & 0x08) ow->char_props[f] |= GATT_PROP_NOTIFY;

		ow->attr_tbl[ai].type.len = ATT_BT_UUID_SIZE;
		ow->attr_tbl[ai].type.uuid = characterUUID;
		ow->attr_tbl[ai].permissions = GATT_PERMIT_READ;
		ow->attr_tbl[ai].handle = 0;
		ow->attr_tbl[ai].pValue = &ow->char_props[f];
		ai++;

		/* 特征值 */
		ow->feat_uuid_bytes[f][0] = LO_UINT16(feats[f].uuid16);
		ow->feat_uuid_bytes[f][1] = HI_UINT16(feats[f].uuid16);
		ow->attr_tbl[ai].type.len = ATT_BT_UUID_SIZE;
		ow->attr_tbl[ai].type.uuid = ow->feat_uuid_bytes[f];
		ow->attr_tbl[ai].permissions = 0;
		if (feats[f].props & 0x01) ow->attr_tbl[ai].permissions |= GATT_PERMIT_READ;
		if (feats[f].props & (0x02 | 0x04)) ow->attr_tbl[ai].permissions |= GATT_PERMIT_WRITE;
		ow->attr_tbl[ai].handle = 0;
		ow->attr_tbl[ai].pValue = ow->feat_value[f];
		ai++;

		/* CCCD（仅 Notify 特征需要） */
		if (feats[f].props & 0x08) {
			GATTServApp_InitCharCfg(INVALID_CONNHANDLE, &ow->cccd[f]);
			ow->attr_tbl[ai].type.len = ATT_BT_UUID_SIZE;
			ow->attr_tbl[ai].type.uuid = clientCharCfgUUID;
			ow->attr_tbl[ai].permissions = GATT_PERMIT_READ | GATT_PERMIT_WRITE;
			ow->attr_tbl[ai].handle = 0;
			ow->attr_tbl[ai].pValue = (uint8_t *)&ow->cccd[f];
			ai++;
		}
	}

	ow->attr_count = ai;
	ow->used = 1;

	bStatus_t st = GATTServApp_RegisterService(ow->attr_tbl, ai,
	                                           GATT_MAX_ENCRYPT_KEY_SIZE, &dynServCBs);
	if (st != SUCCESS) {
		ow->used = 0;
		return -1;
	}
	ow->serv_handle = ow->attr_tbl[0].handle;
	ow->registered = 1;
	return 0;
}

int bleapp_unregister_service(int owner_id)
{
	if (owner_id < 0 || owner_id >= BLEAPP_MAX_OWNERS) return -1;
	bleapp_owner_t *ow = &owners[owner_id];
	if (!ow->used) return -1;

	if (ow->registered) {
		gattAttribute_t *pAttrs = NULL;
		GATTServApp_DeregisterService(ow->serv_handle, &pAttrs);
		ow->registered = 0;
	}
	ow->used = 0;
	return 0;
}

int bleapp_send(int owner_id, int feat_idx, uint16_t conn_handle,
                const uint8_t *data, uint16_t len)
{
	if (owner_id < 0 || owner_id >= BLEAPP_MAX_OWNERS) return -1;
	bleapp_owner_t *ow = &owners[owner_id];
	if (!ow->used || feat_idx < 0 || feat_idx >= ow->feat_count) return -1;
	if (!ble_connected) return -1;
	if (len > BLEAPP_MTU_DATA) return -1;

	uint16_t cfg = GATTServApp_ReadCharCfg(conn_handle, &ow->cccd[feat_idx]);
	if (!(cfg & GATT_CLIENT_CFG_NOTIFY)) return -1;

	/* 找到该特征 value 属性的 handle */
	uint16_t hdl = 0;
	for (int i = 0; i < ow->attr_count; i++) {
		if (ow->attr_tbl[i].pValue == ow->feat_value[feat_idx]) {
			hdl = ow->attr_tbl[i].handle;
			break;
		}
	}
	if (!hdl) return -1;

	/* 用独立发送缓冲，避免与特征写入（dynWriteCB）冲突 */
	memcpy(ow->tx_buf, data, len);
	attHandleValueNoti_t noti;
	noti.len = len;
	noti.pValue = ow->tx_buf;
	noti.handle = hdl;
	return (GATT_Notification(conn_handle, &noti, FALSE) == SUCCESS) ? 0 : -1;
}

int bleapp_is_notify_enabled(int owner_id, int feat_idx, uint16_t conn_handle)
{
	if (owner_id < 0 || owner_id >= BLEAPP_MAX_OWNERS) return 0;
	bleapp_owner_t *ow = &owners[owner_id];
	if (!ow->used || feat_idx < 0 || feat_idx >= ow->feat_count) return 0;
	uint16_t cfg = GATTServApp_ReadCharCfg(conn_handle, &ow->cccd[feat_idx]);
	return (cfg & GATT_CLIENT_CFG_NOTIFY) ? 1 : 0;
}

/* ===========================================================================
 * 扫描结果缓存（仅主机/双模式）
 * =========================================================================== */
#if BLE_ENABLE_CENTRAL
#define BLE_SCAN_MAX  4
static bleapp_dev_t scan_list[BLE_SCAN_MAX];
static volatile int scan_count = 0;

int bleapp_scan_count(void) { return scan_count; }

const bleapp_dev_t *bleapp_scan_get(int idx)
{
	if (idx < 0 || idx >= scan_count) return NULL;
	return &scan_list[idx];
}

/* 从广播数据中提取完整/短名 */
static void extract_name(const uint8_t *data, uint8_t len, char *out, int outsz)
{
	out[0] = 0;
	uint8_t i = 0;
	while (i + 1 < len) {
		uint8_t flen = data[i];
		if (flen == 0) break;
		if (i + 1 + flen > len) break;
		uint8_t type = data[i + 1];
		if ((type == GAP_ADTYPE_LOCAL_NAME_COMPLETE ||
		     type == GAP_ADTYPE_LOCAL_NAME_SHORT) && flen >= 2) {
			int n = flen - 1;
			if (n > outsz - 1) n = outsz - 1;
			memcpy(out, &data[i + 2], n);
			out[n] = 0;
			return;
		}
		i += flen + 1;
	}
}
#else  /* !BLE_ENABLE_CENTRAL：仅从机模式，提供空实现 */
int bleapp_scan_count(void) { return 0; }
const bleapp_dev_t *bleapp_scan_get(int idx) { (void)idx; return NULL; }
#endif /* BLE_ENABLE_CENTRAL */

/* ===========================================================================
 * 从机（Peripheral）回调（仅从机/双模式）
 * =========================================================================== */
#if BLE_ENABLE_PERIPH
static void ble_state_cb(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
	switch (newState) {
	case GAPROLE_STARTED:    ble_advertising = 0; break;
	case GAPROLE_ADVERTISING: ble_advertising = 1; break;
	case GAPROLE_CONNECTED:
		ble_conn_handle = pEvent->linkCmpl.connectionHandle;
		ble_connected = 1;
		ble_advertising = 0;
		if (link_cb) link_cb(1, ble_conn_handle);
		break;
	case GAPROLE_WAITING:
		if (ble_connected && link_cb) link_cb(0, ble_conn_handle);
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
		break;
	default: break;
	}
}

static gapRolesCBs_t ble_role_cb = { ble_state_cb, NULL, NULL };
#endif /* BLE_ENABLE_PERIPH */

/* ===========================================================================
 * 主机（Central）回调（仅主机/双模式）
 * =========================================================================== */
#if BLE_ENABLE_CENTRAL
static void ble_central_cb(gapRoleEvent_t *pEvent)
{
	switch (pEvent->gap.opcode) {
	case GAP_DEVICE_INFO_EVENT: {
		gapDeviceInfoEvent_t *info = &pEvent->deviceInfo;
		/* 去重：同地址已存在则更新 */
		int found = -1;
		for (int i = 0; i < scan_count; i++) {
			if (memcmp(scan_list[i].addr, info->addr, 6) == 0) { found = i; break; }
		}
		if (found < 0 && scan_count < BLE_SCAN_MAX) {
			found = scan_count++;
			memcpy(scan_list[found].addr, info->addr, 6);
			scan_list[found].addr_type = info->addrType;
			scan_list[found].name[0] = 0;
		}
		if (found >= 0) {
			scan_list[found].rssi = info->rssi;
			if (info->dataLen && info->pEvtData) {
				char nm[20];
				extract_name(info->pEvtData, info->dataLen, nm, sizeof(nm));
				if (nm[0]) memcpy(scan_list[found].name, nm, sizeof(nm));
			}
		}
		break;
	}
	case GAP_DEVICE_DISCOVERY_EVENT:
		ble_scanning = 0;
		break;
	case GAP_LINK_ESTABLISHED_EVENT:
		ble_conn_handle = pEvent->linkCmpl.connectionHandle;
		ble_connected = 1;
		ble_scanning = 0;
		if (link_cb) link_cb(1, ble_conn_handle);
		break;
	case GAP_LINK_TERMINATED_EVENT:
		if (ble_connected && link_cb) link_cb(0, ble_conn_handle);
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
		break;
	default: break;
	}
}

static gapCentralRoleCB_t ble_central_role_cb = { NULL, ble_central_cb, NULL };
#endif /* BLE_ENABLE_CENTRAL */

/* ===========================================================================
 * BLE 任务事件
 * =========================================================================== */
#define BLE_START_DEVICE_EVT   0x0001

static uint16_t ble_process_event(uint8_t task_id, uint16_t events)
{
	if (events & SYS_EVENT_MSG) {
		uint8_t *pMsg = tmos_msg_receive(task_id);
		if (pMsg) tmos_msg_deallocate(pMsg);
		return (events ^ SYS_EVENT_MSG);
	}
	if (events & BLE_START_DEVICE_EVT) {
#if BLE_ENABLE_CENTRAL && BLE_ENABLE_PERIPH
		if (ble_role == BLE_ROLE_CENTRAL)
			GAPRole_CentralStartDevice(task_id, NULL, &ble_central_role_cb);
		else
			GAPRole_PeripheralStartDevice(task_id, NULL, &ble_role_cb);
#elif BLE_ENABLE_CENTRAL
		GAPRole_CentralStartDevice(task_id, NULL, &ble_central_role_cb);
#else
		GAPRole_PeripheralStartDevice(task_id, NULL, &ble_role_cb);
#endif
		return (events ^ BLE_START_DEVICE_EVT);
	}
	return 0;
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
void bleapp_init(void)
{
	CH59x_BLEInit();
	HAL_Init();

#if BLE_ENABLE_PERIPH
	GAPRole_PeripheralInit();
#endif
#if BLE_ENABLE_CENTRAL
	GAPRole_CentralInit();
#endif
	ble_task_id = TMOS_ProcessEventRegister(ble_process_event);

	/* GAP / GATT 基础服务 */
	GGS_AddService(GATT_ALL_SERVICES);
	GATTServApp_AddService(GATT_ALL_SERVICES);

	/* 设备名 */
	{
		uint8_t devName[] = "CH591-Gateway";
		GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(devName) - 1, devName);
	}

	/* 透传服务 */
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, txCCCD);
	GATTServApp_RegisterService(attrTbl, GATT_NUM_ATTRS(attrTbl),
	                            GATT_MAX_ENCRYPT_KEY_SIZE, &servCBs);

#if BLE_ENABLE_PERIPH
	/* 广播间隔 100ms */
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, 160);
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, 160);

	/* 广播数据 */
	static uint8_t advData[] = {
		0x02, 0x01, 0x06,               /* Flags */
		0x03, 0x03, 0xF0, 0xFF,         /* UUID 0xFFF0 */
	};
	static uint8_t scanRspData[] = {
		0x0F, 0x09, 'C', 'H', '5', '9', '1', '-', 'G', 'a', 't', 'e', 'w', 'a', 'y',
	};
	uint8_t advEnable = 1;
	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
	GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advData), advData);
	GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);

	ble_role = BLE_ROLE_PERIPHERAL;
#else
	ble_role = BLE_ROLE_CENTRAL;
#endif
	tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);

	ble_initialized = 1;
}

void bleapp_process(void)
{
	if (ble_initialized) {
		TMOS_SystemProcess();
	}
}

/* ===========================================================================
 * 通知
 * =========================================================================== */
int bleapp_notify(const uint8_t *data, uint16_t len)
{
	if (!ble_connected || len == 0 || len > BLE_MAX_LEN) return -1;

	uint16_t cfg = GATTServApp_ReadCharCfg(ble_conn_handle, txCCCD);
	if (!(cfg & GATT_CLIENT_CFG_NOTIFY)) return -1;

	memcpy(txValue, data, len);
	attHandleValueNoti_t noti;
	noti.len = len;
	noti.pValue = txValue;
	noti.handle = attrTbl[4].handle;
	return (GATT_Notification(ble_conn_handle, &noti, FALSE) == SUCCESS) ? 0 : -1;
}

/* ===========================================================================
 * 角色切换（单角色分时）
 * =========================================================================== */
int bleapp_disconnect(void)
{
	if (!ble_connected) return -1;
	return (GAPRole_TerminateLink(ble_conn_handle) == SUCCESS) ? 0 : -1;
}

#if BLE_ENABLE_CENTRAL
int bleapp_central_scan(void)
{
	if (ble_role != BLE_ROLE_CENTRAL) return -1;
	scan_count = 0;
	ble_scanning = 1;
	return (GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE) == SUCCESS) ? 0 : -1;
}

int bleapp_central_stop_scan(void)
{
	if (ble_role != BLE_ROLE_CENTRAL) return -1;
	GAPRole_CentralCancelDiscovery();
	ble_scanning = 0;
	return 0;
}

int bleapp_central_connect(int idx)
{
	if (ble_role != BLE_ROLE_CENTRAL) return -1;
	if (idx < 0 || idx >= scan_count) return -1;

	/* 连接前停止扫描 */
	GAPRole_CentralCancelDiscovery();
	ble_scanning = 0;

	const bleapp_dev_t *d = &scan_list[idx];
	return (GAPRole_CentralEstablishLink(FALSE, FALSE, d->addr_type,
	                                     (uint8_t *)d->addr) == SUCCESS) ? 0 : -1;
}
#else  /* !BLE_ENABLE_CENTRAL：仅从机模式，提供空实现 */
int bleapp_central_scan(void)      { return -1; }
int bleapp_central_stop_scan(void) { return -1; }
int bleapp_central_connect(int idx) { (void)idx; return -1; }
#endif /* BLE_ENABLE_CENTRAL */

#if BLE_ENABLE_CENTRAL && BLE_ENABLE_PERIPH
int bleapp_switch_central(void)
{
	if (ble_role == BLE_ROLE_CENTRAL) return 0;

	/* 1. 停广播 */
	uint8_t advEnable = 0;
	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
	ble_advertising = 0;

	/* 2. 断开现有连接（若有） */
	if (ble_connected) {
		GAPRole_TerminateLink(ble_conn_handle);
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
	}

	/* 3. 初始化主机角色 */
	ble_role = BLE_ROLE_CENTRAL;
	GAPRole_CentralInit();
	tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);

	/* 4. 启动扫描 */
	scan_count = 0;
	ble_scanning = 1;
	GAPRole_CentralStartDiscovery(DEVDISC_MODE_ALL, TRUE, FALSE);
	return 0;
}

int bleapp_switch_peripheral(void)
{
	if (ble_role == BLE_ROLE_PERIPHERAL) return 0;

	/* 1. 停扫描 */
	if (ble_scanning) {
		GAPRole_CentralCancelDiscovery();
		ble_scanning = 0;
	}

	/* 2. 断开主机连接（若有） */
	if (ble_connected) {
		GAPRole_TerminateLink(ble_conn_handle);
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
	}

	/* 3. 切回从机角色 */
	ble_role = BLE_ROLE_PERIPHERAL;
	GAPRole_PeripheralInit();
	tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);

	/* 4. 重新使能广播 */
	uint8_t advEnable = 1;
	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
	return 0;
}
#else
/* 单角色模式：切换为 no-op */
int bleapp_switch_central(void)    { return (ble_role == BLE_ROLE_CENTRAL) ? 0 : -1; }
int bleapp_switch_peripheral(void) { return (ble_role == BLE_ROLE_PERIPHERAL) ? 0 : -1; }
#endif
