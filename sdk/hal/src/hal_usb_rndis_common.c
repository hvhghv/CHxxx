/**
 * @file    hal_usb_rndis_common.c
 * @brief   USB RNDIS 类驱动实现 —— 芯片无关
 *
 * 架构：
 *   控制接口（if0）：类请求走 EP0，处理 RNDIS 消息（OID 查询/设置）
 *   数据接口（if1）：Bulk IN/OUT 传输 RNDIS 数据包
 *
 * RNDIS 数据包格式：
 *   +0  MessageType      (0x00000001 = REMOTE_NDIS_PACKET_MSG)
 *   +4  MessageLength
 *   +8  DataOffset       (相对包起始的以太网帧偏移)
 *   +12 DataLength
 *   ... 其他字段 ...
 *   +DataOffset  以太网帧（14 字节头 + payload）
 *
 * 参考：
 *   [MS-RNDIS] Remote NDIS Specification
 *   Linux: drivers/net/usb/rndis_host.c
 *   TinyUSB: src/class/net/net_device.c
 */
#include "hal_core.h"
#include "hal_usb_rndis.h"
#include <string.h>

/* ===========================================================================
 * RNDIS 协议常量
 * =========================================================================== */
#define RNDIS_MSG_PACKET        0x00000001UL
#define RNDIS_MSG_INITIALIZE    0x00000002UL
#define RNDIS_MSG_INITIALIZE_C  0x80000002UL
#define RNDIS_MSG_HALT          0x00000003UL
#define RNDIS_MSG_QUERY         0x00000004UL
#define RNDIS_MSG_QUERY_C       0x80000004UL
#define RNDIS_MSG_SET           0x00000005UL
#define RNDIS_MSG_SET_C         0x80000005UL
#define RNDIS_MSG_RESET         0x00000006UL
#define RNDIS_MSG_RESET_C       0x80000006UL
#define RNDIS_MSG_INDICATE      0x00000007UL
#define RNDIS_MSG_KEEPALIVE     0x00000008UL
#define RNDIS_MSG_KEEPALIVE_C   0x80000008UL

/* OID 定义 */
#define OID_GEN_SUPPORTED_LIST          0x00010101UL
#define OID_GEN_HARDWARE_STATUS         0x00010102UL
#define OID_GEN_MEDIA_SUPPORTED         0x00010103UL
#define OID_GEN_MEDIA_IN_USE            0x00010104UL
#define OID_GEN_MAXIMUM_FRAME_SIZE      0x00010106UL
#define OID_GEN_LINK_SPEED              0x00010107UL
#define OID_GEN_MAX_TOTAL_SIZE          0x0001010BUL
#define OID_GEN_PHYSICAL_MEDIUM         0x00010202UL
#define OID_GEN_VENDOR_ID               0x0001010CUL
#define OID_GEN_VENDOR_DESCRIPTION      0x0001010DUL
#define OID_GEN_VENDOR_DRIVER_VERSION   0x00010116UL
#define OID_GEN_CURRENT_PACKET_FILTER   0x0001010EUL
#define OID_GEN_CURRENT_LOOKAHEAD       0x0001010FUL
#define OID_GEN_MAXIMUM_TOTAL_SIZE      0x00010111UL
#define OID_GEN_MAC_OPTIONS             0x00010113UL
#define OID_GEN_MEDIA_CONNECT_STATUS    0x00010114UL
#define OID_GEN_XMIT_OK                 0x00020101UL
#define OID_GEN_RCV_OK                  0x00020102UL
#define OID_GEN_XMIT_ERROR              0x00020103UL
#define OID_GEN_RCV_ERROR               0x00020104UL
#define OID_GEN_RCV_NO_BUFFER           0x00020105UL
#define OID_802_3_PERMANENT_ADDRESS     0x01010101UL
#define OID_802_3_CURRENT_ADDRESS       0x01010102UL
#define OID_802_3_MULTICAST_LIST        0x01010103UL
#define OID_802_3_MAXIMUM_LIST_SIZE     0x01010104UL
#define OID_802_3_MAC_OPTIONS           0x01010105UL
#define OID_802_3_RCV_ERROR_ALIGNMENT   0x01020101UL
#define OID_802_3_XMIT_ONE_COLLISION    0x01020102UL
#define OID_802_3_XMIT_MORE_COLLISIONS  0x01020103UL

