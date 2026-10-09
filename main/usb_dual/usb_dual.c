/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_dual.c
 * Description        : CH572 三 USB 串口设备
 *
 * 设备枚举为三个独立的 CDC-ACM 虚拟串口：
 *
 *   串口 A「终端」：
 *     - 显示系统信息（芯片、时钟、内存、复位原因）
 *     - 蓝牙控制命令（adv / conn / disc / info / help）
 *
 *   串口 B「蓝牙桥」：
 *     - USB → BLE：串口收到的数据通过 BLE 通知发出
 *     - BLE → USB：BLE 收到的数据写到串口
 *
 *   串口 C「调试口」：
 *     - printf 输出重定向到这里
 *     - scanf 输入从这个串口读取
 *******************************************************************************/

#include "ch32fun.h"
#include "fsusb.h"
#include "ble_config.h"
#include "usb_ble_profile.h"
#include <stdio.h>
#include <string.h>

/* BLE HAL 接口（由 sdk/<chip>/ble/hal/ble_hal.c 提供） */
#if defined(CH57x)
void CH57x_BLEInit(void);
#define BLE_LIB_INIT()   CH57x_BLEInit()
#elif defined(CH59x)
void CH59x_BLEInit(void);
#define BLE_LIB_INIT()   CH59x_BLEInit()
#else
#error "未定义芯片宏（CH57x / CH59x）"
#endif
void HAL_Init(void);

/* BLE 连接状态（定义在下方，usb_term_send 需要提前引用） */
static volatile int ble_connected;

/* ===========================================================================
 * BLE 内存堆
 * =========================================================================== */
__attribute__((aligned(4)))
uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* ===========================================================================
 * 端点定义
 * =========================================================================== */
#define EP_TERM_NOTIFY   1      /* CDC-A 通知 */
#define EP_TERM_OUT      2      /* CDC-A 数据接收 */
#define EP_TERM_IN       3      /* CDC-A 数据发送 */

/* ===========================================================================
 * 环形缓冲
 * =========================================================================== */
#define RB_SIZE 8

