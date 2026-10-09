/********************************** (C) COPYRIGHT *******************************
 * File Name          : crc.h
 * Description        : 通用 CRC 校验（CRC16-CCITT）
 *
 * 集中实现，供 frame_wire.c（帧 CRC）与 config.c（配置 CRC）共用。
 *******************************************************************************/

#ifndef _CRC_H
#define _CRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CRC16-CCITT（多项式 0x1021，MSB-first）
 *   crc: 初值（首次调用传 0xFFFF；分段续算时传上次返回值）
 *   data/len: 数据块
 * 返回更新后的 CRC */
uint16_t crc16_ccitt(uint16_t crc, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* _CRC_H */