/* RNDIS 数据包头长度（不含以太网帧） */
#define RNDIS_DATA_HDR_LEN      44
#define RNDIS_MAX_TRANSFER      2048

/* 类请求 */
#define CDC_SEND_ENCAPSULATED_COMMAND   0x00
#define CDC_GET_ENCAPSULATED_RESPONSE   0x01

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
#ifndef HAL_USB_RNDIS_MAX_INST
#define HAL_USB_RNDIS_MAX_INST  1
#endif

typedef struct {
    hal_usb_rndis_cfg_t cfg;
    hal_usb_rndis_rx_cb_t rx_cb;
    void                 *user;

    uint8_t  ep_notify, ep_out, ep_in;
    uint8_t  configured;
    uint8_t  link_up;
    uint32_t packet_filter;
    uint8_t  peer_mac[6];

    uint8_t  req_buf[256];      /* EP0 类请求缓冲 */
    uint16_t req_len;
    uint8_t  resp_buf[256];     /* 待主机读取的响应 */
    uint16_t resp_len;
    uint8_t  resp_pending;

    uint8_t  rx_buf[RNDIS_MAX_TRANSFER];
    uint8_t  tx_buf[RNDIS_MAX_TRANSFER];

    uint32_t stat_xmit_ok, stat_rcv_ok, stat_xmit_err, stat_rcv_err;
} rndis_ctx_t;

static rndis_ctx_t      s_rndis[HAL_USB_RNDIS_MAX_INST];
static uint8_t          s_rndis_count;

/* ===========================================================================
 * 工具
 * =========================================================================== */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ===========================================================================
 * 配置描述符
 * ---------------------------------------------------------------------------
 * 结构（2 个接口）：
 *   if0  控制接口：bInterfaceClass=0xE0(无线) / 0x01(RF) / 0x03(RNDIS)
 *        - 中断端点（可选，用于 RNDIS 事件通知）
 *        - 类特定描述符：Header(5) + CallMgmt(5) + ACM(4) + Union(5)
 *   if1  数据接口：bInterfaceClass=0x0A(CDC Data)
 *        - Bulk OUT / Bulk IN
 * =========================================================================== */
