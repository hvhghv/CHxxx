/********************************** (C) COPYRIGHT *******************************
 * File Name          : httpd.c
 * Description        : 极简 HTTP 服务器（LWIP raw TCP API）
 *
 * 接口（REST 风格）：
 *   GET  /                    - 首页（HTML）
 *   GET  /api/info            - 系统信息
 *   GET  /api/pins            - 引脚复用列表
 *   GET  /api/gpio?pin=N      - 读 GPIO
 *   POST /api/gpio            - 写 GPIO（body: pin=N&val=0/1）
 *   GET  /api/adc?ch=N        - 读 ADC
 *   POST /api/pwm             - 配置 PWM（body: ch=N&period=P&duty=D&en=0/1）
 *   POST /api/save            - 保存配置
 *   POST /api/load            - 加载配置
 *******************************************************************************/

#include "ch32fun.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "httpd.h"
#include "pinmux.h"
#include "periph.h"
#include "config.h"
#include "netroute.h"
#include "swio.h"
#include "bleapp.h"
#include "dhcps.h"
#include "settings.h"
#include "webui.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * 连接状态
 * ------------------------------------------------------------------------- */
#define HTTP_REQ_MAX 384

/* 固件上传状态 */
typedef enum {
	UP_NONE = 0,     /* 非上传 */
	UP_HEADER,       /* 正在收 HTTP 头 */
	UP_BODY,         /* 正在收固件 body */
} upload_state_t;

typedef struct {
	struct tcp_pcb *pcb;
	char req[HTTP_REQ_MAX];
	uint16_t req_len;
	uint32_t last_ms;      /* 最后活动时刻（超时清理用） */

	/* 固件上传 */
	uint8_t  up_state;     /* upload_state_t */
	uint32_t up_total;     /* Content-Length */
	uint32_t up_recvd;     /* 已收 body 字节 */
	uint32_t up_addr;      /* 目标烧录地址 */
} http_conn_t;

static struct tcp_pcb *http_listen_pcb = NULL;
static http_conn_t conn;   /* 单连接（省 RAM） */

/* 外部提供（LWIP sys_now，毫秒） */
extern uint32_t sys_now(void);

#define HTTP_TIMEOUT_MS  10000   /* 空闲 10s 断开 */

/* ---------------------------------------------------------------------------
 * 响应辅助
 * ------------------------------------------------------------------------- */
/* 通用响应头前缀（省去 charset，浏览器默认 UTF-8）。
 * 加 CORS 头，允许 GitHub Pages 等外部前端跨域调用本机 API。 */
#define HDR_200_PLAIN "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n"
#define HDR_200_HTML  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n"
#define HDR_404       "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n"
#define HDR_CORS      "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET,POST,OPTIONS\r\nAccess-Control-Allow-Headers: *\r\nConnection: close\r\n\r\n"

static const char *http_200 = HDR_200_PLAIN;
static const char *http_html = HDR_200_HTML;

/* s_cat 快捷宏（复用 body/off/sizeof(body) 样板） */
#define CAT(s)  do { off = s_cat(body, off, sizeof(body), (s)); } while (0)
#define CATI(v) do { off = s_int(body, off, sizeof(body), (v)); } while (0)
#define CATX(v) do { off = s_hex8(body, off, sizeof(body), (v)); } while (0)

static void http_send(struct tcp_pcb *pcb, const char *hdr, const char *body)
{
	tcp_write(pcb, hdr, (u16_t)strlen(hdr), TCP_WRITE_FLAG_COPY);
	if (body) {
		tcp_write(pcb, body, (u16_t)strlen(body), TCP_WRITE_FLAG_COPY);
	}
	tcp_output(pcb);
}

/* ---------------------------------------------------------------------------
 * 轻量字符串拼接（替代 mini_snprintf，省格式化引擎 ~750B）
 * ------------------------------------------------------------------------- */
/* 追加字符串，返回新偏移 */
static uint32_t s_cat(char *buf, uint32_t off, uint32_t max, const char *s)
{
	while (*s && off < max - 1) buf[off++] = *s++;
	buf[off] = '\0';
	return off;
}

