/********************************** (C) COPYRIGHT *******************************
 * File Name          : framelink.h
 * Description        : 帧链路管理（串口/SPI 通道上的帧收发）
 *
 * 负责：
 *   - 从字节流中提取帧（同步 magic）
 *   - 多包重组（fragment）
 *   - 帧分发（按 type 调用处理器）
 *   - 响应帧组装并回发
 *******************************************************************************/

#ifndef _FRAMELINK_H
#define _FRAMELINK_H

#include <stdint.h>
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 链路通道类型 */
typedef enum {
	FRAMELINK_UART0 = 0,
	FRAMELINK_UART1,
	FRAMELINK_UART2,
	FRAMELINK_UART3,
	FRAMELINK_SPI,
	FRAMELINK_CH_COUNT,
} framelink_ch_t;

/* 实际启用的通道数（省 RAM：仅 4 个 UART） */
#define FRAMELINK_ACTIVE_CH  4

/* 帧处理器回调：返回响应长度（0=无响应）
 * ch: 帧到达的链路通道 */
typedef uint32_t (*frame_handler_t)(framelink_ch_t ch, const frame_t *f,
                                    uint8_t *resp, uint32_t resp_max);

/* 初始化 */
void framelink_init(void);

/* 注册帧处理器（按 type） */
void framelink_register(uint8_t type, frame_handler_t handler);

/* 处理从链路收到的字节（内部做帧同步与解析）
 * ch: 链路通道；data/len: 收到的原始字节
 * 解析到完整帧后自动分发并回发响应 */
void framelink_rx(framelink_ch_t ch, const uint8_t *data, uint32_t len);

/* 轮询 SPI 从模式接收 FIFO，把数据喂给 framelink 解析
 * 由主循环调用（仅 SPI 从模式有效） */
void framelink_poll_spi(void);

/* 配置 SPI 链路角色（主/从），并初始化 SPI0 */
void framelink_spi_config(int is_slave, uint8_t clock_div);

/* 通过链路发送一个帧 */
int framelink_send(framelink_ch_t ch, const uint8_t *frame, uint32_t len);

/* 便捷：发送单包响应帧 */
void framelink_send_resp(framelink_ch_t ch, uint8_t type, uint32_t message_id,
                         const uint8_t *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* _FRAMELINK_H */