static uint16_t rndis_get_config_desc(void *ctx, uint8_t *buf,
                                      uint16_t max_len, uint8_t first_if)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;
    uint16_t n = 0;
    uint8_t  has_notify = (c->cfg.ep_notify != 0);

    /* ---- 接口 0：控制 ---- */
    /* 接口描述符 */
    buf[n++] = 0x09; buf[n++] = 0x04;
    buf[n++] = first_if;                 /* bInterfaceNumber */
    buf[n++] = 0x00;                     /* bAlternateSetting */
    buf[n++] = has_notify ? 1 : 0;       /* bNumEndpoints */
    buf[n++] = 0xE0;                     /* bInterfaceClass  = Wireless */
    buf[n++] = 0x01;                     /* bInterfaceSubClass = RF */
    buf[n++] = 0x03;                     /* bInterfaceProtocol = RNDIS */
    buf[n++] = 0x00;                     /* iInterface */

    /* 类特定：Header Functional Descriptor */
    buf[n++] = 0x05; buf[n++] = 0x24; buf[n++] = 0x00;
    buf[n++] = 0x00; buf[n++] = 0x01;    /* bcdCDC 1.00 */

    /* 类特定：Call Management Functional Descriptor */
    buf[n++] = 0x05; buf[n++] = 0x24; buf[n++] = 0x01;
    buf[n++] = 0x00;                     /* bmCapabilities */
    buf[n++] = (uint8_t)(first_if + 1);  /* bDataInterface */

    /* 类特定：Abstract Control Management */
    buf[n++] = 0x04; buf[n++] = 0x24; buf[n++] = 0x02;
    buf[n++] = 0x00;                     /* bmCapabilities */

    /* 类特定：Union Functional Descriptor */
    buf[n++] = 0x05; buf[n++] = 0x24; buf[n++] = 0x06;
    buf[n++] = first_if;                 /* bControlInterface */
    buf[n++] = (uint8_t)(first_if + 1);  /* bSubordinateInterface0 */

    /* 中断端点（可选） */
    if (has_notify) {
        buf[n++] = 0x07; buf[n++] = 0x05;
        buf[n++] = (uint8_t)(0x80 | c->cfg.ep_notify);
        buf[n++] = 0x03;                 /* Interrupt */
        buf[n++] = 0x08; buf[n++] = 0x00;/* wMaxPacketSize = 8 */
        buf[n++] = 0x10;                 /* bInterval = 16ms */
    }

    /* ---- 接口 1：数据 ---- */
    buf[n++] = 0x09; buf[n++] = 0x04;
    buf[n++] = (uint8_t)(first_if + 1);  /* bInterfaceNumber */
    buf[n++] = 0x00;
    buf[n++] = 0x02;                     /* bNumEndpoints = 2 */
    buf[n++] = 0x0A;                     /* bInterfaceClass = CDC Data */
    buf[n++] = 0x00;
    buf[n++] = 0x00;
    buf[n++] = 0x00;

    /* Bulk OUT */
    buf[n++] = 0x07; buf[n++] = 0x05;
    buf[n++] = c->cfg.ep_out;
    buf[n++] = 0x02;                     /* Bulk */
    buf[n++] = 0x40; buf[n++] = 0x00;    /* wMaxPacketSize = 64 */
    buf[n++] = 0x00;

    /* Bulk IN */
    buf[n++] = 0x07; buf[n++] = 0x05;
    buf[n++] = (uint8_t)(0x80 | c->cfg.ep_in);
    buf[n++] = 0x02;
    buf[n++] = 0x40; buf[n++] = 0x00;
    buf[n++] = 0x00;

    HAL_UNUSED(max_len);
    return n;
}

/* ===========================================================================
 * 端点需求
 * =========================================================================== */
