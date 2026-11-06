# CH5xx / CH32 多芯片固件分层架构设计

> 状态：**设计稿 v2（已确认需求，进入实现）**
> 目标芯片（首期）：CH592 / CH591（TSSOP16，10 GPIO）
> 扩展顺序：**CH59x（CH591/CH592）→ CH32 系列 → 其他（CH572/CH585/CH32V003/CH32X035/CH32V20x/CH32L103）**
>
> **已确认需求（v2 新增）**
> | 项 | 决策 |
> |----|------|
> | USB 范围 | **全功能**：CDC + HID + MSC + Audio + 复合设备 |
> | 低功耗 | **需要** `hal_sys_sleep()`（sleep / standby / 唤醒源配置） |
> | 多芯片顺序 | CH59x → CH32 → 其他 |
> | 日志通道 | 默认 UART，**可一键切换** USB CDC / 其他 UART / EEPROM |
> | BLE GATT | **多服务、多特征值**，动态注册；预留经典蓝牙（BR/EDR）扩展 |

---

## 1. 背景与动机

### 1.1 当前架构的问题

现有工程是 **ch32fun（社区轻量内核）+ WCH 官方闭源 BLE 库** 的混合方案，实测暴露了大量难以定位的缺陷：

| 问题 | 现象 | 根因 |
|------|------|------|
| 中断子系统不可靠 | `TMR0` / `BB` / `LLE` 中断全部不触发，`ISR[0]` 持续 pending 但 CPU 从不响应 | ch32fun 的 `NVIC_EnableIRQ` 是**赋值**语义（`IENR[n>>5] = 1<<(n&31)`），而 `IENR` 是**只写**寄存器，无法读-改-写，任何一处调用都会清掉同字其它中断 |
| BLE 广播不可见 | GAP 报告 `GAPROLE_ADVERTISING`，手机扫不到 | 上述中断问题的直接后果：协议栈收不到链路层事件，射频永不发包 |
| 向量表机制不透明 | `mtvec = InterruptVector \| 3`（WCH VTF 模式），与标准 RISC-V 语义不同 | ch32fun 用 `DEFAULT_INTERRUPT_VECTOR_CONTENTS` 宏拼装向量表，`FUNCONF_ISR_IN_RAM` 等开关影响巨大且无文档 |
| `.highcode` 段拷贝 | 代码必须从 Flash 拷到 RAM 才能跑，拷错就静默跑飞 | ch32fun 自定义链接脚本，与官方 `Link.ld` 不一致 |
| `-march` 扩展冲突 | 官方 BLE 库用 GCC13+ 编译，含 `zba/zbb/zbc/zbs`，而 CH59x 内核不完整支持 | 需要「编译用基础 march、链接放宽」的 hack |
| HAL 层重复实现 | 自己重写 `CH59x_BLEInit` / `HAL_Init` / `RTC_IRQHandler`，与官方行为有细微差异 | 官方 `MCU.c` / `RTC.c` / `SLEEP.c` 未被复用 |

**结论**：ch32fun 为「极简裸机」设计，不适合承载 BLE 这种对中断时序、内存布局、启动流程有严格要求的大型闭源协议栈。继续打补丁的边际收益已趋近于零。

### 1.2 重构目标

1. **去除 ch32fun 依赖**，改用 **WCH 官方 EVT 库**作为 HAL 底层
2. **HAL 层按芯片分目录**，各芯片复用官方库，互不干扰
3. **HAL 之上建立通用层（Common Layer）**，为 HAL 做适配，向上提供**统一外设接口**（BLE / USB / UART / I2C / SPI / GPIO / PWM / ADC / Flash）
4. **应用层（main）只依赖通用层**，不直接触碰任何芯片寄存器或官方库头文件
5. 现有 `main/` 下工程**逐步迁移**到新通用层，不推倒重来

### 1.3 非目标

- 不追求「一次支持全部芯片」，首期只做 CH592，验证架构可行后再横向扩展
- 不重写 BLE 协议栈（继续用 WCH 闭源 `libCH59xBLE.a`）
- 不改变构建系统（继续用 CMake）

---

## 2. 总体分层

```
┌─────────────────────────────────────────────────────────────────┐
│  Application  (main/<app>/)                                     │
│  ── 只 #include "hal/*.h" 通用头，不碰芯片寄存器                 │
│  ── 例：ble_echo.c  usb_cdc.c  sensor_hub.c                     │
└────────────────────────────┬────────────────────────────────────┘
                             │ 统一外设接口（函数指针 vtable）
┌────────────────────────────▼────────────────────────────────────┐
│  Common Layer  (common/)                                        │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐  │
│  │ hal_ble      │ hal_usb      │ hal_uart     │ hal_gpio     │  │
│  │ hal_i2c      │ hal_spi      │ hal_pwm      │ hal_adc      │  │
│  │ hal_flash    │ hal_sys      │ hal_time     │ hal_irq      │  │
│  └──────────────┴──────────────┴──────────────┴──────────────┘  │
│  ── 定义接口结构体、注册表、统一错误码、日志/断言                 │
│  ── 不含任何芯片寄存器操作                                       │
└────────────────────────────┬────────────────────────────────────┘
                             │ 每芯片一份实现（port）
┌────────────────────────────▼────────────────────────────────────┐
│  Chip Port Layer  (sdk/port/ch592/, sdk/port/ch572/, sdk/port/ch32v003/ …)  │
│  ── 实现 common 层声明的 vtable                                  │
│  ── 直接调用官方 StdPeriphDriver / core_riscv / BLE HAL          │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│  Vendor SDK  (sdk/vendor/ch592/…)                                   │
│  ── 原样拷贝自 WCH EVT，不做修改（便于升级）                      │
│  ── Startup / StdPeriphDriver / RVMSIS / Link.ld / BLE LIB+HAL  │
└─────────────────────────────────────────────────────────────────┘
```

**依赖方向严格单向**：Application → Common → Port → Vendor。禁止反向依赖，禁止跨层直接调用。

---

## 3. 目录结构

