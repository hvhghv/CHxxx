# 通用层（HAL）API 接口总览

> 面向应用开发者的统一外设接口。应用代码只需 `#include "hal_core.h"`，
> 不出现任何芯片厂商头文件。

---

## 0. 架构分层

```
Application (main/*.c)
        │  只用 hal_*.h
        ▼
Common  (sdk/hal/include, sdk/hal/src)     ← 与芯片无关
        │  只定义接口 + 通用逻辑
        ▼
Port    (sdk/port/ch59x/*.c)                 ← 芯片相关实现
        │  调用官方库
        ▼
Vendor  (sdk/vendor/ch59x/)                  ← 厂商 SDK
```

**依赖方向严格单向**：Application → Common → Port → Vendor。

---

## 1. 基础类型 `hal_types.h`

### 1.1 错误码 `hal_err_t`

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `HAL_OK` | 成功 |
| -1 | `HAL_ERR_PARAM` | 参数非法 |
| -2 | `HAL_ERR_TIMEOUT` | 超时 |
| -3 | `HAL_ERR_BUSY` | 忙（上次操作未完成） |
| -4 | `HAL_ERR_NOMEM` | 无内存 / 缓冲不足 |
| -5 | `HAL_ERR_IO` | 硬件错误 |
| -6 | `HAL_ERR_NOINIT` | 未初始化 |
| -7 | `HAL_ERR_UNSUP` | 芯片不支持该功能 |
| -8 | `HAL_ERR_FULL` | 队列/表已满 |
| -9 | `HAL_ERR_EMPTY` | 队列为空 |

### 1.2 设备基类 `hal_dev_t`

所有外设实例的第一个成员，用于统一管理：

```c
typedef struct {
    const char *name;        /* 实例名，如 "uart0" / "ble" / "i2c0" */
    uint8_t     initialized; /* 0 = 未初始化，1 = 已初始化 */
} hal_dev_t;
```

### 1.3 引脚编码

统一 `(port << 4) | pin`，port 0=A 1=B 2=C 3=D：

```c
HAL_PIN(port, pin)     /* 构造引脚编码 */
HAL_PIN_PORT(p)        /* 取端口号 */
HAL_PIN_NUM(p)         /* 取引脚号 */
HAL_PIN_NONE           /* 0xFF，表示不使用 */
```

常用简写：`HAL_PA8` `HAL_PA9` `HAL_PA12` `HAL_PA13` `HAL_PA14` `HAL_PA15`
`HAL_PB10` `HAL_PB11` `HAL_PB22` `HAL_PB23`

### 1.4 辅助宏

```c
HAL_ARRAY_SIZE(a)      /* 数组元素个数 */
HAL_UNUSED(x)          /* 消除未使用警告 */
HAL_MIN(a, b)
HAL_MAX(a, b)
HAL_STATIC_ASSERT(cond, msg)
```

---

## 2. 系统 `hal_sys.h`

```c
hal_sys_t *s = hal_sys_get();
```

### 接口

| 函数 | 说明 |
|---|---|
| `init(s)` | 系统初始化（时钟树、外设上电） |
| `get_clock_hz(s)` | 获取当前主频（Hz） |
| `set_clock_hz(s, hz)` | 设置主频 |
| `reset(s)` | 软复位 |
| `get_uid(s, uid[8])` | 读芯片唯一 ID（8 字节） |
| `sleep(s, cfg)` | 进入低功耗，返回唤醒原因位掩码 |
| `get_wake_reason(s)` | 查询上次唤醒原因（不睡眠） |
| `clear_wake_reason(s)` | 清除唤醒原因 |

### 低功耗配置 `hal_sleep_cfg_t`

```c
typedef struct {
    hal_sleep_mode_t mode;
    uint32_t wake_sources;      /* HAL_WAKE_* 位或 */
    uint32_t rtc_wake_ms;       /* RTC 定时唤醒毫秒，0 = 不定时 */
    uint8_t  gpio_wake_pin;     /* GPIO 唤醒引脚，HAL_PIN_NONE = 不用 */
    uint8_t  gpio_wake_level;   /* 0 = 低电平，1 = 高电平 */
    uint8_t  gpio_wake_port;
} hal_sleep_cfg_t;
```

