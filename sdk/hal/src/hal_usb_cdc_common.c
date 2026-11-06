/**
 * @file    hal_usb_cdc_common.c
 * @brief   USB CDC 类驱动（芯片无关部分）
 *
 * 负责：
 *   - 提供 CDC 的接口/端点描述符
 *   - 处理 CDC 类请求（SET_LINE_CODING / GET_LINE_CODING / SET_CONTROL_LINE_STATE）
 *   - 维护收发环形缓冲
 *   - DTR 状态跟踪
 *
 * 芯片相关部分（端点收发）由 port 层的 hal_usb_xxx.c 通过
 * hal_usb_t::ep_write / ep_read 提供。
 */
#include "hal_usb_cdc.h"
#include "hal_usb.h"
#include <string.h>

/* ===========================================================================
 * 描述符常量
 * =========================================================================== */
/* 类自身写的描述符长度（不含 IAD，IAD 由核心自动生成）
 *   通信接口 9 + Header 5 + CallMgmt 5 + ACM 4 + Union 5
 *   + 通知端点 7（可选）
 *   + 数据接口 9 + Bulk OUT 7 + Bulk IN 7
 *   = 58（带通知端点）
 * = 51（不带通知端点）
 * 这里取最大值 58 做缓冲检查。 */
#define CDC_DESC_LEN        58

/* 类特定描述符类型 */
#define CS_INTERFACE        0x24
#define CS_ENDPOINT         0x25

/* CDC 子类 */
#define CDC_SUBCLASS_ACM    0x02
#define CDC_PROTOCOL_V25    0x01

/* CDC 类请求 */
#define CDC_SET_LINE_CODING         0x20
#define CDC_GET_LINE_CODING         0x21
#define CDC_SET_CONTROL_LINE_STATE  0x22

/* ===========================================================================
 * 状态
 * ---------------------------------------------------------------------------
 * ★ 通用层不限制实例数量，也不假设芯片能力。
 *   实例用链表串起来，每个实例的缓冲由应用通过 cfg 提供，
 *   或者由本文件从静态池里切分（池大小由 HAL_USB_CDC_BUF_POOL 决定）。
 *
 *   真正的上限来自：
 *     1) port 层端点池（hal_usb_ep_alloc 返回 HAL_ERR_FULL）
 *     2) port 层描述符缓冲（cfg_desc 大小）
 *   两者都与芯片相关，由 port 层负责，通用层不关心。
 * =========================================================================== */
typedef struct cdc_ctx {
    struct cdc_ctx     *next;           /* 链表下一项 */

    hal_usb_cdc_cfg_t   cfg;
    hal_usb_cdc_rx_cb_t rx_cb;
    void               *rx_user;

    uint8_t             configured;
    uint8_t             dtr;            /* 主机置位 DTR */
    uint8_t             line_coding[7]; /* 波特率(4) + 停止位 + 校验 + 数据位 */

    /* 接收环形缓冲 */
    volatile uint16_t   rx_head;
    volatile uint16_t   rx_tail;
    uint8_t            *rx_buf;
    uint16_t            rx_size;

    /* 发送环形缓冲 */
    volatile uint16_t   tx_head;
    volatile uint16_t   tx_tail;
    uint8_t            *tx_buf;
    uint16_t            tx_size;
} cdc_ctx_t;

/* 实例链表头（按注册顺序） */
static cdc_ctx_t *s_cdc_list;

/* 实例计数器，用于生成句柄 */
static uint8_t s_cdc_count;

/* 从 ctx 指针反查实例序号（1 起） */
static uint8_t cdc_handle_of(const void *ctx)
{
    const cdc_ctx_t *p = s_cdc_list;
    uint8_t n = 1;

    while (p) {
        if ((const void *)p == ctx) return n;
        p = p->next;
        n++;
    }
    return HAL_USB_CDC_HANDLE_INVALID;
}

/* 按句柄查实例 */
static cdc_ctx_t *cdc_by_handle(hal_usb_cdc_handle_t h)
{
    cdc_ctx_t *p = s_cdc_list;

    if (h == HAL_USB_CDC_HANDLE_INVALID) return NULL;
    while (p && --h) p = p->next;
    return p;
}

