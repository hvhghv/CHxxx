/**
 * @file    hal_usb_ncm_common.c
 * @brief   USB NCM（Network Control Model）类驱动实现 —— 芯片无关
 *
 * 协议要点（CDC-NCM 1.0，USB-IF 标准）：
 *
 *   接口布局（2 个接口，需 IAD）：
 *     if0  控制接口  bInterfaceClass=0x02(CDC) / 0x0D(NCM) / 0x00
 *          - 中断端点（可选，网络状态通知）
 *          - 类特定描述符：Header + Union + Ethernet + NCM
 *     if1  数据接口  bInterfaceClass=0x0A(CDC Data)
 *          - Bulk OUT / Bulk IN
 *          - 类特定描述符：NCM Data（NTB 参数）
 *
 *   数据封装：NTB（NCM Transfer Block）
 *     NTH16（16 字节 NTB 头，小端）
 *       [0..1]   wLength        整个 NTB 长度（含头）
 *       [2..3]   wNtbFormat     NCM_NTB_FORMAT_DATAGRAM = 0
 *       [4..5]   wNtbType       NCM_NTB_TYPE_DATA = 1
 *       [6..7]   wNdpIndex      NDP 相对 NTB 起点的偏移
 *       [8..9]   wSequence      序号（每次 +1）
 *       [10..11] wBlockLength   = wLength（单块）
 *       [12..13] wNdpCount      本 NTB 内 NDP 个数
 *     NDP16（8 字节 NDP 头 + 每帧 8 字节索引 + 8 字节结束项）
 *       [0..1]   wLength        NDP 总长
 *       [2..3]   wNdpType       NCM_NDP16_TYPE_DATAGRAM = 0
 *       [4..5]   wNextNdpIndex  下一个 NDP 偏移（0 = 无）
 *       [6..7]   保留
 *       [8..]    索引项：wDatagramIndex / wDatagramLength
 *                末尾以 (0,0) 结束
 *
 *   控制请求（标准 CDC + NCM 专有）：
 *     SET_ETHERNET_MULTICAST_FILTERS / SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER
 *     GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER
 *     SET_ETHERNET_PACKET_FILTER / GET_ETHERNET_STATISTIC
 *     SET_NTB_INPUT_SIZE（NCM 专有，主机告知设备可接收的最大 NTB 字节数）
 *     GET_NTB_PARAMETERS / GET_NTB_FORMAT / SET_NTB_FORMAT
 */

#include "hal_usb_ncm.h"
#include <string.h>

/* ===========================================================================
 * 协议常量
 * =========================================================================== */
/* 标准 CDC 类请求（bRequest） */
#define CDC_SET_ETHERNET_MULTICAST_FILTERS          0x40
#define CDC_SET_ETHERNET_PM_PATTERN_FILTER          0x41
#define CDC_GET_ETHERNET_PM_PATTERN_FILTER          0x42
#define CDC_SET_ETHERNET_PACKET_FILTER              0x43
#define CDC_GET_ETHERNET_STATISTIC                  0x44

/* NCM 类请求 */
#define CDC_GET_NTB_PARAMETERS                      0x80
#define CDC_GET_NTB_FORMAT                          0x83
#define CDC_SET_NTB_FORMAT                          0x84
#define CDC_SET_NTB_INPUT_SIZE                      0x86

/* NTB / NDP 常量 */
#define NCM_NTB_FORMAT_DATAGRAM     0
#define NCM_NTB_TYPE_DATA           1
#define NCM_NDP16_TYPE_DATAGRAM     0

#define NTH16_LEN                   16
#define NDP16_HDR_LEN               8       /* 头 8 字节 */
#define NDP16_ENTRY_LEN             8       /* 每帧索引 8 字节 */

/* 子类 / 协议 */
#define NCM_SUBCLASS                0x0D
#define NCM_PROTOCOL                0x00

/* 最大传输单元 */
#define NCM_MAX_TRANSFER            2048

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
#ifndef HAL_USB_NCM_MAX_INST
#define HAL_USB_NCM_MAX_INST    1
#endif

