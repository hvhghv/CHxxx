/********************************** (C) COPYRIGHT *******************************
 * File Name          : gateway.c
 * Description        : CH591/CH592 多功能网关（裸机，无 FreeRTOS）
 *
 * 架构：
 *   - USB 枚举为 3×CDC-ACM + 1×RNDIS
 *   - LWIP（NO_SYS=1，raw API）+ RNDIS 网卡
 *   - BLE（GAP + 动态 GATT）+ DHCP 服务器
 *   - 所有逻辑在单一主循环中，用状态机处理各串口
 *
 * 主循环顺序：
 *   1. sys_nosys_tick()      更新毫秒时间
 *   2. sys_check_timeouts()  LWIP 超时处理
 *   3. RNDIS 接收 → 注入 LWIP
 *   4. flush 各串口发送队列
 *   5. 终端命令解析
 *   6. 数据口回环
 *
 * fsusb 回调运行在中断上下文，只做环形缓冲 push。
 *******************************************************************************/

#include "ch32fun.h"
#include "fsusb.h"
#include <string.h>

/* 引脚复用 + 配置存储 */
#include "pinmux.h"
#include "config.h"
#include "settings.h"

/* 外设 + 帧协议 + HTTP */
#include "periph.h"
#include "frame.h"
#include "framelink.h"
#include "framehdl.h"
#include "netroute.h"
#include "httpd.h"
#include "swio.h"
#include "bleapp.h"
#include "blemgr.h"
#include "dhcps.h"

/* LWIP */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "rndis.h"

/* 由 sys_nosys.c 提供 */
void sys_nosys_tick(void);

/* ===========================================================================
 * 端点定义
 * =========================================================================== */
#define EP_A       1    /* CDC-A 数据（BDIR）终端 */
#define EP_B       2    /* CDC-B 数据（BDIR）UART 转发 0 */
#define EP_C       3    /* CDC-C 数据（BDIR）UART 转发 1 */
#if GW_CDC_COUNT >= 4
#define EP_D       5    /* CDC-D 数据（BDIR）UART 转发 2 */
#endif
#define EP_RNDIS_OUT  7 /* RNDIS 数据接收 */
#define EP_RNDIS_IN   6 /* RNDIS 数据发送 */

/* ===========================================================================
 * 环形缓冲（单生产者单消费者）
 * 终端口（A）用大缓冲；转发口（B/C/D）用小缓冲省 RAM。
 * =========================================================================== */
#define RB_SIZE   128   /* 终端口接收缓冲 */
#define RB_SIZE_F  64   /* 转发口接收缓冲 */
#define TX_SIZE   256   /* 终端口发送缓冲 */
#define TX_SIZE_F  96   /* 转发口发送缓冲 */

