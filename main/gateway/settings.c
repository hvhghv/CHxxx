/********************************** (C) COPYRIGHT *******************************
 * File Name          : settings.c
 * Description        : 统一配置存储实现
 *******************************************************************************/

#include "ch32fun.h"
#include "settings.h"
#include "config.h"
#include <string.h>

/* 统一配置的 magic（与旧 pinmux 配置区分，旧格式自动失效） */
#define SETTINGS_MAGIC   0x47574331u   /* "GWC1" */

/* 默认 DHCP 配置：192.168.7.1（服务器）/ 192.168.7.2（客户端）/ 255.255.255.0 */
#define DEF_DHCP_SRV_IP   0xC0A80701u
#define DEF_DHCP_CLI_IP   0xC0A80702u
#define DEF_DHCP_MASK     0xFFFFFF00u

gateway_cfg_t g_cfg;

void settings_defaults(void)
{
	memset(&g_cfg, 0, sizeof(g_cfg));
	g_cfg.magic = SETTINGS_MAGIC;

	/* 默认引脚：全部未使用 */
	for (int i = 0; i < PIN_COUNT; i++) {
		g_cfg.pins[i].func = PIN_FUNC_NONE;
	}
	/* 示例：PA0 → GPIO 输出，PA1 → GPIO 输入（上拉） */
	g_cfg.pins[PIN_PA(0)].func = PIN_FUNC_GPIO_OUT;
	g_cfg.pins[PIN_PA(1)].func = PIN_FUNC_GPIO_IN;
	g_cfg.pins[PIN_PA(1)].param1 = PIN_GPIO_IN_PU;

	/* 默认 DHCP */
	g_cfg.dhcp_server_ip = DEF_DHCP_SRV_IP;
	g_cfg.dhcp_client_ip = DEF_DHCP_CLI_IP;
	g_cfg.dhcp_mask      = DEF_DHCP_MASK;

	/* 默认前端 URL（空 = 使用内嵌 UI） */
	g_cfg.webui_url[0] = '\0';
}

int settings_load(void)
{
	int n = config_read(&g_cfg, sizeof(g_cfg));
	if (n == (int)sizeof(g_cfg) && g_cfg.magic == SETTINGS_MAGIC) {
		return 0;   /* 加载成功 */
	}
	settings_defaults();
	return -1;
}

int settings_save(void)
{
	g_cfg.magic = SETTINGS_MAGIC;
	return config_write(&g_cfg, sizeof(g_cfg));
}