typedef struct {
    hal_usb_ncm_cfg_t    cfg;
    hal_usb_ncm_rx_cb_t  rx_cb;
    void                *user;

    uint8_t  ep_notify, ep_out, ep_in;
    uint8_t  configured;
    uint8_t  link_up;
    uint16_t packet_filter;
    uint8_t  peer_mac[6];

    uint16_t ntb_in_size;       /* 主机告知的 NTB 输入上限 */
    uint16_t seq_tx;            /* 发送序号 */
    uint16_t seq_rx;            /* 期望的接收序号 */

    uint8_t  req_buf[64];       /* EP0 类请求缓冲 */
    uint16_t req_len;
    uint8_t  resp_buf[64];      /* 待主机读取的响应 */
    uint16_t resp_len;
    uint8_t  resp_pending;

    uint8_t  rx_buf[NCM_MAX_TRANSFER];
    uint8_t  tx_buf[NCM_MAX_TRANSFER];

    uint32_t stat_xmit_ok, stat_rcv_ok, stat_xmit_err, stat_rcv_err;
} ncm_ctx_t;

static ncm_ctx_t  s_ncm[HAL_USB_NCM_MAX_INST];
static uint8_t    s_ncm_count;

/* ===========================================================================
 * 工具
 * =========================================================================== */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ===========================================================================
 * 配置描述符
 * ---------------------------------------------------------------------------
 * 共 2 个接口，需要 IAD（由核心在第一个接口前插入）。
 * =========================================================================== */
static uint16_t ncm_get_config_desc(void *ctx, uint8_t *buf,
                                    uint16_t max_len, uint8_t first_if)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;
    uint16_t n = 0;
    uint8_t  has_notify = (c->cfg.ep_notify != 0);
    uint16_t mtu = c->cfg.mtu ? c->cfg.mtu : 1500;

    (void)max_len;

    /* ---- 接口 0：控制 ---- */
    buf[n++] = 0x09; buf[n++] = 0x04;
    buf[n++] = first_if;                /* bInterfaceNumber */
    buf[n++] = 0x00;                    /* bAlternateSetting */
    buf[n++] = has_notify ? 1 : 0;      /* bNumEndpoints */
    buf[n++] = 0x02;                    /* bInterfaceClass    = CDC */
    buf[n++] = NCM_SUBCLASS;            /* bInterfaceSubClass = NCM */
    buf[n++] = NCM_PROTOCOL;            /* bInterfaceProtocol */
    buf[n++] = 0x00;                    /* iInterface */

    /* Header Functional Descriptor */
    buf[n++] = 0x05; buf[n++] = 0x24; buf[n++] = 0x00;
    buf[n++] = 0x00; buf[n++] = 0x01;   /* bcdCDC 1.00 */

    /* Union Functional Descriptor */
    buf[n++] = 0x05; buf[n++] = 0x24; buf[n++] = 0x06;
    buf[n++] = first_if;                /* bControlInterface */
    buf[n++] = (uint8_t)(first_if + 1); /* bSubordinateInterface0 */

    /* Ethernet Networking Functional Descriptor (0x0F) */
    buf[n++] = 0x0D; buf[n++] = 0x24; buf[n++] = 0x0F;
    buf[n++] = 0x00;                    /* iMACAddress（无字符串） */
    put_u32(&buf[n], 0x00000000UL); n += 4;   /* bmEthernetStatistics */
    put_u16(&buf[n], mtu); n += 2;      /* wMaxSegmentSize */
    put_u16(&buf[n], 0x0000); n += 2;   /* wNumberMCFilters */
    buf[n++] = 0x00;                    /* bNumberPowerFilters */

    /* NCM Functional Descriptor (0x1A) */
    buf[n++] = 0x06; buf[n++] = 0x24; buf[n++] = 0x1A;
    buf[n++] = 0x00; buf[n++] = 0x01;   /* bcdNcmVersion 1.00 */
    buf[n++] = 0x00;                    /* bmNetworkCapabilities = none */

    /* 中断端点（可选） */
    if (has_notify) {
        buf[n++] = 0x07; buf[n++] = 0x05;
        buf[n++] = (uint8_t)(0x80 | c->cfg.ep_notify);
        buf[n++] = 0x03;                /* Interrupt */
        put_u16(&buf[n], 16); n += 2;
        buf[n++] = 0x10;                /* 16ms */
    }

    /* ---- 接口 1：数据 ---- */
    buf[n++] = 0x09; buf[n++] = 0x04;
    buf[n++] = (uint8_t)(first_if + 1);
    buf[n++] = 0x00;
    buf[n++] = 0x02;                    /* 2 个 Bulk 端点 */
    buf[n++] = 0x0A;                    /* bInterfaceClass = CDC Data */
    buf[n++] = 0x00;
    buf[n++] = NCM_PROTOCOL;
    buf[n++] = 0x00;

    /* NCM Data Functional Descriptor (0x1A，数据接口侧) */
    buf[n++] = 0x06; buf[n++] = 0x24; buf[n++] = 0x1A;
    buf[n++] = 0x00; buf[n++] = 0x01;
    buf[n++] = 0x00;

    /* Bulk OUT */
    buf[n++] = 0x07; buf[n++] = 0x05;
    buf[n++] = c->cfg.ep_out;
    buf[n++] = 0x02;
    put_u16(&buf[n], 64); n += 2;
    buf[n++] = 0x00;

    /* Bulk IN */
    buf[n++] = 0x07; buf[n++] = 0x05;
    buf[n++] = (uint8_t)(0x80 | c->cfg.ep_in);
    buf[n++] = 0x02;
    put_u16(&buf[n], 64); n += 2;
    buf[n++] = 0x00;

    return n;
}

