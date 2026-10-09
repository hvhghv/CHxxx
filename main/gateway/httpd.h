/********************************** (C) COPYRIGHT *******************************
 * File Name          : httpd.h
 * Description        : 极简 HTTP 服务器（基于 LWIP raw TCP API）
 *
 * 提供 REST 风格接口，用于配置引脚复用、外设、查看系统信息等。
 *******************************************************************************/

#ifndef _HTTPD_H
#define _HTTPD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 HTTP 服务器（监听端口 80） */
void httpd_init(void);

/* 主循环轮询（NO_SYS 模式下由主循环调用，处理超时等） */
void httpd_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* _HTTPD_H */