/* 前向声明：公共注册流程（add / add_ex 共用） */
static hal_usb_cdc_handle_t cdc_register(cdc_ctx_t *c,
                                         const hal_usb_cdc_cfg_t *cfg,
                                         hal_usb_cdc_rx_cb_t cb, void *user);

/* 前向声明：把环形缓冲数据推到端点 */
static void cdc_flush(cdc_ctx_t *c);

/* ===========================================================================
 * 描述符生成
 * =========================================================================== */
static uint16_t cdc_get_config_desc(void *ctx, uint8_t *buf, uint16_t max_len,
                                    uint8_t first_if)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    uint8_t n = first_if;
    uint16_t i = 0;

    if (!c || max_len < CDC_DESC_LEN) return 0;

    /* ★ IAD（接口关联描述符）由核心自动生成
     *
     * 类驱动通过 s_cdc_drv 里的 if_count=2 / need_iad=1 声明：
     *   "我占 2 个接口，需要 IAD 分组"
     * 核心在调用本函数之前已经把 IAD 写进缓冲了，
     * 所以这里直接从「通信接口」开始写。
     *
     * 这样 CDC / NCM / 其他多接口功能都不必各自实现 IAD 逻辑，
     * 也不会因为忘记加 IAD 导致 Windows 乱试 HID 描述符。 */

    /* 通信接口（接口 n） */
    buf[i++] = 0x09;            /* bLength */
    buf[i++] = 0x04;            /* INTERFACE */
    buf[i++] = n;               /* bInterfaceNumber */
    buf[i++] = 0x00;            /* bAlternateSetting */
    /* ★ 通知端点可选：ep_notify == 0 时不声明通知端点
     *   → 通信接口 bNumEndpoints = 0，节省 1 个端点
     *   → 代价：无法发送 SERIAL_STATE 通知（本实现本来也没发过） */
    buf[i++] = c->cfg.ep_notify ? 0x01 : 0x00;  /* bNumEndpoints */
    buf[i++] = 0x02;            /* bInterfaceClass = CDC */
    buf[i++] = 0x02;            /* bInterfaceSubClass = ACM */
    buf[i++] = 0x01;            /* bInterfaceProtocol */
    buf[i++] = 0x00;            /* iInterface */

    /* Header 功能描述符 */
    buf[i++] = 0x05; buf[i++] = CS_INTERFACE; buf[i++] = 0x00;
    buf[i++] = 0x10; buf[i++] = 0x01;   /* bcdCDC = 1.10 */

    /* Call Management 功能描述符 */
    buf[i++] = 0x05; buf[i++] = CS_INTERFACE; buf[i++] = 0x01;
    buf[i++] = 0x00;            /* bmCapabilities */
    buf[i++] = (uint8_t)(n + 1);/* bDataInterface */

    /* ACM 功能描述符 */
    buf[i++] = 0x04; buf[i++] = CS_INTERFACE; buf[i++] = 0x02;
    buf[i++] = 0x02;            /* bmCapabilities: 支持 Set_Line_Coding */

    /* Union 功能描述符 */
    buf[i++] = 0x05; buf[i++] = CS_INTERFACE; buf[i++] = 0x06;
    buf[i++] = n;               /* bControlInterface */
    buf[i++] = (uint8_t)(n + 1);/* bSubordinateInterface0 */

    /* 通知端点（中断 IN）—— 仅当 ep_notify != 0 时生成 */
    if (c->cfg.ep_notify) {
        buf[i++] = 0x07;            /* bLength */
        buf[i++] = 0x05;            /* ENDPOINT */
        buf[i++] = (uint8_t)(0x80 | c->cfg.ep_notify);
        buf[i++] = 0x03;            /* bmAttributes = Interrupt */
        buf[i++] = 0x08; buf[i++] = 0x00;   /* wMaxPacketSize = 8 */
        buf[i++] = 0x10;            /* bInterval = 16ms */
    }

    /* ★ 数据接口：单 altsetting(0) + 2 个 Bulk 端点
     *
     * 实测 Windows usbser.sys 只使用 altsetting 0：
     *   SET_INTERFACE(interface=1, altsetting=0)
     *   SET_INTERFACE(interface=3, altsetting=0)
     *
     * 所以 altsetting 0 必须带端点（bNumEndpoints=2），
     * 不能做成「alt 0 无端点 / alt 1 有端点」的双 altsetting 结构，
     * 否则 usbser 切到 alt 0 时拿不到端点 → 初始化失败。 */

    /* 数据接口 altsetting 0：2 个 Bulk 端点 */
    buf[i++] = 0x09; buf[i++] = 0x04;
    buf[i++] = (uint8_t)(n + 1);/* bInterfaceNumber */
    buf[i++] = 0x00;            /* bAlternateSetting = 0 */
    buf[i++] = 0x02;            /* bNumEndpoints = 2 */
    buf[i++] = 0x0A;            /* bInterfaceClass = CDC Data */
    buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00;

    /* 数据 OUT 端点 */
    buf[i++] = 0x07; buf[i++] = 0x05;
    buf[i++] = c->cfg.ep_out;
    buf[i++] = 0x02;            /* Bulk */
    buf[i++] = 0x40; buf[i++] = 0x00;   /* 64 字节 */
    buf[i++] = 0x00;

    /* 数据 IN 端点 */
    buf[i++] = 0x07; buf[i++] = 0x05;
    buf[i++] = (uint8_t)(0x80 | c->cfg.ep_in);
    buf[i++] = 0x02;            /* Bulk */
    buf[i++] = 0x40; buf[i++] = 0x00;
    buf[i++] = 0x00;

    return i;
}