typedef struct {
	volatile uint8_t  buf[RB_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
} ringbuf_t;

static ringbuf_t rb_term;       /* 终端串口接收（含 printf 输入） */

/* 环形缓冲
 * rb_push 可能在 USB 中断上下文调用，rb_pop 在主循环调用。
 * head/tail 是 32 位对齐访问，RISC-V 上单次读写是原子的，
 * 采用「单生产者单消费者」模型：生产者只改 head，消费者只改 tail。 */
static void rb_push(ringbuf_t *rb, const uint8_t *data, uint32_t len)
{
	uint32_t head = rb->head;
	uint32_t tail = rb->tail;
	for (uint32_t i = 0; i < len; i++) {
		uint32_t next = (head + 1) % RB_SIZE;
		if (next == tail) break;    /* 满 */
		rb->buf[head] = data[i];
		head = next;
	}
	rb->head = head;                /* 一次性提交 */
}

static int rb_pop(ringbuf_t *rb)
{
	uint32_t tail = rb->tail;
	if (tail == rb->head) return -1;
	int b = rb->buf[tail];
	rb->tail = (tail + 1) % RB_SIZE;
	return b;
}

static int rb_count(ringbuf_t *rb)
{
	return (int)((rb->head - rb->tail + RB_SIZE) % RB_SIZE);
}

/* ===========================================================================
 * USB 发送（非阻塞）
 * ===========================================================================
 * 设计要点：
 *   1. 绝不阻塞等待。USBFS_SendEndpointNEW 返回非 0 就直接放弃本次数据，
 *      由调用方（主循环）下次重试。这样不会卡住 BLE 协议栈。
 *   2. 每个端点一个发送环形缓冲，主循环统一 flush。
 *   3. 返回值：0=已发出，-1=端点忙（稍后重试）。
 *
 * 为什么不用 Delay_Ms 重试：
 *   BLE 库会重配 SysTick，且主循环被阻塞会导致 BLE 断连、USB 看起来「死掉」。
 */
typedef struct {
	volatile uint8_t  buf[4];
	volatile uint32_t head;
	volatile uint32_t tail;
} txbuf_t;

#define TX_SIZE 4

static txbuf_t tx_term;    /* 终端 A 发送队列 */

static void tx_push(txbuf_t *t, const uint8_t *data, uint32_t len)
{
	uint32_t head = t->head;
	uint32_t tail = t->tail;
	for (uint32_t i = 0; i < len; i++) {
		uint32_t next = (head + 1) % TX_SIZE;
		if (next == tail) break;    /* 满，丢弃剩余（调用方负责分块） */
		t->buf[head] = data[i];
		head = next;
	}
	t->head = head;
}

/* 队列剩余空间 */
static uint32_t tx_space(txbuf_t *t)
{
	uint32_t used = (t->head - t->tail + TX_SIZE) % TX_SIZE;
	return TX_SIZE - 1 - used;
}

/* 尝试发送队列里的数据（每次最多发 64 字节，不阻塞） */
static void tx_flush(txbuf_t *t, int ep)
{
	if (t->head == t->tail) return;          /* 空 */
	uint32_t n = (t->head - t->tail + TX_SIZE) % TX_SIZE;
	if (n > 64) n = 64;

	/* 需要连续内存，这里逐字节拷贝到临时缓冲 */
	uint8_t tmp[64];
	uint32_t tail = t->tail;
	for (uint32_t i = 0; i < n; i++) {
		tmp[i] = t->buf[tail];
		tail = (tail + 1) % TX_SIZE;
	}

	if (USBFS_SendEndpointNEW(ep, tmp, (int)n, 1) == 0) {
		t->tail = tail;                      /* 提交成功才推进 tail */
	}
	/* 失败则保留数据，下次再试 */
}

static void usb_term_send(const uint8_t *buf, uint32_t len)
{
	/* 分块发送：队列空间不足时先 flush 腾空间，再推入。
	 * 这样即使数据长度 > TX_SIZE 也能完整发出（不增大队列）。
	 * 注意：flush 只发一包（≤64B）且不阻塞，不会卡死主循环。 */
	uint32_t total = len;
	const uint8_t *p = buf;
	while (len) {
		uint32_t space = tx_space(&tx_term);
		if (space == 0) {
			/* 队列满：flush 一包腾出空间 */
			tx_flush(&tx_term, EP_TERM_IN);
			if (tx_space(&tx_term) == 0) {
				Delay_Ms(1);          /* 端点忙，稍等再试 */
			}
			continue;
		}

		uint32_t n = (len < space) ? len : space;
		tx_push(&tx_term, p, n);
		p += n;
		len -= n;
	}

	/* 同时通过 BLE 通知发出（若已连接且使能通知） */
	if (ble_connected && usb_ble_profile_notify_enabled()) {
		uint32_t off = 0;
		while (off < total) {
			uint32_t n = total - off;
			if (n > USB_BLE_MAX_LEN) n = USB_BLE_MAX_LEN;
			usb_ble_profile_notify(buf + off, (uint16_t)n);
			off += n;
		}
	}
}

static void usb_term_puts(const char *s)
{
	usb_term_send((const uint8_t *)s, (uint32_t)strlen(s));
}

/* ===========================================================================
 * printf / scanf 重定向（串口 A）
 * ===========================================================================
 * 说明：因 CH572 RAM 限制，已去掉独立的调试口 C 和 BLE 桥串口 B。
 * printf 输出改走串口 A（终端），与命令行共用同一串口。
 * BLE 数据通过 GATT 服务直接收发（见 usb_ble_profile）。
 *
 * 由于 usb_term_send 只是入队（非阻塞），在中断里调用也是安全的，
 * 因此不需要 ISR 检测。仅需确认 USB 已就绪。
 */
static volatile int usb_dbg_ready = 0;   /* main 中枚举完成后置 1 */

int _write(int fd, const char *buf, int len)
{
	(void)fd;
	if (len <= 0) return len;
	if (!usb_dbg_ready) return len;       /* USB 未就绪：丢弃 */
	usb_term_send((const uint8_t *)buf, (uint32_t)len);
	return len;
}

/* putchar：printf("%c") 或直接调用时使用 */
int putchar(int c)
{
	char b = (char)c;
	if (usb_dbg_ready) {
		usb_term_send((const uint8_t *)&b, 1);
	}
	return c;
}

/* _read：供 scanf / fgets 等读取输入（从串口 A 读） */
int _read(int fd, char *buf, int len)
{
	(void)fd;
	int n = 0;
	while (n < len) {
		int c = rb_pop(&rb_term);
		if (c < 0) break;
		buf[n++] = (char)c;
	}
	return n;
}

/* getchar：阻塞式读取一个字符（scanf 内部会用到）
 * 用重试次数做超时（不依赖 SysTick，BLE 会重配它）。
 * 注意：这里会短暂阻塞主循环，仅建议在明确需要 scanf 时使用。 */
int getchar(void)
{
	for (int retry = 0; retry < 5000; retry++) {
		int c = rb_pop(&rb_term);
		if (c >= 0) return c;
		Delay_Ms(1);
	}
	return -1;   /* 超时（约 5 秒），返回 EOF */
}

/* ===========================================================================
 * BLE 状态
 * =========================================================================== */
static tmosTaskID  ble_task_id;
static uint16_t    ble_conn_handle = GAP_CONNHANDLE_INIT;
static volatile int ble_advertising;

/* ---------------------------------------------------------------------------
 * BLE → USB：收到浏览器写入的数据，转发到 USB 串口 B
 * --------------------------------------------------------------------------- */
static void ble_rx_from_browser(const uint8_t *data, uint16_t len)
{
	/* BLE 写入的数据 → 终端缓冲（与串口 A 输入等价） */
	rb_push(&rb_term, data, len);
}

/* ===========================================================================
 * BLE GAP 回调
 * =========================================================================== */
static void ble_state_cb(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
	switch (newState) {
	case GAPROLE_STARTED:
		ble_advertising = 0;
		printf("[BLE] GAPROLE_STARTED\r\n");
		break;
	case GAPROLE_ADVERTISING:
		ble_advertising = 1;
		printf("[BLE] GAPROLE_ADVERTISING\r\n");
		break;
	case GAPROLE_CONNECTED:
		ble_conn_handle = pEvent->linkCmpl.connectionHandle;
		ble_connected = 1;
		ble_advertising = 0;
		printf("[BLE] GAPROLE_CONNECTED\r\n");
		break;
	case GAPROLE_WAITING:
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
		printf("[BLE] GAPROLE_WAITING\r\n");
		break;
	case GAPROLE_ERROR:
		printf("[BLE] GAPROLE_ERROR\r\n");
		break;
	default:
		printf("[BLE] state=%d\r\n", (int)newState);
		break;
	}
}

static gapRolesCBs_t ble_role_cb = { ble_state_cb, NULL, NULL };

/* ===========================================================================
 * BLE 初始化与启动
 * =========================================================================== */

/* 自定义事件：启动设备（与官方例程一致，必须通过 TMOS 事件驱动） */
#define BLE_START_DEVICE_EVT   0x0001

static void ble_start_advertising(void)
{
	/* 广播数据（AD 结构：len, type, data...）
	 * len = 1(type) + N(data)，必须精确匹配，否则广播包非法。 */
	static uint8_t advData[] = {
		0x02, 0x01, 0x06,               /* Flags: LE General + BR/EDR Not Supported */
		0x03, 0x03, 0xF0, 0xFF,         /* 16-bit UUID 0xFFF0 */
	};

	/* 扫描响应数据：放完整设备名（Web Bluetooth 的 device.name 来源之一） */
	static uint8_t scanRspData[] = {
		0x0A, 0x09, 'C', 'H', '5', '7', '2', '-', 'B', 'L', 'E',   /* Complete Local Name: len=10 = 1+9 */
	};

	uint8_t advEnable = 1;   /* TRUE */

	/* 关键：必须设置 GAPROLE_ADVERT_ENABLED，否则广播不会真正开启 */
	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
	GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advData), advData);
	GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);

	/* 通过 TMOS 事件延迟启动（与官方例程一致）。
	 * 直接在 ble_init 里调用 GAPRole_PeripheralStartDevice 可能因
	 * 协议栈未就绪而失败，必须由事件循环驱动。 */
	tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);
}