```
CHxxx/
├── docs/
│   ├── ARCHITECTURE.md            ← 本文档
│   ├── PORTING.md                 ← 新增芯片移植指南（后续）
│   └── API.md                     ← 通用层 API 参考（后续）
│
├── common/                        ← 通用层（与芯片无关）
│   ├── include/
│   │   ├── hal.h                  ← 总入口，聚合所有外设头
│   │   ├── hal_types.h            ← 统一类型、错误码、句柄基类
│   │   ├── hal_ble.h              ← 多服务/多特征值 GATT
│   │   ├── hal_bt_classic.h       ← 经典蓝牙（预留）
│   │   ├── hal_usb.h              ← USB 核心（类驱动框架）
│   │   ├── hal_usb_cdc.h          ← CDC 便捷封装
│   │   ├── hal_usb_hid.h          ← HID 便捷封装
│   │   ├── hal_usb_msc.h          ← MSC 便捷封装
│   │   ├── hal_usb_audio.h        ← Audio 便捷封装
│   │   ├── hal_uart.h
│   │   ├── hal_i2c.h
│   │   ├── hal_spi.h
│   │   ├── hal_gpio.h
│   │   ├── hal_pwm.h
│   │   ├── hal_adc.h
│   │   ├── hal_flash.h
│   │   ├── hal_sys.h              ← 时钟、复位、唯一 ID、**低功耗**
│   │   ├── hal_time.h             ← 延时、tick、定时器回调
│   │   ├── hal_log.h              ← 可切换后端日志
│   │   └── hal_irq.h              ← 中断注册/使能（影子副本）
│   └── src/
│       ├── hal_registry.c         ← 各外设实例注册与查找
│       ├── hal_log.c              ← 日志核心 + 各 sink 实现
│       ├── hal_usb_cdc_common.c   ← CDC 类驱动（芯片无关部分）
│       ├── hal_usb_hid_common.c   ← HID 类驱动
│       ├── hal_usb_msc_common.c   ← MSC 类驱动
│       ├── hal_usb_desc.c         ← 复合描述符自动拼装
│       └── hal_assert.c
│
├── port/                          ← 芯片移植层
│   ├── ch59x/                     ← CH591 / CH592（首期，同一内核）
│   │   ├── port_ch59x.c           ← 汇总注册所有外设实现
│   │   ├── hal_ble_ch59x.c
│   │   ├── hal_usb_ch59x.c        ← 端点管理 + 官方 usbdev 适配
│   │   ├── hal_uart_ch59x.c
│   │   ├── hal_i2c_ch59x.c
│   │   ├── hal_spi_ch59x.c
│   │   ├── hal_gpio_ch59x.c
│   │   ├── hal_pwm_ch59x.c
│   │   ├── hal_adc_ch59x.c
│   │   ├── hal_flash_ch59x.c
│   │   ├── hal_sys_ch59x.c        ← 含 hal_sys_sleep 实现
│   │   ├── hal_time_ch59x.c
│   │   ├── hal_irq_ch59x.c        ← ★ 影子副本管理 IENR/IRER
│   │   └── ble_profile_ch59x.c    ← GATT 服务动态注册实现
│   ├── ch32v003/                  ← 第二阶段
│   ├── ch32x035/                  ← 第二阶段
│   ├── ch32v20x/                  ← 第二阶段
│   ├── ch32l103/                  ← 第二阶段
│   ├── ch572/                     ← 第三阶段
│   └── ch585/                     ← 第三阶段
│
├── vendor/                        ← 官方 SDK 原样拷贝
│   ├── ch59x/
│   │   ├── Startup/startup_CH592.S
│   │   ├── RVMSIS/core_riscv.c/.h
│   │   ├── StdPeriphDriver/CH59x_*.c  + inc/
│   │   ├── Ld/Link.ld
│   │   └── BLE/
│   │       ├── HAL/{MCU.c,RTC.c,SLEEP.c,KEY.c,LED.c,include/}
│   │       └── LIB/{libCH59xBLE.a,ble_task_scheduler.S,CH59xBLE_LIB.h}
│   ├── ch32v003/ …
│   └── ch572/ …
│
├── main/                          ← 应用层
│   ├── ble_echo/                  ← BLE 多服务示例
│   ├── usb_cdc/                   ← USB CDC 示例
│   ├── usb_composite/             ← USB CDC + HID 复合设备示例
│   ├── lowpower/                  ← 低功耗示例
│   └── …
│
├── cmake/
│   ├── toolchain-riscv.cmake
│   ├── chip_ch59x.cmake           ← 芯片描述：源文件、宏、内存布局
│   ├── chip_ch32v003.cmake
│   └── hal.cmake                  ← 通用层 + port 层构建规则
│
└── CMakeLists.txt
```

---

## 4. 通用层接口设计

### 4.1 设计原则

1. **面向对象式**：每个外设一个结构体（vtable），实例以指针传递
2. **句柄即指针**：`hal_uart_t *` 就是句柄，无整数 ID 查找开销
3. **静态分配**：所有实例在 `port_<chip>.c` 中静态定义，无 `malloc`
4. **统一错误码**：所有函数返回 `hal_err_t`
5. **同步 + 异步双模式**：阻塞 API 简单可用，异步 API 通过回调

### 4.2 基础类型

```c
/* sdk/hal/include/hal_types.h */

typedef enum {
    HAL_OK          =  0,
    HAL_ERR_PARAM   = -1,   /* 参数非法 */
    HAL_ERR_TIMEOUT = -2,   /* 超时 */
    HAL_ERR_BUSY    = -3,   /* 忙 */
    HAL_ERR_NOMEM   = -4,   /* 无内存/缓冲不足 */
    HAL_ERR_IO      = -5,   /* 硬件错误 */
    HAL_ERR_NOINIT  = -6,   /* 未初始化 */
    HAL_ERR_UNSUP   = -7,   /* 不支持 */
} hal_err_t;

/* 所有外设实例的公共头（可选，便于统一管理） */
typedef struct {
    const char *name;       /* "uart0" / "ble" / "i2c0" */
    uint8_t     initialized;
} hal_dev_t;
```

### 4.3 UART 示例（完整定义）

```c
/* sdk/hal/include/hal_uart.h */

typedef struct hal_uart hal_uart_t;

typedef void (*hal_uart_rx_cb_t)(hal_uart_t *u, const uint8_t *data,
                                 uint16_t len, void *user);

typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;     /* 5..8 */
    uint8_t  stop_bits;     /* 1..2 */
    uint8_t  parity;        /* 0=none 1=odd 2=even */
    uint8_t  tx_pin;        /* port 层解释，如 PA9 */
    uint8_t  rx_pin;
    uint8_t  use_dma;       /* 0=中断 1=DMA */
} hal_uart_cfg_t;

struct hal_uart {
    hal_dev_t dev;

    hal_err_t (*init)   (hal_uart_t *u, const hal_uart_cfg_t *cfg);
    hal_err_t (*deinit) (hal_uart_t *u);

    /* 阻塞写，全部发出才返回 */
    hal_err_t (*write)  (hal_uart_t *u, const uint8_t *buf, uint16_t len);
    /* 非阻塞写，返回实际入队字节数 */
    int       (*write_async)(hal_uart_t *u, const uint8_t *buf, uint16_t len);
    /* 阻塞读，读到 len 或超时才返回 */
    hal_err_t (*read)   (hal_uart_t *u, uint8_t *buf, uint16_t len,
                         uint32_t timeout_ms);
    /* 注册接收回调（异步模式） */
    hal_err_t (*set_rx_cb)(hal_uart_t *u, hal_uart_rx_cb_t cb, void *user);
    /* 发送完成回调（DMA 模式用） */
    hal_err_t (*set_tx_done_cb)(hal_uart_t *u, void (*cb)(hal_uart_t *, void *),
                                void *user);
};

/* 由 port 层提供：返回该芯片的 UART 实例，index=0/1/2/3 */
hal_uart_t *hal_uart_get(uint8_t index);
```

