/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame.c
 * Description        : 帧协议抽象层（编解码器分发）
 *
 * 本文件只做「当前 codec 的选择与分发」，不含具体字节布局。
 * 具体格式见 frame_wire.c。
 *******************************************************************************/

#include "frame.h"
#include <string.h>

/* 默认 codec（由 frame_wire.c 提供） */
extern const frame_codec_t frame_codec_wire;

static const frame_codec_t *g_codec = &frame_codec_wire;

/* ---------------------------------------------------------------------------
 * codec 选择
 * ------------------------------------------------------------------------- */
void frame_set_codec(const frame_codec_t *codec)
{
	if (codec) g_codec = codec;
}

const frame_codec_t *frame_get_codec(void)
{
	return g_codec;
}

/* ---------------------------------------------------------------------------
 * 便捷 API（分发到当前 codec）
 * ------------------------------------------------------------------------- */
uint32_t frame_encode(uint8_t *buf, const frame_t *f)
{
	if (!g_codec || !g_codec->encode) return 0;
	return g_codec->encode(g_codec, buf, f);
}

int frame_decode(const uint8_t *buf, uint32_t len, frame_t *out)
{
	if (!g_codec || !g_codec->decode) return -1;
	return g_codec->decode(g_codec, buf, len, out);
}

int frame_sync(const uint8_t *buf, uint32_t len)
{
	if (!g_codec || !g_codec->sync) return -1;
	return g_codec->sync(g_codec, buf, len);
}

uint32_t frame_frame_size(const uint8_t *buf, uint32_t len)
{
	if (!g_codec || !g_codec->frame_size) return 0;
	return g_codec->frame_size(g_codec, buf, len);
}

uint16_t frame_header_len(void)
{
	return g_codec ? g_codec->header_len : 0;
}

uint16_t frame_max_payload(void)
{
	return g_codec ? g_codec->max_payload : 0;
}

/* ---------------------------------------------------------------------------
 * 便捷构造
 * ------------------------------------------------------------------------- */
uint32_t frame_build(uint8_t *buf, uint8_t type, uint32_t message_id,
                     uint8_t packet_index, uint8_t packet_count,
                     uint8_t fragment, uint8_t crc_range,
                     const uint8_t *payload, uint16_t payload_len)
{
	frame_t f;
	memset(&f, 0, sizeof(f));
	f.version      = 0;   /* 0 = 用 codec 默认版本（wire_encode 会填 WIRE_VERSION） */
	f.crc_range    = crc_range;
	f.fragment     = fragment;
	f.type         = type;
	f.message_id   = message_id;
	f.packet_index = packet_index;
	f.packet_count = packet_count;
	f.payload_len  = payload_len;
	f.payload      = payload;
	return frame_encode(buf, &f);
}

uint32_t frame_build_single(uint8_t *buf, uint8_t type, uint32_t message_id,
                            const uint8_t *payload, uint16_t payload_len)
{
	return frame_build(buf, type, message_id, 0, 1,
	                   FRAME_FRAG_SINGLE, FRAME_CRC_RANGE_FULL,
	                   payload, payload_len);
}