static uint8_t cdc_get_eps(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    uint8_t n = 0;

    if (!c) return 0;

    /* ★ 通知端点可选（ep_notify == 0 时跳过）
     *   省掉它后每个 CDC 只占 2 个端点（Bulk OUT + Bulk IN），
     *   这样 CH59x 的 EP1~EP7 可以支持 3 个 CDC。
     *   代价：无法发送 SERIAL_STATE 通知（本实现本来也没发）。 */
    if (c->cfg.ep_notify && max_eps > n) {
        eps[n].ep_num     = c->cfg.ep_notify;
        eps[n].type       = HAL_USB_EP_INT;
        eps[n].dir        = HAL_USB_EP_IN;
        eps[n].max_packet = 8;
        eps[n].interval   = 16;
        n++;
    }

    if (max_eps > n) {
        eps[n].ep_num     = c->cfg.ep_out;
        eps[n].type       = HAL_USB_EP_BULK;
        eps[n].dir        = HAL_USB_EP_OUT;
        eps[n].max_packet = 64;
        eps[n].interval   = 0;
        n++;
    }

    if (max_eps > n) {
        eps[n].ep_num     = c->cfg.ep_in;
        eps[n].type       = HAL_USB_EP_BULK;
        eps[n].dir        = HAL_USB_EP_IN;
        eps[n].max_packet = 64;
        eps[n].interval   = 0;
        n++;
    }

    return n;
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t cdc_init(void *ctx)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    if (!c) return HAL_ERR_PARAM;

    c->rx_head = c->rx_tail = 0;
    c->tx_head = c->tx_tail = 0;
    c->dtr = 0;
    c->configured = 1;

    /* 默认 115200 8N1 */
    c->line_coding[0] = 0x00;
    c->line_coding[1] = 0xC2;
    c->line_coding[2] = 0x01;
    c->line_coding[3] = 0x00;
    c->line_coding[4] = 0x00;    /* 1 停止位 */
    c->line_coding[5] = 0x00;    /* 无校验 */
    c->line_coding[6] = 0x08;    /* 8 数据位 */
    return HAL_OK;
}

/* ===========================================================================
 * 类请求处理
 * =========================================================================== */
static hal_err_t cdc_on_setup(void *ctx, const uint8_t setup[8])
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    uint8_t bmRequestType = setup[0];
    uint8_t bRequest      = setup[1];
    uint16_t wLength      = (uint16_t)(setup[6] | (setup[7] << 8));

    if (!c) return HAL_ERR_PARAM;

    /* 类请求：bmRequestType bit6:5 = 01 (Class) */
    if ((bmRequestType & 0x60) != 0x20) return HAL_ERR_UNSUP;

    switch (bRequest) {
    case CDC_SET_LINE_CODING:
        /* OUT 请求，7 字节数据阶段由 port 层接收后调用 on_ep_out。
         * 这里只需返回 HAL_OK，port 层会按 wLength>0 准备 OUT 接收。 */
        HAL_UNUSED(wLength);
        return HAL_OK;

    case CDC_GET_LINE_CODING:
        /* ★ IN 请求：必须通过 hal_usb_class_setup_reply() 提供 7 字节应答，
         *   否则 port 层没有数据可发，主机超时 → usbser.sys 初始化失败
         *   → 接口报 CM_PROB_FAILED_START（COM 口出现但打不开）。
         *
         *   line_coding 布局（CDC 1.2 规范 6.3.11）：
         *     [0..3] dwDTERate   (小端，如 115200 = 00 C2 01 00)
         *     [4]    bCharFormat (0=1 停止位)
         *     [5]    bParityType (0=无校验)
         *     [6]    bDataBits   (8) */
        hal_usb_class_setup_reply(c->line_coding, 7);
        return HAL_OK;

    case CDC_SET_CONTROL_LINE_STATE:
        /* DTR/RTS 状态，无数据阶段 */
        c->dtr = (uint8_t)(setup[2] & 0x01);
        return HAL_OK;

    default:
        return HAL_ERR_UNSUP;
    }
}