**睡眠模式** `hal_sleep_mode_t`：

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `HAL_SLEEP_MODE_SLEEP` | 仅关 CPU 时钟，外设继续运行 |
| 1 | `HAL_SLEEP_MODE_STANDBY` | 深度睡眠，RAM 保持 |
| 2 | `HAL_SLEEP_MODE_SHUTDOWN` | 最低功耗，RAM 丢失（唤醒等同复位） |

**唤醒源**：

```c
HAL_WAKE_GPIO   HAL_WAKE_RTC   HAL_WAKE_USB
HAL_WAKE_BLE    HAL_WAKE_UART  HAL_WAKE_TIMER
```

---

## 3. 时间 `hal_time.h`

```c
hal_time_t *t = hal_time_get();
```

| 函数 | 说明 |
|---|---|
| `init(t)` | 初始化时基（main 开头调一次） |
| `delay_ms(t, ms)` | 毫秒延时 |
| `delay_us(t, us)` | 微秒延时 |
| `get_tick_ms(t)` | 自启动毫秒 tick（32 位，约 49 天回绕） |
| `get_tick_us(t)` | 微秒级时间戳 |
| `timer_create(t, period_ms, repeat, cb, user)` | 创建软件定时器，返回 ID |
| `timer_start(t, id)` | 启动 |
| `timer_stop(t, id)` | 停止 |
| `timer_delete(t, id)` | 删除 |
| `tick_isr(t)` | port 层 tick 中断内部调用 |

---

## 4. 中断 `hal_irq.h`

> **设计要点**：CH59x 的 `PFIC->IENR` / `IRER` 是**只写**寄存器，
> 官方 `NVIC_EnableIRQ(n)` 是**赋值**语义，会清掉同字其它位。
> 本层用 RAM 影子副本维护，绝不破坏其它中断。

| 函数 | 说明 |
|---|---|
| `hal_irq_enable(irqn)` | 安全使能（不影响同字其它中断） |
| `hal_irq_disable(irqn)` | 安全禁用 |
| `hal_irq_is_enabled(irqn)` | 查询是否使能（读影子副本） |
| `hal_irq_set_priority(irqn, prio)` | 设置优先级 |
| `hal_irq_set_pending(irqn)` | 手动置 pending |
| `hal_irq_clear_pending(irqn)` | 清除 pending |
| `hal_irq_get_pending_mask(word_idx)` | 读 pending 状态 |
| `hal_irq_global_enable()` | 开全局中断 |
| `hal_irq_global_disable()` | 关全局中断 |
| `hal_irq_global_is_enabled()` | 查询全局中断状态 |
| `hal_irq_save()` | 保存并关全局中断，返回原状态 |
| `hal_irq_restore(state)` | 恢复 |
| `hal_irq_attach(irqn, isr, user)` | 注册 C 中断处理函数 |
| `hal_irq_detach(irqn)` | 解除注册 |
| `hal_irq_dispatch(irqn)` | port 层 trampoline 内部调用 |
| `hal_irq_get_shadow(out[8])` | 诊断：读影子副本 |

**优先级常量**：

```c
HAL_IRQ_PRIO_HIGHEST  HAL_IRQ_PRIO_HIGH
HAL_IRQ_PRIO_NORMAL   HAL_IRQ_PRIO_LOW
```

**ISR 回调类型**：

```c
typedef void (*hal_isr_t)(void *user);
```

---

## 5. GPIO `hal_gpio.h`

```c
hal_gpio_t *g = hal_gpio_get();
```

| 函数 | 说明 |
|---|---|
| `init(g, pin, mode, pull)` | 初始化引脚（pull: 0=none 1=up 2=down） |
| `deinit(g, pin)` | 反初始化 |
| `write(g, pin, level)` | 写电平 |
| `read(g, pin)` | 读电平 |
| `toggle(g, pin)` | 翻转 |
| `write_port(g, port, value)` | 批量写整个端口（32 位） |
| `read_port(g, port)` | 批量读整个端口 |
| `set_irq(g, pin, edge, cb, user)` | 配置引脚中断 |
| `irq_dispatch(g, port)` | port 层 GPIOA/B_IRQHandler 调用 |

**模式** `hal_gpio_mode_t`：

