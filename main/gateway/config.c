/********************************** (C) COPYRIGHT *******************************
 * File Name          : config.c
 * Description        : 配置存储实现（Flash 读写 + CRC16）
 *******************************************************************************/

#include "ch32fun.h"
#include "config.h"
#include "crc.h"
#include "ch5xx_flash.h"
#include <string.h>

/* 读配置：校验 magic + CRC，返回数据长度 */
int config_read(void *data, uint32_t max_len)
{
	config_hdr_t hdr;
	ch5xx_flash_cmd_read(CONFIG_FLASH_ADDR, (uint8_t *)&hdr, sizeof(hdr));

	if (hdr.magic != CONFIG_MAGIC) {
		return -1;   /* 无有效配置 */
	}
	if (hdr.length == 0 || hdr.length > max_len) {
		return -1;
	}

	/* 读数据 */
	ch5xx_flash_cmd_read(CONFIG_FLASH_ADDR + sizeof(hdr),
	                     (uint8_t *)data, hdr.length);

	/* 校验 CRC（CRC16-CCITT） */
	if (crc16_ccitt(0xFFFF, data, hdr.length) != hdr.crc) {
		return -2;   /* CRC 错误 */
	}

	return (int)hdr.length;
}

/* 写配置：擦除扇区 → 写头 → 写数据 */
int config_write(const void *data, uint32_t len)
{
	uint8_t buf[64];
	config_hdr_t hdr;

	if (len == 0 || len > 2048) {
		return -1;
	}

	hdr.magic = CONFIG_MAGIC;
	hdr.version = 1;
	hdr.length = len;
	hdr.crc = crc16_ccitt(0xFFFF, data, len);
	hdr.reserved = 0;

	/* 擦除扇区（4KB） */
	ch5xx_flash_cmd_erase(CONFIG_FLASH_ADDR, 4096);

	/* 写头（分块到 64B 对齐） */
	memset(buf, 0xFF, sizeof(buf));
	memcpy(buf, &hdr, sizeof(hdr));
	if (ch5xx_flash_cmd_write(CONFIG_FLASH_ADDR, buf, sizeof(buf)) != 0) {
		return -2;
	}

	/* 写数据（按 64B 块） */
	uint32_t written = 0;
	const uint8_t *p = (const uint8_t *)data;
	while (written < len) {
		uint32_t chunk = len - written;
		if (chunk > sizeof(buf)) chunk = sizeof(buf);
		memset(buf, 0xFF, sizeof(buf));
		memcpy(buf, p + written, chunk);
		if (ch5xx_flash_cmd_write(CONFIG_FLASH_ADDR + sizeof(hdr) + written,
		                          buf, sizeof(buf)) != 0) {
			return -3;
		}
		written += chunk;
	}

	return 0;
}

int config_erase(void)
{
	ch5xx_flash_cmd_erase(CONFIG_FLASH_ADDR, 4096);
	return 0;
}