static uint8_t rndis_get_eps(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;
    uint8_t n = 0;
    hal_usb_ep_t ep;

    /* 中断端点（可选） */
    if (c->cfg.ep_notify && n < max_eps) {
        if (hal_usb_ep_alloc_at(c->cfg.ep_notify, HAL_USB_EP_INT,
                                HAL_USB_EP_IN, 8, &ep) == HAL_OK) {
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
 * 初始化
 * =========================================================================== */
static hal_err_t rndis_init(void *ctx)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;

    c->configured   = 0;
    c->link_up      = 0;
    c->packet_filter = 0;
    c->resp_pending = 0;
    c->resp_len     = 0;
    memset(c->peer_mac, 0xFF, 6);
    return HAL_OK;
}

/* ===========================================================================
 * RNDIS 消息处理
 * ---------------------------------------------------------------------------
 * 主机通过 EP0 的类请求（SEND_ENCAPSULATED_COMMAND）下发 RNDIS 消息，
 * 设备通过 GET_ENCAPSULATED_RESPONSE 返回响应。
 * =========================================================================== */
static uint16_t rndis_build_query_resp(rndis_ctx_t *c, uint32_t oid,
                                       uint8_t *out)
{
    uint16_t n = 0;
    uint8_t  vals[64];
    uint16_t vlen = 4;

    switch (oid) {
    case OID_GEN_SUPPORTED_LIST:
        /* 返回支持的 OID 列表 */
        put_u32(&vals[0],  OID_GEN_SUPPORTED_LIST);
        put_u32(&vals[4],  OID_GEN_HARDWARE_STATUS);
        put_u32(&vals[8],  OID_GEN_MEDIA_SUPPORTED);
        put_u32(&vals[12], OID_GEN_MEDIA_IN_USE);
        put_u32(&vals[16], OID_GEN_MAXIMUM_FRAME_SIZE);
        put_u32(&vals[20], OID_GEN_LINK_SPEED);
        put_u32(&vals[24], OID_GEN_PHYSICAL_MEDIUM);
        put_u32(&vals[28], OID_GEN_VENDOR_ID);
        put_u32(&vals[32], OID_GEN_CURRENT_PACKET_FILTER);
        put_u32(&vals[36], OID_GEN_MEDIA_CONNECT_STATUS);
        put_u32(&vals[40], OID_802_3_PERMANENT_ADDRESS);
        put_u32(&vals[44], OID_802_3_CURRENT_ADDRESS);
        put_u32(&vals[48], OID_802_3_MAXIMUM_LIST_SIZE);
        vlen = 52;
        break;

    case OID_GEN_HARDWARE_STATUS:
        put_u32(vals, 0);   /* NdisHardwareStatusReady */
        break;

    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        put_u32(vals, 0);   /* NdisMedium802_3 */
        break;

    case OID_GEN_PHYSICAL_MEDIUM:
        put_u32(vals, 0);   /* NdisPhysicalMediumUnspecified */
        break;

    case OID_GEN_MAXIMUM_FRAME_SIZE:
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
    case OID_GEN_MAX_TOTAL_SIZE:
        put_u32(vals, c->cfg.mtu ? c->cfg.mtu : 1500);
        break;

    case OID_GEN_LINK_SPEED:
        put_u32(vals, 10000000UL / 100);   /* 10Mbps，单位 100bps */
        break;

    case OID_GEN_VENDOR_ID:
        put_u32(vals, 0x00000001UL);
        break;

    case OID_GEN_VENDOR_DESCRIPTION: {
        static const char desc[] = "CHxxx RNDIS";
        memcpy(vals, desc, sizeof(desc));
        vlen = (uint16_t)sizeof(desc);
        break;
    }

    case OID_GEN_VENDOR_DRIVER_VERSION:
        put_u32(vals, 0x00010000UL);
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        put_u32(vals, c->packet_filter);
        break;

    case OID_GEN_CURRENT_LOOKAHEAD:
        put_u32(vals, c->cfg.mtu ? c->cfg.mtu : 1500);
        break;

    case OID_GEN_MAC_OPTIONS:
        put_u32(vals, 0x00000004UL);   /* NDIS_MAC_OPTION_NO_LOOPBACK */
        break;

    case OID_GEN_MEDIA_CONNECT_STATUS:
        put_u32(vals, c->link_up ? 0 : 1);  /* 0=Connected, 1=Disconnected */
        break;

    case OID_GEN_XMIT_OK:
        put_u32(vals, c->stat_xmit_ok);
        break;
    case OID_GEN_RCV_OK:
        put_u32(vals, c->stat_rcv_ok);
        break;
    case OID_GEN_XMIT_ERROR:
        put_u32(vals, c->stat_xmit_err);
        break;
    case OID_GEN_RCV_ERROR:
        put_u32(vals, c->stat_rcv_err);
        break;
    case OID_GEN_RCV_NO_BUFFER:
        put_u32(vals, 0);
        break;

    case OID_802_3_PERMANENT_ADDRESS:
    case OID_802_3_CURRENT_ADDRESS:
        memcpy(vals, c->cfg.mac, 6);
        vlen = 6;
        break;

    case OID_802_3_MAXIMUM_LIST_SIZE:
        put_u32(vals, 1);
        break;

    case OID_802_3_MAC_OPTIONS:
        put_u32(vals, 0);
        break;

    default:
        /* 未支持的 OID：返回 0 值 */
        put_u32(vals, 0);
        break;
    }

    /* 构造 REMOTE_NDIS_QUERY_CMPLT */
    put_u32(&out[n], RNDIS_MSG_QUERY_C);    n += 4;   /* MessageType */
    put_u32(&out[n], 0);                    n += 4;   /* MessageLength（稍后填） */
    put_u32(&out[n], 0);                    n += 4;   /* RequestId */
    put_u32(&out[n], 0);                    n += 4;   /* Status = SUCCESS */
    put_u32(&out[n], (uint32_t)(24 + vlen)); n += 4;  /* InformationBufferLength */
    put_u32(&out[n], 24);                   n += 4;   /* InformationBufferOffset */
    memcpy(&out[n], vals, vlen);            n += vlen;

    put_u32(&out[4], n);    /* 回填 MessageLength */
    return n;
}

static void rndis_handle_msg(rndis_ctx_t *c, const uint8_t *msg, uint16_t len)
{
    uint32_t type;

    if (len < 8) return;
    type = get_u32(msg);

    switch (type) {
    case RNDIS_MSG_INITIALIZE: {
        /* REMOTE_NDIS_INITIALIZE_MSG → INITIALIZE_CMPLT */
        uint8_t *o = c->resp_buf;
        uint32_t req_id = (len >= 12) ? get_u32(&msg[8]) : 0;

        put_u32(&o[0],  RNDIS_MSG_INITIALIZE_C);
        put_u32(&o[4],  52);                /* MessageLength */
        put_u32(&o[8],  req_id);
        put_u32(&o[12], 0);                 /* Status = SUCCESS */
        put_u32(&o[16], 1);                 /* MajorVersion */
        put_u32(&o[20], 0);                 /* MinorVersion */
        put_u32(&o[24], 0);                 /* DeviceFlags */
        put_u32(&o[28], 0);                 /* Medium = 802.3 */
        put_u32(&o[32], 1);                 /* MaxPacketsPerTransfer */
        put_u32(&o[36], RNDIS_MAX_TRANSFER);/* MaxTransferSize */
        put_u32(&o[40], 14 + (c->cfg.mtu ? c->cfg.mtu : 1500)); /* PacketAlignmentFactor */
        put_u32(&o[44], 0);                 /* AFListOffset */
        put_u32(&o[48], 0);                 /* AFListSize */
        c->resp_len = 52;
        c->resp_pending = 1;
        break;
    }

    case RNDIS_MSG_QUERY: {
        uint32_t req_id = (len >= 12) ? get_u32(&msg[8]) : 0;
        uint32_t oid    = (len >= 20) ? get_u32(&msg[16]) : 0;
        uint16_t n = rndis_build_query_resp(c, oid, c->resp_buf);
        put_u32(&c->resp_buf[8], req_id);
        c->resp_len = n;
        c->resp_pending = 1;
        break;
    }

    case RNDIS_MSG_SET: {
        uint32_t req_id = (len >= 12) ? get_u32(&msg[8]) : 0;
        uint32_t oid    = (len >= 20) ? get_u32(&msg[16]) : 0;
        uint32_t vlen   = (len >= 24) ? get_u32(&msg[20]) : 0;
        uint32_t voff   = (len >= 28) ? get_u32(&msg[24]) : 0;
        uint8_t *o = c->resp_buf;

        if (oid == OID_GEN_CURRENT_PACKET_FILTER && vlen >= 4 &&
            (voff + 4) <= len) {
            c->packet_filter = get_u32(&msg[voff]);
            c->link_up = (c->packet_filter != 0);
        }

        put_u32(&o[0],  RNDIS_MSG_SET_C);
        put_u32(&o[4],  16);
        put_u32(&o[8],  req_id);
        put_u32(&o[12], 0);     /* Status = SUCCESS */
        c->resp_len = 16;
        c->resp_pending = 1;
        break;
    }

    case RNDIS_MSG_RESET: {
        uint32_t req_id = (len >= 12) ? get_u32(&msg[8]) : 0;
        uint8_t *o = c->resp_buf;

        put_u32(&o[0],  RNDIS_MSG_RESET_C);
        put_u32(&o[4],  16);
        put_u32(&o[8],  req_id);
        put_u32(&o[12], 0);     /* Status = SUCCESS */
        c->resp_len = 16;
        c->resp_pending = 1;
        break;
    }

    case RNDIS_MSG_KEEPALIVE: {
        uint32_t req_id = (len >= 12) ? get_u32(&msg[8]) : 0;
        uint8_t *o = c->resp_buf;

        put_u32(&o[0],  RNDIS_MSG_KEEPALIVE_C);
        put_u32(&o[4],  16);
        put_u32(&o[8],  req_id);
        put_u32(&o[12], 0);
        c->resp_len = 16;
        c->resp_pending = 1;
        break;
    }

    default:
        break;
    }
}

/* ===========================================================================
 * 标准/类请求
 * =========================================================================== */
static hal_err_t rndis_on_setup(void *ctx, const uint8_t setup[8])
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;
    uint8_t  bmRequestType = setup[0];
    uint8_t  bRequest      = setup[1];
    uint16_t wLength       = (uint16_t)(setup[6] | (setup[7] << 8));

    /* 类请求（bmRequestType & 0x60 == 0x20） */
    if ((bmRequestType & 0x60) == 0x20) {
        if (bRequest == CDC_SEND_ENCAPSULATED_COMMAND) {
            /* 主机下发 RNDIS 消息，数据在 OUT 阶段 */
            c->req_len = (wLength > sizeof(c->req_buf))
                         ? sizeof(c->req_buf) : wLength;
            return HAL_OK;
        }
        if (bRequest == CDC_GET_ENCAPSULATED_RESPONSE) {
            /* 主机读取响应 */
            if (c->resp_pending) {
                uint16_t n = (wLength < c->resp_len) ? wLength : c->resp_len;
                hal_usb_class_setup_reply(c->resp_buf, n);
                c->resp_pending = 0;
            } else {
                hal_usb_class_setup_reply(c->resp_buf, 0);
            }
            return HAL_OK;
        }
        /* 其他类请求：STALL 由核心处理 */
        return HAL_ERR_UNSUP;
    }

    /* 标准请求：SET_INTERFACE 时激活数据通道 */
    if (bRequest == 0x0B /* SET_INTERFACE */) {
        c->configured = 1;
        return HAL_OK;
    }
    if (bRequest == 0x09 /* SET_CONFIGURATION */) {
        c->configured = 1;
        return HAL_OK;
    }
    return HAL_ERR_UNSUP;
}

static void rndis_on_setup_data(void *ctx, const uint8_t *data, uint16_t len)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;

    if (len > sizeof(c->req_buf)) len = sizeof(c->req_buf);
    memcpy(c->req_buf, data, len);
    c->req_len = len;

    /* 立即处理（主机随后会发 GET_ENCAPSULATED_RESPONSE 取结果） */
    rndis_handle_msg(c, c->req_buf, c->req_len);
}

