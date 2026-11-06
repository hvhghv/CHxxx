# USB 复合设备框架 —— 端点池 + 统一类注册

> 本文档说明通用层 USB 框架的设计，以及如何新增一个 USB 功能类
> （CDC / HID / NCM / MSC / Audio / Vendor）。

---

## 1. 设计目标

| 目标 | 说明 |
|---|---|
| **端点自动分配** | 类驱动不写死端点号，向核心申请即可 |
| **接口号自动分配** | 核心按 `if_count` 累加，类驱动只管填 `bInterfaceNumber` |
| **IAD 自动生成** | 多接口功能自动插入 IAD，不用每个类各写一遍 |
| **描述符自动拼装** | `wTotalLength` / `bNumInterfaces` 自动回填 |
| **资源预检** | 端点不够时注册立即失败，不会配出无效端点 |
| **可插拔** | 新增功能类只需实现 `hal_usb_class_drv_t` |

---

## 2. 分层结构

```
应用层 main/*.c
    │  hal_usb_cdc_add() / hal_usb_hid_add() / ...
    ▼
类驱动层 sdk/hal/src/hal_usb_<class>_common.c
    │  实现 hal_usb_class_drv_t 回调
    │  通过 hal_usb_ep_alloc() 申请端点
    ▼
USB 核心 sdk/port/ch59x/hal_usb_ch59x.c
    │  端点池、描述符拼装、IAD 生成、SETUP 状态机
    ▼
厂商库 sdk/vendor/ch59x/StdPeriphDriver
```

---

## 3. 端点资源池

### 3.1 容量

CH59x 端点资源：

| 端点 | 说明 |
|---|---|
| EP0 | 控制传输，**不可分配** |
| EP1~EP3 | 独立缓冲 |
| EP4 | 与 EP0 共享缓冲（`UEP0_DMA+64/+128`），可用 |
| EP5~EP7 | 独立缓冲 |

**端点池 = EP1~EP7，共 7 个端点 × 2 方向 = 14 个方向端点。**

### 3.2 API

```c
/* 自动选号 */
hal_err_t hal_usb_ep_alloc(uint8_t type, uint8_t dir, uint16_t max_packet,
                           hal_usb_ep_t *out);

/* 指定端点号（兼容旧代码；已被占用返回 HAL_ERR_BUSY） */
hal_err_t hal_usb_ep_alloc_at(uint8_t ep_num, uint8_t type, uint8_t dir,
                              uint16_t max_packet, hal_usb_ep_t *out);

/* 释放 */
hal_err_t hal_usb_ep_free(uint8_t ep_num, uint8_t dir);
void      hal_usb_ep_free_all(void);

/* 查询 */
void      hal_usb_ep_pool_info(hal_usb_ep_pool_t *info);
uint8_t   hal_usb_ep_is_used(uint8_t ep_num, uint8_t dir);
```

**参数：**

| 参数 | 取值 |
|---|---|
| `type` | `HAL_USB_EP_BULK` / `HAL_USB_EP_INT` / `HAL_USB_EP_ISO` |
| `dir` | `HAL_USB_EP_IN` / `HAL_USB_EP_OUT` |
| `max_packet` | 64（批量）、8（中断）等 |

### 3.3 典型用法（类驱动内部）

```c
static uint8_t my_class_get_eps(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps)
{
    my_ctx_t *c = (my_ctx_t *)ctx;
    uint8_t n = 0;

    if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_OUT, 64, &eps[n]) == HAL_OK)
        c->ep_out = eps[n++].ep_num;

    if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_IN, 64, &eps[n]) == HAL_OK)
        c->ep_in = eps[n++].ep_num;

    return n;   /* 返回实际分配到的数量 */
}
```

---

## 4. 类驱动接口

### 4.1 结构体