/* ===========================================================================
 * 数据收发
 * =========================================================================== */
/* 类请求的 OUT 数据阶段（CDC SET_LINE_CODING 的 7 字节） */
static void cdc_on_setup_data(void *ctx, const uint8_t *data, uint16_t len)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    if (!c || !data) return;

    /* SET_LINE_CODING 数据格式（CDC 1.2 规范 6.3.10）：
     *   [0..3] dwDTERate
     *   [4]    bCharFormat
     *   [5]    bParityType
     *   [6]    bDataBits
     * 长度必须为 7，否则忽略。 */
    if (len == 7) {
        memcpy(c->line_coding, data, 7);
    }
}

static void cdc_on_ep_out(void *ctx, uint8_t ep, uint16_t len)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    if (!c || !len) return;

    /* 数据已由 port 层收进端点缓冲，这里触发回调 */
    if (c->rx_cb) {
        c->rx_cb(c->rx_buf, len, c->rx_user);
    }
    HAL_UNUSED(ep);
}

static void cdc_on_ep_in(void *ctx, uint8_t ep, uint16_t len)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;

    HAL_UNUSED(ep);
    HAL_UNUSED(len);

    if (!c) return;

    /* ★ 上一次发送完成 → 继续把环形缓冲里剩下的数据推出去
     *   这样大块数据会自动分成多个 64 字节包连续发送。 */
    cdc_flush(c);
}

static void cdc_on_reset(void *ctx)
{
    cdc_ctx_t *c = (cdc_ctx_t *)ctx;
    if (!c) return;
    c->configured = 0;
    c->dtr = 0;
    c->rx_head = c->rx_tail = 0;
    c->tx_head = c->tx_tail = 0;
}

/* ===========================================================================
 * 类驱动实例
 * =========================================================================== */
static const hal_usb_class_drv_t s_cdc_drv = {
    .type            = HAL_USB_CLASS_CDC,
    .name            = "cdc",

    /* ★ 接口布局：CDC ACM 占 2 个接口（通信 + 数据），需要 IAD 分组
     *   核心据此自动分配接口号、生成 IAD、累加 bNumInterfaces。 */
    .if_count        = 2,
    .need_iad        = 1,
    .func_class      = 0x02,     /* CDC */
    .func_subclass   = 0x02,     /* ACM */
    .func_protocol   = 0x01,     /* AT commands */

    .get_config_desc = cdc_get_config_desc,
    .get_eps         = cdc_get_eps,
    .init            = cdc_init,
    .on_setup        = cdc_on_setup,
    .on_setup_data   = cdc_on_setup_data,
    .on_ep_in        = cdc_on_ep_in,
    .on_ep_out       = cdc_on_ep_out,
    .on_reset        = cdc_on_reset,
    .on_suspend      = NULL,
    .on_resume       = NULL,
};

/* ===========================================================================
 * 实例内存管理
 * ---------------------------------------------------------------------------
 * 通用层不预设实例数量上限。实例内存来源：
 *   1) 应用通过 hal_usb_cdc_add_ex() 提供（HAL_USB_CDC_INSTANCE 宏）
 *   2) 应用通过 hal_usb_cdc_add(NULL, ...) 时从静态池切分
 *
 * 静态池大小 HAL_USB_CDC_STATIC_POOL 只是"便捷路径"的容量，
 * 不是芯片能力限制。应用需要更多实例时用 add_ex() 自行提供内存。
 * =========================================================================== */