| 值 | 名称 | 说明 |
|---|---|---|
| 0 | `HAL_GPIO_IN` | 浮空输入 |
| 1 | `HAL_GPIO_IN_PULLUP` | 上拉输入 |
| 2 | `HAL_GPIO_IN_PULLDOWN` | 下拉输入 |
| 3 | `HAL_GPIO_IN_ANALOG` | 模拟输入（ADC 用） |
| 4 | `HAL_GPIO_OUT_PP` | 推挽输出 |
| 5 | `HAL_GPIO_OUT_OD` | 开漏输出 |
| 6 | `HAL_GPIO_OUT_AF_PP` | 复用推挽 |
| 7 | `HAL_GPIO_OUT_AF_OD` | 复用开漏 |

**中断边沿** `hal_gpio_irq_t`：

```c
HAL_GPIO_IRQ_NONE  HAL_GPIO_IRQ_RISING
HAL_GPIO_IRQ_FALLING  HAL_GPIO_IRQ_BOTH
```

**回调**：`void (*hal_gpio_irq_cb_t)(uint8_t pin, void *user)`

---

## 6. UART `hal_uart.h`

```c
hal_uart_t *u = hal_uart_get(index);   /* index: 0..3 */
```

| 函数 | 说明 |
|---|---|
| `init(u, cfg)` | 初始化 |
| `deinit(u)` | 反初始化 |
| `write(u, buf, len)` | **阻塞写**，全部发出才返回 |
| `write_async(u, buf, len)` | **非阻塞写**，返回入队字节数 |
| `read(u, buf, len, timeout_ms)` | **阻塞读**，返回实际读到字节数 |
| `set_rx_cb(u, cb, user)` | 设置接收回调 |
| `set_tx_done_cb(u, cb, user)` | 设置发送完成回调 |
| `irq_handler(u)` | port 层 UARTx_IRQHandler 调用 |

**配置** `hal_uart_cfg_t`：

```c
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;   /* 5..8，默认 8 */
    uint8_t  stop_bits;   /* 1..2，默认 1 */
    uint8_t  parity;      /* 0=none 1=odd 2=even */
    uint8_t  tx_pin;      /* HAL_PIN 编码，HAL_PIN_NONE = 不用 */
    uint8_t  rx_pin;
    uint8_t  use_dma;     /* 0=中断 1=DMA */
} hal_uart_cfg_t;
```

**回调**：

```c
void (*hal_uart_rx_cb_t)(hal_uart_t *u, const uint8_t *data,
                         uint16_t len, void *user);
void (*hal_uart_tx_done_cb_t)(hal_uart_t *u, void *user);
```

---

## 7. 日志 `hal_log.h`

> **需求**：默认走 UART，可一键切换 USB CDC / 其他 UART / EEPROM。

| 函数 | 说明 |
|---|---|
| `hal_log_init()` | 初始化（默认后端 NONE，级别 INFO） |
| `hal_log_set_sink(sink, arg)` | 切换后端 |
| `hal_log_get_sink()` | 查询当前后端 |
| `hal_log_set_level(lvl)` | 设置级别 |
| `hal_log_get_level()` | 查询级别 |
| `hal_log_enabled(lvl)` | 判断某级别是否启用 |
| `hal_log_printf(fmt, ...)` | 格式化输出 |
| `hal_log_write(data, len)` | 原始字节输出 |
| `hal_log_hexdump(data, len)` | 十六进制 dump |
| `hal_log_ram_read(buf, max_len)` | 读 RAM 环形缓冲 |
| `hal_log_ram_clear()` | 清 RAM 缓冲 |

**后端** `hal_log_sink_t`：

| 值 | 名称 | arg 类型 |
|---|---|---|
| 0 | `HAL_LOG_SINK_NONE` | — |
| 1 | `HAL_LOG_SINK_UART` | `hal_uart_t*` |
| 2 | `HAL_LOG_SINK_USB_CDC` | NULL |
| 3 | `HAL_LOG_SINK_EEPROM` | `uint32_t*` 起始地址 |
| 4 | `HAL_LOG_SINK_CUSTOM` | `hal_log_write_fn` |
| 5 | `HAL_LOG_SINK_RAM` | NULL（诊断用） |

**级别** `hal_log_level_t`：

```c
HAL_LOG_LVL_NONE  HAL_LOG_LVL_ERROR  HAL_LOG_LVL_WARN
HAL_LOG_LVL_INFO  HAL_LOG_LVL_DEBUG
```

**便捷宏**：

```c
LOG_E(...)  LOG_W(...)  LOG_I(...)  LOG_D(...)
```

**自定义后端**：`int (*hal_log_write_fn)(const uint8_t *data, uint16_t len, void *user)`