```c
typedef struct {
    hal_usb_class_t type;
    const char     *name;

    /* ---- 接口布局（供核心自动分配接口号 / 生成 IAD）---- */
    uint8_t  if_count;        /* 占用接口数：CDC=2, HID=1, NCM=2 */
    uint8_t  need_iad;        /* 1 = 核心自动插入 IAD */
    uint8_t  func_class;      /* IAD 的 bFunctionClass */
    uint8_t  func_subclass;   /* IAD 的 bFunctionSubClass */
    uint8_t  func_protocol;   /* IAD 的 bFunctionProtocol */

    /* ---- 回调 ---- */
    uint16_t (*get_config_desc)(void *ctx, uint8_t *buf, uint16_t max_len,
                                uint8_t first_if_num);
    uint8_t  (*get_eps)(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps);
    hal_err_t(*init)(void *ctx);
    hal_err_t(*on_setup)(void *ctx, const uint8_t setup[8]);
    void     (*on_setup_data)(void *ctx, const uint8_t *data, uint16_t len);
    void     (*on_ep_in )(void *ctx, uint8_t ep, uint16_t len);
    void     (*on_ep_out)(void *ctx, uint8_t ep, uint16_t len);
    void     (*on_reset)(void *ctx);
    void     (*on_suspend)(void *ctx);
    void     (*on_resume)(void *ctx);
} hal_usb_class_drv_t;
```

### 4.2 各字段含义

| 字段 | 说明 |
|---|---|
| `if_count` | 该功能占用的 USB 接口数。CDC = 2（通信 + 数据），HID = 1，NCM = 2 |
| `need_iad` | 1 = 核心在第一个接口前自动插入 IAD。`if_count > 1` 时通常必须为 1 |
| `func_*` | IAD 里声明的功能类/子类/协议。CDC ACM = `0x02/0x02/0x01` |

### 4.3 回调职责

| 回调 | 何时调用 | 职责 |
|---|---|---|
| `get_config_desc` | 拼装配置描述符时 | 按 `first_if_num` 写接口 + 端点 + 类特定描述符，**不要写 IAD** |
| `get_eps` | SET_CONFIGURATION 时 | 用 `hal_usb_ep_alloc()` 申请端点，返回数量 |
| `init` | SET_CONFIGURATION 时 | 初始化内部状态、清缓冲 |
| `on_setup` | 收到类请求 SETUP 时 | 返回 `HAL_OK` 表示已处理 |
| `on_setup_data` | 类请求有 OUT 数据时 | 接收数据（如 `SET_LINE_CODING` 的 7 字节） |
| `on_ep_in/out` | 数据端点传输完成 | 收发数据 |

---

## 5. 描述符自动拼装流程

`rebuild_config_desc()` 内部逻辑：

```
1. 写 9 字节配置描述符头（wTotalLength/bNumInterfaces 先留空）
2. 遍历所有已注册的类：
   a. 若 need_iad && if_count > 1 → 插入 8 字节 IAD
   b. 调用 get_config_desc(ctx, buf+i, ..., if_num)
   c. if_num += if_count
3. 回填 wTotalLength = 累计字节数
4. 回填 bNumInterfaces = if_num
```

**类驱动完全不需要关心：**
- IAD 的 `bFirstInterface` / `bInterfaceCount`
- 接口号分配
- 总长度计算
- `bNumInterfaces`

---

## 6. 新增一个 USB 功能类（以 HID 为例）

### 步骤 1：定义上下文和配置

```c
typedef struct {
    uint8_t  ep_in;
    const uint8_t *report_desc;
    uint16_t report_len;
    uint8_t  in_use;
} hid_ctx_t;

typedef struct {
    uint8_t  ep_in;          /* 0 = 自动分配 */
    const uint8_t *report_desc;
    uint16_t report_len;
} hal_usb_hid_cfg_t;
```

### 步骤 2：实现描述符生成

