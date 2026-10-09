/********************************** (C) COPYRIGHT *******************************
 * File Name          : blemgr.c
 * Description        : BLE 帧命令管理器实现
 *******************************************************************************/

#include "ch32fun.h"
#include "blemgr.h"
#include "ble_proto.h"
#include "bleapp.h"
#include "frame.h"
#include <string.h>

/* LWIP 提供 sys_now()（毫秒） */
extern uint32_t sys_now(void);

/* ---------------------------------------------------------------------------
 * 小端读写
 * ------------------------------------------------------------------------- */
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

/* ---------------------------------------------------------------------------
 * owner 注册表
 * ------------------------------------------------------------------------- */
typedef struct {
	int      used;
	int      ch;                  /* 注册时所用通道（事件回推目标） */
	uint8_t  link_mode;           /* EAGER / LAZY */
	int      feat_count;
	uint8_t  link_up_sent;        /* 当前 logical_id 是否已发 LINK_UP */
} blemgr_owner_t;

static blemgr_owner_t m_owners[BLE_MAX_OWNERS];

/* ---------------------------------------------------------------------------
 * 逻辑连接表
 * ------------------------------------------------------------------------- */
#define BLEMGR_MAX_LINKS  4
typedef struct {
	int      used;
	uint32_t logical_id;          /* 单调递增，永不复用 */
	uint16_t conn_handle;
	uint8_t  ack_pending;         /* LINK_DOWN 已发，等待 ACK */
	uint32_t ack_deadline;        /* 超时时刻（ms） */
} blemgr_link_t;

static blemgr_link_t m_links[BLEMGR_MAX_LINKS];
static uint32_t next_logical_id = 1;   /* 从 1 开始，0 保留为无效 */

/* 按 conn_handle 查找连接 */
static blemgr_link_t *link_by_conn(uint16_t conn_handle)
{
	for (int i = 0; i < BLEMGR_MAX_LINKS; i++)
		if (m_links[i].used && m_links[i].conn_handle == conn_handle)
			return &m_links[i];
	return NULL;
}

