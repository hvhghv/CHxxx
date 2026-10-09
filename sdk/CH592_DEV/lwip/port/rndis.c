/********************************** (C) COPYRIGHT *******************************
 * File Name          : rndis.c
 * Description        : RNDIS (Remote NDIS) 类实现
 *
 * 处理主机（Windows）发来的 RNDIS 控制消息与数据包，并提供以太网帧收发。
 * 参考 MS-RNDIS 规范 4.x。
 *******************************************************************************/

#include "ch32fun.h"
#include "rndis.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * RNDIS 消息结构（小端）
 * ------------------------------------------------------------------------- */
#define RNDIS_HDR_LEN   8

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
} rndis_hdr_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t max_transfer_size;
} rndis_init_msg_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t status;
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t device_flags;
	uint32_t medium;
	uint32_t max_packets_per_transfer;
	uint32_t max_transfer_size;
	uint32_t packet_alignment;
	uint32_t af_list_offset;
	uint32_t af_list_size;
} rndis_init_cmplt_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t oid;
	uint32_t info_buffer_length;
	uint32_t info_buffer_offset;
	uint32_t device_vc_handle;
} rndis_query_msg_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t status;
	uint32_t info_buffer_length;
	uint32_t info_buffer_offset;
} rndis_query_cmplt_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t status;
} rndis_status_cmplt_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t status;
	uint32_t buf_len;
	uint32_t buf_offset;
} rndis_reset_cmplt_t;

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	uint32_t request_id;
	uint32_t oid;
	uint32_t info_buffer_length;
	uint32_t info_buffer_offset;
	uint32_t device_vc_handle;
} rndis_set_msg_t;

/* ---------------------------------------------------------------------------
 * 状态
 * ------------------------------------------------------------------------- */
static volatile int rndis_ready = 0;
static uint32_t rndis_request_id = 0;
static uint32_t rndis_packet_filter = 0;
static uint8_t  rndis_mac[6] = { 0x02, 0x00, 0x00, 0x12, 0x09, 0x59 };