/* ===========================================================================
 * 端点申请
 * =========================================================================== */
static uint8_t ncm_get_eps(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;
    uint8_t n = 0;
    hal_usb_ep_t ep;

    /* 中断端点（可选） */
    if (c->cfg.ep_notify && n < max_eps) {
        if (hal_usb_ep_alloc_at(c->cfg.ep_notify, HAL_USB_EP_INT,
                                HAL_USB_EP_IN, 16, &ep) == HAL_OK) {
            eps[n++] = ep;
        }
    }

    /* Bulk OUT */
    if (n < max_eps) {
        if (c->cfg.ep_out) {
            if (hal_usb_ep_alloc_at(c->cfg.ep_out, HAL_USB_EP_BULK,
                                    HAL_USB_EP_OUT, 64, &ep) == HAL_OK) {
                eps[n++] = ep;
            }
        } else if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_OUT,
                                    64, &ep) == HAL_OK) {
            c->cfg.ep_out = ep.ep_num;      /* 回填，描述符要用 */
            eps[n++] = ep;
        }
    }

    /* Bulk IN */
    if (n < max_eps) {
        if (c->cfg.ep_in) {
            if (hal_usb_ep_alloc_at(c->cfg.ep_in, HAL_USB_EP_BULK,
                                    HAL_USB_EP_IN, 64, &ep) == HAL_OK) {
                eps[n++] = ep;
            }
        } else if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_IN,
                                    64, &ep) == HAL_OK) {
            c->cfg.ep_in = ep.ep_num;
            eps[n++] = ep;
        }
    }
    return n;
}

/* ===========================================================================
 * NTB 封装 / 解封装
 * =========================================================================== */