---

## 8. USB `hal_usb.h` + `hal_usb_cdc.h`

### 8.1 USB 核心 `hal_usb.h`

```c
hal_usb_t *u = hal_usb_get();
```

| 函数 | 说明 |
|---|---|
| `add_class(u, drv, ctx)` | 注册功能类（**必须在 init 之前**） |
| `init(u, desc)` | 初始化并连接 |
| `deinit(u)` | 断开 |
| `reconnect(u)` | 重新枚举（改描述符后调用） |
| `ep_write(u, ep, d, n)` | 底层端点写（类驱动内部用） |
| `ep_read(u, ep, d, n)` | 底层端点读 |
| `poll(u)` | 主循环处理事件 |
| `is_configured(u)` | 是否枚举完成 |

**功能类** `hal_usb_class_t`：

```c
HAL_USB_CLASS_CDC     /* 通信设备类：虚拟串口 */
HAL_USB_CLASS_HID     /* 人机接口：键盘/鼠标/自定义 */
HAL_USB_CLASS_MSC     /* 大容量存储 */
HAL_USB_CLASS_AUDIO   /* 音频 */
HAL_USB_CLASS_VENDOR  /* 厂商自定义 */
```

**端点类型 / 方向**：

```c
HAL_USB_EP_CTRL  HAL_USB_EP_ISO  HAL_USB_EP_BULK  HAL_USB_EP_INT
HAL_USB_EP_OUT   HAL_USB_EP_IN
```

**设备描述** `hal_usb_dev_desc_t`：

```c
typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    uint16_t    bcd_device;
    const char *manufacturer;
    const char *product;
    const char *serial;
    uint8_t     max_power_ma;
    uint8_t     self_powered;
} hal_usb_dev_desc_t;
```

**类驱动接口** `hal_usb_class_drv_t`（实现这些回调即可新增一个 USB 类）：

```c
uint16_t (*get_config_desc)(uint8_t *buf, uint16_t max_len, uint8_t first_if_num);
uint8_t  (*get_eps)(hal_usb_ep_t *eps, uint8_t max_eps);
hal_err_t(*init)(void *ctx);
hal_err_t(*on_setup)(void *ctx, const uint8_t setup[8]);
void     (*on_ep_in )(void *ctx, uint8_t ep, uint16_t len);
void     (*on_ep_out)(void *ctx, uint8_t ep, uint16_t len);
void     (*on_reset)(void *ctx);
void     (*on_suspend)(void *ctx);
void     (*on_resume)(void *ctx);
```

### 8.2 CDC 便捷封装 `hal_usb_cdc.h`

| 函数 | 说明 |
|---|---|
| `hal_usb_cdc_add(cfg, cb, user)` | 注册 CDC 类（**必须在 usb init 之前**） |
| `hal_usb_cdc_write(data, len)` | 非阻塞发送，返回入队字节数 |
| `hal_usb_cdc_connected()` | 是否已连接（主机置位 DTR） |
| `hal_usb_cdc_log_write(data, len)` | 供 hal_log 使用 |

**配置** `hal_usb_cdc_cfg_t`：

```c
typedef struct {
    uint8_t  ep_in;        /* 数据 IN 端点，默认 3 */
    uint8_t  ep_out;       /* 数据 OUT 端点，默认 2 */
    uint8_t  ep_notify;    /* 中断 IN 端点，默认 1 */
    uint16_t rx_buf_size;  /* 默认 256 */
    uint16_t tx_buf_size;  /* 默认 256 */
} hal_usb_cdc_cfg_t;
```

**回调**：`void (*hal_usb_cdc_rx_cb_t)(const uint8_t *data, uint16_t len, void *user)`

---

## 9. BLE `hal_ble.h`

```c
hal_ble_t *b = hal_ble_get();
```

| 函数 | 说明 |
|---|---|
| `init(b, cfg)` | 初始化协议栈 |
| `deinit(b)` | 关闭 |
| `add_service(b, def, out_chars, cbs)` | 注册服务（init 之后、adv_start 之前） |
| `bind_cb(b, ch, cbs)` | 为特征值绑定/更新回调 |
| `adv_start(b)` | 开始广播 |
| `adv_stop(b)` | 停止广播 |
| `notify(b, ch, conn, data, len)` | 发送通知（需对端订阅 CCCD） |
| `indicate(b, ch, conn, data, len)` | 发送指示（需对端确认） |
| `set_value(b, ch, data, len)` | 主动更新特征值 |
| `set_evt_cb(b, cb, user)` | 设置事件回调 |
| `poll(b)` | 协议栈主循环（应用 while(1) 周期调用） |
| `is_connected(b)` | 是否已连接 |

