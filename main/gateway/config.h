/********************************** (C) COPYRIGHT *******************************
 * File Name          : config.h
 * Description        : 配置存储（Flash 读写 + 校验）
 *
 * 把引脚复用配置等保存到 Flash 的保留扇区，上电自动加载。
 *******************************************************************************/

#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 配置存储地址（Flash 最后一个 4KB 扇区）
 * CH591: 192KB = 0x30000 → 用 0x2F000
 * CH592: 448KB = 0x70000 → 用 0x6F000
 * 注意：避开 BLE SNV 区域（0x77000 - FLASH_ROM_MAX_SIZE）。 */
#if defined(CH592)
#define CONFIG_FLASH_ADDR   0x0006F000
#else
#define CONFIG_FLASH_ADDR   0x0002F000
#endif

#define CONFIG_MAGIC        0x43484731u   /* "CHG1" */

/* 配置块头 */
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t length;      /* 数据长度 */
	uint16_t crc;         /* 数据 CRC16（CCITT） */
	uint16_t reserved;
} config_hdr_t;

/* 读配置（返回数据长度，<0 失败） */
int config_read(void *data, uint32_t max_len);

/* 写配置（返回 0 成功） */
int config_write(const void *data, uint32_t len);

/* 擦除配置扇区 */
int config_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* _CONFIG_H */