/* ---------------------------------------------------------------------------
 * BLE 任务事件处理（TMOS 回调）
 * --------------------------------------------------------------------------- */
static uint16_t ble_process_event(uint8_t task_id, uint16_t events)
{
	/* 系统消息 */
	if (events & SYS_EVENT_MSG) {
		uint8_t *pMsg = tmos_msg_receive(task_id);
		if (pMsg) tmos_msg_deallocate(pMsg);
		return (events ^ SYS_EVENT_MSG);
	}

	/* 启动设备 */
	if (events & BLE_START_DEVICE_EVT) {
		GAPRole_PeripheralStartDevice(task_id, NULL, &ble_role_cb);
		return (events ^ BLE_START_DEVICE_EVT);
	}

	return 0;
}

static void ble_init(void)
{
	BLE_LIB_INIT();
	HAL_Init();

	/* 初始化 GAP 外设角色（必须！否则 GAPRole_PeripheralStartDevice 会失败） */
	GAPRole_PeripheralInit();

	/* 注册事件处理函数（不能传 NULL！否则 GAP 事件无人处理，广播不会启动） */
	ble_task_id = TMOS_ProcessEventRegister(ble_process_event);

	/* 注册 GAP / GATT 基础服务（Web Bluetooth 需要） */
	GGS_AddService(GATT_ALL_SERVICES);          /* GAP 服务（含 Device Name 0x2A00） */
	GATTServApp_AddService(GATT_ALL_SERVICES);  /* GATT 服务 */

	/* 设置 GAP 设备名（Web Bluetooth 的 device.name 来自这里） */
	{
		uint8_t devName[] = "CH572-BLE";
		GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(devName) - 1, devName);
	}

	/* 注册 USB ↔ BLE 透传 GATT 服务（0xFFF0） */
	usb_ble_profile_set_rx_cb(ble_rx_from_browser);
	usb_ble_profile_init();

	/* 广播间隔 100ms */
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, 160);
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, 160);

	ble_connected = 0;
	ble_advertising = 0;
}