/* 分配新逻辑连接 */
static blemgr_link_t *link_alloc(uint16_t conn_handle)
{
	for (int i = 0; i < BLEMGR_MAX_LINKS; i++) {
		if (!m_links[i].used) {
			m_links[i].used = 1;
			m_links[i].logical_id = next_logical_id++;
			m_links[i].conn_handle = conn_handle;
			m_links[i].ack_pending = 0;
			return &m_links[i];
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------------------
 * 事件回推：向 owner 所在通道发送事件帧
 * ------------------------------------------------------------------------- */
static void push_evt(int owner_id, uint8_t evt_type, int feat_idx,
                     uint32_t logical_id, const uint8_t *data, uint16_t len)
{
	if (owner_id < 0 || owner_id >= BLE_MAX_OWNERS) return;
	blemgr_owner_t *ow = &m_owners[owner_id];
	if (!ow->used) return;

	/* 事件帧最大 = 6 字节头 + BLE 数据（单次 ATT Write ≤ 20B）= 26B，留余量取 64 */
	uint8_t buf[64];
	int n = 0;
	buf[n++] = (uint8_t)owner_id;

	if (evt_type == BLE_EVT_LINK_UP || evt_type == BLE_EVT_LINK_DOWN) {
		wr32(buf + n, logical_id); n += 4;
	} else {
		/* NOTIFY_EN/DIS, DATA_RX：owner + feat_idx + logical_id (+ data) */
		buf[n++] = (uint8_t)feat_idx;
		wr32(buf + n, logical_id); n += 4;
		if (evt_type == BLE_EVT_DATA_RX && data && len) {
			uint16_t copy = len;
			if (n + copy > (int)sizeof(buf)) copy = (uint16_t)(sizeof(buf) - n);
			memcpy(buf + n, data, copy);
			n += copy;
		}
	}

	framelink_send_resp((framelink_ch_t)ow->ch, evt_type, 0, buf, (uint16_t)n);
}

/* ---------------------------------------------------------------------------
 * bleapp 回调：连接建立/断开
 * ------------------------------------------------------------------------- */
static void on_link(int up, uint16_t conn_handle)
{
	if (up) {
		blemgr_link_t *lk = link_by_conn(conn_handle);
		if (!lk) lk = link_alloc(conn_handle);
		if (!lk) return;
		/* 新连接：所有 owner 的 link_up_sent 重置 */
		for (int o = 0; o < BLE_MAX_OWNERS; o++) {
			m_owners[o].link_up_sent = 0;
			/* EAGER：立即发 LINK_UP */
			if (m_owners[o].used && m_owners[o].link_mode == BLE_LINK_EAGER) {
				push_evt(o, BLE_EVT_LINK_UP, 0, lk->logical_id, NULL, 0);
				m_owners[o].link_up_sent = 1;
			}
		}
	} else {
		blemgr_link_t *lk = link_by_conn(conn_handle);
		if (!lk) return;
		/* 发 LINK_DOWN，等待 ACK */
		for (int o = 0; o < BLE_MAX_OWNERS; o++) {
			if (m_owners[o].used && m_owners[o].link_up_sent) {
				push_evt(o, BLE_EVT_LINK_DOWN, 0, lk->logical_id, NULL, 0);
			}
		}
		lk->ack_pending = 1;
		lk->ack_deadline = sys_now() + 5000;   /* 5 秒 */
	}
}

/* ---------------------------------------------------------------------------
 * bleapp 回调：CCCD 订阅变化
 * ------------------------------------------------------------------------- */
static void on_cccd(int owner_id, int feat_idx, uint16_t conn_handle, int en)
{
	blemgr_link_t *lk = link_by_conn(conn_handle);
	if (!lk) lk = link_alloc(conn_handle);
	if (!lk) return;
	push_evt(owner_id, en ? BLE_EVT_NOTIFY_EN : BLE_EVT_NOTIFY_DIS,
	         feat_idx, lk->logical_id, NULL, 0);
}

/* ---------------------------------------------------------------------------
 * bleapp 回调：收到手机写入
 * ------------------------------------------------------------------------- */
static void on_data(int owner_id, int feat_idx, uint16_t conn_handle,
                    const uint8_t *data, uint16_t len)
{
	blemgr_link_t *lk = link_by_conn(conn_handle);
	/* 连接未注册（异常）：补建逻辑连接，避免 lid=0 发给应用 */
	if (!lk) lk = link_alloc(conn_handle);
	if (!lk) return;   /* 连接表满，丢弃 */
	uint32_t lid = lk->logical_id;

	/* LAZY：首次写入才发 LINK_UP */
	if (owner_id >= 0 && owner_id < BLE_MAX_OWNERS) {
		blemgr_owner_t *ow = &m_owners[owner_id];
		if (ow->used && ow->link_mode == BLE_LINK_LAZY && !ow->link_up_sent) {
			push_evt(owner_id, BLE_EVT_LINK_UP, 0, lid, NULL, 0);
			ow->link_up_sent = 1;
		}
	}

	push_evt(owner_id, BLE_EVT_DATA_RX, feat_idx, lid, data, len);
}

/* ---------------------------------------------------------------------------
 * 帧命令：注册服务
 * ------------------------------------------------------------------------- */
static uint32_t cmd_reg_svc(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	const uint8_t *p = f->payload;
	uint16_t plen = f->payload_len;
	if (plen < 3 || max < 1) { resp[0] = BLE_EINVAL; return 1; }

	int owner_id = p[0];
	int count = p[1];
	uint8_t link_mode = p[2];
	if (owner_id < 0 || owner_id >= BLE_MAX_OWNERS ||
	    count <= 0 || count > BLE_MAX_FEATURES) {
		resp[0] = BLE_EINVAL; return 1;
	}

	/* 解析特征描述 */
	bleapp_feat_t feats[BLE_MAX_FEATURES];
	int off = 3;
	for (int i = 0; i < count; i++) {
		if (off + 3 > plen) { resp[0] = BLE_EINVAL; return 1; }
		uint16_t u16 = rd16(p + off);
		feats[i].uuid16 = u16;
		feats[i].props = p[off + 2];
		off += 3;
#if BLE_UUID128_SUPPORT
		if (u16 == 0) {
			/* 128bit UUID 扩展 */
			if (off + 16 > plen) { resp[0] = BLE_EINVAL; return 1; }
			memcpy(feats[i].uuid128, p + off, 16);
			off += 16;
		}
#else
		if (u16 == 0) { resp[0] = BLE_EINVAL; return 1; }   /* 不支持 128bit UUID */
#endif
	}

	/* 注册（UUID 独占由 bleapp 服务 UUID 派生保证） */
	if (bleapp_register_service(owner_id, feats, count) != 0) {
		resp[0] = BLE_EBUSY; return 1;
	}

	/* 记录 owner */
	m_owners[owner_id].used = 1;
	m_owners[owner_id].ch = (int)ch;   /* 事件回推通道 */
	m_owners[owner_id].link_mode = link_mode;
	m_owners[owner_id].feat_count = count;
	m_owners[owner_id].link_up_sent = 0;

	resp[0] = BLE_OK;
	return 1;
}

/* ---------------------------------------------------------------------------
 * 帧命令：注销服务
 * ------------------------------------------------------------------------- */
static uint32_t cmd_unreg_svc(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 1 || max < 1) { resp[0] = BLE_EINVAL; return 1; }
	int owner_id = f->payload[0];
	if (owner_id < 0 || owner_id >= BLE_MAX_OWNERS || !m_owners[owner_id].used) {
		resp[0] = BLE_ENODEV; return 1;
	}
	bleapp_unregister_service(owner_id);
	m_owners[owner_id].used = 0;
	resp[0] = BLE_OK;
	return 1;
}

/* ---------------------------------------------------------------------------
 * 帧命令：发送数据
 * ------------------------------------------------------------------------- */
static uint32_t cmd_send(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	const uint8_t *p = f->payload;
	if (f->payload_len < 6 || max < 1) { resp[0] = BLE_EINVAL; return 1; }

	int owner_id = p[0];
	int feat_idx = p[1];
	uint32_t lid = rd32(p + 2);
	const uint8_t *data = p + 6;
	uint16_t dlen = (uint16_t)(f->payload_len - 6);

	/* 查逻辑连接 */
	blemgr_link_t *lk = NULL;
	for (int i = 0; i < BLEMGR_MAX_LINKS; i++)
		if (m_links[i].used && m_links[i].logical_id == lid) { lk = &m_links[i]; break; }
	if (!lk) { resp[0] = BLE_ESTALE; return 1; }
	if (lk->ack_pending) { resp[0] = BLE_ESTALE; return 1; }

	int r = bleapp_send(owner_id, feat_idx, lk->conn_handle, data, dlen);
	resp[0] = (r == 0) ? BLE_OK : BLE_EAGAIN;
	return 1;
}

/* ---------------------------------------------------------------------------
 * 帧命令：LINK_DOWN ACK
 * ------------------------------------------------------------------------- */
static uint32_t cmd_link_ack(framelink_ch_t ch, const frame_t *f, uint8_t *resp, uint32_t max)
{
	(void)ch;
	if (f->payload_len < 5 || max < 1) { resp[0] = BLE_EINVAL; return 1; }
	int owner_id = f->payload[0];
	uint32_t lid = rd32(f->payload + 1);
	(void)owner_id;

	for (int i = 0; i < BLEMGR_MAX_LINKS; i++) {
		if (m_links[i].used && m_links[i].logical_id == lid) {
			m_links[i].used = 0;      /* 失效 */
			m_links[i].ack_pending = 0;
			resp[0] = BLE_OK;
			return 1;
		}
	}
	resp[0] = BLE_ESTALE;
	return 1;
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void blemgr_init(void)
{
	memset(m_owners, 0, sizeof(m_owners));
	memset(m_links, 0, sizeof(m_links));
	next_logical_id = 1;

	/* 绑定 bleapp 回调 */
	bleapp_set_link_cb(on_link);
	bleapp_set_cccd_cb(on_cccd);
	bleapp_set_evt_cb(on_data);

	/* 注册帧命令处理器 */
	framelink_register(BLE_CMD_REG_SVC,   cmd_reg_svc);
	framelink_register(BLE_CMD_UNREG_SVC, cmd_unreg_svc);
	framelink_register(BLE_CMD_SEND,      cmd_send);
	framelink_register(BLE_CMD_LINK_ACK,  cmd_link_ack);
}

/* ---------------------------------------------------------------------------
 * 轮询：ACK 超时处理
 * ------------------------------------------------------------------------- */
void blemgr_poll(void)
{
	uint32_t now = sys_now();
	for (int i = 0; i < BLEMGR_MAX_LINKS; i++) {
		if (m_links[i].used && m_links[i].ack_pending) {
			if ((int32_t)(now - m_links[i].ack_deadline) >= 0) {
				/* ACK 超时：强制失效 */
				m_links[i].used = 0;
				m_links[i].ack_pending = 0;
			}
		}
	}
}

/* ---------------------------------------------------------------------------
 * 测试辅助：模拟应用芯片注册 owner=0 的演示服务
 * ------------------------------------------------------------------------- */
int blemgr_test_reg(void)
{
	bleapp_feat_t feats[2];
	/* 特征 0：RX（写）UUID 0xAAA1 */
	feats[0].uuid16 = 0xAAA1;
	feats[0].props = BLE_PROP_WRITE | BLE_PROP_WRITE_NR;
	/* 特征 1：TX（通知）UUID 0xAAA2 */
	feats[1].uuid16 = 0xAAA2;
	feats[1].props = BLE_PROP_NOTIFY;

	if (bleapp_register_service(0, feats, 2) != 0) return -1;

	m_owners[0].used = 1;
	m_owners[0].ch = (int)FRAMELINK_UART0;   /* 测试默认回推 UART0 */
	m_owners[0].link_mode = BLE_LINK_EAGER;
	m_owners[0].feat_count = 2;
	m_owners[0].link_up_sent = 0;
	return 0;
}
