/********************************** (C) COPYRIGHT *******************************
 * File Name          : frame.h
 * Description        : 帧协议抽象接口
 *
 * 本文件只定义「帧的通用结构」与「编解码器接口」，不绑定具体字节布局。
 * 具体帧格式实现在 frame_wire.c（当前格式），后续如需改帧结构，只需：
 *   1. 新增一个 frame_codec_t 实现（如 frame_wire_v2.c）
 *   2. 调用 frame_set_codec() 切换
 * framelink.c 等上层代码无需改动。
 *
 * 当前格式（frame_wire.c 实现，16 字节头 + 负载）：
 *   偏移     字段
 *   0-1      magic
 *   2[0-4]   version: 固定 0x01
 *   2[5]     crc_range: 1=校验[0..13,16..n+16], 0=校验[0..13]
 *   2[6-7]   fragment: 单包 0b10, 多包非末包 0b01, 多包末包 0b11
 *   3        type
 *   4-7      message_id (uint32 小端，同一 message_id = 同一包)
 *   8        packet_index
 *   9        packet_count
 *   10-11    payload_length (uint16 小端，记为 n)
 *   12-13    reserved（发送端必须 0）
 *   14-15    crc16（多项式 0x1021，小端）
 *   16-(16+n) payload
 *******************************************************************************/

#ifndef _FRAME_H
#define _FRAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 通用帧结构（与具体字节布局无关）
 * ------------------------------------------------------------------------- */
typedef struct {
	uint8_t  version;
	uint8_t  crc_range;       /* 是否校验 payload（语义由 codec 定义） */
	uint8_t  fragment;        /* 分片标志 */
	uint8_t  type;
	uint32_t message_id;
	uint8_t  packet_index;
	uint8_t  packet_count;
	uint16_t payload_len;
	const uint8_t *payload;   /* 指向原始缓冲内的负载 */
} frame_t;

/* fragment 语义（通用） */
#define FRAME_FRAG_SINGLE   0x02     /* 0b10 单包 */
#define FRAME_FRAG_MORE     0x01     /* 0b01 多包非末包 */
#define FRAME_FRAG_LAST     0x03     /* 0b11 多包末包 */

/* crc_range 语义（当前 codec） */
#define FRAME_CRC_RANGE_HEAD    0    /* 仅校验头部 */
#define FRAME_CRC_RANGE_FULL    1    /* 校验头部 + payload */

/* 帧类型（type 字段，与 codec 无关） */
typedef enum {
	FRAME_TYPE_PING       = 0x00,   /* 心跳/探测 */
	FRAME_TYPE_GPIO_READ  = 0x10,   /* 读 GPIO 电平 */
	FRAME_TYPE_GPIO_WRITE = 0x11,   /* 写 GPIO 电平 */
	FRAME_TYPE_ADC_READ   = 0x20,   /* 读 ADC */
	FRAME_TYPE_PWM_SET    = 0x30,   /* 配置 PWM */
	FRAME_TYPE_PWM_START  = 0x31,   /* 启动 PWM */
	FRAME_TYPE_PWM_STOP   = 0x32,   /* 停止 PWM */
	FRAME_TYPE_UART_CFG   = 0x40,   /* 配置串口 */
	FRAME_TYPE_NET_FWD    = 0x50,   /* 网络帧转发 */
	FRAME_TYPE_BLE_CFG    = 0x60,   /* 蓝牙配置 */
	FRAME_TYPE_RESP       = 0x80,   /* 响应（type | 0x80） */
	FRAME_TYPE_ERR        = 0xFF,   /* 错误 */
} frame_type_t;

/* ---------------------------------------------------------------------------
 * 编解码器接口（可插拔）
 * ---------------------------------------------------------------------------
 * 后续改帧结构时，实现一个 frame_codec_t 并通过 frame_set_codec() 注册即可。
 * ------------------------------------------------------------------------- */
typedef struct frame_codec {
	const char *name;
	uint16_t header_len;      /* 帧头长度 */
	uint16_t max_payload;     /* 单帧最大负载 */

	/* 编码：把 frame_t 写入 buf，返回总长度（0=失败）
	 * buf 至少 header_len + max_payload 字节 */
	uint32_t (*encode)(const struct frame_codec *c, uint8_t *buf,
	                   const frame_t *f);

	/* 解码：从 buf 解析出 frame_t（payload 指向 buf 内部），返回 0 成功 */
	int (*decode)(const struct frame_codec *c, const uint8_t *buf,
	              uint32_t len, frame_t *out);

	/* 帧同步：在 buf[0..len) 中查找帧起始偏移，返回偏移（-1=未找到） */
	int (*sync)(const struct frame_codec *c, const uint8_t *buf, uint32_t len);

	/* 从帧头计算整帧长度（含头），返回 0 表示头部不完整或非法 */
	uint32_t (*frame_size)(const struct frame_codec *c,
	                       const uint8_t *buf, uint32_t len);
} frame_codec_t;

/* 设置当前编解码器（默认使用 frame_wire 格式） */
void frame_set_codec(const frame_codec_t *codec);

/* 获取当前编解码器 */
const frame_codec_t *frame_get_codec(void);

/* ---------------------------------------------------------------------------
 * 便捷 API（基于当前 codec）
 * ------------------------------------------------------------------------- */

/* 编码帧，返回总长度（0=失败） */
uint32_t frame_encode(uint8_t *buf, const frame_t *f);

/* 解码帧，返回 0 成功 */
int frame_decode(const uint8_t *buf, uint32_t len, frame_t *out);

/* 字节流帧同步，返回帧起始偏移（-1=未找到） */
int frame_sync(const uint8_t *buf, uint32_t len);

/* 从帧头计算整帧长度 */
uint32_t frame_frame_size(const uint8_t *buf, uint32_t len);

/* 当前 codec 的头部长度 / 最大负载 */
uint16_t frame_header_len(void);
uint16_t frame_max_payload(void);

/* 便捷：构造单包帧（fragment=SINGLE, crc_range=FULL） */
uint32_t frame_build_single(uint8_t *buf, uint8_t type, uint32_t message_id,
                            const uint8_t *payload, uint16_t payload_len);

/* 便捷：构造完整参数帧 */
uint32_t frame_build(uint8_t *buf, uint8_t type, uint32_t message_id,
                     uint8_t packet_index, uint8_t packet_count,
                     uint8_t fragment, uint8_t crc_range,
                     const uint8_t *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* _FRAME_H */
