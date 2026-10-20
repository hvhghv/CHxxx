/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_test.c
 * Description        : CH572/CH59x 纯 BLE 收发回传测试
 *
 * 功能：
 *   - BLE 从机（Peripheral）广播，设备名 CH572-BLE
 *   - GATT 透传服务 0xFFF0
 *       RX 0xFFF1 (Write / WriteWithoutResponse)  手机/浏览器 → 设备
 *       TX 0xFFF2 (Notify)                        设备 → 手机/浏览器
 *   - 收到什么就原样通知回去（echo / 回环测试）
 *   - 不依赖 USB，纯 BLE 收发
 *
 * 初始化顺序严格对齐 WCH 官方 Peripheral 例程：
 *   CH57x_BLEInit -> HAL_Init -> GAPRole_PeripheralInit
 *   -> TMOS_ProcessEventRegister(handler)
 *   -> GGS/GATTServApp_AddService -> 设置参数
 *   -> tmos_set_event(START_DEVICE_EVT)
 *   -> 事件里调用 GAPRole_PeripheralStartDevice
 *******************************************************************************/

#include "ch32fun.h"
#include "ble_config.h"
#include "ble_profile.h"
#include <string.h>

/* BLE HAL（由 sdk/<chip>/ble/hal/ble_hal.c 提供） */
#if defined(CH57x)
void CH57x_BLEInit(void);
#define BLE_LIB_INIT()   CH57x_BLEInit()
#elif defined(CH58x)
void CH58x_BLEInit(void);
#define BLE_LIB_INIT()   CH58x_BLEInit()
#elif defined(CH59x)
void CH59x_BLEInit(void);
#define BLE_LIB_INIT()   CH59x_BLEInit()
#else
#error "未定义芯片宏（CH57x / CH58x / CH59x）"
#endif
void HAL_Init(void);

/* ===========================================================================
 * printf 重定向（本项目无 USB/UART 输出，直接丢弃）
 * ===========================================================================
 * BLE 库内部会调用 printf（如版本校验失败提示），
 * ch32fun 的 printf 依赖 _write，这里提供空实现避免链接错误。
 */
int _write(int fd, const char *buf, int len)
{
	(void)fd; (void)buf;
	return len;
}

int putchar(int c)
{
	return c;
}

/* ===========================================================================
 * BLE 内存堆
 * =========================================================================== */
__attribute__((aligned(4)))
uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* ===========================================================================
 * BLE 状态
 * =========================================================================== */
static tmosTaskID  ble_task_id;
static uint16_t    ble_conn_handle = GAP_CONNHANDLE_INIT;
static volatile int ble_connected;
static volatile int ble_advertising;

/* 统计 */
static volatile uint32_t rx_bytes;      /* 从对端收到 */
static volatile uint32_t tx_bytes;      /* 通知给对端 */
static volatile uint32_t conn_count;    /* 连接次数 */
static volatile int      last_state = -1;

/* ===========================================================================
 * 回传发送队列（BLE 通知是异步的，用队列平滑突发数据）
 * =========================================================================== */
#if defined(CH57x)
#define TXQ_SIZE 64
#else
#define TXQ_SIZE 256
#endif