/* 通用环形缓冲（size = 有效容量，字段布局一致便于统一操作） */
typedef struct {
	volatile uint8_t  buf[RB_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
	uint32_t          size;
} ringbuf_t;

typedef struct {
	volatile uint8_t  buf[TX_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
	uint32_t          size;
} txbuf_t;

/* 转发口缓冲（小容量，复用同一操作函数） */
typedef struct {
	volatile uint8_t  buf[RB_SIZE_F];
	volatile uint32_t head;
	volatile uint32_t tail;
	uint32_t          size;
} ringbuf_f_t;

typedef struct {
	volatile uint8_t  buf[TX_SIZE_F];
	volatile uint32_t head;
	volatile uint32_t tail;
	uint32_t          size;
} txbuf_f_t;

/* CDC-A 终端：rb_a/tx_a；CDC-B/C/D 转发：数组 rb_f[]/tx_f[]（索引 = UART 号）
 * 固定映射：UART0→[0](CDC-B), UART1→[1](CDC-C), UART2→[2](CDC-D)
 * 数组方式省去 switch 分支，最省 Flash。 */
static ringbuf_t   rb_a = { .size = RB_SIZE };
static txbuf_t     tx_a = { .size = TX_SIZE };
#define GW_FWD_MAX (GW_CDC_COUNT - 1)   /* 转发口数量：CH591=2, CH592=3 */
static ringbuf_f_t rb_f[GW_FWD_MAX] = {
	{ .size = RB_SIZE_F }, { .size = RB_SIZE_F }, { .size = RB_SIZE_F }
};
static txbuf_f_t   tx_f[GW_FWD_MAX] = {
	{ .size = TX_SIZE_F }, { .size = TX_SIZE_F }, { .size = TX_SIZE_F }
};

static void rb_push(void *rbv, const uint8_t *data, uint32_t len)
{
	ringbuf_t *rb = (ringbuf_t *)rbv;
	uint32_t head = rb->head, tail = rb->tail;
	for (uint32_t i = 0; i < len; i++) {
		uint32_t next = (head + 1) % rb->size;
		if (next == tail) break;
		rb->buf[head] = data[i];
		head = next;
	}
	rb->head = head;
}

static int rb_pop(void *rbv)
{
	ringbuf_t *rb = (ringbuf_t *)rbv;
	uint32_t tail = rb->tail;
	if (tail == rb->head) return -1;
	int b = rb->buf[tail];
	rb->tail = (tail + 1) % rb->size;
	return b;
}

static void tx_push(void *tv, const uint8_t *data, uint32_t len)
{
	txbuf_t *t = (txbuf_t *)tv;
	uint32_t head = t->head, tail = t->tail;
	for (uint32_t i = 0; i < len; i++) {
		uint32_t next = (head + 1) % t->size;
		if (next == tail) break;
		t->buf[head] = data[i];
		head = next;
	}
	t->head = head;
}

static uint32_t tx_flush(void *tv, int ep)
{
	txbuf_t *t = (txbuf_t *)tv;
	if (t->head == t->tail) return 0;
	uint32_t n = (t->head - t->tail + t->size) % t->size;
	if (n > 64) n = 64;

	uint8_t tmp[64];
	uint32_t tail = t->tail;
	for (uint32_t i = 0; i < n; i++) {
		tmp[i] = t->buf[tail];
		tail = (tail + 1) % t->size;
	}
	if (USBFS_SendEndpointNEW(ep, tmp, (int)n, 1) == 0) {
		t->tail = tail;
		return n;
	}
	return 0;
}

static void cdc_puts(void *t, const char *s)
{
	tx_push(t, (const uint8_t *)s, (uint32_t)strlen(s));
}

/* 输出无符号十进制（省 mini_snprintf） */
static void cdc_put_u32(void *t, uint32_t v)
{
	char b[11];
	int i = 0;
	if (v == 0) { cdc_puts(t, "0"); return; }
	while (v && i < 10) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
	while (i > 0) { char c = b[--i]; tx_push(t, (const uint8_t *)&c, 1); }
}

/* 输出有符号十进制 */
static void cdc_put_i32(void *t, int32_t v)
{
	if (v < 0) { cdc_puts(t, "-"); cdc_put_u32(t, (uint32_t)(-v)); }
	else cdc_put_u32(t, (uint32_t)v);
}

/* ===========================================================================
 * RNDIS 缓冲
 * =========================================================================== */
/* 完整 RNDIS 帧（收全后由主循环注入 LWIP）
 * 仅存以太网帧（≤ RNDIS_MAX_FRAME），不含 RNDIS 消息头 */
static uint8_t rndis_rx_buf[RNDIS_MAX_FRAME];
static uint32_t rndis_rx_frame_len = 0;
static volatile int rndis_frame_ready = 0;

/* 单包 USB 发送回调（供 rndis_usb_tx 分块调用） */
static int rndis_usb_send_pkt(const uint8_t *pkt, uint32_t pkt_len)
{
	return USBFS_SendEndpointNEW(EP_RNDIS_IN, (uint8_t *)pkt, (int)pkt_len, 1);
}

/* 由 RNDIS 模块调用的 USB 发送接口：分块发送 */
int rndis_usb_send(const uint8_t *data, uint32_t len)
{
	return rndis_usb_tx(data, len, rndis_usb_send_pkt);
}

/* ===========================================================================
 * LWIP
 * =========================================================================== */
struct netif rndis_netif;   /* 非 static：netroute.c 注入用 */

static err_t rndis_netif_output(struct netif *netif, struct pbuf *p)
{
	(void)netif;
	static uint8_t txframe[RNDIS_MAX_FRAME];
	if (p->tot_len > sizeof(txframe)) return ERR_MEM;
	pbuf_copy_partial(p, txframe, p->tot_len, 0);
	if (rndis_send_frame(txframe, p->tot_len) != 0) return ERR_IF;
	return ERR_OK;
}

/* netif->output 钩子：先查路由表，匹配则帧转发，否则走 ARP/以太网 */
static err_t netif_output_hook(struct netif *netif, struct pbuf *p,
                               const ip4_addr_t *ipaddr)
{
	/* 提取 IP 包（p 此时是 IP 包，尚未加以太网头）
	 * 仅当长度不超过转发上限时才尝试路由 */
	if (ipaddr && p->tot_len <= RNDIS_MTU) {
		static uint8_t ipbuf[RNDIS_MTU];
		pbuf_copy_partial(p, ipbuf, p->tot_len, 0);
		uint32_t dest = ip4_addr_get_u32(ipaddr);
		if (netroute_output(ipbuf, p->tot_len, dest)) {
			return ERR_OK;   /* 已帧转发 */
		}
	}
	/* 未匹配 → 走默认以太网输出 */
	return etharp_output(netif, p, ipaddr);
}

static err_t rndis_netif_init_cb(struct netif *netif)
{
	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->output = netif_output_hook;   /* 路由钩子 */
	netif->linkoutput = rndis_netif_output;
	netif->mtu = RNDIS_MTU;              /* 方案 B：降 MTU */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->hwaddr_len = ETH_HWADDR_LEN;
	rndis_get_mac(netif->hwaddr);
	return ERR_OK;
}

/* ===========================================================================
 * fsusb 回调（中断上下文）
 * =========================================================================== */
void HandleDataOut(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	(void)ctx;
	if (len <= 0 || !data) return;

	switch (endp) {
	case EP_A: rb_push(&rb_a, data, (uint32_t)len); break;
	case EP_B: rb_push(&rb_f[0], data, (uint32_t)len); break;   /* UART0 */
	case EP_C: rb_push(&rb_f[1], data, (uint32_t)len); break;   /* UART1 */
#if GW_CDC_COUNT >= 4
	case EP_D: rb_push(&rb_f[2], data, (uint32_t)len); break;   /* UART2 */
#endif
	case EP_RNDIS_OUT: {
		/* 多包累积：每次 USB OUT 中断送一个 64B 包 */
		const uint8_t *frame;
		uint32_t flen;
		int r = rndis_usb_rx(data, (uint32_t)len, &frame, &flen);
		if (r == 1 && flen <= sizeof(rndis_rx_buf)) {
			memcpy(rndis_rx_buf, frame, flen);
			rndis_rx_frame_len = flen;
			rndis_frame_ready = 1;
		}
		break;
	}
	default: break;
	}
}

int HandleInRequest(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	(void)ctx; (void)endp; (void)data; (void)len;
	return 0;
}

int HandleSetupCustom(struct _USBState *ctx, int setup_code)
{
	(void)ctx;
	if (setup_code == 0x20 || setup_code == 0x22) {
		return -1;
	}
	return 0;
}

/* ===========================================================================
 * 终端命令（状态机）
 * =========================================================================== */
static char term_line[96];
static int  term_pos = 0;

/* ---- 命令处理器 ---- */
static void cmd_help(void)
{
	cdc_puts(&tx_a,
		"help info net pins gpio set adc save load default frame spi\r\n"
		"swio swiohs swiochip swioreset swioflash ble blef dhcp echo\r\n"
		"gpio N|set N V|adc N|frame U T|spi master [d]|slave\r\n"
#if GW_CDC_COUNT >= 4
		"uart N [mode forward|frame|baud B|parity none|odd|even|stop 1|2"
#if GW_CDC_DYNAMIC
		"|bind 0..3"
#endif
		"]\r\n"
#endif
		"swio N|swiohs|swiochip|swioreset|dhcp [ip mask]|echo X\r\n"
		"swioflash <addr> <len> (then send raw bytes)|webui [url]\r\n"
		"ble central|peripheral|scan|scanstop|list|conn N|disc|send X\r\n");
}

static void cmd_info(void)
{
	cdc_puts(&tx_a, "clk=");
	cdc_put_u32(&tx_a, (uint32_t)FUNCONF_SYSTEM_CORE_CLOCK);
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_net(void)
{
	cdc_puts(&tx_a, "ip=");
	cdc_puts(&tx_a, ip4addr_ntoa(netif_ip4_addr(&rndis_netif)));
	cdc_puts(&tx_a, " rndis=");
	cdc_put_u32(&tx_a, (uint32_t)rndis_is_ready());
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_pins(void)
{
	for (int i = 0; i < PIN_COUNT; i++) {
		if (g_pin_cfg[i].func == PIN_FUNC_NONE) continue;
		cdc_puts(&tx_a, "  ");
		cdc_puts(&tx_a, pinmux_pin_name(i));
		cdc_puts(&tx_a, " = ");
		cdc_puts(&tx_a, pinmux_func_name(g_pin_cfg[i].func));
		cdc_puts(&tx_a, "\r\n");
	}
}

static void cmd_save(void)
{
	cdc_puts(&tx_a, pinmux_save() == 0 ? "saved\r\n" : "fail\r\n");
}

static void cmd_load(void)
{
	int r = pinmux_load();
	pinmux_apply_all();
	cdc_puts(&tx_a, r == 0 ? "loaded\r\n" : "default\r\n");
}

static void cmd_default(void)
{
	pinmux_reset_default();
	pinmux_apply_all();
	cdc_puts(&tx_a, "default applied\r\n");
}

static void cmd_gpio(const char *arg)
{
	int pin = atoi(arg);
	cdc_puts(&tx_a, pinmux_pin_name(pin));
	cdc_puts(&tx_a, " = ");
	cdc_put_i32(&tx_a, pinmux_gpio_read(pin));
	cdc_puts(&tx_a, "\r\n");
}

/* 解析两个十进制数 "N M" */
static void parse2(const char *p, int *a, int *b)
{
	*a = 0; *b = 0;
	while (*p >= '0' && *p <= '9') { *a = *a * 10 + (*p - '0'); p++; }
	while (*p == ' ') p++;
	while (*p >= '0' && *p <= '9') { *b = *b * 10 + (*p - '0'); p++; }
}

static void cmd_set(const char *arg)
{
	int pin, val;
	parse2(arg, &pin, &val);
	pinmux_gpio_write(pin, val);
	cdc_puts(&tx_a, "OK\r\n");
}

static void cmd_adc(const char *arg)
{
	int ch = atoi(arg);
	cdc_puts(&tx_a, "adc");
	cdc_put_i32(&tx_a, ch);
	cdc_puts(&tx_a, "=");
	cdc_put_i32(&tx_a, periph_adc_read((uint8_t)ch));
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_frame(const char *arg)
{
	int uart, type;
	parse2(arg, &uart, &type);
	uint8_t buf[64];
	uint32_t n = frame_build_single(buf, (uint8_t)type, 0x1234,
	                                (const uint8_t *)"hello", 5);
	if (n && uart >= 0 && uart <= 3) {
		periph_uart_write((uint8_t)uart, buf, n);
		cdc_puts(&tx_a, "sent\r\n");
	} else {
		cdc_puts(&tx_a, "fail\r\n");
	}
}

static void cmd_spi(const char *p)
{
	if (strncmp(p, "master", 6) == 0) {
		p += 6;
		while (*p == ' ') p++;
		uint8_t div = (*p >= '0' && *p <= '9') ? (uint8_t)atoi(p) : 4;
		framelink_spi_config(0, div);
		cdc_puts(&tx_a, "SPI master\r\n");
	} else if (strncmp(p, "slave", 5) == 0) {
		framelink_spi_config(1, 0);
		cdc_puts(&tx_a, "SPI slave\r\n");
	} else {
		cdc_puts(&tx_a, "spi master [d]|slave\r\n");
	}
}

/* uart 命令（仅 CDC>=4 的 CH592 启用，CH591 Flash 受限时省略） */
#if GW_CDC_COUNT >= 4
/* 查找 UART 号对应的 TX 引脚配置索引（-1 未配置） */
static int uart_tx_idx(uint8_t uart)
{
	for (int i = 0; i < PIN_COUNT; i++) {
		if (g_pin_cfg[i].func == PIN_FUNC_UART_TX && g_pin_cfg[i].param1 == uart)
			return i;
	}
	return -1;
}

/* 打印 UART 配置（模式/波特率/校验/停止位） */
static void uart_show(int idx)
{
	const pin_cfg_t *c = &g_pin_cfg[idx];
	cdc_puts(&tx_a, "UART");
	cdc_put_i32(&tx_a, c->param1);
	cdc_puts(&tx_a, (c->param2 & 1) == PIN_UART_MODE_FRAME ? " frame" : " forward");
	cdc_puts(&tx_a, " baud=");
	cdc_put_u32(&tx_a, uart_baud_value(c->arg1));
	uint8_t p = (c->param2 >> 1) & 3;
	cdc_puts(&tx_a, p == 1 ? " odd" : p == 2 ? " even" : " none");
	cdc_puts(&tx_a, " stop=");
	cdc_put_i32(&tx_a, ((c->param2 >> 3) & 1) + 1);
#if GW_CDC_DYNAMIC
	cdc_puts(&tx_a, " bind=");
	cdc_put_i32(&tx_a, c->flags & 0x03);
#endif
	cdc_puts(&tx_a, "\r\n");
}

/* uart 命令：
 *   uart N                       查看 UART N 配置
 *   uart N mode forward|frame    设置模式
 *   uart N baud <值>             设置波特率（9600..921600）
 *   uart N parity none|odd|even  设置校验
 *   uart N stop 1|2              设置停止位
 * 修改后自动应用并保存。 */
static void cmd_uart(const char *arg)
{
	int n = atoi(arg);
	int idx = (n >= 0 && n <= 3) ? uart_tx_idx((uint8_t)n) : -1;
	if (idx < 0) { cdc_puts(&tx_a, "uart N [mode|baud|parity|stop] [val]\r\n"); return; }
	/* 跳过 UART 号，取子命令 */
	while (*arg == ' ') arg++;
	while (*arg >= '0' && *arg <= '9') arg++;
	while (*arg == ' ') arg++;
	if (*arg == '\0') { uart_show(idx); return; }   /* 仅查看 */

	pin_cfg_t *c = &g_pin_cfg[idx];
	if (strncmp(arg, "mode ", 5) == 0) {
		const char *v = arg + 5;
		if (strncmp(v, "frame", 5) == 0) c->param2 = (c->param2 & ~1) | PIN_UART_MODE_FRAME;
		else if (strncmp(v, "forward", 7) == 0) c->param2 &= ~1;
		else goto usage;
	} else if (strncmp(arg, "baud ", 5) == 0) {
		c->arg1 = (uint8_t)uart_baud_index((uint32_t)atoi(arg + 5));
	} else if (strncmp(arg, "parity ", 7) == 0) {
		const char *v = arg + 7;
		uint8_t p;
		if (strncmp(v, "odd", 3) == 0) p = PIN_UART_PARITY_ODD;
		else if (strncmp(v, "even", 4) == 0) p = PIN_UART_PARITY_EVEN;
		else if (strncmp(v, "none", 4) == 0) p = PIN_UART_PARITY_NONE;
		else goto usage;
		c->param2 = (uint8_t)((c->param2 & ~0x06) | (p << 1));
	} else if (strncmp(arg, "stop ", 5) == 0) {
		int s = atoi(arg + 5);
		if (s != 1 && s != 2) goto usage;
		c->param2 = (uint8_t)((c->param2 & ~0x08) | ((s - 1) << 3));
#if GW_CDC_DYNAMIC
	} else if (strncmp(arg, "bind ", 5) == 0) {
		int b = atoi(arg + 5);   /* 0=自动, 1..N=CDC-B/C/D */
		if (b < 0 || b > GW_FWD_MAX) goto usage;
		c->flags = (uint8_t)((c->flags & ~0x03) | (b & 0x03));
#endif
	} else {
usage:
		cdc_puts(&tx_a, "uart N [mode|baud|parity|stop] [val]\r\n");
		return;
	}
	pinmux_apply(idx);
	int sr = settings_save();
	uart_show(idx);
	cdc_puts(&tx_a, sr == 0 ? "saved\r\n" : "save fail\r\n");
}
#endif /* GW_CDC_COUNT >= 4 (uart 命令) */

static void cmd_swio(const char *arg)
{
	int pin = atoi(arg);
	swio_set_pin(pin);
	cdc_puts(&tx_a, "swio pin=");
	cdc_put_i32(&tx_a, pin);
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_swiohs(void)
{
	uint32_t id = 0;
	int r = swio_handshake(&id);
	cdc_puts(&tx_a, "swiohs r=");
	cdc_put_i32(&tx_a, r);
	cdc_puts(&tx_a, " id=");
	/* 8 位十六进制 */
	for (int i = 7; i >= 0; i--) {
		uint8_t nib = (uint8_t)((id >> (i * 4)) & 0xF);
		char c = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
		tx_push(&tx_a, (const uint8_t *)&c, 1);
	}
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_swiochip(void)
{
	uint32_t id = 0;
	int r = swio_read_chip_id_pub(&id);
	cdc_puts(&tx_a, "swiochip r=");
	cdc_put_i32(&tx_a, r);
	cdc_puts(&tx_a, " id=0x");
	for (int i = 1; i >= 0; i--) {
		uint8_t nib = (uint8_t)((id >> (i * 4)) & 0xF);
		char c = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
		tx_push(&tx_a, (const uint8_t *)&c, 1);
	}
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_swioreset(void)
{
	swio_reset_target();
	cdc_puts(&tx_a, "swio reset\r\n");
}

/* ---------------------------------------------------------------------------
 * SWIO 流式烧录（终端口二进制接收模式）
 * 用法：swioflash <addr> <len>  然后紧跟 len 字节原始固件数据
 * 例：  swioflash 0x08000000 12345   （随后发送 12345 字节）
 * ------------------------------------------------------------------------- */
static uint32_t flash_remaining = 0;   /* 剩余待收字节 */
static uint32_t flash_addr = 0;

static void cmd_swioflash(const char *arg)
{
	/* 解析 addr len */
	while (*arg == ' ') arg++;
	uint32_t addr = 0;
	if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
		arg += 2;
		while ((*arg >= '0' && *arg <= '9') || (*arg >= 'a' && *arg <= 'f') || (*arg >= 'A' && *arg <= 'F')) {
			uint8_t d = (uint8_t)(*arg <= '9' ? *arg - '0' : (*arg | 0x20) - 'a' + 10);
			addr = (addr << 4) | d; arg++;
		}
	} else {
		while (*arg >= '0' && *arg <= '9') { addr = addr * 10 + (uint32_t)(*arg - '0'); arg++; }
	}
	while (*arg == ' ') arg++;
	uint32_t len = 0;
	while (*arg >= '0' && *arg <= '9') { len = len * 10 + (uint32_t)(*arg - '0'); arg++; }

	if (!len) { cdc_puts(&tx_a, "usage: swioflash <addr> <len>\r\n"); return; }

	int r = swio_stream_begin(addr, len);
	if (r != SWIO_OK) {
		cdc_puts(&tx_a, "flash begin fail r=");
		cdc_put_i32(&tx_a, r);
		cdc_puts(&tx_a, "\r\n");
		return;
	}
	flash_addr = addr;
	flash_remaining = len;
	cdc_puts(&tx_a, "ready ");   /* 提示：随后发送二进制数据 */
	cdc_put_u32(&tx_a, len);
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_echo(const char *arg)
{
	cdc_puts(&tx_a, arg);
	cdc_puts(&tx_a, "\r\n");
}

static void cmd_ble(const char *arg)
{
	if (!arg || *arg == '\0') {
		cdc_puts(&tx_a, "BLE ");
		cdc_puts(&tx_a, bleapp_role_str());
		cdc_puts(&tx_a, " ");
		cdc_puts(&tx_a, bleapp_state_str());
		cdc_puts(&tx_a, " c=");
		cdc_put_i32(&tx_a, bleapp_is_connected());
		cdc_puts(&tx_a, " a=");
		cdc_put_i32(&tx_a, bleapp_is_advertising());
		cdc_puts(&tx_a, " s=");
		cdc_put_i32(&tx_a, bleapp_is_scanning());
		cdc_puts(&tx_a, "\r\n");
	} else if (strcmp(arg, "central") == 0) {
		cdc_puts(&tx_a, bleapp_switch_central() == 0 ? "->central\r\n" : "fail\r\n");
	} else if (strcmp(arg, "peripheral") == 0) {
		cdc_puts(&tx_a, bleapp_switch_peripheral() == 0 ? "->peripheral\r\n" : "fail\r\n");
	} else if (strcmp(arg, "scan") == 0) {
		cdc_puts(&tx_a, bleapp_central_scan() == 0 ? "scanning\r\n" : "fail\r\n");
	} else if (strcmp(arg, "scanstop") == 0) {
		bleapp_central_stop_scan();
		cdc_puts(&tx_a, "stopped\r\n");
	} else if (strcmp(arg, "list") == 0) {
		int n = bleapp_scan_count();
		cdc_puts(&tx_a, "scan:");
		cdc_put_i32(&tx_a, n);
		cdc_puts(&tx_a, "\r\n");
		for (int i = 0; i < n; i++) {
			const bleapp_dev_t *d = bleapp_scan_get(i);
			if (!d) continue;
			cdc_puts(&tx_a, "[");
			cdc_put_i32(&tx_a, i);
			cdc_puts(&tx_a, "] ");
			for (int k = 5; k >= 0; k--) {
				uint8_t b = d->addr[k];
				char c = (char)((b >> 4) < 10 ? '0' + (b >> 4) : 'A' + (b >> 4) - 10);
				tx_push(&tx_a, (const uint8_t *)&c, 1);
				c = (char)((b & 0xF) < 10 ? '0' + (b & 0xF) : 'A' + (b & 0xF) - 10);
				tx_push(&tx_a, (const uint8_t *)&c, 1);
				if (k) cdc_puts(&tx_a, ":");
			}
			cdc_puts(&tx_a, " r=");
			cdc_put_i32(&tx_a, d->rssi);
			cdc_puts(&tx_a, " ");
			cdc_puts(&tx_a, d->name[0] ? d->name : "-");
			cdc_puts(&tx_a, "\r\n");
		}
	} else if (strncmp(arg, "conn ", 5) == 0) {
		cdc_puts(&tx_a, bleapp_central_connect(atoi(arg + 5)) == 0 ? "connecting\r\n" : "fail\r\n");
	} else if (strcmp(arg, "disc") == 0) {
		cdc_puts(&tx_a, bleapp_disconnect() == 0 ? "disconnecting\r\n" : "not conn\r\n");
	} else if (strncmp(arg, "send ", 5) == 0) {
		const char *msg = arg + 5;
		cdc_puts(&tx_a, bleapp_notify((const uint8_t *)msg, (uint16_t)strlen(msg)) == 0 ? "sent\r\n" : "fail\r\n");
	} else {
		cdc_puts(&tx_a, "?ble\r\n");
	}
}

static void cmd_blef(const char *arg)
{
	if (strcmp(arg, "reg") == 0) {
		extern int blemgr_test_reg(void);
		cdc_puts(&tx_a, blemgr_test_reg() == 0 ?
			"reg ok\r\n" : "reg fail\r\n");
	} else {
		cdc_puts(&tx_a, "blef reg\r\n");
	}
}

/* 输出点分十进制 IP */
static void put_ip(txbuf_t *t, uint32_t ip)
{
	cdc_put_u32(t, (ip >> 24) & 0xFF); cdc_puts(t, ".");
	cdc_put_u32(t, (ip >> 16) & 0xFF); cdc_puts(t, ".");
	cdc_put_u32(t, (ip >> 8) & 0xFF);  cdc_puts(t, ".");
	cdc_put_u32(t, ip & 0xFF);
}

/* 解析 "a.b.c.d" → uint32（大端）。失败返回 0 */
static uint32_t parse_ip_arg(const char **pp)
{
	const char *s = *pp;
	uint32_t v = 0;
	int oct = 0, n = 0;
	for (; *s && oct < 4; s++) {
		if (*s >= '0' && *s <= '9') n = n * 10 + (*s - '0');
		else if (*s == '.') { v = (v << 8) | (n & 0xFF); n = 0; oct++; }
		else break;
	}
	if (oct != 3) return 0;
	v = (v << 8) | (n & 0xFF);
	*pp = s;
	return v;
}

static void cmd_dhcp(const char *arg)
{
	if (arg && *arg) {
		/* 设置：dhcp <client_ip> <mask> */
		const char *p = arg;
		uint32_t ip = parse_ip_arg(&p);
		while (*p == ' ') p++;
		uint32_t mask = parse_ip_arg(&p);
		if (!ip || !mask) {
			cdc_puts(&tx_a, "dhcp <ip> <mask>\r\n");
			return;
		}
		dhcps_config(ip, mask);
		/* 同步到配置并持久化 */
		g_cfg.dhcp_client_ip = ip;
		g_cfg.dhcp_mask = mask;
		int r = settings_save();
		cdc_puts(&tx_a, "dhcp set ");
		put_ip(&tx_a, ip);
		cdc_puts(&tx_a, " / ");
		put_ip(&tx_a, mask);
		cdc_puts(&tx_a, r == 0 ? " saved\r\n" : " save fail\r\n");
		return;
	}
	/* 查看状态 */
	uint32_t sip = 0, cip_cfg = 0, msk = 0;
	dhcps_get_config(&sip, &cip_cfg, &msk);
	cdc_puts(&tx_a, "dhcp srv=");
	put_ip(&tx_a, sip);
	cdc_puts(&tx_a, " offer=");
	put_ip(&tx_a, cip_cfg);
	cdc_puts(&tx_a, " mask=");
	put_ip(&tx_a, msk);
	cdc_puts(&tx_a, " cli=");
	cdc_put_i32(&tx_a, dhcps_client_count());
	cdc_puts(&tx_a, " leased=");
	uint32_t ip = dhcps_client_ip();
	if (ip) put_ip(&tx_a, ip); else cdc_puts(&tx_a, "-");
	cdc_puts(&tx_a, "\r\n");
}

/* Web UI 前端 URL：webui [url] */
static void cmd_webui(const char *arg)
{
	if (arg && *arg) {
		int i = 0;
		while (arg[i] && arg[i] != ' ' && i < WEBUI_URL_MAX - 1) {
			g_cfg.webui_url[i] = arg[i];
			i++;
		}
		g_cfg.webui_url[i] = '\0';
		int r = settings_save();
		cdc_puts(&tx_a, "webui url=");
		cdc_puts(&tx_a, g_cfg.webui_url);
		cdc_puts(&tx_a, r == 0 ? " (saved)\r\n" : " (save failed)\r\n");
		return;
	}
	cdc_puts(&tx_a, "webui url=");
	cdc_puts(&tx_a, g_cfg.webui_url[0] ? g_cfg.webui_url : "(embedded)");
	cdc_puts(&tx_a, "\r\n");
}

/* ---- 命令表 ---- */
typedef struct {
	const char *name;      /* 命令名（不含参数） */
	uint8_t  arg;          /* 0=无参数, 1=必需参数, 2=可选参数 */
	void (*fn)(const char *arg);
} term_cmd_t;

static const term_cmd_t term_cmds[] = {
	{ "help",       0, (void (*)(const char *))cmd_help },
	{ "info",       0, (void (*)(const char *))cmd_info },
	{ "net",        0, (void (*)(const char *))cmd_net },
	{ "pins",       0, (void (*)(const char *))cmd_pins },
	{ "save",       0, (void (*)(const char *))cmd_save },
	{ "load",       0, (void (*)(const char *))cmd_load },
	{ "default",    0, (void (*)(const char *))cmd_default },
	{ "gpio",       1, cmd_gpio },
	{ "set",        1, cmd_set },
	{ "adc",        1, cmd_adc },
	{ "frame",      1, cmd_frame },
	{ "spi",        1, cmd_spi },
#if GW_CDC_COUNT >= 4
	{ "uart",       1, cmd_uart },
#endif
	{ "swio",       1, cmd_swio },
	{ "swiohs",     0, (void (*)(const char *))cmd_swiohs },
	{ "swiochip",   0, (void (*)(const char *))cmd_swiochip },
	{ "swioreset",  0, (void (*)(const char *))cmd_swioreset },
	{ "swioflash",  1, cmd_swioflash },
	{ "echo",       1, cmd_echo },
	{ "ble",        1, cmd_ble },
	{ "blef",       1, cmd_blef },
	{ "dhcp",       2, cmd_dhcp },
	{ "webui",      2, cmd_webui },
};
#define TERM_CMD_COUNT (sizeof(term_cmds) / sizeof(term_cmds[0]))

static void term_run(const char *line)
{
	for (unsigned i = 0; i < TERM_CMD_COUNT; i++) {
		const term_cmd_t *c = &term_cmds[i];
		size_t n = strlen(c->name);
		if (strncmp(line, c->name, n) != 0) continue;
		if (c->arg == 0) {
			if (line[n] != '\0') continue;   /* 不接受参数 */
			c->fn(NULL);
		} else if (c->arg == 1) {
			if (line[n] != ' ') continue;    /* 必需参数 */
			c->fn(line + n + 1);
		} else {
			/* 可选参数：有空格则传参数，否则传空串 */
			if (line[n] == '\0') c->fn("");
			else if (line[n] == ' ') c->fn(line + n + 1);
			else continue;
		}
		return;
	}
	cdc_puts(&tx_a, "?cmd (help)\r\n");
}

static void term_handle_char(int ch)
{
	/* 二进制烧录模式：所有字节直接喂给 SWIO 流 */
	if (flash_remaining > 0) {
		uint8_t b = (uint8_t)ch;
		int r = swio_stream_data(&b, 1);
		if (r != SWIO_OK) {
			swio_stream_end();
			flash_remaining = 0;
			cdc_puts(&tx_a, "\r\nflash data fail r=");
			cdc_put_i32(&tx_a, r);
			cdc_puts(&tx_a, "\r\n> ");
			return;
		}
		if (--flash_remaining == 0) {
			/* 收满 → 收尾 */
			int r2 = swio_stream_end();
			cdc_puts(&tx_a, r2 == SWIO_OK ? "\r\nflash OK\r\n> " : "\r\nflash end fail\r\n> ");
		}
		return;
	}

	if (ch == '\r' || ch == '\n') {
		term_line[term_pos] = '\0';
		cdc_puts(&tx_a, "\r\n");
		if (term_pos > 0) term_run(term_line);
		cdc_puts(&tx_a, "> ");
		term_pos = 0;
	} else if (ch == 0x7F || ch == 0x08) {
		if (term_pos > 0) { term_pos--; cdc_puts(&tx_a, "\b \b"); }
	} else if (term_pos < (int)sizeof(term_line) - 1) {
		term_line[term_pos++] = (char)ch;
		uint8_t c = (uint8_t)ch;
		tx_push(&tx_a, &c, 1);   /* 回显 */
	}
}

/* ===========================================================================
 * 主循环各步骤
 * =========================================================================== */
static void netif_poll(void)
{
	/* RNDIS 收到的帧注入 LWIP */
	if (rndis_frame_ready) {
		rndis_frame_ready = 0;
		const uint8_t *frame;
		uint32_t flen;
		if (rndis_data_out(rndis_rx_buf, rndis_rx_frame_len, &frame, &flen) == 0) {
			struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)flen, PBUF_POOL);
			if (p) {
				pbuf_take(p, frame, (u16_t)flen);
				if (rndis_netif.input(p, &rndis_netif) != ERR_OK) {
					pbuf_free(p);
				}
			}
		}
	}
}

/* 查找某 UART 号对应的 TX 引脚配置（用于读模式/波特率等参数）。
 * 返回 NULL 表示该 UART 未配置。 */
static const pin_cfg_t *uart_tx_cfg(uint8_t uart)
{
	for (int i = 0; i < PIN_COUNT; i++) {
		if (g_pin_cfg[i].func == PIN_FUNC_UART_TX && g_pin_cfg[i].param1 == uart)
			return &g_pin_cfg[i];
	}
	return NULL;
}

/* 帧链路轮询：仅处理配置为「帧模式」的 UART
 * 阶段 6：UART0~UART3 中 param2 == PIN_UART_MODE_FRAME 的通道 */
static void frame_poll(void)
{
	static const uint8_t uart_map[4] = { FRAMELINK_UART0, FRAMELINK_UART1,
	                                     FRAMELINK_UART2, FRAMELINK_UART3 };
	uint8_t buf[64];

	for (uint8_t u = 0; u < 4; u++) {
		const pin_cfg_t *c = uart_tx_cfg(u);
		if (!c || (c->param2 & 1) != PIN_UART_MODE_FRAME) continue;   /* 仅帧模式 */
		int n = periph_uart_read(u, buf, sizeof(buf));
		if (n > 0) {
			framelink_rx((framelink_ch_t)uart_map[u], buf, (uint32_t)n);
		}
	}

	/* SPI 从模式接收 */
	framelink_poll_spi();
}

/* UART 号 → CDC 转发缓冲
 *   固定模式（GW_CDC_DYNAMIC=0）：数组索引 = UART 号
 *     CH592：UART0→CDC-B, UART1→CDC-C, UART2→CDC-D
 *     CH591：UART0→CDC-B, UART1→CDC-C
 *   动态模式（GW_CDC_DYNAMIC=1）：按 pin_cfg.flags 的绑定号查找（0=未绑定）
 * 越界/未绑定返回 NULL。 */
#if GW_CDC_DYNAMIC
/* 绑定号（1..GW_FWD_MAX）→ 转发缓冲索引 */
static int uart_bind_idx(uint8_t u)
{
	const pin_cfg_t *c = uart_tx_cfg(u);
	if (!c) return -1;
	uint8_t b = c->flags & 0x03;   /* bit0-1 = 绑定号 */
	if (b == 0 || b > GW_FWD_MAX) return -1;
	return (int)b - 1;
}
static void *uart_tx_tb(uint8_t u)
{
	int i = uart_bind_idx(u);
	return (i >= 0) ? &tx_f[i] : NULL;
}
#else
static void *uart_tx_tb(uint8_t u)
{
	return (u < GW_FWD_MAX) ? &tx_f[u] : NULL;
}
#endif

/* 查找绑定到指定转发口索引（0..GW_FWD_MAX-1）的 UART 号；-1 未绑定 */
#if GW_CDC_DYNAMIC
static int fwd_idx_uart(uint8_t idx)
{
	uint8_t bind = idx + 1;   /* 绑定号 = 索引 + 1 */
	for (uint8_t u = 0; u < 4; u++) {
		const pin_cfg_t *c = uart_tx_cfg(u);
		if (c && (c->flags & 0x03) == bind) return (int)u;
	}
	return -1;
}
#endif

/* CDC 转发轮询：仅处理配置为「转发模式」的 UART
 * UART → 对应 CDC；反向在 data_poll 中处理 */
static void uart_fwd_poll(void)
{
	uint8_t buf[64];
	for (uint8_t u = 0; u < 4; u++) {
		const pin_cfg_t *c = uart_tx_cfg(u);
		if (!c || (c->param2 & 1) != PIN_UART_MODE_FORWARD) continue;  /* 仅转发模式 */
		void *tb = uart_tx_tb(u);
		if (!tb) continue;   /* 该 UART 未绑定 CDC */
		int n = periph_uart_read(u, buf, sizeof(buf));
		if (n > 0) tx_push(tb, buf, (uint32_t)n);   /* UART → CDC */
	}
}

static void term_poll(void)
{
	int ch;
	while ((ch = rb_pop(&rb_a)) >= 0) {
		term_handle_char(ch);
	}
}

/* CDC 数据口 → UART 转发（按绑定关系） */
static void data_poll(void)
{
	for (uint8_t i = 0; i < GW_FWD_MAX; i++) {
		void *rb = &rb_f[i];
#if GW_CDC_DYNAMIC
		int u = fwd_idx_uart(i);        /* 反查绑定的 UART */
		if (u < 0) continue;
#else
		uint8_t u = i;                  /* 固定：索引即 UART 号 */
#endif
		const pin_cfg_t *cfg = uart_tx_cfg((uint8_t)u);
		if (!cfg || (cfg->param2 & 1) != PIN_UART_MODE_FORWARD) continue;  /* 仅转发模式 */
		int ch;
		while ((ch = rb_pop(rb)) >= 0) {
			uint8_t c = (uint8_t)ch;
			periph_uart_write((uint8_t)u, &c, 1);
		}
	}
}

/* ===========================================================================
 * main
 * =========================================================================== */
int main(void)
{
	SystemInit();
	USBFSSetup();
	Delay_Ms(500);

	/* 引脚复用：加载配置并应用 */
	pinmux_init();

	/* 外设初始化 */
	periph_pwm_init();
	periph_adc_init();

	/* 帧链路初始化 + 注册命令处理器 */
	framelink_init();
	framehdl_init();

	/* 网络路由初始化 */
	netroute_init();

	/* LWIP 初始化（NO_SYS 模式） */
	lwip_init();

	ip4_addr_t ip, mask, gw;
	IP4_ADDR(&ip,   192, 168, 7, 1);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw,   192, 168, 7, 1);

	netif_add(&rndis_netif, &ip, &mask, &gw, NULL,
	          rndis_netif_init_cb, ethernet_input);
	netif_set_default(&rndis_netif);
	netif_set_up(&rndis_netif);

	rndis_init();

	/* 启动 HTTP 服务器 */
	httpd_init();

	/* 启动 DHCP 服务器：从配置读取派发 IP 与掩码（不派发网关）
	 * 配置在 pinmux_init → settings_load 时已加载到 g_cfg */
	dhcps_start(&rndis_netif, g_cfg.dhcp_server_ip,
	            g_cfg.dhcp_client_ip, g_cfg.dhcp_mask);

	/* BLE 初始化（GAP + GATT 透传服务） */
	bleapp_init();

	/* BLE 帧命令管理器（应用芯片注册服务/透传） */
	blemgr_init();

	cdc_puts(&tx_a, "\r\nCH591 Gateway (nosys)\r\n");
#if GW_CDC_COUNT >= 4
	cdc_puts(&tx_a, "Port A: terminal | B/C/D: UART0/1/2 | RNDIS: net\r\n");
#else
	cdc_puts(&tx_a, "Port A: terminal | B/C: UART0/1 | RNDIS: net\r\n");
#endif
	cdc_puts(&tx_a, "Type 'help' for commands.\r\n> ");

	/* 主循环 */
	while (1) {
		sys_nosys_tick();        /* 更新时间 */
		sys_check_timeouts();    /* LWIP 超时 */
		netif_poll();            /* RNDIS 接收注入 */
		frame_poll();            /* 帧链路轮询（串口）*/
		uart_fwd_poll();         /* CDC 转发轮询（串口→CDC）*/
		tx_flush(&tx_a, EP_A);   /* flush 发送队列 */
		/* 转发口 flush：端点号数组（索引 = UART 号） */
		static const uint8_t fwd_ep[GW_FWD_MAX] = { EP_B, EP_C
#if GW_CDC_COUNT >= 4
			, EP_D
#endif
		};
		for (uint8_t i = 0; i < GW_FWD_MAX; i++) tx_flush(&tx_f[i], fwd_ep[i]);
		term_poll();             /* 终端命令 */
		data_poll();             /* CDC → UART 转发 */
		bleapp_process();        /* BLE TMOS 事件 */
		blemgr_poll();           /* BLE 帧管理（ACK 超时） */
		httpd_poll();            /* HTTP 空闲连接超时 */
	}
}