**应用层用法**：
```c
hal_uart_t *u = hal_uart_get(1);            /* UART1 */
hal_uart_cfg_t cfg = { .baudrate = 115200, .data_bits = 8,
                       .stop_bits = 1, .parity = 0,
                       .tx_pin = 9, .rx_pin = 8 };
u->init(u, &cfg);
u->write(u, (const uint8_t *)"hello\r\n", 7);
```

### 4.4 BLE 接口（多服务 / 多特征值 / 预留经典蓝牙）

BLE 与普通外设不同：底层是**事件驱动协议栈**，不能简单套用「init/write/read」。
且需求是**多服务、多特征值**，因此必须提供**动态 GATT 注册机制**，而不是硬编码一个 0xFFF0 透传服务。

#### 4.4.1 属性定义（声明式）

```c
/* sdk/hal/include/hal_ble.h */

/* 特征值属性位 */
#define HAL_BLE_PROP_READ        0x02
#define HAL_BLE_PROP_WRITE_NR    0x04   /* Write Without Response */
#define HAL_BLE_PROP_WRITE       0x08
#define HAL_BLE_PROP_NOTIFY      0x10
#define HAL_BLE_PROP_INDICATE    0x20

/* 特征值权限 */
#define HAL_BLE_PERM_READ        0x01
#define HAL_BLE_PERM_WRITE       0x02

typedef enum {
    HAL_BLE_UUID_16 = 2,
    HAL_BLE_UUID_128 = 16,
} hal_ble_uuid_len_t;

typedef struct {
    uint8_t  len;            /* 2 或 16 */
    uint8_t  bytes[16];      /* 小端存放 */
} hal_ble_uuid_t;

/* 特征值描述（静态表，由应用提供） */
typedef struct {
    const char      *name;        /* 调试用，如 "TX" */
    hal_ble_uuid_t   uuid;
    uint8_t          props;       /* HAL_BLE_PROP_* */
    uint8_t          perms;       /* HAL_BLE_PERM_* */
    uint16_t         max_len;     /* 特征值最大长度（MTU 上限内） */
    const uint8_t   *init_value;  /* 初始值，可为 NULL */
    uint16_t         init_len;
} hal_ble_char_def_t;

/* 服务描述 */
typedef struct {
    const char              *name;      /* 调试用，如 "Echo" */
    hal_ble_uuid_t           uuid;
    uint8_t                  char_count;
    const hal_ble_char_def_t *chars;
} hal_ble_service_def_t;
```

#### 4.4.2 运行时句柄与回调

```c
/* 注册成功后返回的运行时句柄（应用保存，用于 notify / 读写） */
typedef struct {
    uint16_t service_idx;   /* 应用侧服务序号 */
    uint16_t char_idx;      /* 服务内特征值序号 */
    uint16_t value_handle;  /* 协议栈分配的属性句柄 */
    uint16_t cccd_handle;   /* CCCD 句柄（通知用），0 表示无 */
} hal_ble_char_t;

/* 特征值写事件 */
typedef void (*hal_ble_write_cb_t)(const hal_ble_char_t *ch,
                                   const uint8_t *data, uint16_t len,
                                   uint16_t conn_handle, void *user);
/* 特征值读事件（可选，用于动态值） */
typedef uint16_t (*hal_ble_read_cb_t)(const hal_ble_char_t *ch,
                                      uint8_t *buf, uint16_t max_len,
                                      uint16_t conn_handle, void *user);
/* CCCD 变更（手机订阅/取消订阅通知） */
typedef void (*hal_ble_cccd_cb_t)(const hal_ble_char_t *ch, uint8_t enabled,
                                  uint16_t conn_handle, void *user);

typedef struct {
    hal_ble_write_cb_t write_cb;
    hal_ble_read_cb_t  read_cb;
    hal_ble_cccd_cb_t  cccd_cb;
    void              *user;
} hal_ble_char_cbs_t;
```

#### 4.4.3 连接与事件

```c
typedef enum {
    HAL_BLE_EVT_ADV_STARTED,
    HAL_BLE_EVT_ADV_STOPPED,
    HAL_BLE_EVT_CONNECTED,        /* param: hal_ble_conn_t* */
    HAL_BLE_EVT_DISCONNECTED,     /* param: hal_ble_conn_t* */
    HAL_BLE_EVT_MTU_CHANGED,      /* param: hal_ble_conn_t* */
    HAL_BLE_EVT_PASSKEY,          /* param: uint32_t* */
    HAL_BLE_EVT_PAIRING_DONE,
} hal_ble_evt_t;

typedef struct {
    uint16_t conn_handle;
    uint8_t  addr[6];             /* 对端地址（小端） */
    uint8_t  addr_type;
    uint16_t mtu;
} hal_ble_conn_t;

typedef void (*hal_ble_evt_cb_t)(hal_ble_evt_t evt, const void *param, void *user);

typedef struct {
    const char *device_name;
    uint8_t     adv_data[31];
    uint8_t     adv_len;
    uint8_t     scan_rsp_data[31];
    uint8_t     scan_rsp_len;
    uint16_t    adv_interval_ms;
    uint16_t    min_conn_interval_ms;
    uint16_t    max_conn_interval_ms;
    uint16_t    mtu;              /* 期望 MTU，默认 23，可设 247 */
    uint8_t     max_connections;  /* 1..4 */
    uint8_t     enable_pairing;   /* 1 = 启用绑定 */
} hal_ble_cfg_t;
```

#### 4.4.4 主接口

```c
typedef struct hal_ble hal_ble_t;

struct hal_ble {
    hal_dev_t dev;

    /* 初始化协议栈（内部完成 BLEInit / HAL_Init / GAPRole 初始化） */
    hal_err_t (*init)    (hal_ble_t *b, const hal_ble_cfg_t *cfg);
    hal_err_t (*deinit)  (hal_ble_t *b);

    /* ---- GATT 动态注册（必须在 init 之后、adv_start 之前调用） ---- */
    /* 注册一个服务，返回该服务内所有特征值的运行时句柄数组 */
    hal_err_t (*add_service)(hal_ble_t *b,
                             const hal_ble_service_def_t *def,
                             hal_ble_char_t *out_chars,      /* 长度 >= def->char_count */
                             const hal_ble_char_cbs_t *cbs); /* 可为 NULL，后续再绑 */

    /* 为已注册特征值绑定/更新回调 */
    hal_err_t (*bind_cb)(hal_ble_t *b, const hal_ble_char_t *ch,
                         const hal_ble_char_cbs_t *cbs);

    /* ---- 广播 ---- */
    hal_err_t (*adv_start)(hal_ble_t *b);
    hal_err_t (*adv_stop) (hal_ble_t *b);

    /* ---- 数据收发 ---- */
    /* 发送通知（需对端已订阅 CCCD） */
    hal_err_t (*notify)(hal_ble_t *b, const hal_ble_char_t *ch,
                        uint16_t conn_handle,
                        const uint8_t *data, uint16_t len);
    /* 发送指示（需对端确认） */
    hal_err_t (*indicate)(hal_ble_t *b, const hal_ble_char_t *ch,
                          uint16_t conn_handle,
                          const uint8_t *data, uint16_t len);
    /* 主动更新特征值（读请求会返回新值） */
    hal_err_t (*set_value)(hal_ble_t *b, const hal_ble_char_t *ch,
                           const uint8_t *data, uint16_t len);

    /* ---- 事件 ---- */
    hal_err_t (*set_evt_cb)(hal_ble_t *b, hal_ble_evt_cb_t cb, void *user);

    /* ---- 协议栈主循环（应用 while(1) 中周期调用） ---- */
    void      (*poll)    (hal_ble_t *b);
};

hal_ble_t *hal_ble_get(void);
```