/* 发送/接收缓冲（静态，省 RAM；大小见 rndis.h） */
static uint8_t rndis_txbuf[RNDIS_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * 辅助
 * ------------------------------------------------------------------------- */
static inline void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
void rndis_init(void)
{
	rndis_ready = 0;
	rndis_request_id = 0;
	rndis_packet_filter = 0;
}

int rndis_is_ready(void)
{
	return rndis_ready;
}

void rndis_get_mac(uint8_t *mac)
{
	memcpy(mac, rndis_mac, 6);
}

/* ---------------------------------------------------------------------------
 * 控制消息处理
 * ------------------------------------------------------------------------- */
int rndis_control_message(const uint8_t *data, uint32_t len, uint8_t *reply, uint32_t *reply_len)
{
	if (len < RNDIS_HDR_LEN) {
		*reply_len = 0;
		return -1;
	}

	uint32_t msg_type = get32(data);
	uint32_t msg_len = get32(data + 4);

	switch (msg_type) {
	case RNDIS_MSG_INITIALIZE: {
		rndis_init_msg_t *m = (rndis_init_msg_t *)data;
		rndis_init_cmplt_t *r = (rndis_init_cmplt_t *)reply;
		memset(r, 0, sizeof(*r));
		r->msg_type = RNDIS_MSG_INITIALIZE_CMPLT;
		r->msg_len = sizeof(*r);
		r->request_id = m->request_id;
		r->status = RNDIS_STATUS_SUCCESS;
		r->major_version = 1;
		r->minor_version = 0;
		r->device_flags = 0;   /* 无连接介质 */
		r->medium = 0;         /* 802.3 */
		r->max_packets_per_transfer = 1;
		r->max_transfer_size = 0x4000;
		r->packet_alignment = 0;
		r->af_list_offset = 0;
		r->af_list_size = 0;
		*reply_len = sizeof(*r);
		rndis_ready = 1;
		return 0;
	}

	case RNDIS_MSG_HALT: {
		rndis_ready = 0;
		rndis_status_cmplt_t *r = (rndis_status_cmplt_t *)reply;
		r->msg_type = RNDIS_MSG_HALT;
		r->msg_len = sizeof(*r);
		r->request_id = get32(data + 8);
		r->status = RNDIS_STATUS_SUCCESS;
		*reply_len = sizeof(*r);
		return 0;
	}

	case RNDIS_MSG_QUERY: {
		rndis_query_msg_t *m = (rndis_query_msg_t *)data;
		rndis_query_cmplt_t *r = (rndis_query_cmplt_t *)reply;
		uint8_t *ibuf = reply + sizeof(*r);
		uint32_t ilen = 0;
		uint32_t status = RNDIS_STATUS_SUCCESS;

		switch (m->oid) {
		case OID_GEN_SUPPORTED_LIST: {
			static const uint32_t list[] = {
				OID_GEN_SUPPORTED_LIST, OID_GEN_HARDWARE_STATUS,
				OID_GEN_MEDIA_SUPPORTED, OID_GEN_MEDIA_IN_USE,
				OID_GEN_MAXIMUM_FRAME_SIZE, OID_GEN_LINK_SPEED,
				OID_GEN_TRANSMIT_BLOCK_SIZE, OID_GEN_RECEIVE_BLOCK_SIZE,
				OID_GEN_VENDOR_ID, OID_GEN_VENDOR_DESCRIPTION,
				OID_GEN_CURRENT_PACKET_FILTER, OID_GEN_MAXIMUM_TOTAL_SIZE,
				OID_GEN_MEDIA_CONNECT_STATUS, OID_GEN_PHYSICAL_MEDIUM,
				OID_802_3_PERMANENT_ADDRESS, OID_802_3_CURRENT_ADDRESS,
				OID_802_3_MULTICAST_LIST, OID_802_3_MAXIMUM_LIST_SIZE,
			};
			ilen = sizeof(list);
			memcpy(ibuf, list, ilen);
			break;
		}
		case OID_GEN_HARDWARE_STATUS:
			put32(ibuf, 0); ilen = 4; break;   /* NdisHardwareStatusReady */
		case OID_GEN_MEDIA_SUPPORTED:
		case OID_GEN_MEDIA_IN_USE:
			put32(ibuf, 0); ilen = 4; break;   /* 802.3 */
		case OID_GEN_MAXIMUM_FRAME_SIZE:
			put32(ibuf, RNDIS_MTU); ilen = 4; break;
		case OID_GEN_LINK_SPEED:
			put32(ibuf, 10000000 / 100); ilen = 4; break;  /* 10Mbps */
		case OID_GEN_TRANSMIT_BLOCK_SIZE:
		case OID_GEN_RECEIVE_BLOCK_SIZE:
			put32(ibuf, RNDIS_MTU); ilen = 4; break;
		case OID_GEN_VENDOR_ID:
			put32(ibuf, 0x00001209); ilen = 4; break;
		case OID_GEN_VENDOR_DESCRIPTION: {
			const char *d = "CH591 RNDIS";
			ilen = (uint32_t)strlen(d) + 1;
			memcpy(ibuf, d, ilen);
			break;
		}
		case OID_GEN_CURRENT_PACKET_FILTER:
			put32(ibuf, rndis_packet_filter); ilen = 4; break;
		case OID_GEN_MAXIMUM_TOTAL_SIZE:
			put32(ibuf, RNDIS_MAX_FRAME); ilen = 4; break;
		case OID_GEN_MEDIA_CONNECT_STATUS:
			put32(ibuf, 0); ilen = 4; break;   /* connected */
		case OID_GEN_PHYSICAL_MEDIUM:
			put32(ibuf, 0); ilen = 4; break;
		case OID_802_3_PERMANENT_ADDRESS:
		case OID_802_3_CURRENT_ADDRESS:
			memcpy(ibuf, rndis_mac, 6); ilen = 6; break;
		case OID_802_3_MULTICAST_LIST:
			status = RNDIS_STATUS_NOT_SUPPORTED; ilen = 0; break;
		case OID_802_3_MAXIMUM_LIST_SIZE:
			put32(ibuf, 0); ilen = 4; break;
		default:
			status = RNDIS_STATUS_NOT_SUPPORTED;
			ilen = 0;
			break;
		}

		memset(r, 0, sizeof(*r));
		r->msg_type = RNDIS_MSG_QUERY_CMPLT;
		r->msg_len = sizeof(*r) + ilen;
		r->request_id = m->request_id;
		r->status = status;
		r->info_buffer_length = ilen;
		r->info_buffer_offset = ilen ? sizeof(*r) - 8 : 0;  /* offset 相对于 msg_type */
		*reply_len = sizeof(*r) + ilen;
		return 0;
	}

	case RNDIS_MSG_SET: {
		rndis_set_msg_t *m = (rndis_set_msg_t *)data;
		rndis_status_cmplt_t *r = (rndis_status_cmplt_t *)reply;
		uint32_t status = RNDIS_STATUS_SUCCESS;

		if (m->oid == OID_GEN_CURRENT_PACKET_FILTER &&
		    m->info_buffer_length >= 4) {
			uint32_t off = m->info_buffer_offset;
			rndis_packet_filter = get32(data + off - 8 + 8);
		} else if (m->oid == OID_802_3_MULTICAST_LIST) {
			/* 忽略多播列表 */
		} else {
			status = RNDIS_STATUS_NOT_SUPPORTED;
		}

		memset(r, 0, sizeof(*r));
		r->msg_type = RNDIS_MSG_SET_CMPLT;
		r->msg_len = sizeof(*r);
		r->request_id = m->request_id;
		r->status = status;
		*reply_len = sizeof(*r);
		return 0;
	}

	case RNDIS_MSG_RESET: {
		rndis_reset_cmplt_t *r = (rndis_reset_cmplt_t *)reply;
		memset(r, 0, sizeof(*r));
		r->msg_type = RNDIS_MSG_RESET_CMPLT;
		r->msg_len = sizeof(*r);
		r->status = RNDIS_STATUS_SUCCESS;
		r->buf_len = 0;
		*reply_len = sizeof(*r);
		rndis_ready = 1;
		return 0;
	}

	case RNDIS_MSG_KEEPALIVE: {
		rndis_status_cmplt_t *r = (rndis_status_cmplt_t *)reply;
		r->msg_type = RNDIS_MSG_KEEPALIVE_CMPLT;
		r->msg_len = sizeof(*r);
		r->request_id = get32(data + 8);
		r->status = RNDIS_STATUS_SUCCESS;
		*reply_len = sizeof(*r);
		return 0;
	}

	default:
		*reply_len = 0;
		return -1;
	}
}

/* ---------------------------------------------------------------------------
 * 数据包收发
 * ---------------------------------------------------------------------------
 * RNDIS 数据包格式：
 *   [msg_type(4)=RNDIS_PACKET_MSG(0x1)][msg_len(4)][data_offset(4)]
 *   [data_len(4)][oob_offset(4)][oob_len(4)][num_oob(4)][per_packet_info_offset(4)]
 *   [per_packet_info_len(4)][reserved(4)][reserved(4)]  = 44 字节头
 *   [以太网帧...]
 * ------------------------------------------------------------------------- */
#define RNDIS_PACKET_MSG  0x00000001
#define RNDIS_DATA_HDR_LEN 44

int rndis_data_out(const uint8_t *data, uint32_t len, const uint8_t **frame, uint32_t *frame_len)
{
	if (len < RNDIS_DATA_HDR_LEN) return -1;
	if (get32(data) != RNDIS_PACKET_MSG) return -1;

	uint32_t data_offset = get32(data + 8);
	uint32_t data_len = get32(data + 12);

	if (data_offset + data_len > len) return -1;

	*frame = data + data_offset;
	*frame_len = data_len;
	return 0;
}

int rndis_send_frame(const uint8_t *frame, uint32_t len)
{
	/* 构造 RNDIS 数据包头 + 以太网帧 */
	if (RNDIS_DATA_HDR_LEN + len > RNDIS_BUF_SIZE) return -1;

	uint8_t *p = rndis_txbuf;
	memset(p, 0, RNDIS_DATA_HDR_LEN);
	put32(p + 0, RNDIS_PACKET_MSG);
	put32(p + 4, RNDIS_DATA_HDR_LEN + len);
	put32(p + 8, RNDIS_DATA_HDR_LEN);   /* data_offset */
	put32(p + 12, len);                 /* data_len */
	/* 其余字段为 0 */

	memcpy(p + RNDIS_DATA_HDR_LEN, frame, len);

	/* 通过 USB EP 发送（由调用方提供发送函数） */
	extern int rndis_usb_send(const uint8_t *data, uint32_t len);
	return rndis_usb_send(p, RNDIS_DATA_HDR_LEN + len);
}

/* ===========================================================================
 * USB 多包传输（CH59x 端点 64B）
 * =========================================================================== */

/* 接收累积缓冲 */
static uint8_t rndis_rxbuf[RNDIS_BUF_SIZE];
static uint32_t rndis_rxlen = 0;      /* 已累积字节数 */
static uint32_t rndis_rxneed = 0;     /* 本帧需要的总字节数（0=未确定） */

/* 接收累积：每次 USB OUT 中断调用
 * 返回：1=完整帧就绪，0=继续等待，-1=错误 */
int rndis_usb_rx(const uint8_t *data, uint32_t len,
                 const uint8_t **frame, uint32_t *frame_len)
{
	if (len == 0 || !data) return 0;

	/* 追加到累积缓冲 */
	if (rndis_rxlen + len > RNDIS_BUF_SIZE) {
		/* 溢出：重置 */
		rndis_rxlen = 0;
		rndis_rxneed = 0;
		return -1;
	}
	memcpy(rndis_rxbuf + rndis_rxlen, data, len);
	rndis_rxlen += len;

	/* 首次收到：读取 RNDIS msg_len 确定总长度 */
	if (rndis_rxneed == 0) {
		if (rndis_rxlen < 8) return 0;   /* 头部未完整 */
		uint32_t msg_type = get32(rndis_rxbuf);
		uint32_t msg_len = get32(rndis_rxbuf + 4);
		if (msg_type != RNDIS_PACKET_MSG) {
			/* 非数据包（可能是控制消息，走 EP0），丢弃 */
			rndis_rxlen = 0;
			return -1;
		}
		if (msg_len < RNDIS_DATA_HDR_LEN || msg_len > RNDIS_BUF_SIZE) {
			rndis_rxlen = 0;
			return -1;
		}
		rndis_rxneed = msg_len;
	}

	/* 是否收全 */
	if (rndis_rxlen >= rndis_rxneed) {
		/* 解析 RNDIS 数据包 → 以太网帧 */
		int r = rndis_data_out(rndis_rxbuf, rndis_rxneed, frame, frame_len);
		rndis_rxlen = 0;
		rndis_rxneed = 0;
		return (r == 0) ? 1 : -1;
	}

	return 0;   /* 继续等待 */
}

/* 发送分块：把报文拆成 ≤64B 的 USB 包发送 */
int rndis_usb_tx(const uint8_t *data, uint32_t len,
                 int (*send_fn)(const uint8_t *pkt, uint32_t pkt_len))
{
	uint32_t sent = 0;
	while (sent < len) {
		uint32_t chunk = len - sent;
		if (chunk > 64) chunk = 64;
		if (send_fn(data + sent, chunk) != 0) {
			return -1;   /* 端点忙 */
		}
		sent += chunk;
	}
	return 0;
}
