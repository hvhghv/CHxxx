/********************************** (C) COPYRIGHT *******************************
 * File Name          : settings.h
 * Description        : 统一配置存储（引脚复用 + DHCP 等）
 *
 * 把整个 gateway_cfg_t 作为一块写入 Flash 保留扇区，上电自动加载。
 * 相比分散存储，统一结构更清晰，save/load 一次完成。
 *******************************************************************************/

#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <stdint.h>
#include "pinmux.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Web UI 前端 URL 最大长度（含结尾 NUL） */
#define WEBUI_URL_MAX  96

/* 统一配置结构（存入 Flash） */
typedef struct {
	uint32_t  magic;              /* 校验 magic（防止未初始化） */
	pin_cfg_t pins[PIN_COUNT];    /* 引脚复用 */
	uint32_t  dhcp_server_ip;     /* DHCP 服务器 IP */
	uint32_t  dhcp_client_ip;     /* DHCP 派发的客户端 IP */
	uint32_t  dhcp_mask;          /* DHCP 子网掩码 */
	char      webui_url[WEBUI_URL_MAX];  /* 前端页面 URL（GitHub Pages 等） */
} gateway_cfg_t;

/* 全局配置（RAM 副本） */
extern gateway_cfg_t g_cfg;

/* 用编译期默认值填充 g_cfg（不写 Flash） */
void settings_defaults(void);

/* 从 Flash 加载到 g_cfg（无有效配置则用默认值）。返回 0=加载成功，-1=用默认 */
int settings_load(void);

/* 把 g_cfg 保存到 Flash。返回 0 成功 */
int settings_save(void);

#ifdef __cplusplus
}
#endif

#endif /* _SETTINGS_H */