#### 4.4.5 应用层用法（多服务示例）

```c
/* 定义两个服务：Echo(0xFFF0) 和 Sensor(0x181A) */
static const hal_ble_char_def_t echo_chars[] = {
    { .name="RX", .uuid={2,{0xF1,0xFF}}, .props=HAL_BLE_PROP_WRITE|HAL_BLE_PROP_WRITE_NR,
      .perms=HAL_BLE_PERM_WRITE, .max_len=247 },
    { .name="TX", .uuid={2,{0xF2,0xFF}}, .props=HAL_BLE_PROP_NOTIFY,
      .perms=HAL_BLE_PERM_READ,  .max_len=247 },
};
static const hal_ble_service_def_t services[] = {
    { .name="Echo",   .uuid={2,{0xF0,0xFF}}, .char_count=2, .chars=echo_chars },
    { .name="Sensor", .uuid={2,{0x1A,0x18}}, .char_count=1, .chars=sensor_chars },
};

static hal_ble_char_t echo_rx, echo_tx, sensor_val;

static void on_echo_write(const hal_ble_char_t *ch, const uint8_t *d,
                          uint16_t n, uint16_t conn, void *u) {
    hal_ble_t *b = hal_ble_get();
    b->notify(b, &echo_tx, conn, d, n);      /* 原样回传 */
}

void app_main(void) {
    hal_ble_t *b = hal_ble_get();
    hal_ble_cfg_t cfg = { .device_name="CH592-BLE", .adv_interval_ms=100, .mtu=247 };
    b->init(b, &cfg);

    hal_ble_char_t tmp[2];
    b->add_service(b, &services[0], tmp, NULL);
    echo_rx = tmp[0]; echo_tx = tmp[1];

    hal_ble_char_cbs_t cbs = { .write_cb = on_echo_write };
    b->bind_cb(b, &echo_rx, &cbs);

    b->add_service(b, &services[1], &sensor_val, NULL);

    b->adv_start(b);
    while (1) { b->poll(b); }
}
```

#### 4.4.6 经典蓝牙（BR/EDR）预留

需求提到「后期也有传统蓝牙需求」。CH592 是 **BLE-only** 芯片，不支持经典蓝牙。
架构上预留扩展点：

```c
/* 未来：sdk/hal/include/hal_bt_classic.h */
typedef struct hal_bt_classic hal_bt_classic_t;
struct hal_bt_classic {
    hal_dev_t dev;
    hal_err_t (*init)(hal_bt_classic_t *, const void *cfg);
    hal_err_t (*spp_connect)(hal_bt_classic_t *, const uint8_t addr[6]);
    hal_err_t (*spp_send)(hal_bt_classic_t *, const uint8_t *d, uint16_t n);
    /* ... */
};
/* 由支持 BR/EDR 的芯片（如 CH582/CH583 部分型号）的 port 层实现；
 * CH59x 的 hal_bt_classic_get() 返回 NULL。 */
hal_bt_classic_t *hal_bt_classic_get(void);
```

**结论**：`hal_ble` 与 `hal_bt_classic` 是两个独立接口，应用按需 `if (hal_bt_classic_get())` 判断芯片能力。

### 4.5 USB 接口（全功能：CDC / HID / MSC / Audio / 复合设备）

需求是 **USB 全功能**，因此不能只做 CDC。设计要点：

1. **类驱动（Class Driver）可插拔**：每个 USB 功能类是一个独立模块，应用选择组合
2. **描述符动态生成**：按启用的类自动拼装 Configuration / Interface / Endpoint 描述符
3. **端点资源管理**：统一分配 EP 号，避免冲突
4. **复合设备**：多个类共存于一个 Configuration

#### 4.5.1 类驱动注册

```c
/* sdk/hal/include/hal_usb.h */

typedef enum {
    HAL_USB_CLASS_CDC,      /* 通信设备类：虚拟串口 */
    HAL_USB_CLASS_HID,      /* 人机接口：键盘/鼠标/自定义 */
    HAL_USB_CLASS_MSC,      /* 大容量存储 */
    HAL_USB_CLASS_AUDIO,    /* 音频 */
    HAL_USB_CLASS_VENDOR,   /* 厂商自定义 */
} hal_usb_class_t;

/* 每个类驱动实现这组回调（由 port 层或 common 层提供） */
typedef struct {
    hal_usb_class_t type;

    /* 返回该类的接口描述符字节数/内容 */
    uint16_t (*get_config_desc)(uint8_t *buf, uint16_t max_len,
                                uint8_t first_if_num);
    uint16_t (*get_device_desc_extra)(uint8_t *buf, uint16_t max_len);

    /* 端点需求：返回需要的 EP 数量及方向/类型 */
    uint8_t  (*get_ep_requirement)(void *ep_req_out);

    /* 初始化（分配端点、准备缓冲） */
    hal_err_t (*init)(void *ctx, const void *cfg);

    /* 标准请求处理（SETUP 阶段） */
    hal_err_t (*on_setup)(void *ctx, const uint8_t setup[8]);

    /* 数据收发完成 */
    void      (*on_ep_in )(void *ctx, uint8_t ep, uint16_t len);
    void      (*on_ep_out)(void *ctx, uint8_t ep, uint16_t len);

    /* 总线事件 */
    void      (*on_reset)(void *ctx);
    void      (*on_suspend)(void *ctx);
    void      (*on_resume)(void *ctx);
} hal_usb_class_drv_t;
```

#### 4.5.2 主接口