/* ===========================================================================
 * 数据收发
 * =========================================================================== */
static void rndis_on_ep_out(void *ctx, uint8_t ep, uint16_t len)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;
    uint32_t type, data_off, data_len;

    HAL_UNUSED(ep);
    if (len < RNDIS_DATA_HDR_LEN) return;

    type = get_u32(&c->rx_buf[0]);
    if (type != RNDIS_MSG_PACKET) return;

    data_off = get_u32(&c->rx_buf[8]);
    data_len = get_u32(&c->rx_buf[12]);

    if (data_off + data_len > len) return;
    if (data_len == 0) return;

    c->stat_rcv_ok++;
    if (c->rx_cb) {
        c->rx_cb(&c->rx_buf[data_off], (uint16_t)data_len, c->user);
    }
}

static void rndis_on_ep_in(void *ctx, uint8_t ep, uint16_t len)
{
    HAL_UNUSED(ctx); HAL_UNUSED(ep); HAL_UNUSED(len);
}

static void rndis_on_reset(void *ctx)
{
    rndis_ctx_t *c = (rndis_ctx_t *)ctx;
    c->configured = 0;
    c->link_up = 0;
    c->resp_pending = 0;
}

/* ===========================================================================
 * 类驱动描述
 * =========================================================================== */
static const hal_usb_class_drv_t s_rndis_drv = {
    .type           = HAL_USB_CLASS_RNDIS,
    .name           = "rndis",
    .if_count       = 2,
    .need_iad       = 1,
    .func_class     = 0xE0,     /* Wireless Controller */
    .func_subclass  = 0x01,     /* RF */
    .func_protocol  = 0x03,     /* RNDIS */
    .get_config_desc = rndis_get_config_desc,
    .get_eps        = rndis_get_eps,
    .init           = rndis_init,
    .on_setup       = rndis_on_setup,
    .on_setup_data  = rndis_on_setup_data,
    .on_ep_in       = rndis_on_ep_in,
    .on_ep_out      = rndis_on_ep_out,
    .on_reset       = rndis_on_reset,
};