static void ble_stop(void)
{
	if (ble_connected) {
		GAPRole_TerminateLink(ble_conn_handle);
	}
}

/* ===========================================================================
 * 系统信息（串口 A）
 * =========================================================================== */

/* 链接脚本提供的符号（见 CH572_DEV/core/ch32fun.ld） */
extern uint32_t _sbss, _ebss;           /* .bss 起止 */
extern uint32_t _data_vma, _edata;      /* .data 起止（RAM 中的运行地址） */
extern uint32_t _eusrstack;             /* 栈顶（RAM 末尾） */
extern uint32_t _highcode_vma_start, _highcode_vma_end;  /* highcode 段 */

/* 芯片物理规格 */
#define CHIP_FLASH_SIZE     (240 * 1024)
#define CHIP_RAM_SIZE       (12 * 1024)
#define CHIP_RAM_BASE       0x20000000

/* 计算 ROM（Flash）占用
 * ROM 用量 = .text 结束位置 + .data 的 Flash 加载副本大小
 *   _etext     : .text 段结束地址（Flash 中）
 *   _data_vma  : .data 在 RAM 中的起始
 *   _edata     : .data 在 RAM 中的结束
 * .data 的 Flash 副本紧跟在 .text 之后，大小等于 (_edata - _data_vma)。 */
static uint32_t calc_rom_usage(void)
{
	extern uint32_t _etext;
	uint32_t text_end  = (uint32_t)&_etext;
	uint32_t data_size = (uint32_t)&_edata - (uint32_t)&_data_vma;
	return text_end + data_size;
}