```c
typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint16_t bcd_device;
    const char *manufacturer;
    const char *product;
    const char *serial;
    uint8_t  max_power_ma;
    uint8_t  self_powered;
} hal_usb_dev_desc_t;

typedef struct hal_usb hal_usb_t;

struct hal_usb {
    hal_dev_t dev;

    /* 注册一个功能类（必须在 init 之前调用） */
    hal_err_t (*add_class)(hal_usb_t *u, const hal_usb_class_drv_t *drv, void *ctx);

    hal_err_t (*init)   (hal_usb_t *u, const hal_usb_dev_desc_t *desc);
    hal_err_t (*deinit) (hal_usb_t *u);
    /* 重新枚举（修改描述符后调用） */
    hal_err_t (*reconnect)(hal_usb_t *u);

    /* 底层传输（类驱动内部用，应用一般不直接调） */
    hal_err_t (*ep_write)(hal_usb_t *u, uint8_t ep, const uint8_t *d, uint16_t n);
    hal_err_t (*ep_read) (hal_usb_t *u, uint8_t ep, uint8_t *d, uint16_t n);

    /* 主循环（处理 SETUP / 端点事件） */
    void      (*poll)(hal_usb_t *u);
};

hal_usb_t *hal_usb_get(void);
```

#### 4.5.3 CDC 便捷封装（最常用）

```c
/* sdk/hal/include/hal_usb_cdc.h —— 对 hal_usb 的语法糖 */

typedef struct {
    uint8_t  ep_in;          /* 数据 IN 端点号 */
    uint8_t  ep_out;         /* 数据 OUT 端点号 */
    uint8_t  ep_notify;      /* 中断 IN 端点号 */
    uint16_t rx_buf_size;
    uint16_t tx_buf_size;
} hal_usb_cdc_cfg_t;

typedef void (*hal_usb_cdc_rx_cb_t)(const uint8_t *d, uint16_t n, void *user);

/* 注册 CDC 类并返回句柄 */
hal_err_t hal_usb_cdc_add(const hal_usb_cdc_cfg_t *cfg, hal_usb_cdc_rx_cb_t cb,
                          void *user);
/* 发送（非阻塞，返回入队字节数） */
int       hal_usb_cdc_write(const uint8_t *d, uint16_t n);
/* 是否已连接（DTR 置位） */
uint8_t   hal_usb_cdc_connected(void);
```

#### 4.5.4 复合设备示例（CDC + HID）

```c
void app_main(void) {
    hal_usb_t *u = hal_usb_get();

    /* 1. 注册 CDC */
    hal_usb_cdc_cfg_t cdc = { .ep_in=3, .ep_out=2, .ep_notify=1 };
    hal_usb_cdc_add(&cdc, on_cdc_rx, NULL);

    /* 2. 注册 HID 键盘 */
    hal_usb_hid_cfg_t hid = { .ep_in=4, .report_desc=kbd_desc, .report_len=sizeof(kbd_desc) };
    hal_usb_hid_add(&hid, NULL);

    /* 3. 初始化（自动生成复合描述符） */
    hal_usb_dev_desc_t desc = {
        .vid=0x1A86, .pid=0xFE07, .manufacturer="MyCo", .product="DualDemo",
    };
    u->init(u, &desc);

    while (1) { u->poll(u); }
}
```

### 4.6 日志接口（可切换后端）

需求：**默认 UART，可方便切换 USB CDC / 其他 UART / EEPROM**。

```c
/* sdk/hal/include/hal_log.h */

typedef enum {
    HAL_LOG_SINK_NONE = 0,
    HAL_LOG_SINK_UART,      /* 指定 UART 实例 */
    HAL_LOG_SINK_USB_CDC,   /* USB CDC */
    HAL_LOG_SINK_EEPROM,    /* 写入 Data Flash（无输出通道时诊断用） */
    HAL_LOG_SINK_CUSTOM,    /* 应用自定义回调 */
} hal_log_sink_t;

typedef enum {
    HAL_LOG_LVL_NONE = 0,
    HAL_LOG_LVL_ERROR,
    HAL_LOG_LVL_WARN,
    HAL_LOG_LVL_INFO,
    HAL_LOG_LVL_DEBUG,
} hal_log_level_t;

/* 切换后端（运行时可随时切换，例如 USB 枚举完成后再切过去） */
hal_err_t hal_log_set_sink(hal_log_sink_t sink, void *arg);
/*   arg 解释：
 *     HAL_LOG_SINK_UART    -> (hal_uart_t *)  用哪个 UART
 *     HAL_LOG_SINK_USB_CDC -> NULL
 *     HAL_LOG_SINK_EEPROM  -> (uint32_t *)    起始地址
 *     HAL_LOG_SINK_CUSTOM  -> (hal_log_write_fn) 回调
 */

void hal_log_set_level(hal_log_level_t lvl);

/* 格式化输出（内部用 vsnprintf 到栈缓冲，再送 sink） */
void hal_log_printf(const char *fmt, ...) __attribute__((format(printf,1,2)));
void hal_log_hexdump(const void *data, uint16_t len);

#define LOG_E(...) do { if (hal_log_enabled(HAL_LOG_LVL_ERROR)) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_W(...) do { if (hal_log_enabled(HAL_LOG_LVL_WARN )) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_I(...) do { if (hal_log_enabled(HAL_LOG_LVL_INFO )) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_D(...) do { if (hal_log_enabled(HAL_LOG_LVL_DEBUG)) hal_log_printf(__VA_ARGS__); } while (0)
```

**典型用法（USB 枚举完成后切换）**：
```c
hal_uart_t *u1 = hal_uart_get(1);
hal_log_set_sink(HAL_LOG_SINK_UART, u1);        /* 启动阶段走 UART */
LOG_I("boot ok\n");

/* ... USB 初始化 ... */
if (hal_usb_cdc_connected()) {
    hal_log_set_sink(HAL_LOG_SINK_USB_CDC, NULL);  /* 切到 USB */
    LOG_I("switched to USB CDC\n");
}
```

### 4.7 低功耗接口

```c
/* sdk/hal/include/hal_sys.h */

typedef enum {
    HAL_SLEEP_MODE_SLEEP,       /* 仅关 CPU 时钟，外设继续 */
    HAL_SLEEP_MODE_STANDBY,     /* 深度睡眠，RAM 保持 */
    HAL_SLEEP_MODE_SHUTDOWN,    /* 最低功耗，RAM 丢失 */
} hal_sleep_mode_t;

/* 唤醒源位掩码 */
#define HAL_WAKE_GPIO      0x0001
#define HAL_WAKE_RTC       0x0002
#define HAL_WAKE_USB       0x0004
#define HAL_WAKE_BLE       0x0008
#define HAL_WAKE_UART      0x0010
#define HAL_WAKE_TIMER     0x0020

typedef struct {
    hal_sleep_mode_t mode;
    uint32_t         wake_sources;   /* HAL_WAKE_* 位或 */
    uint32_t         rtc_wake_ms;    /* RTC 定时唤醒，0 = 不定时 */
    uint8_t          gpio_wake_pin;  /* GPIO 唤醒引脚编码，0xFF = 不用 */
    uint8_t          gpio_wake_level;/* 0=低电平 1=高电平 */
} hal_sleep_cfg_t;

/* 进入低功耗，被唤醒后返回；返回值表示唤醒原因（HAL_WAKE_* 位） */
uint32_t hal_sys_sleep(const hal_sleep_cfg_t *cfg);

/* 查询上次唤醒原因 */
uint32_t hal_sys_get_wake_reason(void);

/* 其它系统功能 */
uint32_t hal_sys_get_clock_hz(void);
hal_err_t hal_sys_set_clock_hz(uint32_t hz);
void     hal_sys_reset(void);
void     hal_sys_get_uid(uint8_t uid[8]);
```