/* 追加有符号十进制 */
static uint32_t s_int(char *buf, uint32_t off, uint32_t max, int32_t v)
{
	if (v < 0) { off = s_cat(buf, off, max, "-"); v = -v; }
	char t[11]; int n = 0;
	if (v == 0) t[n++] = '0';
	while (v && n < 10) { t[n++] = (char)('0' + v % 10); v /= 10; }
	while (n > 0 && off < max - 1) buf[off++] = t[--n];
	buf[off] = '\0';
	return off;
}

/* 追加 8 位十六进制（大写） */
static uint32_t s_hex8(char *buf, uint32_t off, uint32_t max, uint32_t v)
{
	for (int i = 7; i >= 0; i--) {
		uint8_t nib = (uint8_t)((v >> (i * 4)) & 0xF);
		if (off < max - 1) buf[off++] = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
	}
	buf[off] = '\0';
	return off;
}

/* 发送 settings_save 结果后缀：" (saved)\r\n" / " (save failed)\r\n" */
static void http_send_save_tail(struct tcp_pcb *pcb, char *body, uint32_t off, int sr)
{
	off = s_cat(body, off, 256, sr == 0 ? " (saved)\r\n" : " (save failed)\r\n");
	(void)off;
	http_send(pcb, http_200, body);
}

/* 发送 SWIO 烧录结果响应："OK flashed\r\n" / "ERR flash r=N\r\n" */
static void http_send_flash_result(struct tcp_pcb *pcb, int r)
{
	char resp[64];
	uint32_t o = 0;
	o = s_cat(resp, o, sizeof(resp), r == SWIO_OK ? "OK flashed" : "ERR flash r=");
	if (r != SWIO_OK) o = s_int(resp, o, sizeof(resp), r);
	o = s_cat(resp, o, sizeof(resp), "\r\n");
	http_send(pcb, http_200, resp);
}

/* ---------------------------------------------------------------------------
 * 请求解析辅助：提取 query 参数（如 pin=5）
 * ------------------------------------------------------------------------- */
/* 简单子串搜索（替代库 strstr，省 ~846B 的 twoway 算法） */
static const char *s_find(const char *hay, const char *needle)
{
	if (!*needle) return hay;
	for (; *hay; hay++) {
		const char *h = hay, *n = needle;
		while (*n && *h == *n) { h++; n++; }
		if (!*n) return hay;
	}
	return NULL;
}

static int query_int(const char *req, const char *key, int def)
{
	const char *p = s_find(req, key);
	if (!p) return def;
	p += strlen(key);
	if (*p == '=') p++;
	return atoi(p);
}

/* 解析 "a.b.c.d" → uint32（大端序：a<<24|b<<16|c<<8|d）。失败返回 0 */
static uint32_t parse_ip(const char *s)
{
	uint32_t v = 0;
	int oct = 0, n = 0;
	for (; *s && oct < 4; s++) {
		if (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); }
		else if (*s == '.') { v = (v << 8) | (n & 0xFF); n = 0; oct++; }
		else break;
	}
	if (oct == 3) { v = (v << 8) | (n & 0xFF); return v; }
	return 0;
}

/* 提取 query 参数值（返回指向值的指针，或 NULL） */
static const char *query_str(const char *req, const char *key)
{
	const char *p = s_find(req, key);
	if (!p) return NULL;
	p += strlen(key);
	if (*p == '=') p++;
	return p;
}

/* 解析 Content-Length 头（返回字节数，未找到返回 0） */
static uint32_t parse_content_length(const char *req)
{
	const char *p = s_find(req, "Content-Length:");
	if (!p) p = s_find(req, "content-length:");
	if (!p) return 0;
	p += 15;
	while (*p == ' ' || *p == '\t') p++;
	uint32_t v = 0;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; }
	return v;
}

/* ---------------------------------------------------------------------------
 * 处理请求
 * ------------------------------------------------------------------------- */