/* 把一帧以太网数据封进 NTB（单帧单 NDP） */
static uint16_t ncm_build_ntb(ncm_ctx_t *c, const uint8_t *frame, uint16_t len)
{
    uint8_t *p = c->tx_buf;
    uint16_t ntb_len;
    uint16_t ndp_index  = NTH16_LEN;
    uint16_t data_index = NTH16_LEN + NDP16_HDR_LEN + NDP16_ENTRY_LEN * 2;

    ntb_len = data_index + len;

    /* NTH16 */
    put_u16(&p[0],  ntb_len);                   /* wLength */
    put_u16(&p[2],  NCM_NTB_FORMAT_DATAGRAM);   /* wNtbFormat */
    put_u16(&p[4],  NCM_NTB_TYPE_DATA);         /* wNtbType */
    put_u16(&p[6],  ndp_index);                 /* wNdpIndex */
    put_u16(&p[8],  c->seq_tx);                 /* wSequence */
    put_u16(&p[10], ntb_len);                   /* wBlockLength */
    put_u16(&p[12], 1);                         /* wNdpCount */

    /* NDP16 */
    put_u16(&p[ndp_index + 0], NDP16_HDR_LEN + NDP16_ENTRY_LEN * 2);
    put_u16(&p[ndp_index + 2], NCM_NDP16_TYPE_DATAGRAM);
    put_u16(&p[ndp_index + 4], 0);              /* wNextNdpIndex */
    put_u16(&p[ndp_index + 6], 0);              /* 保留 */

    /* 索引项 0 */
    put_u16(&p[ndp_index + 8],  data_index);    /* wDatagramIndex */
    put_u16(&p[ndp_index + 10], len);           /* wDatagramLength */

    /* 结束项 (0,0) */
    put_u16(&p[ndp_index + 12], 0);
    put_u16(&p[ndp_index + 14], 0);

    /* 载荷 */
    memcpy(&p[data_index], frame, len);

    c->seq_tx++;
    return ntb_len;
}

/* 从一个 NTB 里拆出所有以太网帧并回调 */
static void ncm_parse_ntb(ncm_ctx_t *c, uint16_t len)
{
    const uint8_t *buf = c->rx_buf;
    uint16_t ntb_len, ndp_index, block_len;
    uint16_t off;

    if (len < NTH16_LEN) return;

    ntb_len   = get_u16(&buf[0]);
    ndp_index = get_u16(&buf[6]);
    block_len = get_u16(&buf[10]);

    if (ntb_len > len)       ntb_len = len;
    if (block_len > ntb_len) block_len = ntb_len;
    if ((uint32_t)ndp_index + NDP16_HDR_LEN > block_len) return;

    c->seq_rx = get_u16(&buf[8]);

    /* 遍历 NDP 链 */
    off = ndp_index;
    while ((uint32_t)off + NDP16_HDR_LEN <= block_len) {
        uint16_t next = get_u16(&buf[off + 4]);
        uint16_t e    = (uint16_t)(off + NDP16_HDR_LEN);

        /* 遍历索引项，直到 (0,0) */
        while ((uint32_t)e + NDP16_ENTRY_LEN <= block_len) {
            uint16_t idx  = get_u16(&buf[e]);
            uint16_t dlen = get_u16(&buf[e + 2]);

            if (idx == 0 && dlen == 0) break;
            if ((uint32_t)idx + dlen <= block_len && dlen > 0) {
                if (c->rx_cb) {
                    c->rx_cb(&buf[idx], dlen, c->user);
                }
                c->stat_rcv_ok++;
            } else {
                c->stat_rcv_err++;
            }
            e = (uint16_t)(e + NDP16_ENTRY_LEN);
        }

        if (next == 0 || next >= block_len) break;
        off = next;
    }
}

/* ===========================================================================
 * 类请求处理
 * =========================================================================== */