### 4.8 中断管理接口（针对本次踩坑的核心修复）

```c
/* sdk/hal/include/hal_irq.h */

/* 安全使能：内部维护影子副本，绝不破坏同字其它中断 */
void hal_irq_enable (int irqn);
void hal_irq_disable(int irqn);
void hal_irq_set_priority(int irqn, uint8_t prio);
void hal_irq_set_pending (int irqn);
void hal_irq_clear_pending(int irqn);
uint32_t hal_irq_get_pending_mask(uint8_t word_idx);  /* 读 ISR[n]，可读 */

/* 注册 C 处理函数（替代 __attribute__((interrupt)) 宏，
 * 由 port 层用官方汇编 trampoline 包装，保证寄存器完整保存） */
typedef void (*hal_isr_t)(void *user);
hal_err_t hal_irq_attach(int irqn, hal_isr_t isr, void *user);
```

**port 层实现要点（CH592）**：
- `IENR`/`IRER` 均为**只写**寄存器 → 必须用 RAM 影子副本 `s_ienr_shadow[8]`
- 所有使能/禁用都改影子副本，然后**整体回写** `PFIC->IENR[0..7]`
- 禁止直接调用官方 `NVIC_EnableIRQ` 宏（赋值语义）
- 提供 `hal_irq_attach` 时，中断入口用官方 `ble_task_scheduler.S` 风格的汇编 trampoline
- **回归测试**：连续 `hal_irq_enable(16)` / `hal_irq_enable(20)` / `hal_irq_enable(21)`，
  三者必须同时生效（`ISR[0]` 的 bit16/20/21 都能被服务）

### 4.9 其余外设接口概要

| 头文件 | 主要函数 | 备注 |
|--------|---------|------|
| `hal_gpio.h` | `init / write / read / toggle / set_irq` | pin 编码 `(port<<4)\|pin` |
| `hal_i2c.h` | `init / write / read / write_reg / read_reg / scan` | 支持 7 位地址、10 位地址 |
| `hal_spi.h` | `init / transfer / write / read / cs_ctrl` | 主机 + 从机 |
| `hal_pwm.h` | `init / set_duty / set_freq / start / stop` | 通道独立 |
| `hal_adc.h` | `init / read / read_multi / set_cb` | 单次/连续/注入 |
| `hal_flash.h` | `read / write / erase_sector / get_size` | 区分 Code-Flash / Data-Flash(EEPROM) |
| `hal_sys.h` | `get_clock / set_clock / reset / get_uid / **sleep**` | 见 4.7 |
| `hal_time.h` | `delay_ms / delay_us / get_tick / set_timer_cb` | |
| `hal_usb.h` | `add_class / init / ep_write / ep_read / poll` | **全功能，见 4.5** |
| `hal_usb_cdc.h` | `add / write / connected` | CDC 便捷封装 |
| `hal_usb_hid.h` | `add / send_report` | HID 便捷封装 |
| `hal_usb_msc.h` | `add / set_storage_cb` | MSC 便捷封装 |
| `hal_ble.h` | `init / add_service / bind_cb / adv_start / notify / poll` | **多服务，见 4.4** |
| `hal_bt_classic.h` | `init / spp_connect / spp_send` | **预留，CH59x 返回 NULL** |
| `hal_log.h` | `set_sink / set_level / printf` | **可切换后端，见 4.6** |
| `hal_irq.h` | `enable / disable / attach / set_priority` | **影子副本，见 4.8** |

---

## 5. 关键设计决策

### 5.1 为什么用 vtable 而不是全局函数？

- **多芯片并存**：同一份应用代码编译到 CH592 和 CH32V003 时，只需链接不同的 port 层
- **可测试**：可以用 mock 实现替换 port 层，在 PC 上跑单元测试
- **可扩展**：新增外设只加头文件 + port 实现，不改已有代码

代价：多一层间接调用（约 2-3 周期），对 60MHz 的 BLE 时序无影响（BLE 关键路径在闭源库内部）。

### 5.2 为什么 vendor 层不做任何修改？

- 便于跟随 WCH 官方升级（直接替换目录）
- 避免「改了一处忘了另一处」的隐性 bug
- 所有适配逻辑集中在 port 层，职责清晰

### 5.3 BLE 协议栈如何接入？

官方 BLE 需要：
1. `startup_CH592.S` 的向量表（`BB_IRQHandler` / `LLE_IRQHandler`）
2. `ble_task_scheduler.S` 的汇编中断入口（保存 x1/x4-x31 + `mret`）
3. `MCU.c` / `RTC.c` / `SLEEP.c` 的 HAL 实现
4. `libCH59xBLE.a` + `libISP592.a`
5. `Link.ld` 中 `.highcode` 段正确布局

→ 这些**全部原样来自 sdk/vendor/ch592**，port 层只负责：
- 调用 `CH59x_BLEInit()` / `HAL_Init()`
- 用 `hal_irq_enable()` 安全使能 BB/LLE/RTC
- 把 GAP 事件转成 `hal_ble_evt_t` 回调

### 5.4 内存布局

CH592：Flash 448KB（Code 192KB + Data 256KB），RAM 26KB。

| 区域 | 用途 | 大小 |
|------|------|------|
| `.init` | 向量表 + 启动代码 | ~0x90 |
| `.highcode` | 从 Flash 拷到 RAM 执行（RAM 0x20000000） | BLE 库要求 |
| `.text` | 常规代码 | 剩余 |
| `.data/.bss` | 全局变量 | |
| BLE heap | `MEM_BUF` | 4096 B（CH592 推荐） |
| 栈 | `_eusrstack` 向下 | ~2KB |

**Link.ld 必须用官方版本**，不要再用 ch32fun 的自定义脚本。

### 5.5 构建系统

保持 CMake，但重构 target 定义：

```cmake
# sdk/cmake/chip_ch592.cmake
set(CHIP_NAME        "ch592")
set(CHIP_MARCH       "rv32imac_zicsr_zifencei_zmmul")
set(CHIP_LINK_MARCH  "rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs")  # 仅链接
set(CHIP_VENDOR_DIR  "${CMAKE_SOURCE_DIR}/sdk/vendor/ch592")
set(CHIP_PORT_DIR    "${CMAKE_SOURCE_DIR}/sdk/port/ch592")
set(CHIP_LINKER      "${CHIP_VENDOR_DIR}/Ld/Link.ld")
set(CHIP_FLASH_SIZE  192K)
set(CHIP_RAM_SIZE    26K)

# sdk/cmake/hal.cmake
function(hal_add_executable TARGET)
    cmake_parse_arguments(ARG "BLE;USB" "LINKER_SCRIPT"
        "SOURCES;INCLUDES;LIBS" ${ARGN})
    # 1. 通用层
    # 2. port 层（按 ARG_BLE/ARG_USB 选择性加入）
    # 3. vendor 层（StdPeriphDriver 按需）
    # 4. 应用源文件
endfunction()
```