static void http_handle(struct tcp_pcb *pcb, const char *req)
{
	char body[256];

	/* CORS 预检（外部前端跨域调用） */
	if (strncmp(req, "OPTIONS ", 8) == 0) {
		http_send(pcb, HDR_CORS, NULL);
		return;
	}

	/* 首页：若配置了外部前端 URL，返回含 iframe 的小页面；否则用内嵌 UI */
	if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index", 10) == 0) {
		if (g_cfg.webui_url[0]) {
			uint32_t off = 0;
			CAT("<!doctype html><meta charset=utf-8><title>CH59x GW</title>");
			CAT("<style>html,body{margin:0;height:100%}iframe{border:0;width:100%;height:100%}</style>");
			CAT("<iframe src=\"");
			CAT(g_cfg.webui_url);
			CAT("\"></iframe>");
			http_send(pcb, http_html, body);
		}
#if GW_EMBED_WEBUI
		else {
			http_send(pcb, http_html, WEBUI_HTML);
		}
#else
		else {
			http_send(pcb, http_html,
				"<!doctype html><meta charset=utf-8><title>CH59x GW</title>"
				"<body style='font:14px sans-serif;background:#0f172a;color:#e2e8f0;padding:20px'>"
				"<h2>CH59x Gateway</h2><p>未配置前端 URL。</p>"
				"<p>设置：<code>POST /api/webui?url=&lt;前端地址&gt;</code></p></body>");
		}
#endif
		return;
	}

	/* 系统信息 */
	if (strncmp(req, "GET /api/info", 13) == 0) {
		uint32_t off = 0;
		CAT("chip=CH59x\r\nclk=");
		CATI((int32_t)FUNCONF_SYSTEM_CORE_CLOCK);
		CAT("\r\n");
		/* 芯片型号 ID（R8_CHIP_ID，固定值） */
		CAT("chipid=");
		CATI((int32_t)R8_CHIP_ID);
		CAT("\r\n");
		/* 芯片唯一 ID：FlashROM 信息区 0x0007F000，8 字节（ID + 校验和） */
		{
			const volatile uint8_t *uid = (const volatile uint8_t *)0x0007F000;
			static const char hx[] = "0123456789ABCDEF";
			char hex[3];
			CAT("uid=");
			for (int i = 0; i < 8; i++) {
				uint8_t b = uid[i];
				hex[0] = hx[b >> 4];
				hex[1] = hx[b & 0xF];
				hex[2] = '\0';
				CAT(hex);
			}
			CAT("\r\n");
		}
		CAT("heap_free=0\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* 引脚复用列表：每行 `<pin> <func> <p1> <p2> <arg1>`（流式发送）
	 * UART 引脚 func 带实例号（UARTn_TX/UARTn_RX），p1 输出实例号 */
	if (strncmp(req, "GET /api/pins", 13) == 0) {
		tcp_write(pcb, http_200, (u16_t)strlen(http_200), TCP_WRITE_FLAG_COPY);
		char line[48];
		for (int i = 0; i < PIN_COUNT; i++) {
			uint32_t off = 0;
			off = s_cat(line, off, sizeof(line), pinmux_pin_name(i));
			off = s_cat(line, off, sizeof(line), " ");
			/* UART：func 名带实例号 */
			if (g_pin_cfg[i].func == PIN_FUNC_UART_TX || g_pin_cfg[i].func == PIN_FUNC_UART_RX) {
				off = s_cat(line, off, sizeof(line), "UART");
				off = s_int(line, off, sizeof(line), g_pin_cfg[i].param1);
				off = s_cat(line, off, sizeof(line),
					g_pin_cfg[i].func == PIN_FUNC_UART_TX ? "_TX" : "_RX");
			} else {
				off = s_cat(line, off, sizeof(line), pinmux_func_name(g_pin_cfg[i].func));
			}
			off = s_cat(line, off, sizeof(line), " ");
			off = s_int(line, off, sizeof(line), g_pin_cfg[i].param1);
			off = s_cat(line, off, sizeof(line), " ");
			off = s_int(line, off, sizeof(line), g_pin_cfg[i].param2);
			off = s_cat(line, off, sizeof(line), " ");
			/* 第 5 字段：UART 输出波特率值（其余输出 arg1） */
			if (g_pin_cfg[i].func == PIN_FUNC_UART_TX || g_pin_cfg[i].func == PIN_FUNC_UART_RX)
				off = s_int(line, off, sizeof(line), (int32_t)uart_div_to_baud(g_pin_cfg[i].uart_dl));
			else
				off = s_int(line, off, sizeof(line), g_pin_cfg[i].arg1);
			off = s_cat(line, off, sizeof(line), " ");
			off = s_int(line, off, sizeof(line), g_pin_cfg[i].flags & 0x03);   /* bind */
			off = s_cat(line, off, sizeof(line), "\r\n");
			tcp_write(pcb, line, (u16_t)off, TCP_WRITE_FLAG_COPY);
		}
		tcp_output(pcb);
		return;
	}

	/* 设置引脚配置：POST /api/pin?pin=N&func=F[&p1=X&p2=Y&arg=Z]
	 * func 取值：NONE/GPIO_IN/GPIO_OUT/UART_TX/UART_RX/PWM/ADC/SPI/I2C/SWIO */
	if (strncmp(req, "POST /api/pin", 13) == 0) {
		int pin = query_int(req, "pin", -1);
		const char *fs = query_str(req, "func");
		uint32_t off = 0;
		if (pin < 0 || pin >= PIN_COUNT || !fs) {
			http_send(pcb, http_200, "ERR bad args\r\n");
			return;
		}
		/* 解析 func 名 → 枚举。
		 * 支持带实例号的 UARTn_TX / UARTn_RX（n=0..3），实例号存入 param1。
		 * 其余为无实例名：NONE/GPIO_IN/GPIO_OUT/PWM/ADC/SPI/I2C/SWIO */
		int func = -1;
		int inst = 0;   /* UART 实例号 */
		if (strncmp(fs, "UART", 4) == 0 && fs[4] >= '0' && fs[4] <= '3') {
			inst = fs[4] - '0';
			if (strncmp(fs + 5, "_TX", 3) == 0) func = PIN_FUNC_UART_TX;
			else if (strncmp(fs + 5, "_RX", 3) == 0) func = PIN_FUNC_UART_RX;
		} else {
			static const char *names[] = { "NONE","GPIO_IN","GPIO_OUT","PWM",
				"ADC","SPI","I2C","SWIO" };
			static const int vals[] = { PIN_FUNC_NONE, PIN_FUNC_GPIO_IN, PIN_FUNC_GPIO_OUT,
				PIN_FUNC_PWM, PIN_FUNC_ADC, PIN_FUNC_SPI, PIN_FUNC_I2C, PIN_FUNC_SWIO };
			for (int i = 0; i < 8; i++) {
				size_t n = strlen(names[i]);
				if (strncmp(fs, names[i], n) == 0 && (fs[n] == '\0' || fs[n] == ' ' || fs[n] == '&')) {
					func = vals[i]; break;
				}
			}
		}
		if (func < 0) {
			http_send(pcb, http_200, "ERR bad func\r\n");
			return;
		}
		pinmux_set_func(pin, func);
		/* UART 引脚：param1 自动填实例号；其余引脚 param1 由 p1 指定 */
		if (func == PIN_FUNC_UART_TX || func == PIN_FUNC_UART_RX)
			g_pin_cfg[pin].param1 = (uint8_t)inst;
		else
			g_pin_cfg[pin].param1 = (uint8_t)query_int(req, "p1", 0);
		g_pin_cfg[pin].param2 = (uint8_t)query_int(req, "p2", 0);
		g_pin_cfg[pin].arg1   = (uint8_t)query_int(req, "arg", 0);
		/* baud：UART 波特率（任意值，最高 6Mbps），转分频值存 uart_dl */
		{
			const char *bs = query_str(req, "baud");
			if (bs) g_pin_cfg[pin].uart_dl = uart_baud_to_div((uint32_t)query_int(req, "baud", 0));
		}
		/* bind：CDC 动态绑定号（0=自动/固定，1..N=CDC-B/C/D），存 flags bit0-1 */
		{
			const char *bs = query_str(req, "bind");
			if (bs) g_pin_cfg[pin].flags = (uint8_t)(query_int(req, "bind", 0) & 0x03);
		}
		pinmux_apply(pin);
		int sr = settings_save();
		CAT("set ");
		CAT(pinmux_pin_name(pin));
		CAT("=");
		CAT(pinmux_func_name(g_pin_cfg[pin].func));
		http_send_save_tail(pcb, body, off, sr);
		return;
	}

	/* GPIO：带 val= 则写，否则读 */
	if (strncmp(req, "GET /api/gpio", 13) == 0 ||
	    strncmp(req, "POST /api/gpio", 14) == 0) {
		int pin = query_int(req, "pin", -1);
		const char *pv = s_find(req, "val=");
		uint32_t off = 0;
		if (pv) {
			int val = atoi(pv + 4);
			pinmux_gpio_write(pin, val);
			CAT("pin=");
			CATI(pin);
			CAT(" set=");
			CATI(val);
		} else {
			int val = pinmux_gpio_read(pin);
			CAT("pin=");
			CATI(pin);
			CAT(" val=");
			CATI(val);
		}
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* 读 ADC */
	if (strncmp(req, "GET /api/adc", 12) == 0) {
		int ch = query_int(req, "ch", -1);
		int val = periph_adc_read((uint8_t)ch);
		uint32_t off = 0;
		CAT("ch=");
		CATI(ch);
		CAT(" val=");
		CATI(val);
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* 保存配置 */
	if (strncmp(req, "POST /api/save", 14) == 0) {
		int r = pinmux_save();
		http_send(pcb, http_200, r == 0 ? "saved\r\n" : "failed\r\n");
		return;
	}

	/* 加载配置 */
	if (strncmp(req, "POST /api/load", 14) == 0) {
		pinmux_load();
		pinmux_apply_all();
		http_send(pcb, http_200, "loaded\r\n");
		return;
	}

	/* 路由表查询 */
	if (strncmp(req, "GET /api/routes", 15) == 0) {
		int cnt = 0;
		const netroute_entry_t *rt = netroute_table(&cnt);
		uint32_t off = 0;
		CAT("tx=");
		CATI((int32_t)netroute_tx_count());
		CAT(" rx=");
		CATI((int32_t)netroute_rx_count());
		CAT("\r\n");
		for (int i = 0; i < NETROUTE_MAX_ENTRIES && off < sizeof(body) - 48; i++) {
			if (!rt[i].enabled) continue;
			CATX(rt[i].dest);
			CAT("/");
			CATX(rt[i].mask);
			CAT(" -> ch");
			CATI(rt[i].ch);
			CAT("\r\n");
		}
		http_send(pcb, http_200, body);
		return;
	}

	/* 添加路由：POST /api/route/add?dest=X&mask=Y&ch=Z */
	if (strncmp(req, "POST /api/route/add", 19) == 0) {
		uint32_t dest = (uint32_t)query_int(req, "dest", 0);
		uint32_t mask = (uint32_t)query_int(req, "mask", 0);
		int ch = query_int(req, "ch", -1);
		int r = netroute_add(dest, mask, (uint8_t)ch);
		http_send(pcb, http_200, r == 0 ? "added\r\n" : "failed\r\n");
		return;
	}

	/* 删除路由：POST /api/route/del?dest=X&mask=Y */
	if (strncmp(req, "POST /api/route/del", 19) == 0) {
		uint32_t dest = (uint32_t)query_int(req, "dest", 0);
		uint32_t mask = (uint32_t)query_int(req, "mask", 0);
		int r = netroute_del(dest, mask);
		http_send(pcb, http_200, r == 0 ? "deleted\r\n" : "not found\r\n");
		return;
	}

	/* SWIO 设置引脚：POST /api/swio?pin=N */
	if (strncmp(req, "POST /api/swio", 14) == 0) {
		int pin = query_int(req, "pin", -1);
		swio_set_pin(pin);
		uint32_t off = 0;
		CAT("swio_pin=");
		CATI(pin);
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* SWIO 烧录状态：GET /api/swio */
	if (strncmp(req, "GET /api/swio", 13) == 0) {
		uint32_t off = 0;
		CAT("progress=");
		CATI(swio_progress());
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* SWIO 握手测试：GET /api/swio/handshake */
	if (strncmp(req, "GET /api/swio/handshake", 23) == 0) {
		uint32_t id = 0;
		int r = swio_handshake(&id);
		uint32_t off = 0;
		CAT("result=");
		CATI(r);
		CAT(" chip_id=");
		CATX(id);
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* BLE 状态：GET /api/ble */
	if (strncmp(req, "GET /api/ble", 12) == 0) {
		uint32_t off = 0;
		CAT("role=");
		CAT(bleapp_role_str());
		CAT("\r\nstate=");
		CAT(bleapp_state_str());
		CAT("\r\nconn=");
		CATI(bleapp_is_connected());
		CAT(" adv=");
		CATI(bleapp_is_advertising());
		CAT(" scan=");
		CATI(bleapp_is_scanning());
		CAT(" devs=");
		CATI(bleapp_scan_count());
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* DHCP 配置：POST /api/dhcp?ip=X&mask=Y */
	if (strncmp(req, "POST /api/dhcp", 14) == 0) {
		const char *ips = query_str(req, "ip");
		const char *ms = query_str(req, "mask");
		uint32_t nip = ips ? parse_ip(ips) : 0;
		uint32_t nm = ms ? parse_ip(ms) : 0;
		if (!nip && !nm) {
			http_send(pcb, http_200, "no change\r\n");
			return;
		}
		dhcps_config(nip, nm);
		/* 同步到配置并持久化 */
		if (nip) g_cfg.dhcp_client_ip = nip;
		if (nm)  g_cfg.dhcp_mask = nm;
		int sr = settings_save();
		uint32_t off = 0;
		CAT("dhcp set ip=");
		CATX(nip);
		CAT(" mask=");
		CATX(nm);
		http_send_save_tail(pcb, body, off, sr);
		return;
	}

	/* DHCP 状态：GET /api/dhcp */
	if (strncmp(req, "GET /api/dhcp", 13) == 0) {
		uint32_t off = 0;
		uint32_t sip = 0, cip_cfg = 0, msk = 0;
		dhcps_get_config(&sip, &cip_cfg, &msk);
		uint32_t cip = dhcps_client_ip();
		CAT("clients=");
		CATI(dhcps_client_count());
		CAT(" server=");
		CATX(sip);
		CAT(" offer=");
		CATX(cip_cfg);
		CAT(" mask=");
		CATX(msk);
		CAT(" leased=");
		CATX(cip);
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}

	/* Web UI 前端 URL：GET /api/webui  查看；POST /api/webui?url=X  设置 */
	if (strncmp(req, "GET /api/webui", 14) == 0) {
		uint32_t off = 0;
		CAT("url=");
		CAT(g_cfg.webui_url[0] ? g_cfg.webui_url : "(embedded)");
		CAT("\r\n");
		http_send(pcb, http_200, body);
		return;
	}
	if (strncmp(req, "POST /api/webui", 15) == 0) {
		const char *u = query_str(req, "url");
		if (u) {
			/* 复制到 webui_url（截断到最大长度，遇 ' ' 或 '&' 停止） */
			int i = 0;
			while (u[i] && u[i] != ' ' && u[i] != '&' && i < WEBUI_URL_MAX - 1) {
				g_cfg.webui_url[i] = u[i];
				i++;
			}
			g_cfg.webui_url[i] = '\0';
			int sr = settings_save();
			uint32_t off = 0;
			CAT("set url=");
			CAT(g_cfg.webui_url);
			http_send_save_tail(pcb, body, off, sr);
			return;
		}
		http_send(pcb, http_200, "ERR no url\r\n");
		return;
	}

	/* 404 */
	http_send(pcb, HDR_404, "404 Not Found\r\n");
}

/* ---------------------------------------------------------------------------
 * TCP 回调
 * ------------------------------------------------------------------------- */
static void http_err(void *arg, err_t err)
{
	(void)arg; (void)err;
	if (conn.up_state == UP_BODY) swio_stream_end();
	conn.up_state = UP_NONE;
	conn.pcb = NULL;
	conn.req_len = 0;
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
	(void)arg;
	if (err != ERR_OK) return err;

	if (p == NULL) {
		/* 对端关闭 */
		if (conn.up_state == UP_BODY) {
			/* 上传中途断开：清理 */
			swio_stream_end();
		}
		tcp_close(pcb);
		conn.pcb = NULL;
		conn.req_len = 0;
		conn.up_state = UP_NONE;
		return ERR_OK;
	}

	conn.last_ms = sys_now();   /* 刷新活动时间 */

	/* ---- 固件上传 body 模式 ---- */
	if (conn.up_state == UP_BODY) {
		uint32_t remaining = conn.up_total - conn.up_recvd;
		uint32_t take = p->tot_len < remaining ? p->tot_len : remaining;
		uint8_t chunk[64];
		uint32_t off = 0;

		/* 分块拷贝并流式写入（避免大栈缓冲） */
		while (off < take) {
			uint32_t n = take - off;
			if (n > sizeof(chunk)) n = sizeof(chunk);
			pbuf_copy_partial(p, chunk, n, (u16_t)off);
			if (swio_stream_data(chunk, n) != SWIO_OK) {
				swio_stream_end();
				conn.up_state = UP_NONE;
				tcp_recved(pcb, p->tot_len);
				pbuf_free(p);
				tcp_abort(pcb);
				conn.pcb = NULL;
				return ERR_ABRT;
			}
			off += n;
		}
		conn.up_recvd += take;
		tcp_recved(pcb, p->tot_len);
		pbuf_free(p);

		if (conn.up_recvd >= conn.up_total) {
			/* 上传完成 → 收尾 */
			int r = swio_stream_end();
			http_send_flash_result(pcb, r);
			conn.up_state = UP_NONE;
			conn.req_len = 0;
		}
		return ERR_OK;
	}

	/* ---- 普通请求累积 ---- */
	uint16_t avail = HTTP_REQ_MAX - 1 - conn.req_len;
	uint16_t n = p->tot_len < avail ? p->tot_len : avail;
	pbuf_copy_partial(p, conn.req + conn.req_len, n, 0);
	conn.req_len += n;
	conn.req[conn.req_len] = '\0';

	/* 请求头结束（\r\n\r\n）→ 处理 */
	const char *hdr_end = s_find(conn.req, "\r\n\r\n");
	if (hdr_end || conn.req_len >= HTTP_REQ_MAX - 1) {
		/* 检查是否固件上传：若是，body 可能已随本包到达 */
		if (strncmp(conn.req, "POST /api/swio/flash", 20) == 0) {
			uint32_t total = parse_content_length(conn.req);
			uint32_t addr = (uint32_t)query_int(conn.req, "addr", 0x08000000);
			uint32_t hdr_len = hdr_end ? (uint32_t)(hdr_end - conn.req) + 4 : conn.req_len;
			uint32_t body_in_req = conn.req_len > hdr_len ? (uint32_t)(conn.req_len - hdr_len) : 0;

			if (total > 0) {
				int r = swio_stream_begin(addr, total);
				if (r != SWIO_OK) {
					http_send_flash_result(pcb, r);
				} else {
					conn.up_state = UP_BODY;
					conn.up_total = total;
					conn.up_recvd = 0;
					conn.up_addr = addr;
					/* 处理已随请求头到达的 body 部分 */
					if (body_in_req > 0) {
						uint32_t take = body_in_req < total ? body_in_req : total;
						if (swio_stream_data((const uint8_t *)(conn.req + hdr_len), take) == SWIO_OK) {
							conn.up_recvd = take;
						}
					}
					if (conn.up_recvd >= conn.up_total) {
						int r2 = swio_stream_end();
						http_send_flash_result(pcb, r2);
						conn.up_state = UP_NONE;
					} else {
						static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
						tcp_write(pcb, cont, (u16_t)(sizeof(cont) - 1), TCP_WRITE_FLAG_COPY);
						tcp_output(pcb);
					}
				}
			} else {
				http_send(pcb, http_200, "ERR no content-length\r\n");
			}
		} else {
			http_handle(pcb, conn.req);
		}
		conn.req_len = 0;
	}

	tcp_recved(pcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	(void)arg;
	if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

	/* 单连接：占线则拒绝 */
	if (conn.pcb != NULL) {
		tcp_abort(newpcb);
		return ERR_ABRT;
	}

	conn.pcb = newpcb;
	conn.req_len = 0;
	conn.last_ms = sys_now();
	conn.up_state = UP_NONE;
	conn.up_recvd = 0;

	tcp_arg(newpcb, &conn);
	tcp_recv(newpcb, http_recv);
	tcp_err(newpcb, http_err);

	return ERR_OK;
}

/* ---------------------------------------------------------------------------
 * 启动
 * ------------------------------------------------------------------------- */
void httpd_init(void)
{
	http_listen_pcb = tcp_new();
	if (!http_listen_pcb) return;

	tcp_bind(http_listen_pcb, IP_ADDR_ANY, 80);
	http_listen_pcb = tcp_listen(http_listen_pcb);
	tcp_accept(http_listen_pcb, http_accept);
}

void httpd_poll(void)
{
	/* 空闲连接超时清理（防残留半包数据） */
	if (conn.pcb != NULL) {
		if ((int32_t)(sys_now() - conn.last_ms) > HTTP_TIMEOUT_MS) {
			if (conn.up_state == UP_BODY) swio_stream_end();
			conn.up_state = UP_NONE;
			tcp_abort(conn.pcb);
			conn.pcb = NULL;
			conn.req_len = 0;
		}
	}
}