### 9.1 GATT 声明式定义

**特征值定义** `hal_ble_char_def_t`：

```c
typedef struct {
    const char     *name;        /* 调试用，如 "TX" */
    hal_ble_uuid_t  uuid;
    uint8_t         props;       /* HAL_BLE_PROP_* */
    uint8_t         perms;       /* HAL_BLE_PERM_* */
    uint16_t        max_len;
    const uint8_t  *init_value;  /* 可为 NULL */
    uint16_t        init_len;
} hal_ble_char_def_t;
```

**服务定义** `hal_ble_service_def_t`：

```c
typedef struct {
    const char               *name;
    hal_ble_uuid_t            uuid;
    uint8_t                   char_count;
    const hal_ble_char_def_t *chars;
} hal_ble_service_def_t;
```

**运行时句柄** `hal_ble_char_t`（注册后返回，用于 notify/读写）：

```c
typedef struct {
    uint16_t service_idx;
    uint16_t char_idx;
    uint16_t value_handle;   /* 协议栈分配的属性句柄 */
    uint16_t cccd_handle;    /* CCCD 句柄，0 = 无 */
} hal_ble_char_t;
```

### 9.2 属性与权限

```c
HAL_BLE_PROP_READ      0x02
HAL_BLE_PROP_WRITE_NR  0x04   /* Write Without Response */
HAL_BLE_PROP_WRITE     0x08
HAL_BLE_PROP_NOTIFY    0x10
HAL_BLE_PROP_INDICATE  0x20

HAL_BLE_PERM_READ      0x01
HAL_BLE_PERM_WRITE     0x02
```

**UUID 宏**：`HAL_BLE_UUID16(v)`

### 9.3 回调

```c
/* 特征值写 */
void (*hal_ble_write_cb_t)(const hal_ble_char_t *ch,
                           const uint8_t *data, uint16_t len,
                           uint16_t conn_handle, void *user);
/* 特征值读（动态值） */
uint16_t (*hal_ble_read_cb_t)(const hal_ble_char_t *ch,
                              uint8_t *buf, uint16_t max_len,
                              uint16_t conn_handle, void *user);
/* CCCD 变更（手机订阅/取消订阅） */
void (*hal_ble_cccd_cb_t)(const hal_ble_char_t *ch, uint8_t enabled,
                          uint16_t conn_handle, void *user);
/* 协议栈事件 */
void (*hal_ble_evt_cb_t)(hal_ble_evt_t evt, const void *param, void *user);
```

**回调集合** `hal_ble_char_cbs_t`：

```c
typedef struct {
    hal_ble_write_cb_t write_cb;
    hal_ble_read_cb_t  read_cb;
    hal_ble_cccd_cb_t  cccd_cb;
    void              *user;
} hal_ble_char_cbs_t;
```

### 9.4 事件 `hal_ble_evt_t`

| 事件 | param 类型 |
|---|---|
| `HAL_BLE_EVT_ADV_STARTED` | — |
| `HAL_BLE_EVT_ADV_STOPPED` | — |
| `HAL_BLE_EVT_CONNECTED` | `hal_ble_conn_t*` |
| `HAL_BLE_EVT_DISCONNECTED` | `hal_ble_conn_t*` |
| `HAL_BLE_EVT_MTU_CHANGED` | `hal_ble_conn_t*` |
| `HAL_BLE_EVT_PASSKEY` | `uint32_t*` |
| `HAL_BLE_EVT_PAIRING_DONE` | — |

**连接信息** `hal_ble_conn_t`：

```c
typedef struct {
    uint16_t conn_handle;
    uint8_t  addr[6];      /* 对端地址（小端） */
    uint8_t  addr_type;
    uint16_t mtu;
} hal_ble_conn_t;
```

### 9.5 配置 `hal_ble_cfg_t`