应用侧 `CMakeLists.txt` 极简：
```cmake
hal_add_executable(ble_echo
    SOURCES  ble_echo.c
    BLE
)
```

---

## 6. 迁移路线

分 6 个阶段，每阶段可独立验证，失败可回退。

### 阶段 0：准备（0.5 天）
- [ ] 建 `sdk/vendor/ch592/`，从 `tmp/CH592EVT/EVT/EXAM/SRC` 原样拷贝
- [ ] 拷贝 `BLE/HAL`、`BLE/LIB` 到 `sdk/vendor/ch592/BLE/`
- [ ] 建 `docs/PORTING.md` 骨架

**验收**：`sdk/vendor/ch592` 内容与官方 EVT 逐字节一致（`FC /B` 校验）

### 阶段 1：最小裸机（1 天）
- [ ] 建 `sdk/hal/include/hal_types.h`、`hal_sys.h`、`hal_time.h`、`hal_gpio.h`
- [ ] 建 `sdk/port/ch592/hal_sys_ch592.c`、`hal_time_ch592.c`、`hal_gpio_ch592.c`
- [ ] 建 `sdk/cmake/chip_ch592.cmake`、`sdk/cmake/hal.cmake`
- [ ] 应用 `main/blink_new/`：用 `hal_gpio` 闪灯

**验收**：PA13 方波可见（万用表/示波器），或 EEPROM 心跳计数递增

### 阶段 2：中断子系统（1 天）★ 关键
- [ ] `sdk/port/ch592/hal_irq_ch592.c`：影子副本 + 整体回写
- [ ] 用 `TMR0` 做验证：注册回调，确认每秒触发 N 次
- [ ] **必须验证**：调用 `hal_irq_enable(16)` 后，`hal_irq_enable(20)` 不会把 16 关掉

**验收**：TMR0 回调计数与预期一致；连续使能 3 个中断后 `ISR[0]` 全部能被服务

> 这一步是整个重构的**基石**。当前工程卡在中断不触发，必须在这一阶段彻底解决并留下回归测试。

### 阶段 3：UART + 日志（1 天）
- [ ] `sdk/port/ch59x/hal_uart_ch59x.c`（注意 `R8_UARTx_IER |= RB_IER_TXD_EN`）
- [ ] `sdk/hal/src/hal_log.c`：sink 抽象 + UART sink + EEPROM sink
- [ ] `hal_log_set_sink()` 运行时切换验证
- [ ] 应用 `main/uart_new/`：打印 + 回显

**验收**：PC 串口助手收到 "hello" 并能回显；运行中切换 sink 到 EEPROM 后能 dump 出日志

### 阶段 4：BLE 多服务（3-4 天）★ 核心目标
- [ ] `sdk/port/ch59x/hal_ble_ch59x.c`：封装 `CH59x_BLEInit` / `HAL_Init` / GAPRole
- [ ] `sdk/port/ch59x/ble_profile_ch59x.c`：**动态 GATT 注册**（`add_service` / `bind_cb`）
- [ ] 支持多服务、多特征值、notify/indicate/read/write 全属性
- [ ] 应用 `main/ble_multi/`：2 个服务 + 3 个特征值
- [ ] **关键验证**：`hal_irq_enable(BB)` / `hal_irq_enable(LLE)` 后，手机能扫到设备

**验收**：手机 nRF Connect 能看到设备、列出全部服务/特征值，能连接、能收发、能订阅通知

### 阶段 5：USB 全功能（4-5 天）
- [ ] `sdk/port/ch59x/hal_usb_ch59x.c`：端点管理 + 官方 `CH59x_usbdev.c` 适配
- [ ] `sdk/hal/src/hal_usb_desc.c`：复合设备描述符自动拼装
- [ ] `sdk/hal/src/hal_usb_cdc_common.c`：CDC 类驱动
- [ ] `sdk/hal/src/hal_usb_hid_common.c`：HID 类驱动
- [ ] `sdk/hal/src/hal_usb_msc_common.c`：MSC 类驱动
- [ ] 应用 `main/usb_cdc/`、`main/usb_composite/`（CDC+HID）
- [ ] **关键验证**：USB CDC 与 BLE 同时工作（当前架构做不到）

**验收**：COM 口打开正常；复合设备（CDC+HID）在 Windows 设备管理器正确识别；BLE 可同时扫描连接

### 阶段 6：低功耗（2 天）
- [ ] `sdk/port/ch59x/hal_sys_ch59x.c`：`hal_sys_sleep()`（sleep / standby / shutdown）
- [ ] 唤醒源：GPIO / RTC / USB / BLE / UART / Timer
- [ ] 应用 `main/lowpower/`：RTC 定时唤醒 + GPIO 唤醒
- [ ] **关键验证**：sleep 后 BLE 广播能自动恢复

**验收**：万用表实测电流符合芯片手册；唤醒原因正确返回

### 阶段 7：经典蓝牙预留 + 迁移旧工程（按需）
- [ ] `sdk/hal/include/hal_bt_classic.h` 定义（CH59x 返回 NULL）
- [ ] `usb_dual` → `main/usb_ble_new/`
- [ ] `uart_via_usb` → 合并到 `hal_log`
- [ ] 删除 `sdk/common/core/ch32fun.c` 及 `sdk/CH592_DEV/core/ch5xxhw.h`

**验收**：所有旧功能在新架构下等价可用

### 阶段 8：横向扩展芯片（按需）
- [ ] CH32V003（最小验证，无 BLE/USB）
- [ ] CH32X035
- [ ] CH32V20x / CH32L103
- [ ] CH572 / CH585

**验收**：同一份 `main/blink` 不改代码即可在新芯片上编译运行

---

## 7. 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 阶段 2 中断问题仍未解决 | 后续全部阻塞 | **先做阶段 2**，用 TMR0 最小复现；若影子副本方案仍失败，改用官方 `core_riscv.h` 的 `PFIC_EnableIRQ` 并逐个排查谁在清位 |
| 官方 `Link.ld` 与现有 flash 烧录流程不兼容 | 烧录失败 | 阶段 0 先用官方 `Peripheral.hex` 验证烧录链路 |
| BLE 库对 `-march` 敏感 | 链接失败或运行时异常 | 保持「编译基础 march / 链接放宽」策略，写在 `chip_ch59x.cmake` 注释里 |
| vtable 间接调用影响 BLE 时序 | 丢包/连接不稳 | BLE 关键路径全在闭源库内，port 层只在事件回调边界跨层，实测验证 |
| USB 复合描述符拼装复杂 | 枚举失败 | 阶段 5 先用官方 `BLE_USB` 示例的描述符做基准，逐个类加入并每次验证枚举 |
| 动态 GATT 注册与闭源库 API 不匹配 | 服务注册失败 | 阶段 4 先照搬官方 `SimpleProfile` 静态表，再抽象成 `add_service`；保留静态回退路径 |
| 低功耗与 BLE 时序冲突 | 唤醒后广播中断 | 阶段 6 参考官方 `SLEEP.c`，先做「不睡」基线，再逐步加睡眠 |
| 迁移期新旧代码并存 | 构建混乱 | 新旧目录物理隔离，CMake 用 `CHIP_SDK_RESOLVED` 区分，迁移完成前不删旧代码 |