static hal_err_t ncm_on_setup(void *ctx, const uint8_t setup[8])
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;
    uint8_t  bmRequestType = setup[0];
    uint8_t  bRequest      = setup[1];
    uint16_t wValue        = get_u16(&setup[2]);
    uint16_t wLength       = get_u16(&setup[6]);

    /* 只处理类请求（bmRequestType & 0x60 == 0x20） */
    if ((bmRequestType & 0x60) != 0x20) return HAL_ERR_UNSUP;

    switch (bRequest) {
    case CDC_SET_ETHERNET_PACKET_FILTER:
        c->packet_filter = wValue;
        return HAL_OK;

    case CDC_SET_ETHERNET_MULTICAST_FILTERS:
        /* 主机下发多播地址列表，OUT 阶段丢弃即可 */
        c->req_len = (wLength > sizeof(c->req_buf)) ? sizeof(c->req_buf)
                                                    : wLength;
        return HAL_OK;

    case CDC_SET_ETHERNET_PM_PATTERN_FILTER:
        c->req_len = (wLength > sizeof(c->req_buf)) ? sizeof(c->req_buf)
                                                    : wLength;
        return HAL_OK;

    case CDC_GET_ETHERNET_PM_PATTERN_FILTER:
        c->resp_buf[0] = 0;
        c->resp_buf[1] = 0;
        c->resp_len = 2;
        c->resp_pending = 1;
        return HAL_OK;

    case CDC_GET_ETHERNET_STATISTIC:
        memset(c->resp_buf, 0, 4);
        c->resp_len = (wLength >= 4) ? 4 : wLength;
        c->resp_pending = 1;
        return HAL_OK;

    case CDC_SET_NTB_INPUT_SIZE:
        /* 主机告知可接收的最大 NTB 字节数（4 字节小端） */
        c->req_len = (wLength > sizeof(c->req_buf)) ? sizeof(c->req_buf)
                                                    : wLength;
        return HAL_OK;

    case CDC_GET_NTB_PARAMETERS: {
        /* 28 字节 NTB 参数（NCM 1.0 表 6-6） */
        uint8_t *p = c->resp_buf;
        uint16_t ntb = c->cfg.max_ntb ? c->cfg.max_ntb : NCM_MAX_TRANSFER;

        put_u16(&p[0],  0x001C);        /* wLength = 28 */
        put_u16(&p[2],  0x0001);        /* bmNtbFormatsSupported */
        put_u32(&p[4],  ntb);           /* dwNtbInMaxSize */
        put_u16(&p[8],  1);             /* wNdpInDivisor */
        put_u16(&p[10], 0);             /* wNdpInPayloadRemainder */
        put_u16(&p[12], 0);             /* wNdpInAlignment */
        put_u16(&p[14], 0);             /* wReserved */
        put_u32(&p[16], ntb);           /* dwNtbOutMaxSize */
        put_u16(&p[20], 1);             /* wNdpOutDivisor */
        put_u16(&p[22], 0);             /* wNdpOutPayloadRemainder */
        put_u16(&p[24], 0);             /* wNdpOutAlignment */
        put_u16(&p[26], 0);             /* wNtbOutMaxDatagrams */

        c->resp_len = (wLength >= 28) ? 28 : wLength;
        c->resp_pending = 1;
        return HAL_OK;
    }

    case CDC_GET_NTB_FORMAT:
        c->resp_buf[0] = 0;
        c->resp_buf[1] = 0;
        c->resp_len = 2;
        c->resp_pending = 1;
        return HAL_OK;

    case CDC_SET_NTB_FORMAT:
        return HAL_OK;

    default:
        return HAL_ERR_UNSUP;
    }
}

/* 类请求的 OUT 数据阶段 */
static void ncm_on_setup_data(void *ctx, const uint8_t *data, uint16_t len)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;

    if (len > sizeof(c->req_buf)) len = sizeof(c->req_buf);
    memcpy(c->req_buf, data, len);
    c->req_len = len;

    /* SET_NTB_INPUT_SIZE：前 2 字节是最大 NTB 字节数（小端） */
    if (len >= 2) {
        c->ntb_in_size = get_u16(c->req_buf);
    }
}

/* ===========================================================================
 * 数据收发
 * =========================================================================== */
static void ncm_on_ep_out(void *ctx, uint8_t ep, uint16_t len)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;

    HAL_UNUSED(ep);
    if (!c->configured || !c->link_up) return;
    if (len > sizeof(c->rx_buf)) len = sizeof(c->rx_buf);

    ncm_parse_ntb(c, len);
}

static void ncm_on_ep_in(void *ctx, uint8_t ep, uint16_t len)
{
    HAL_UNUSED(ctx); HAL_UNUSED(ep); HAL_UNUSED(len);
}

/* ===========================================================================
 * 生命周期
 * =========================================================================== */