/* ===========================================================================
 * 对外接口
 * =========================================================================== */
hal_usb_rndis_handle_t hal_usb_rndis_add(const hal_usb_rndis_cfg_t *cfg,
                                         hal_usb_rndis_rx_cb_t cb,
                                         void *user)
{
    rndis_ctx_t *c;
    hal_usb_t   *u = hal_usb_get();

    if (!cfg || s_rndis_count >= HAL_USB_RNDIS_MAX_INST) {
        return HAL_USB_RNDIS_HANDLE_INVALID;
    }

    c = &s_rndis[s_rndis_count];
    memset(c, 0, sizeof(*c));
    memcpy(&c->cfg, cfg, sizeof(*cfg));
    c->rx_cb = cb;
    c->user  = user;

    if (!c->cfg.mtu) c->cfg.mtu = 1500;

    /* 端点由核心在 init 时按 get_eps() 自动分配并回填 */
    if (u->add_class(u, &s_rndis_drv, c) != HAL_OK) {
        return HAL_USB_RNDIS_HANDLE_INVALID;
    }

    return s_rndis_count++;
}

int hal_usb_rndis_write(hal_usb_rndis_handle_t h,
                        const uint8_t *frame, uint16_t len)
{
    rndis_ctx_t *c;
    hal_usb_t   *u = hal_usb_get();
    uint16_t     total;
    uint32_t     hdr_len;

    if (h >= s_rndis_count || !frame || !len) return -1;
    c = &s_rndis[h];
    if (!c->configured || !c->link_up) return -1;

    /* 构造 RNDIS 数据包：44 字节头 + 以太网帧 */
    hdr_len = RNDIS_DATA_HDR_LEN;
    total = (uint16_t)(hdr_len + len);
    if (total > RNDIS_MAX_TRANSFER) return -1;

    memset(c->tx_buf, 0, hdr_len);
    put_u32(&c->tx_buf[0],  RNDIS_MSG_PACKET);
    put_u32(&c->tx_buf[4],  total);
    put_u32(&c->tx_buf[8],  hdr_len);       /* DataOffset */
    put_u32(&c->tx_buf[12], len);           /* DataLength */
    /* OOB 数据块（全 0 表示无） */
    put_u32(&c->tx_buf[16], 0);             /* OOBDataOffset */
    put_u32(&c->tx_buf[20], 0);             /* OOBDataLength */
    put_u32(&c->tx_buf[24], 0);             /* NumOOBDataElements */
    put_u32(&c->tx_buf[28], 0);             /* PerPacketInfoOffset */
    put_u32(&c->tx_buf[32], 0);             /* PerPacketInfoLength */
    put_u32(&c->tx_buf[36], 0);             /* VcHandle */
    put_u32(&c->tx_buf[40], 0);             /* Reserved */

    memcpy(&c->tx_buf[hdr_len], frame, len);

    if (u->ep_write(u, c->ep_in, c->tx_buf, total) != HAL_OK) {
        c->stat_xmit_err++;
        return -1;
    }
    c->stat_xmit_ok++;
    return (int)len;
}

uint8_t hal_usb_rndis_link_up(hal_usb_rndis_handle_t h)
{
    if (h >= s_rndis_count) return 0;
    return s_rndis[h].link_up;
}

void hal_usb_rndis_peer_mac(hal_usb_rndis_handle_t h, uint8_t mac[6])
{
    if (h >= s_rndis_count || !mac) return;
    memcpy(mac, s_rndis[h].peer_mac, 6);
}