```c
static uint16_t hid_get_config_desc(void *ctx, uint8_t *buf, uint16_t max_len,
                                    uint8_t first_if)
{
    hid_ctx_t *c = (hid_ctx_t *)ctx;
    uint16_t i = 0;

    /* 接口描述符 */
    buf[i++] = 0x09; buf[i++] = 0x04;
    buf[i++] = first_if;      /* ← 用核心传进来的接口号 */
    buf[i++] = 0x00;          /* bAlternateSetting */
    buf[i++] = 0x01;          /* bNumEndpoints */
    buf[i++] = 0x03;          /* bInterfaceClass = HID */
    buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00;

    /* HID 描述符 */
    buf[i++] = 0x09; buf[i++] = 0x21;
    buf[i++] = 0x11; buf[i++] = 0x01;   /* bcdHID = 1.11 */
    buf[i++] = 0x00;                    /* bCountryCode */
    buf[i++] = 0x01;                    /* bNumDescriptors */
    buf[i++] = 0x22;                    /* REPORT */
    buf[i++] = (uint8_t)(c->report_len & 0xFF);
    buf[i++] = (uint8_t)(c->report_len >> 8);

    /* 端点描述符 */
    buf[i++] = 0x07; buf[i++] = 0x05;
    buf[i++] = (uint8_t)(0x80 | c->ep_in);
    buf[i++] = 0x03;                    /* Interrupt */
    buf[i++] = 0x08; buf[i++] = 0x00;   /* 8 字节 */
    buf[i++] = 0x0A;                    /* 10ms */

    return i;
}
```

### 步骤 3：实现端点申请

```c
static uint8_t hid_get_eps(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps)
{
    hid_ctx_t *c = (hid_ctx_t *)ctx;
    if (max_eps < 1) return 0;
    if (hal_usb_ep_alloc(HAL_USB_EP_INT, HAL_USB_EP_IN, 8, &eps[0]) != HAL_OK)
        return 0;
    c->ep_in = eps[0].ep_num;
    return 1;
}
```

### 步骤 4：注册类驱动

```c
static const hal_usb_class_drv_t s_hid_drv = {
    .type            = HAL_USB_CLASS_HID,
    .name            = "hid",
    .if_count        = 1,       /* HID 只占 1 个接口 */
    .need_iad        = 0,       /* 单接口不需要 IAD */
    .get_config_desc = hid_get_config_desc,
    .get_eps         = hid_get_eps,
    .init            = hid_init,
    .on_setup        = hid_on_setup,   /* 处理 GET_DESCRIPTOR(REPORT) */
    .on_ep_in        = hid_on_ep_in,
};
```

### 步骤 5：对外 API

```c
hal_usb_hid_handle_t hal_usb_hid_add(const hal_usb_hid_cfg_t *cfg)
{
    /* 找空闲槽位 → 填 cfg → u->add_class(u, &s_hid_drv, ctx) */
}
```

**完成。** 核心会自动：
- 分配接口号
- 拼装描述符
- 分配端点
- 在 SET_CONFIGURATION 时调用 `init` 和 `get_eps`

---

## 7. 组合示例

### 7.1 3 个 CDC（已验证）

```c
hal_usb_t *u = hal_usb_get();

/* 前两个用默认配置（带通知端点） */
hal_usb_cdc_handle_t h1 = hal_usb_cdc_add(NULL, on_rx1, NULL);
hal_usb_cdc_handle_t h2 = hal_usb_cdc_add(NULL, on_rx2, NULL);

/* 第三个必须省掉通知端点，否则端点不够 */
hal_usb_cdc_cfg_t c3 = { .ep_notify = 0 };
hal_usb_cdc_handle_t h3 = hal_usb_cdc_add(&c3, on_rx3, NULL);

u->init(u, &desc);
```

**结果：COM30 / COM31 / COM32 全部正常。**

### 7.2 CDC + HID（规划中）

```c
hal_usb_cdc_handle_t cdc = hal_usb_cdc_add(NULL, on_cdc_rx, NULL);

hal_usb_hid_cfg_t hid_cfg = {
    .report_desc = my_report,
    .report_len  = sizeof(my_report),
};
hal_usb_hid_handle_t hid = hal_usb_hid_add(&hid_cfg);

u->init(u, &desc);
```

