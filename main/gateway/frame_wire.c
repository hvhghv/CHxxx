/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame_wire.c
 * Description        : 当前帧格式实现（8 字节头 + CRC16）
 *
 * 这是「具体字节布局」的唯一实现处。后续若改帧结构，可：
 *   - 直接修改本文件，或
 *   - 新建 frame_wire_v2.c 实现另一套 frame_codec_t，用 frame_set_codec() 切换
 *
 * 格式：
 *   0-1      magic (0x55AA 小端)
 *   2[0-4]   version = 0x04
 *   2[5]     crc_range: 1=校验[0..5,8..n+8], 0=校验[0..5]
 *   2[6-7]   reserved（发送端必须 0）
 *   3        type
 *   4-5      payload_length (uint16 小端，记为 n)
 *   6-7      crc16 (0x1021 小端)
 *   8-(8+n)  payload
 *
 * 最大 2048 bytes/帧。
 *******************************************************************************/

#include "frame.h"
#include "crc.h"
#include <string.h>

#define WIRE_MAGIC       0x55AA
#define WIRE_VERSION     0x04
#define WIRE_HEADER_LEN  8
#define WIRE_MAX_PAYLOAD 2048

/* ---------------------------------------------------------------------------
 * 小端读写
 * ------------------------------------------------------------------------- */
static inline void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

/* ---------------------------------------------------------------------------
 * 整帧 CRC：按 crc_range 决定是否覆盖 payload
 *   crc_range=0: 仅 [0..5]
 *   crc_range=1: [0..5] + [8..8+payload_len)
 * 使用通用 crc16_ccitt（分段续算）
 * ------------------------------------------------------------------------- */
static uint16_t wire_crc_calc(const uint8_t *frame, uint32_t payload_len, uint8_t crc_range)
{
	uint16_t crc = crc16_ccitt(0xFFFF, frame, 6);          /* [0..5] */
	if (crc_range) {
		crc = crc16_ccitt(crc, frame + WIRE_HEADER_LEN, payload_len);  /* [8..8+n) */
	}
	return crc;
}

/* ---------------------------------------------------------------------------
 * 编码
 * ------------------------------------------------------------------------- */
static uint32_t wire_encode(const frame_codec_t *c, uint8_t *buf, const frame_t *f)
{
	(void)c;
	if (f->payload_len > WIRE_MAX_PAYLOAD) return 0;

	memset(buf, 0, WIRE_HEADER_LEN);

	put16(buf + 0, WIRE_MAGIC);

	/* 2: version[0-4] | crc_range[5] | reserved[6-7]=0 */
	buf[2] = (uint8_t)(((f->version ? f->version : WIRE_VERSION) & 0x1F) |
	                   ((f->crc_range & 0x01) << 5));

	buf[3] = f->type;
	put16(buf + 4, f->payload_len);
	/* 6-7: crc16（稍后填） */

	if (f->payload_len && f->payload) {
		memcpy(buf + WIRE_HEADER_LEN, f->payload, f->payload_len);
	}

	uint16_t crc = wire_crc_calc(buf, f->payload_len, f->crc_range);
	put16(buf + 6, crc);

	return WIRE_HEADER_LEN + f->payload_len;
}

/* ---------------------------------------------------------------------------
 * 解码
 * ------------------------------------------------------------------------- */
static int wire_decode(const frame_codec_t *c, const uint8_t *buf, uint32_t len, frame_t *out)
{
	(void)c;
	if (len < WIRE_HEADER_LEN) return -1;

	if (get16(buf + 0) != WIRE_MAGIC) return -2;

	uint8_t version = buf[2] & 0x1F;
	if (version != WIRE_VERSION) return -3;

	uint8_t crc_range = (buf[2] >> 5) & 0x01;

	uint16_t payload_len = get16(buf + 4);
	if (len < (uint32_t)(WIRE_HEADER_LEN + payload_len)) return -4;
	if (payload_len > WIRE_MAX_PAYLOAD) return -5;

	uint16_t crc_calc = wire_crc_calc(buf, payload_len, crc_range);
	uint16_t crc_recv = get16(buf + 6);
	if (crc_calc != crc_recv) return -6;

	out->version      = version;
	out->crc_range    = crc_range;
	out->fragment     = FRAME_FRAG_SINGLE;   /* 新格式无分片字段，固定单包 */
	out->type         = buf[3];
	out->message_id   = 0;                   /* 新格式无 message_id */
	out->packet_index = 0;
	out->packet_count = 1;
	out->payload_len  = payload_len;
	out->payload      = buf + WIRE_HEADER_LEN;

	return 0;
}

/* ---------------------------------------------------------------------------
 * 帧同步：查找 magic
 * ------------------------------------------------------------------------- */
static int wire_sync(const frame_codec_t *c, const uint8_t *buf, uint32_t len)
{
	(void)c;
	if (len < 2) return -1;
	for (uint32_t i = 0; i + 1 < len; i++) {
		if (buf[i] == (WIRE_MAGIC & 0xFF) &&
		    buf[i + 1] == ((WIRE_MAGIC >> 8) & 0xFF)) {
			return (int)i;
		}
	}
	return -1;
}

/* ---------------------------------------------------------------------------
 * 帧长度
 * ------------------------------------------------------------------------- */
static uint32_t wire_frame_size(const frame_codec_t *c, const uint8_t *buf, uint32_t len)
{
	(void)c;
	if (len < WIRE_HEADER_LEN) return 0;
	uint16_t plen = get16(buf + 4);
	if (plen > WIRE_MAX_PAYLOAD) return 0;
	return WIRE_HEADER_LEN + plen;
}

/* ---------------------------------------------------------------------------
 * codec 实例
 * ------------------------------------------------------------------------- */
const frame_codec_t frame_codec_wire = {
	.name        = "wire",
	.header_len  = WIRE_HEADER_LEN,
	.max_payload = WIRE_MAX_PAYLOAD,
	.encode      = wire_encode,
	.decode      = wire_decode,
	.sync        = wire_sync,
	.frame_size  = wire_frame_size,
};