static hal_err_t ncm_init(void *ctx)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;

    c->configured    = 0;
    c->link_up       = 0;
    c->packet_filter = 0;
    c->resp_pending  = 0;
    c->resp_len      = 0;
    c->req_len       = 0;
    c->seq_tx        = 0;
    c->seq_rx        = 0;
    c->ntb_in_size   = 0;
    memset(c->peer_mac, 0xFF, 6);
    return HAL_OK;
}

static void ncm_on_reset(void *ctx)
{
    ncm_ctx_t *c = (ncm_ctx_t *)ctx;

    c->configured   = 0;
    c->link_up      = 0;
    c->resp_pending = 0;
    c->seq_tx       = 0;
    c->seq_rx       = 0;
}

/* ===========================================================================
 * 类驱动描述
 * =========================================================================== */
static const hal_usb_class_drv_t s_ncm_drv = {
    .type            = HAL_USB_CLASS_NCM,
    .name            = "ncm",
    .if_count        = 2,
    .need_iad        = 1,
    .func_class      = 0x02,    /* CDC */
    .func_subclass   = NCM_SUBCLASS,
    .func_protocol   = NCM_PROTOCOL,
    .get_config_desc = ncm_get_config_desc,
    .get_eps         = ncm_get_eps,
    .init            = ncm_init,
    .on_setup        = ncm_on_setup,
    .on_setup_data   = ncm_on_setup_data,
    .on_ep_in        = ncm_on_ep_in,
    .on_ep_out       = ncm_on_ep_out,
    .on_reset        = ncm_on_reset,
};

/* ===========================================================================
 * 对外接口
 * =========================================================================== */
hal_usb_ncm_handle_t hal_usb_ncm_add(const hal_usb_ncm_cfg_t *cfg,
                                     hal_usb_ncm_rx_cb_t cb,
                                     void *user)
{
    ncm_ctx_t *c;
    hal_usb_t *u = hal_usb_get();

    if (!cfg || s_ncm_count >= HAL_USB_NCM_MAX_INST) {
        return HAL_USB_NCM_HANDLE_INVALID;
    }

    c = &s_ncm[s_ncm_count];
    memset(c, 0, sizeof(*c));
    memcpy(&c->cfg, cfg, sizeof(*cfg));
    c->rx_cb = cb;
    c->user  = user;

    if (!c->cfg.mtu)     c->cfg.mtu     = 1500;
    if (!c->cfg.max_ntb) c->cfg.max_ntb = NCM_MAX_TRANSFER;

    /* 端点由核心在 init 时按 get_eps() 自动分配并回填 */
    if (u->add_class(u, &s_ncm_drv, c) != HAL_OK) {
        return HAL_USB_NCM_HANDLE_INVALID;
    }

    return s_ncm_count++;
}

int hal_usb_ncm_write(hal_usb_ncm_handle_t h, const uint8_t *frame,
                      uint16_t len)
{
    ncm_ctx_t *c;
    hal_usb_t *u = hal_usb_get();
    uint16_t   ntb_len;

    if (h >= s_ncm_count || !frame || !len) return -1;
    c = &s_ncm[h];
    if (!c->configured || !c->link_up) return -1;
    if (!c->ep_in) return -1;

    ntb_len = ncm_build_ntb(c, frame, len);
    if (ntb_len > NCM_MAX_TRANSFER) return -1;

    if (u->ep_write(u, c->ep_in, c->tx_buf, ntb_len) != HAL_OK) {
        c->stat_xmit_err++;
        return -1;
    }
    c->stat_xmit_ok++;
    return (int)len;
}

uint8_t hal_usb_ncm_link_up(hal_usb_ncm_handle_t h)
{
    if (h >= s_ncm_count) return 0;
    return s_ncm[h].link_up;
}

void hal_usb_ncm_peer_mac(hal_usb_ncm_handle_t h, uint8_t mac[6])
{
    if (h >= s_ncm_count || !mac) return;
    memcpy(mac, s_ncm[h].peer_mac, 6);
}