#ifndef HAL_USB_CDC_STATIC_POOL
#define HAL_USB_CDC_STATIC_POOL  3
#endif

/* 静态池：ctx + 默认 rx/tx 缓冲 */
static cdc_ctx_t s_pool_ctx[HAL_USB_CDC_STATIC_POOL];
static uint8_t   s_pool_rx[HAL_USB_CDC_STATIC_POOL][128];
static uint8_t   s_pool_tx[HAL_USB_CDC_STATIC_POOL][128];
static uint8_t   s_pool_used[HAL_USB_CDC_STATIC_POOL];

/* 从静态池取一个 ctx */
static cdc_ctx_t *cdc_alloc_from_pool(void)
{
    uint8_t i;
    for (i = 0; i < HAL_USB_CDC_STATIC_POOL; i++) {
        if (!s_pool_used[i]) {
            s_pool_used[i] = 1;
            return &s_pool_ctx[i];
        }
    }
    return NULL;
}

/* 归还 ctx（仅静态池的需要标记） */
static void cdc_free_ctx(cdc_ctx_t *c)
{
    uint8_t i;
    if (!c) return;
    for (i = 0; i < HAL_USB_CDC_STATIC_POOL; i++) {
        if (&s_pool_ctx[i] == c) {
            s_pool_used[i] = 0;
            return;
        }
    }
}

/* ===========================================================================
 * 对外 API
 * =========================================================================== */
void hal_usb_cdc_inst_init(hal_usb_cdc_inst_t *inst,
                           uint8_t *rx, uint16_t rx_size,
                           uint8_t *tx, uint16_t tx_size)
{
    if (!inst) return;
    inst->priv[0] = NULL;                       /* ctx 指针，注册时填 */
    inst->priv[1] = (void *)rx;
    inst->priv[2] = (void *)(uintptr_t)rx_size;
    inst->priv[3] = (void *)tx;
    inst->priv[4] = (void *)(uintptr_t)tx_size;
}

hal_usb_cdc_handle_t hal_usb_cdc_add_ex(hal_usb_cdc_inst_t *inst,
                                        const hal_usb_cdc_cfg_t *cfg,
                                        hal_usb_cdc_rx_cb_t cb, void *user)
{
    cdc_ctx_t *c;

    if (!inst) return HAL_USB_CDC_HANDLE_INVALID;

    /* 实例已被注册过 */
    if (inst->priv[0]) return HAL_USB_CDC_HANDLE_INVALID;

    /* ★ 用 inst 自身作为 cdc_ctx_t 存储。
     *   hal_usb_cdc_inst_t.priv[16] = 16 个指针 = 64 字节（32 位平台），
     *   而 cdc_ctx_t 约 48 字节，放得下。
     *   为安全起见用编译期断言保证。 */
    HAL_STATIC_ASSERT(sizeof(hal_usb_cdc_inst_t) >= sizeof(cdc_ctx_t),
                      cdc_inst_too_small);

    c = (cdc_ctx_t *)inst;

    /* 先取出应用提供的缓冲指针（memset 会清掉） */
    {
        uint8_t  *rx = (uint8_t *)inst->priv[1];
        uint16_t  rs = (uint16_t)(uintptr_t)inst->priv[2];
        uint8_t  *tx = (uint8_t *)inst->priv[3];
        uint16_t  ts = (uint16_t)(uintptr_t)inst->priv[4];

        memset(c, 0, sizeof(*c));
        c->rx_buf  = rx;
        c->rx_size = rs ? rs : 128;
        c->tx_buf  = tx;
        c->tx_size = ts ? ts : 128;
    }

    /* 标记已占用（priv[0] 非 NULL 表示已注册） */
    inst->priv[0] = (void *)c;

    return cdc_register(c, cfg, cb, user);
}

hal_usb_cdc_handle_t hal_usb_cdc_add(const hal_usb_cdc_cfg_t *cfg,
                                     hal_usb_cdc_rx_cb_t cb, void *user)
{
    cdc_ctx_t *c = cdc_alloc_from_pool();
    uint8_t    i;

    if (!c) return HAL_USB_CDC_HANDLE_INVALID;

    memset(c, 0, sizeof(*c));

    /* 默认缓冲 */
    for (i = 0; i < HAL_USB_CDC_STATIC_POOL; i++) {
        if (&s_pool_ctx[i] == c) {
            c->rx_buf  = s_pool_rx[i];
            c->rx_size = sizeof(s_pool_rx[i]);
            c->tx_buf  = s_pool_tx[i];
            c->tx_size = sizeof(s_pool_tx[i]);
            break;
        }
    }

    return cdc_register(c, cfg, cb, user);
}