**端点分配（自动）：**
```
CDC: EP1(notify) + EP2(out) + EP3(in)
HID: EP4(in)
```

**描述符（自动）：**
```
配置描述符 (bNumInterfaces = 3)
├─ IAD (接口 0-1, CDC)
├─ 接口 0: CDC 通信
├─ 接口 1: CDC 数据
└─ 接口 2: HID        ← 接口号自动分配，HID 驱动不用管
```

### 7.3 CDC + NCM + HID（规划中）

```c
hal_usb_cdc_add(NULL, on_cdc_rx, NULL);   /* 3 端点 */
hal_usb_ncm_add(&ncm_cfg, on_ncm);        /* 2 端点 */
hal_usb_hid_add(&hid_cfg);                /* 1 端点 */
/* 共 6 端点，EP1~EP7 够用 */
```

---

## 8. 端点预算速查

| 功能 | 端点需求 |
|---|---|
| CDC（带通知） | 3（notify IN + data OUT + data IN） |
| CDC（不带通知） | 2（data OUT + data IN） |
| HID | 1（interrupt IN） |
| MSC | 2（bulk OUT + bulk IN） |
| NCM | 2（bulk OUT + bulk IN）+ 1（notify）= 3 |
| Audio | 2~3 |

**CH59x 端点池 = 14 个方向端点**，组合示例：

| 组合 | 端点占用 | 可行 |
|---|---|---|
| 3 × CDC（不带通知） | 6 | ✅ 已验证 |
| 2 × CDC（带通知） | 6 | ✅ 已验证 |
| CDC + HID | 4 | ✅ |
| CDC + NCM + HID | 3 + 3 + 1 = 7 | ✅ |
| 2 × CDC + NCM | 3 + 3 + 3 = 9 | ✅ |
| 4 × CDC（不带通知） | 8 | ✅（受描述符缓冲限制） |

---

## 9. 注意事项

### 9.1 描述符缓冲大小

`cfg_desc[256]`，每个 CDC 约 51~58 字节（不含 IAD），IAD 8 字节。

| CDC 数量 | 描述符占用 | 可行 |
|---|---|---|
| 1 | 66 | ✅ |
| 2 | 132 | ✅ |
| 3 | 198 | ✅ |
| 4 | 264 | ❌ 超出 256 |

**要支持 4 个以上需把 `cfg_desc` 加大到 512。**

### 9.2 端点池生命周期

- `hal_usb_ep_free_all()` 在 `usb_deinit()` 里调用
- 注册失败时类驱动负责释放已申请的端点
- 重复 `init()` 前必须先 `deinit()`

### 9.3 IAD 与 bDeviceClass

多接口功能（`class_count > 1`）时，设备描述符自动用：

```
bcdUSB          = 0x0200
bDeviceClass    = 0xEF (Miscellaneous)
bDeviceSubClass = 0x02 (Common Class)
bDeviceProtocol = 0x01 (IAD)
```

单功能时用 `bDeviceClass = 0x02`（CDC）。

### 9.4 关键坑（已修复，勿回退）

| 坑 | 症状 | 修复 |
|---|---|---|
| `ep0_send` 不按 `wLength` 截断 | `usbccgp` 启动失败 `0xC0000001` | 所有 `GET_DESCRIPTOR` 按 `wLength` 截断 |
| 缺 IAD | Windows 把 CDC 数据接口当 HID 乱试 | 核心自动生成 IAD |
| 类请求返回 `HAL_OK` 后直接 `return` | EP0 停在 `0xC2`，`usbser` 超时 | 按方向自动响应 + `hal_usb_class_setup_reply()` |
| `SET_INTERFACE` 在 alt=0 时禁用端点 | Windows 用 alt=0，端点被关 | 无论 alt 值都重新 `ep_config` |
| 端点初始化遍历 EP1~EP7 | 写未使能的 EP5/6/7 导致不枚举 | 只初始化 EP1~EP4 |