```c
typedef struct {
    const char *device_name;
    uint8_t  adv_data[31];       /* 自定义广播包，adv_len=0 用默认 */
    uint8_t  adv_len;
    uint8_t  scan_rsp_data[31];
    uint8_t  scan_rsp_len;
    uint16_t adv_interval_ms;    /* 默认 100ms */
    uint16_t min_conn_interval_ms;
    uint16_t max_conn_interval_ms;
    uint16_t mtu;                /* 默认 23，可设 247 */
    uint8_t  max_connections;    /* 1..4 */
    uint8_t  enable_pairing;     /* 1 = 启用绑定 */
} hal_ble_cfg_t;
```

---

## 10. 总入口 `hal_core.h`

```c
#include "hal_core.h"

hal_err_t hal_init(void);   /* main() 最开始调用一次 */
```

内部按序初始化：系统时钟 → 时基 → 中断 → GPIO。

---

## 11. 实现状态

| 模块 | 头文件 | 实现文件 | 状态 |
|---|---|---|---|
| 基础类型 | `hal_types.h` | 头文件内联 | ✅ 完成 |
| 系统 | `hal_sys.h` | `hal_sys_ch59x.c` | ✅ 完成 |
| 时间 | `hal_time.h` | `hal_time_ch59x.c` | ✅ 完成 |
| 中断 | `hal_irq.h` | `hal_irq_ch59x.c` + trampoline.S | ✅ 完成 |
| GPIO | `hal_gpio.h` | `hal_gpio_ch59x.c` | ✅ 完成 |
| UART | `hal_uart.h` | `hal_uart_ch59x.c` | ✅ 完成 |
| 日志 | `hal_log.h` | `hal_log.c` | ✅ 完成 |
| USB 核心 | `hal_usb.h` | `hal_usb_ch59x.c` | ✅ 完成（COM15 枚举成功） |
| USB CDC | `hal_usb_cdc.h` | `hal_usb_cdc_common.c` | ✅ 完成 |
| BLE | `hal_ble.h` | `hal_ble_ch59x.c` | ⚠️ 部分（见下） |

### BLE 待补部分

| 功能 | 状态 | 说明 |
|---|---|---|
| 协议栈初始化 | ✅ | `CH59x_BLEInit` / `HAL_Init` / `GAPRole_PeripheralInit` |
| 广播 | ✅ | `adv_start` / `adv_stop` |
| 事件回调 | ✅ | 连接/断开/MTU |
| **GATT 服务注册** | ❌ | `ble_add_service` 只记录，未真正调用 `GATT_AddService` |
| **`notify`** | ❌ | 依赖 `hal_ble_profile_notify`，该函数**只有 weak 声明，无实现** |
| **`indicate`** | ❌ | 同上 |
| **`set_value`** | ⚠️ | 部分 |

> **结论**：BLE 目前能广播、能连接，但 **GATT 服务与通知功能尚未实现**，
> 需要补 `sdk/port/ch59x/ble_profile_ch59x.c`（参考官方 `BLE_USB/Profile/*.c`）。

---

## 12. 尚未实现的模块

`hal_core.h` 中已预留但未启用：

```c
/* hal_i2c.h        */   I2C
/* hal_spi.h        */   SPI
/* hal_pwm.h        */   PWM
/* hal_adc.h        */   ADC
/* hal_flash.h      */   Flash / EEPROM
/* hal_usb_hid.h    */   USB HID
/* hal_usb_msc.h    */   USB MSC
/* hal_bt_classic.h */   经典蓝牙
```

---

## 13. 典型用法

### 13.1 最小程序

```c
#include "hal_core.h"

int main(void)
{
    hal_init();

    hal_gpio_t *g = hal_gpio_get();
    g->init(g, HAL_PA13, HAL_GPIO_OUT_PP, 0);

    hal_uart_t *u = hal_uart_get(1);
    hal_uart_cfg_t cfg = { .baudrate = 115200,
                           .tx_pin = HAL_PA9, .rx_pin = HAL_PA8 };
    u->init(u, &cfg);

    hal_log_init();
    hal_log_set_sink(HAL_LOG_SINK_UART, u);
    hal_log_set_level(HAL_LOG_LVL_INFO);

    while (1) {
        g->toggle(g, HAL_PA13);
        LOG_I("tick=%u\r\n", hal_time_get()->get_tick_ms(hal_time_get()));
        hal_time_get()->delay_ms(hal_time_get(), 500);
    }
}
```

### 13.2 USB CDC + BLE 复合