/* 计算 RAM 占用
 * RAM 用量 = .data + .bss + highcode 段
 * 栈空间从 _eusrstack 向下增长，这里单独报告剩余量。 */
static uint32_t calc_ram_usage(void)
{
	uint32_t data_size = (uint32_t)&_edata - (uint32_t)&_data_vma;
	uint32_t bss_size  = (uint32_t)&_ebss  - (uint32_t)&_sbss;
	uint32_t hc_size   = (uint32_t)&_highcode_vma_end - (uint32_t)&_highcode_vma_start;
	return data_size + bss_size + hc_size;
}

static void term_print_info(void)
{
	char buf[128];
	uint32_t rom = calc_rom_usage();
	uint32_t ram = calc_ram_usage();
	uint32_t stack_free;

	usb_term_puts("\r\n=== CH572 System Info ===\r\n");

	snprintf(buf, sizeof(buf), "Chip ID     : 0x%02X\r\n", R8_CHIP_ID);
	usb_term_puts(buf);

	snprintf(buf, sizeof(buf), "Core clock  : %lu Hz\r\n",
	         (unsigned long)FUNCONF_SYSTEM_CORE_CLOCK);
	usb_term_puts(buf);

	/* --- ROM 使用情况 --- */
	snprintf(buf, sizeof(buf), "ROM (Flash) : %lu / %lu B  (%lu%%)\r\n",
	         (unsigned long)rom, (unsigned long)CHIP_FLASH_SIZE,
	         (unsigned long)(rom * 100 / CHIP_FLASH_SIZE));
	usb_term_puts(buf);

	/* --- RAM 使用情况 --- */
	snprintf(buf, sizeof(buf), "RAM         : %lu / %lu B  (%lu%%)\r\n",
	         (unsigned long)ram, (unsigned long)CHIP_RAM_SIZE,
	         (unsigned long)(ram * 100 / CHIP_RAM_SIZE));
	usb_term_puts(buf);

	/* 栈剩余空间（栈从 RAM 顶部向下增长） */
	stack_free = (uint32_t)&_eusrstack - (CHIP_RAM_BASE + ram);
	snprintf(buf, sizeof(buf), "  .data     : %lu B\r\n",
	         (unsigned long)((uint32_t)&_edata - (uint32_t)&_data_vma));
	usb_term_puts(buf);
	snprintf(buf, sizeof(buf), "  .bss      : %lu B\r\n",
	         (unsigned long)((uint32_t)&_ebss - (uint32_t)&_sbss));
	usb_term_puts(buf);
	snprintf(buf, sizeof(buf), "  .highcode : %lu B\r\n",
	         (unsigned long)((uint32_t)&_highcode_vma_end - (uint32_t)&_highcode_vma_start));
	usb_term_puts(buf);
	snprintf(buf, sizeof(buf), "  stack free: %lu B\r\n", (unsigned long)stack_free);
	usb_term_puts(buf);

	/* --- BLE / USB --- */
	snprintf(buf, sizeof(buf), "BLE stack   : %s\r\n",
	         ble_connected ? "connected" : (ble_advertising ? "advertising" : "idle"));
	usb_term_puts(buf);

	snprintf(buf, sizeof(buf), "BLE notify  : %s\r\n",
	         usb_ble_profile_notify_enabled() ? "enabled" : "disabled");
	usb_term_puts(buf);

	snprintf(buf, sizeof(buf), "BLE heap    : %d bytes\r\n", BLE_MEMHEAP_SIZE);
	usb_term_puts(buf);

	usb_term_puts("USB         : dual CDC (terminal + BLE bridge)\r\n");
	usb_term_puts("BLE GATT    : svc 0xFFF0 / RX 0xFFF1 (write) / TX 0xFFF2 (notify)\r\n");

	usb_term_puts("\r\n");
}