typedef struct {
	volatile uint8_t  buf[TXQ_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
} txq_t;

static txq_t txq;

static void txq_push(const uint8_t *data, uint32_t len)
{
	uint32_t head = txq.head;
	uint32_t tail = txq.tail;
	for (uint32_t i = 0; i < len; i++) {
		uint32_t next = (head + 1) % TXQ_SIZE;
		if (next == tail) break;      /* 满，丢弃 */
		txq.buf[head] = data[i];
		head = next;
	}
	txq.head = head;
}

/* 从队列取一包（最多 BLE_MAX_LEN）发出；返回 1 表示发出了数据 */
static int txq_flush_one(void)
{
	if (txq.head == txq.tail) return 0;

	uint32_t n = (txq.head - txq.tail + TXQ_SIZE) % TXQ_SIZE;
	if (n > BLE_MAX_LEN) n = BLE_MAX_LEN;

	uint8_t tmp[BLE_MAX_LEN];
	uint32_t tail = txq.tail;
	for (uint32_t i = 0; i < n; i++) {
		tmp[i] = txq.buf[tail];
		tail = (tail + 1) % TXQ_SIZE;
	}

	if (ble_profile_notify(tmp, (uint16_t)n) == SUCCESS) {
		txq.tail = tail;
		tx_bytes += n;
		return 1;
	}
	return 0;   /* 端点忙，下次再试 */
}

/* ===========================================================================
 * GAP 回调
 * =========================================================================== */
static void ble_state_cb(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
	last_state = (int)newState;

	switch (newState) {
	case GAPROLE_STARTED:
		ble_advertising = 0;
		break;

	case GAPROLE_ADVERTISING:
		ble_advertising = 1;
		break;

	case GAPROLE_CONNECTED:
		ble_conn_handle = pEvent->linkCmpl.connectionHandle;
		ble_connected = 1;
		ble_advertising = 0;
		conn_count++;
		break;

	case GAPROLE_WAITING:
		ble_connected = 0;
		ble_conn_handle = GAP_CONNHANDLE_INIT;
		/* 断开后清空待发队列 */
		txq.head = txq.tail = 0;
		break;

	case GAPROLE_ERROR:
		break;

	default:
		break;
	}
}

static gapRolesCBs_t ble_role_cb = { ble_state_cb, NULL, NULL };

/* ===========================================================================
 * 收到对端写入 → 原样回传（echo）
 * =========================================================================== */
static void ble_rx_from_peer(const uint8_t *data, uint16_t len)
{
	rx_bytes += len;
	/* 回环：把收到的数据放回发送队列 */
	txq_push(data, len);
}

/* ===========================================================================
 * BLE 事件处理
 * =========================================================================== */
#define BLE_START_DEVICE_EVT   0x0001

static uint16_t ble_process_event(uint8_t task_id, uint16_t events)
{
	if (events & SYS_EVENT_MSG) {
		uint8_t *pMsg = tmos_msg_receive(task_id);
		if (pMsg) tmos_msg_deallocate(pMsg);
		return (events ^ SYS_EVENT_MSG);
	}

	if (events & BLE_START_DEVICE_EVT) {
		/* 与官方例程一致：在事件里启动设备 */
		GAPRole_PeripheralStartDevice(task_id, NULL, &ble_role_cb);
		return (events ^ BLE_START_DEVICE_EVT);
	}

	return 0;
}

/* ===========================================================================
 * 广播数据
 * =========================================================================== */
static void ble_start_advertising(void)
{
	/* 广播包：Flags + UUID 0xFFF0 */
	static uint8_t advData[] = {
		0x02, 0x01, 0x06,               /* Flags: LE General + BR/EDR Not Supported */
		0x03, 0x03, 0xF0, 0xFF,         /* 16-bit UUID 0xFFF0 */
	};

	/* 扫描响应包：完整设备名（len = 1 + 9 = 10） */
	static uint8_t scanRspData[] = {
		0x0A, 0x09, 'C', 'H', '5', '7', '2', '-', 'B', 'L', 'E',
	};

	uint8_t advEnable = 1;

	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
	GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advData), advData);
	GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);

	/* 首次：通过事件启动设备；之后：仅重新使能广播 */
	if (last_state < 0) {
		tmos_set_event(ble_task_id, BLE_START_DEVICE_EVT);
	}
}

/* ===========================================================================
 * BLE 初始化
 * =========================================================================== */
static void ble_init(void)
{
	/* 1. 协议栈 + HAL */
	BLE_LIB_INIT();
	HAL_Init();

	/* 2. GAP 外设角色初始化（关键，缺失会导致 StartDevice 失败） */
	GAPRole_PeripheralInit();

	/* 3. 注册事件处理（必须传函数指针，不能 NULL） */
	ble_task_id = TMOS_ProcessEventRegister(ble_process_event);

	/* 4. GAP / GATT 基础服务 */
	GGS_AddService(GATT_ALL_SERVICES);
	GATTServApp_AddService(GATT_ALL_SERVICES);

	/* 5. 设备名（Web Bluetooth 的 device.name 来源） */
	{
		static uint8_t devName[] = "CH572-BLE";
		GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(devName) - 1, devName);
	}

	/* 6. 注册透传 GATT 服务 */
	ble_profile_set_rx_cb(ble_rx_from_peer);
	ble_profile_init();

	/* 7. GAP 角色参数（官方例程必需） */
	{
		uint8_t  advEnable = 1;
		uint16_t minInt = 160;   /* 100ms */
		uint16_t maxInt = 160;

		GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advEnable);
		GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &minInt);
		GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &maxInt);
	}

	/* 8. 广播间隔 */
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, 160);
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, 160);
	GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, 1);

	ble_connected = 0;
	ble_advertising = 0;
}

/* ===========================================================================
 * 主函数
 * =========================================================================== */
int main(void)
{
	SystemInit();

	ble_init();

	/* 上电即开始广播（无需任何外部触发） */
	ble_start_advertising();

	while (1) {
		/* BLE 协议栈 */
		TMOS_SystemProcess();

		/* 回传队列：每轮发一包 */
		if (ble_connected && ble_profile_notify_enabled()) {
			txq_flush_one();
		} else {
			/* 未连接/未使能通知：丢弃积压数据 */
			txq.head = txq.tail = 0;
		}
	}
}
