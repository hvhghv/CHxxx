/********************************** (C) COPYRIGHT *******************************
 * File Name          : crc.c
 * Description        : 通用 CRC 校验实现
 *******************************************************************************/

#include "crc.h"

/* CRC16-CCITT（多项式 0x1021，MSB-first，可分段续算） */
uint16_t crc16_ccitt(uint16_t crc, const uint8_t *data, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int j = 0; j < 8; j++) {
			crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
		}
	}
	return crc;
}
