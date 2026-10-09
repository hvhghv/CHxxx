/********************************** (C) COPYRIGHT *******************************
 * File Name          : webui.h
 * Description        : 内嵌单页 Web UI
 *
 * HTML/CSS/JS 内容放在 webui.html（纯 HTML，可直接浏览器预览）。
 * 构建时由 tools/html2h.py 转成 webui_html.h（C 字符串数组），此处引入。
 *
 * 存储：static const → .rodata（Flash），不占 RAM。
 *******************************************************************************/

#ifndef _WEBUI_H
#define _WEBUI_H

#if GW_EMBED_WEBUI
/* 单页 UI（由 webui.html 生成，见 CMakeLists 的 html2h 步骤） */
#include "webui_html.h"
#endif

#endif /* _WEBUI_H */