/* ===========================================================================
 * 公共注册流程（add / add_ex 共用）
 * =========================================================================== */
static hal_usb_cdc_handle_t cdc_register(cdc_ctx_t *c,
                                         const hal_usb_cdc_cfg_t *cfg,
                                         hal_usb_cdc_rx_cb_t cb, void *user)
{
    hal_usb_t *u = hal_usb_get();
    hal_usb_ep_t e;

    if (!u || !c || !c->rx_buf || !c->tx_buf) return HAL_USB_CDC_HANDLE_INVALID;

    /* ★ 端点分配：统一走端点池，不再手工推导编号
     *
     * 规则：
     *   cfg == NULL                    → 自动分配，带通知端点
     *   cfg->ep_notify == 0            → 自动分配，不带通知端点（省 1 个）
     *   cfg->ep_notify != 0            → 自动分配，带通知端点
     *   cfg->ep_out/ep_in != 0         → 使用指定端点号（兼容旧代码）
     *
     * 端点池由 port 层实现（hal_usb_ep_alloc），自动跳过已占用的端点，
     * 因此不会出现「两个实例抢同一个 EP」的问题。
     *
     * ★ 实例数量上限完全由 port 层决定：
     *     端点耗尽 → hal_usb_ep_alloc 返回 HAL_ERR_FULL → 注册失败
     *   通用层不设任何芯片相关的数量限制。 */
    {
        uint8_t want_notify = 1;

        if (cfg && cfg->ep_notify == 0) want_notify = 0;

        /* 1) 通知端点（中断 IN，可选） */
        if (want_notify) {
            if (cfg && cfg->ep_notify) {
                if (hal_usb_ep_alloc_at(cfg->ep_notify, HAL_USB_EP_INT,
                                        HAL_USB_EP_IN, 8, &e) != HAL_OK) {
                    return HAL_USB_CDC_HANDLE_INVALID;
                }
            } else {
                if (hal_usb_ep_alloc(HAL_USB_EP_INT, HAL_USB_EP_IN, 8,
                                     &e) != HAL_OK) {
                    return HAL_USB_CDC_HANDLE_INVALID;
                }
            }
            c->cfg.ep_notify = e.ep_num;
        } else {
            c->cfg.ep_notify = 0;
        }

        /* 2) 数据 OUT 端点（批量） */
        if (cfg && cfg->ep_out) {
            if (hal_usb_ep_alloc_at(cfg->ep_out, HAL_USB_EP_BULK,
                                    HAL_USB_EP_OUT, 64, &e) != HAL_OK) {
                if (c->cfg.ep_notify)
                    hal_usb_ep_free(c->cfg.ep_notify, HAL_USB_EP_IN);
                return HAL_USB_CDC_HANDLE_INVALID;
            }
        } else {
            if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_OUT, 64,
                                 &e) != HAL_OK) {
                if (c->cfg.ep_notify)
                    hal_usb_ep_free(c->cfg.ep_notify, HAL_USB_EP_IN);
                return HAL_USB_CDC_HANDLE_INVALID;
            }
        }
        c->cfg.ep_out = e.ep_num;

        /* 3) 数据 IN 端点（批量） */
        if (cfg && cfg->ep_in) {
            if (hal_usb_ep_alloc_at(cfg->ep_in, HAL_USB_EP_BULK,
                                    HAL_USB_EP_IN, 64, &e) != HAL_OK) {
                if (c->cfg.ep_notify)
                    hal_usb_ep_free(c->cfg.ep_notify, HAL_USB_EP_IN);
                hal_usb_ep_free(c->cfg.ep_out, HAL_USB_EP_OUT);
                return HAL_USB_CDC_HANDLE_INVALID;
            }
        } else {
            if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_IN, 64,
                                 &e) != HAL_OK) {
                if (c->cfg.ep_notify)
                    hal_usb_ep_free(c->cfg.ep_notify, HAL_USB_EP_IN);
                hal_usb_ep_free(c->cfg.ep_out, HAL_USB_EP_OUT);
                return HAL_USB_CDC_HANDLE_INVALID;
            }
        }
        c->cfg.ep_in = e.ep_num;
    }

    c->rx_cb   = cb;
    c->rx_user = user;

    /* 加入实例链表 */
    c->next     = s_cdc_list;
    s_cdc_list  = c;
    s_cdc_count++;

    if (u->add_class(u, &s_cdc_drv, c) != HAL_OK) {
        /* 注册失败 → 归还端点和实例 */
        if (c->cfg.ep_notify) hal_usb_ep_free(c->cfg.ep_notify, HAL_USB_EP_IN);
        hal_usb_ep_free(c->cfg.ep_out, HAL_USB_EP_OUT);
        hal_usb_ep_free(c->cfg.ep_in,  HAL_USB_EP_IN);
        /* 从链表摘除 */
        {
            cdc_ctx_t **pp = &s_cdc_list;
            while (*pp && *pp != c) pp = &(*pp)->next;
            if (*pp) *pp = c->next;
        }
        s_cdc_count--;
        cdc_free_ctx(c);
        return HAL_USB_CDC_HANDLE_INVALID;
    }

    return cdc_handle_of(c);
}