---

## 8. 已确认决策（v2）

| 项 | 决策 | 影响章节 |
|----|------|---------|
| USB 范围 | 全功能：CDC / HID / MSC / Audio / 复合设备 | 4.5、阶段 5 |
| 低功耗 | 需要 `hal_sys_sleep()`，支持 sleep/standby/shutdown + 多唤醒源 | 4.7、阶段 6 |
| 多芯片顺序 | CH59x → CH32 系列 → 其他 | 3、阶段 8 |
| 日志通道 | 默认 UART，运行时可切换 USB CDC / 其他 UART / EEPROM | 4.6、阶段 3 |
| BLE GATT | 多服务、多特征值，动态注册；预留经典蓝牙 | 4.4、阶段 4 |

### 8.1 仍需确认的细节

1. **USB MSC 存储介质**：用芯片内部 Flash 的一段，还是外部 SPI Flash / SD 卡？
2. **HID 类型**：需要标准键盘/鼠标，还是自定义 HID 报告？
3. **经典蓝牙**：是否有具体目标型号（CH582/CH583 支持 BR/EDR）？还是仅架构预留？
4. **日志 EEPROM sink**：环形缓冲大小？是否需要在复位后保留？
5. **低功耗目标电流**：是否有具体指标（决定是否需要 shutdown 模式）？

---

## 附录 A：本次调试的关键结论（供实现参考）

1. **`PFIC->IENR` / `IRER` / `IPSR` / `IPRR` 均为只写**，读回恒 0
2. **`NVIC_EnableIRQ(n)` = `IENR[n>>5] = 1<<(n&31)`，是赋值，会清掉同字其它位**
3. **`ISR[0]`（0xE000E000）可读**，反映当前 pending 状态
4. **`mtvec = InterruptVector | 3`**（WCH VTF 模式），向量表在 `InterruptVector` 处
5. **`RB_IER_TXD_EN (0x40)` 必须置位**，否则 UART TXD 无波形
6. **`.highcode` 段必须从 Flash 拷到 RAM**（`_highcode_lma` → `_highcode_vma_start`）
7. **官方 `startup_CH592.S` 的 `BB_IRQHandler` / `LLE_IRQHandler` 是弱符号**，需 `ble_task_scheduler.S` 覆盖
8. **`ble_task_scheduler.S` 必须保存 x1/x4-x31 共 28 个寄存器**，C 版本会导致 `BLE_LibInit` 卡死
9. **CH592 的 `0x0000` 处是向量表**，`[0]` 为 `j handle_reset`（`FUNCONF_ISR_IN_RAM=0` 时）
10. **EEPROM（Data Flash）是唯一可靠的无输出诊断通道**：`wchisp eeprom dump` / `erase`

---

## 附录 B：新架构实现中踩到的坑（阶段 0-4 实测）

| # | 坑 | 现象 | 解决 |
|---|-----|------|------|
| 1 | **`hal.h` 与官方 `HAL.h` 文件名冲突** | 官方 HAL 声明全部丢失，`HAL_GetInterTempValue` 未声明 | 我们的总入口改名 `hal_core.h`（Windows 文件系统大小写不敏感） |
| 2 | **`EEPROM_ERASE` 的 Length 必须是 4096 整数倍** | 程序静默卡死在 `while(1)`（`ISP592.h` 第 148 行） | 传 4096，不是 256 |
| 3 | **`ISP592.h` 无 include guard** | 重复包含导致 `EEPROM_ERASE` 重定义 | 补 `#ifndef __ISP592_H__`（唯一改动的 vendor 文件） |
| 4 | **newlib `vsnprintf` 拖进大量代码** | FLASH 从 158KB 涨到 201KB，RAM 溢出 | `hal_log.c` 自带轻量格式化（省 43KB FLASH） |
| 5 | **中断入口重复定义** | `RTC_IRQHandler` / `LLE_IRQHandler` multiple definition | trampoline 里改 `.weak` |
| 6 | **`MEM_BUF` 未定义** | `undefined reference to MEM_BUF` | 应用侧定义 `uint32_t MEM_BUF[BLE_MEMHEAP_SIZE/4]` |
| 7 | **BLE HAL 依赖 ADC** | `undefined reference to ADC_InterTSSampInit` | `CHIP_BLE_EXTRA_SOURCES` 加入 `CH59x_adc.c` |
| 8 | **SysTick 缺 STRE 位** | 延时函数死锁（tick 不递增） | `CTLR` 加 `SysTick_CTLR_STRE` |
| 9 | **`delay_ms` 依赖中断** | 中断未使能时死锁 | 改用 SysTick 硬件计数器忙等 |
| 10 | **BLE 堆 6KB 导致 RAM 溢出** | `region RAM overflowed` | 降到 4096（官方最小值），UART 缓冲降到 128 |

### 实测资源占用（`ble_echo`）

```
FLASH: 155464 B (33.89% of 448K)
RAM:   26 KB (100% 报告值，实际余量 ~3.5KB 栈)
  .highcode  0x303C (12348 B)  ← BLE 库代码，RAM 执行
  .data      0x1E0  (480 B)
  .bss       0x2830 (10288 B)  ← 含 MEM_BUF 4096
```

### 构建命令

```powershell
# 配置
cmake -B build_hal -G Ninja `
      "-DCMAKE_TOOLCHAIN_FILE=C:/.../sdk/cmake/toolchain-riscv.cmake" `
      -DHAL_BUILD=ON -DCHIP=ch59x

# 编译
cmake --build build_hal --target ble_echo

# 烧录
.\flash_hal.ps1 ble_echo
```

### 当前进度

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 拷贝官方库到 sdk/vendor/ch59x | ✅ 完成 |
| 1 | common 层 + port 层 + CMake + blink | ✅ 编译通过 |
| 2 | 中断子系统（影子副本 + 汇编 trampoline） | ✅ 编译通过，待硬件验证 |
| 3 | UART + 可切换日志 | ✅ 编译通过，待硬件验证 |
| 4 | BLE 多服务 | ✅ 编译通过，待硬件验证 |
| 5 | USB 全功能 | ⬜ 未开始 |
| 6 | 低功耗 | ⬜ 未开始 |