```c
#include "hal_core.h"

static void on_usb_rx(const uint8_t *d, uint16_t n, void *user)
{
    hal_usb_cdc_write(d, n);           /* 回显 */
}

int main(void)
{
    hal_init();

    /* 1. 先注册 CDC 类 */
    hal_usb_cdc_cfg_t cdc = { .ep_in = 3, .ep_out = 2, .ep_notify = 1 };
    hal_usb_cdc_add(&cdc, on_usb_rx, NULL);

    /* 2. 再 init USB */
    hal_usb_dev_desc_t desc = { .vid = 0x1A86, .pid = 0x7523,
                                .bcd_device = 0x0263,
                                .product = "BLE+CDC" };
    hal_usb_t *u = hal_usb_get();
    u->init(u, &desc);

    /* 3. BLE */
    hal_ble_t *b = hal_ble_get();
    hal_ble_cfg_t bcfg = { .device_name = "CH592-BLE", .mtu = 247 };
    b->init(b, &bcfg);
    b->adv_start(b);

    while (1) {
        b->poll(b);
        u->poll(u);
    }
}
```

### 13.3 BLE 多服务多特征值

```c
/* 服务 1：Echo（2 个特征值） */
static const hal_ble_char_def_t echo_chars[] = {
    { .name = "RX", .uuid = HAL_BLE_UUID16(0xFFF1),
      .props = HAL_BLE_PROP_WRITE | HAL_BLE_PROP_WRITE_NR,
      .perms = HAL_BLE_PERM_WRITE, .max_len = 128 },
    { .name = "TX", .uuid = HAL_BLE_UUID16(0xFFF2),
      .props = HAL_BLE_PROP_NOTIFY, .perms = HAL_BLE_PERM_READ,
      .max_len = 128 },
};

static const hal_ble_service_def_t echo_service = {
    .name = "Echo", .uuid = HAL_BLE_UUID16(0xFFF0),
    .char_count = 2, .chars = echo_chars,
};

/* 服务 2：电池（1 个特征值） */
static const hal_ble_char_def_t batt_chars[] = {
    { .name = "Level", .uuid = HAL_BLE_UUID16(0x2A19),
      .props = HAL_BLE_PROP_READ | HAL_BLE_PROP_NOTIFY,
      .perms = HAL_BLE_PERM_READ, .max_len = 1 },
};

static const hal_ble_service_def_t batt_service = {
    .name = "Battery", .uuid = HAL_BLE_UUID16(0x180F),
    .char_count = 1, .chars = batt_chars,
};

/* 注册 */
hal_ble_char_t echo_h[2], batt_h[1];
hal_ble_char_cbs_t cbs = { .write_cb = on_write };
b->add_service(b, &echo_service, echo_h, &cbs);
b->add_service(b, &batt_service, batt_h, NULL);
```

### 13.4 日志后端切换

```c
/* 启动时走 UART */
hal_log_init();
hal_log_set_sink(HAL_LOG_SINK_UART, hal_uart_get(1));
hal_log_set_level(HAL_LOG_LVL_INFO);
LOG_I("boot ok\r\n");

/* USB 枚举完成后切到 CDC */
hal_log_set_sink(HAL_LOG_SINK_USB_CDC, NULL);
LOG_I("switched to USB CDC\r\n");
```

### 13.5 低功耗

```c
hal_sys_t *s = hal_sys_get();
hal_sleep_cfg_t scfg = {
    .mode            = HAL_SLEEP_MODE_STANDBY,
    .wake_sources    = HAL_WAKE_GPIO | HAL_WAKE_RTC,
    .rtc_wake_ms     = 60000,          /* 60 秒定时唤醒 */
    .gpio_wake_pin   = HAL_PB22,
    .gpio_wake_level = 0,              /* 低电平唤醒 */
    .gpio_wake_port  = 1,
};
uint32_t reason = s->sleep(s, &scfg);
LOG_I("woke up, reason=0x%04X\r\n", reason);
```

---

## 14. 设计约定

1. **实例获取统一用 `hal_xxx_get()`**，返回全局唯一指针。
2. **所有外设都是"结构体 + 函数指针"**，通过 `p->method(p, ...)` 调用。
3. **`hal_dev_t dev` 必须是结构体第一个成员**，便于统一管理。
4. **回调在中断上下文执行**，必须尽量短，不能阻塞。
5. **`poll()` 必须在主循环周期调用**（BLE / USB）。
6. **应用层不出现任何厂商头文件**。