int hal_usb_cdc_write(hal_usb_cdc_handle_t h,
                      const uint8_t *data, uint16_t len)
{
    cdc_ctx_t *c;
    uint16_t i, next;

    if (!data || !len) return 0;

    c = cdc_by_handle(h);
    if (!c || !c->configured) return 0;

    /* 1) 把数据放进环形缓冲（满了就截断） */
    for (i = 0; i < len; i++) {
        next = (uint16_t)((c->tx_head + 1) % c->tx_size);
        if (next == c->tx_tail) break;      /* 缓冲满 */
        c->tx_buf[c->tx_head] = data[i];
        c->tx_head = next;
    }

    /* 2) 尝试立即发出（端点忙时会失败，等下次 flush） */
    cdc_flush(c);

    return (int)i;
}

/* ===========================================================================
 * 把环形缓冲里待发的数据推到 USB 端点
 * ---------------------------------------------------------------------------
 * ★ 关键：USB 端点一次最多发 64 字节，且端点忙时不能覆盖缓冲。
 *   所以必须：
 *     1) 检查端点是否空闲（ep_write 内部会返回 HAL_ERR_BUSY）
 *     2) 只取连续的、不超过 64 字节的一段
 *     3) 环形缓冲回绕时先发到末尾，下次再发剩余部分
 *   发送成功后由 on_ep_in 回调推进 tx_tail。
 * =========================================================================== */
static void cdc_flush(cdc_ctx_t *c)
{
    hal_usb_t *u = hal_usb_get();
    uint16_t   avail, n;

    if (!u || !c || !c->configured) return;
    if (c->tx_tail == c->tx_head) return;      /* 无数据 */

    /* 计算可发送字节数（环形缓冲连续段） */
    if (c->tx_head > c->tx_tail) {
        avail = (uint16_t)(c->tx_head - c->tx_tail);
    } else {
        avail = (uint16_t)(c->tx_size - c->tx_tail);   /* 到缓冲末尾 */
    }
    if (avail > 64) avail = 64;                        /* 端点最大包长 */

    n = avail;
    if (u->ep_write(u, c->cfg.ep_in, &c->tx_buf[c->tx_tail], n) == HAL_OK) {
        c->tx_tail = (uint16_t)((c->tx_tail + n) % c->tx_size);
    }
    /* 端点忙则保持 tx_tail 不变，下次 flush 重试 */
}

uint8_t hal_usb_cdc_connected(hal_usb_cdc_handle_t h)
{
    cdc_ctx_t *c = cdc_by_handle(h);
    return (c && c->configured) ? c->dtr : 0;
}

int hal_usb_cdc_log_write(hal_usb_cdc_handle_t h,
                          const uint8_t *data, uint16_t len)
{
    return hal_usb_cdc_write(h, data, len);
}

/* 供 port 层访问内部状态（按索引，1 起） */
void *hal_usb_cdc_ctx_at(int idx)
{
    cdc_ctx_t *p = s_cdc_list;

    if (idx < 1) return NULL;
    while (p && --idx) p = p->next;
    return (void *)p;
}

int hal_usb_cdc_count(void)
{
    return (int)s_cdc_count;
}