static void term_print_help(void)
{
	usb_term_puts("\r\nCommands:\r\n");
	usb_term_puts("  info      - system information\r\n");
	usb_term_puts("  adv       - start BLE advertising\r\n");
	usb_term_puts("  stop      - stop BLE (disconnect)\r\n");
	usb_term_puts("  status    - BLE connection status\r\n");
	usb_term_puts("  dbg       - printf demo (output on this port)\r\n");
	usb_term_puts("  reboot    - software reset (restart firmware)\r\n");
	usb_term_puts("  isp       - show how to enter USB ISP mode\r\n");
	usb_term_puts("  web       - how to use the Web Bluetooth page\r\n");
	usb_term_puts("  help      - this help\r\n\r\n");
}

/* ===========================================================================
 * 终端命令处理（串口 A）
 * =========================================================================== */
static void term_handle_line(char *line)
{
	if (strcmp(line, "info") == 0) {
		term_print_info();
	} else if (strcmp(line, "help") == 0) {
		term_print_help();
	} else if (strcmp(line, "adv") == 0) {
		if (ble_connected) {
			usb_term_puts("Already connected.\r\n");
		} else {
			usb_term_puts("Starting advertising...\r\n");
			ble_start_advertising();
		}
	} else if (strcmp(line, "stop") == 0) {
		usb_term_puts("Stopping BLE...\r\n");
		ble_stop();
	} else if (strcmp(line, "status") == 0) {
		usb_term_puts(ble_connected ? "BLE: connected\r\n" :
		              (ble_advertising ? "BLE: advertising\r\n" : "BLE: idle\r\n"));
	} else if (strcmp(line, "dbg") == 0) {
		/* 演示 printf 输出（走串口 C）
		 * 注意：ch32fun 的 mini_printf 不支持 %f 浮点 */
		usb_term_puts("Sending demo output via printf...\r\n");
		printf("=== printf demo ===\r\n");
		printf("dec  : %d\r\n", 12345);
		printf("hex  : 0x%08X\r\n", 0xDEADBEEF);
		printf("str  : %s\r\n", "hello CH572");
		printf("char : %c\r\n", 'A');
		printf("===================\r\n");
	} else if (strcmp(line, "reboot") == 0) {
		usb_term_puts("Rebooting...\r\n");
		Delay_Ms(50);          /* 等数据发出 */
		NVIC_SystemReset();    /* 软件复位，重新运行固件 */
	} else if (strcmp(line, "isp") == 0) {
		/* CH572 的 USB ISP 进入条件（实测 + 官方配置寄存器分析）：
		 *   - USER_CFG 无 bootloader 引脚选择位（不同于 CH571/573 的 PB22/PB11）
		 *   - bootloader 在上电复位(POR)时检测，软复位无效
		 *   - 必须真正断电重启，且保持触发条件
		 *
		 * 因此软件无法主动进入 ISP，只能给出操作指引。 */
		usb_term_puts("\r\nCH572 USB ISP entry (hardware only):\r\n");
		usb_term_puts("  1. Hold PA1 button\r\n");
		usb_term_puts("  2. Unplug and re-plug USB (true power cycle)\r\n");
		usb_term_puts("  3. Release PA1\r\n");
		usb_term_puts("  -> device enters ISP (VID 4348:55e0)\r\n");
		usb_term_puts("  Then run: .\\build flash\r\n\r\n");
	} else if (strcmp(line, "web") == 0) {
		/* 提示使用 Web Bluetooth 页面 */
		usb_term_puts("\r\nWeb Bluetooth (BLE <-> USB bridge):\r\n");
		usb_term_puts("  1. Type 'adv' here to start BLE advertising\r\n");
		usb_term_puts("  2. Open main/usb_dual/web_ble.html in Chrome/Edge\r\n");
		usb_term_puts("     (must be https:// or http://localhost)\r\n");
		usb_term_puts("  3. Click 'Connect' and pick 'CH572-BLE'\r\n");
		usb_term_puts("  4. Data goes: Browser <-> BLE <-> USB Port B\r\n");
		usb_term_puts("  GATT: svc 0xFFF0, RX 0xFFF1 (write), TX 0xFFF2 (notify)\r\n\r\n");
	} else if (line[0] != 0) {
		usb_term_puts("Unknown command. Type 'help'.\r\n");
	}
}

/* ===========================================================================
 * fsusb 回调
 * =========================================================================== */
void HandleDataOut(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	(void)ctx;
	if (len <= 0 || !data) return;

	switch (endp) {
	case EP_TERM_OUT:
		/* 终端串口：缓冲，主循环解析 */
		rb_push(&rb_term, data, (uint32_t)len);
		break;

	default:
		break;
	}
}

void HandleDataIn(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	(void)ctx; (void)endp; (void)data; (void)len;
}

/* 主机打开串口 A 时置位（收到 SET_CONTROL_LINE_STATE） */
static volatile int term_opened = 0;

int HandleSetupCustom(struct _USBState *ctx, int setup_code)
{
	/* CDC 类请求：SET_LINE_CODING (0x20) / SET_CONTROL_LINE_STATE (0x22)
	 * 返回 -1（USB_ACK）表示已处理，fsusb 会清零长度并 ACK 状态阶段。
	 * 返回正数会被当作「响应数据长度」，导致 OUT 请求只 ACK 部分字节。 */
	if (setup_code == 0x20 || setup_code == 0x22) {
		/* SET_CONTROL_LINE_STATE 的 wValue 低字节 bit0 = DTR */
		if (setup_code == 0x22) {
			uint16_t v = (uint16_t)(ctx->USBFS_IndexValue & 0xFFFF);
			if (v & 0x01) term_opened = 1;
		}
		return -1;
	}
	return 0;
}
int HandleInRequest(struct _USBState *ctx, int endp, uint8_t *data, int len)
{
	(void)ctx; (void)endp; (void)data; (void)len;
	return 0;
}

void HandleUSBInput(int numbytes, uint8_t *data)
{
	rb_push(&rb_term, data, (uint32_t)numbytes);
}

/* ===========================================================================
 * 主函数
 * =========================================================================== */
int main(void)
{
	SystemInit();

	/* 初始化 USB 复合设备 */
	USBFSSetup();

	/* 等待主机完成 USB 枚举（读取设备/配置/字符串描述符）。
	 * BLE 初始化耗时较长，若立即执行会阻塞 USB 中断响应，
	 * 导致主机报「配置描述符无效 / 设备无响应」。
	 * 这里先跑 1 秒 USB 事件循环，确保枚举完成后再启动 BLE。 */
	for (uint32_t t = 0; t < 1000; t++) {
		Delay_Ms(1);
	}

	/* 初始化 BLE */
	ble_init();

	/* USB 已枚举完成，允许 printf 输出到串口 C */
	usb_dbg_ready = 1;

	/* 终端命令行缓冲 */
	char line[128];
	int  pos = 0;

	int welcomed = 0;

	while (1) {
		/* --- BLE 协议栈处理 --- */
		TMOS_SystemProcess();

		/* --- 发送队列 flush（非阻塞）--- */
		tx_flush(&tx_term, EP_TERM_IN);

		/* --- 串口 A：主机打开后发送欢迎信息 --- */
		if (term_opened && !welcomed) {
			welcomed = 1;
			usb_term_puts("\r\nCH572 BLE CDC Device\r\n");
			usb_term_puts("Port A: terminal | BLE: GATT 0xFFF0\r\n");
			usb_term_puts("Type 'help' for commands.\r\n");
			usb_term_puts("> ");
		}

		/* --- 终端串口（A）--- */
		int c;
		while ((c = rb_pop(&rb_term)) >= 0) {
			if (c == '\r' || c == '\n') {
				line[pos] = 0;
				usb_term_puts("\r\n");
				term_handle_line(line);
				pos = 0;
				usb_term_puts("> ");
			} else if (c == 0x08 || c == 0x7F) {
				if (pos > 0) {
					pos--;
					usb_term_puts("\b \b");
				}
			} else if (pos < (int)sizeof(line) - 1) {
				line[pos++] = (char)c;
				usb_term_send((uint8_t *)&c, 1);   /* 回显 */
			}
		}
	}
}
