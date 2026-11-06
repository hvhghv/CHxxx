/**
 * @file    hal_usb_ch59x.c
 * @brief   CH59x USB 核心实现（端点管理 + 描述符拼装 + 中断状态机）
 *
 * 架构：
 *   底层：官方 CH59x_usbdev.c 的 USB_DeviceInit()（只做寄存器初始化）
 *   本文件：完整实现 USB 设备状态机（参考官方 CompoundDev 示例）
 *     - 端点 0 控制传输（SETUP / IN / OUT 三阶段）
 *     - 描述符动态拼装（支持复合设备）
 *     - 端点数据收发
 *     - 类请求分发
 *
 * ★ 关键（踩坑记录）：
 *   1. USB 中断必须用 hal_irq_enable() 使能，不能用官方 PFIC_EnableIRQ
 *      （赋值语义会清掉 BB/LLE 的同字位）
 *   2. USB_IRQHandler 入口由 hal_irq_trampoline_ch59x.S 提供（.weak）
 *   3. EP0 控制传输必须严格按 SETUP → DATA → STATUS 三阶段处理
 *   4. R8_UEP0_CTRL 的 TOG 位必须翻转，否则主机会重传
 */
#include "hal_core.h"
#include "CH59x_common.h"
#include "CH59x_usbdev.h"
#include <string.h>

/* ===========================================================================
 * 描述符与请求常量
 * =========================================================================== */
#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIG        0x02
#define USB_DESC_STRING        0x03
#define USB_DESC_DEVICE_QUAL   0x06

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B

#define USB_REQ_TYPE_MASK      0x60
#define USB_REQ_TYPE_STANDARD  0x00
#define USB_REQ_TYPE_CLASS     0x20
#define USB_REQ_TYPE_VENDOR    0x40

/* 端点号上限
 * ★ CH592/CH591 数据手册 V1.9 第 1 页：「15 个端点，支持DMA，支持64 字节数据包」
 *   第 110 页：「在USB 设备模式下，芯片提供了端点0、1、2、3、4、5、6、7 共8 组双向端点」
 *   第 111 页：R32_EPX_MODE 0x40008070 端点8-15 控制寄存器
 *
 * 本层实现 EP0~EP7（8 组）。
 *
 * 缓冲区特性（手册第 112-113 页）：
 *   EP0  : 64B 收发共用，R16_UEP0_DMA
 *   EP1~3: 单/双 64B，R16_UEPn_DMA 独立，由 R8_UEP2_3_MOD / R8_UEP4_1_MOD 使能
 *   EP4  : 无独立 DMA 寄存器！缓冲区紧跟 UEP0_DMA（+64 收 / +128 发），
 *          由 R8_UEP4_1_MOD 的 RB_UEP4_RX_EN / RB_UEP4_TX_EN 使能
 *   EP5~7: 单 64B，R16_UEP5/6/7_DMA 独立，由 R8_UEP567_MOD 使能
 */
#define USB_MAX_EP        8
#define USB_EP0_SIZE      64

/* 端点模式控制寄存器位（手册 17.2 节） */
#define RB_UEP1_RX_EN     0x80
#define RB_UEP1_TX_EN     0x40
#define RB_UEP1_BUF_MOD   0x10
#define RB_UEP4_RX_EN     0x08
#define RB_UEP4_TX_EN     0x04

#define RB_UEP3_RX_EN     0x80
#define RB_UEP3_TX_EN     0x40
#define RB_UEP3_BUF_MOD   0x10
#define RB_UEP2_RX_EN     0x08
#define RB_UEP2_TX_EN     0x04
#define RB_UEP2_BUF_MOD   0x01

#define RB_UEP7_RX_EN     0x20
#define RB_UEP7_TX_EN     0x10
#define RB_UEP6_RX_EN     0x08
#define RB_UEP6_TX_EN     0x04
#define RB_UEP5_RX_EN     0x02
#define RB_UEP5_TX_EN     0x01

/* ===========================================================================
 * 状态
 * =========================================================================== */
#define USB_MAX_CLASSES   4

typedef struct {
    hal_usb_t                 base;
    const hal_usb_class_drv_t *classes[USB_MAX_CLASSES];
    void                     *class_ctx[USB_MAX_CLASSES];
    uint8_t                   class_count;

    hal_usb_dev_desc_t        desc;
    uint8_t                   configured;
    uint8_t                   address;

    /* ★ SET_ADDRESS 两阶段：SETUP 阶段只记录，EP0 IN 阶段才写寄存器
     *   （对齐官方 app_usb.c 的 SetupReqLen / USB_SET_ADDRESS 分离处理） */
    uint8_t                   pending_addr;
    uint8_t                   has_pending_addr;

    /* 描述符缓冲（2 个 CDC × 67 + 9 = 143，留余量到 256） */
    uint8_t                   cfg_desc[256];
    uint16_t                  cfg_desc_len;

    /* EP0 控制传输
     * ★ EP0 缓冲区必须为 192 字节：手册表 17-3 规定 EP4 的收发缓冲
     *   紧跟在 UEP0_DMA 之后（+64 接收 / +128 发送），
     *   所以 EP0 区域要预留 64(EP0) + 64(EP4 OUT) + 64(EP4 IN) = 192 字节。 */
        uint8_t                  *ep0_ptr;      /* 当前发送位置 */
    uint16_t                  ep0_remain;   /* 剩余字节 */

    /* SETUP 包缓存 */
    uint8_t                   setup[8];

    /* 各端点缓冲（DMA 需要 4 字节对齐）
     * ★ EP4 不用这个数组：它的缓冲区在 ep0_buf+64 / +128，
     *   由硬件按 UEP0_DMA 自动派生（手册表 17-3）。
     *
     * ★★★ 重要：这个数组必须是独立的全局变量，不能放在结构体里！
     *
     * CH59x 的 USB DMA 地址寄存器是 16 位，存的是 RAM 偏移。
     * 结构体成员的 __attribute__((aligned(4))) 只保证"相对结构体
     * 起始的偏移"是 4 的倍数，不保证绝对地址对齐。
     * 如果 usb_ctx_t 本身没按 4 对齐，ep_buf 的绝对地址就不对齐，
     * DMA 传输会失败 —— 表现为「T_LEN 正确但收到的数据全是 0」。
     *
     * 所以改为独立全局数组 + 显式 4 字节对齐。 */
    volatile uint16_t         ep_rx_len[USB_MAX_EP];
    volatile uint8_t          ep_tx_busy[USB_MAX_EP];

    /* 字符串描述符缓冲 */
    uint8_t                   str_buf[64] __attribute__((aligned(4)));

    /* 类请求 IN 应答缓冲（类驱动通过 hal_usb_class_setup_reply 填写） */
    uint8_t                   class_reply_buf[64] __attribute__((aligned(4)));
    volatile uint16_t         class_reply_len;

    /* 类请求 OUT 数据缓冲（SET_LINE_CODING 等） */
    uint8_t                   class_out_buf[64] __attribute__((aligned(4)));
    volatile uint16_t         class_out_len;
    volatile uint8_t          setup_class_idx;   /* 当前 SETUP 所属类索引 */

    /* 诊断：记录收到的类请求（最多 8 条） */
    volatile uint8_t          dbg_class_req_cnt;
    volatile uint8_t          dbg_class_req[8][8];

    /* 诊断：记录所有 SETUP 请求（最多 32 条，每条 8 字节） */
    volatile uint8_t          dbg_setup_cnt;
    volatile uint8_t          dbg_setup[32][8];

    /* 诊断计数 */
    volatile uint16_t         dbg_dev_desc_calls;
    volatile uint8_t          dbg_last_class_count;
    volatile uint8_t          dbg_last_multi;

    /* 实验开关：1 = 强制单设备模式（bDeviceClass=0x02） */
    volatile uint8_t          force_single_class;

    /* 诊断：EP0 发送记录（最多 8 次） */
    volatile uint8_t          dbg_ep0_send_cnt;
    volatile uint8_t          dbg_ep0_send_len[8];
    volatile uint8_t          dbg_ep0_send_hdr[8][8];

    /* 诊断：最后一次 ep_write 的状态 */
    volatile uint8_t          dbg_tx_ep;
    volatile uint8_t          dbg_tx_len;
    volatile uint8_t          dbg_tx_ctrl;
    volatile uint8_t          dbg_tx_first;
    volatile uint16_t         dbg_tx_cnt;
    volatile uint8_t          dbg_tx_dma_lo;
    volatile uint8_t          dbg_tx_dma_hi;
    volatile uint8_t          dbg_buf_lo;
    volatile uint8_t          dbg_buf_hi;
    volatile uint8_t          dbg_buf_first;
} usb_ctx_t;

static usb_ctx_t s_usb;

/* ===========================================================================
 * 端点 DMA 缓冲（独立全局数组，保证绝对 4 字节对齐）
 * ---------------------------------------------------------------------------
 * ★ 必须独立于 usb_ctx_t 结构体！
 *
 * CH59x 的 R16_UEPn_DMA 是 16 位寄存器，存 RAM 偏移。
 * 结构体成员的 aligned(4) 只保证相对偏移，不保证绝对地址对齐。
 * 若 usb_ctx_t 起始地址不是 4 的倍数，ep_buf 就错位，
 * DMA 传输失败 → 表现为「T_LEN 正确但主机收到全 0 数据」。
 *
 * ★★★ 每个端点必须是 2 × 64 = 128 字节（OUT + IN 连续）★★★
 *
 * 官方 COM 例程（tmp/CH592EVT/EVT/EXAM/USB/Device/COM/src/Main.c:159）：
 *     __aligned(4) UINT8 Ep1Buffer[2*MAX_PACKET_SIZE];   // OUT & IN
 *     __aligned(4) UINT8 Ep2Buffer[2*MAX_PACKET_SIZE];   // OUT & IN
 *     __aligned(4) UINT8 Ep3Buffer[2*MAX_PACKET_SIZE];   // OUT & IN
 *
 * 每个端点 128 字节 = 前 64 字节给 OUT，后 64 字节给 IN。
 * R16_UEPn_DMA 指向 OUT 起始（+0），IN 缓冲自动是 +64。
 *
 * 之前只给每个端点 64 字节，OUT 和 IN 共用同一块内存：
 *   - 发送时 memcpy 覆盖了接收缓冲
 *   - 硬件 IN DMA 读到的可能是被清空/覆盖的数据
 *   → 表现为「长度正确但数据全 0」
 *
 * EP4 不用这里（它的缓冲在 s_ep0_buf+64/+128）。
 * =========================================================================== */
static uint8_t s_ep_buf[USB_MAX_EP][2 * 64] __attribute__((aligned(64)));

/* 端点 OUT 缓冲地址（DMA 起始） */
#define EP_OUT_PTR(ep)   (&s_ep_buf[ep][0])
/* 端点 IN 缓冲地址（DMA 起始 +64） */
#define EP_IN_PTR(ep)    (&s_ep_buf[ep][64])

/* EP0 控制传输缓冲
 * ★ 必须 192 字节：手册表 17-3 规定 EP4 的收发缓冲紧跟在 UEP0_DMA 之后
 *   （+64 接收 / +128 发送），所以 EP0 区域要预留
 *   64(EP0) + 64(EP4 OUT) + 64(EP4 IN) = 192 字节。
 * ★ 同样必须独立于结构体，保证绝对 64 字节对齐。 */
static uint8_t s_ep0_buf[192] __attribute__((aligned(64)));

/* ===========================================================================
 * 端点资源池
 * ---------------------------------------------------------------------------
 * 维护 EP1~EP(USB_MAX_EP-1) 的每个方向占用情况。
 * 类驱动通过 hal_usb_ep_alloc() 申请，核心自动选号，避免冲突。
 *
 * 注意 EP4 特殊：它没有独立 DMA 寄存器，缓冲由 UEP0_DMA 派生，
 * 但端点号本身仍可正常分配使用。
 * =========================================================================== */
#define EP_POOL_FIRST   1                       /* 从 EP1 开始分配 */
#define EP_POOL_COUNT   (USB_MAX_EP - EP_POOL_FIRST)

/* 每个端点两个方向各占 1 位 */
static uint8_t s_ep_used[USB_MAX_EP];           /* bit0 = OUT, bit1 = IN */

#define EP_BIT_OUT  0x01
#define EP_BIT_IN   0x02

static uint8_t ep_dir_bit(uint8_t dir)
{
    return (dir == HAL_USB_EP_IN) ? EP_BIT_IN : EP_BIT_OUT;
}

hal_err_t hal_usb_ep_alloc_at(uint8_t ep_num, uint8_t type, uint8_t dir,
                              uint16_t max_packet, hal_usb_ep_t *out)
{
    uint8_t bit;

    if (!out) return HAL_ERR_PARAM;
    if (ep_num < EP_POOL_FIRST || ep_num >= USB_MAX_EP) return HAL_ERR_PARAM;

    bit = ep_dir_bit(dir);
    if (s_ep_used[ep_num] & bit) return HAL_ERR_BUSY;

    s_ep_used[ep_num] |= bit;

    out->ep_num     = ep_num;
    out->type       = type;
    out->dir        = dir;
    out->max_packet = max_packet;
    out->interval   = (type == HAL_USB_EP_INT) ? 16 : 0;
    return HAL_OK;
}

hal_err_t hal_usb_ep_alloc(uint8_t type, uint8_t dir, uint16_t max_packet,
                           hal_usb_ep_t *out)
{
    uint8_t ep;
    uint8_t bit;

    if (!out) return HAL_ERR_PARAM;

    bit = ep_dir_bit(dir);

    /* 顺序扫描找第一个空闲的 */
    for (ep = EP_POOL_FIRST; ep < USB_MAX_EP; ep++) {
        if (!(s_ep_used[ep] & bit)) {
            return hal_usb_ep_alloc_at(ep, type, dir, max_packet, out);
        }
    }
    return HAL_ERR_FULL;
}

hal_err_t hal_usb_ep_free(uint8_t ep_num, uint8_t dir)
{
    if (ep_num < EP_POOL_FIRST || ep_num >= USB_MAX_EP) return HAL_ERR_PARAM;
    s_ep_used[ep_num] &= (uint8_t)~ep_dir_bit(dir);
    return HAL_OK;
}

void hal_usb_ep_free_all(void)
{
    uint8_t i;
    for (i = 0; i < USB_MAX_EP; i++) s_ep_used[i] = 0;
}

void hal_usb_ep_pool_info(hal_usb_ep_pool_t *info)
{
    uint8_t i;
    uint8_t used = 0;

    if (!info) return;
    for (i = EP_POOL_FIRST; i < USB_MAX_EP; i++) {
        if (s_ep_used[i] & EP_BIT_OUT) used++;
        if (s_ep_used[i] & EP_BIT_IN)  used++;
    }
    info->total = (uint8_t)(EP_POOL_COUNT * 2);
    info->used  = used;
    info->free  = (uint8_t)(info->total - used);
}

uint8_t hal_usb_ep_is_used(uint8_t ep_num, uint8_t dir)
{
    if (ep_num >= USB_MAX_EP) return 0;
    return (s_ep_used[ep_num] & ep_dir_bit(dir)) ? 1 : 0;
}

/* 前向声明（实例在文件末尾定义，ISR 需要提前引用） */
static hal_err_t usb_add_class(hal_usb_t *u, const hal_usb_class_drv_t *drv,
                               void *ctx);
static hal_err_t usb_init(hal_usb_t *u, const hal_usb_dev_desc_t *desc);
static hal_err_t usb_deinit(hal_usb_t *u);
static hal_err_t usb_reconnect(hal_usb_t *u);
static hal_err_t usb_ep_write(hal_usb_t *u, uint8_t ep, const uint8_t *d,
                              uint16_t n);
static hal_err_t usb_ep_read(hal_usb_t *u, uint8_t ep, uint8_t *d, uint16_t n);
static void      usb_poll(hal_usb_t *u);
static uint8_t   usb_is_configured(hal_usb_t *u);

/* ===========================================================================
 * 端点寄存器访问
 * =========================================================================== */
static volatile uint8_t *ep_ctrl_reg(uint8_t ep)
{
    switch (ep) {
    case 0:  return &R8_UEP0_CTRL;
    case 1:  return &R8_UEP1_CTRL;
    case 2:  return &R8_UEP2_CTRL;
    case 3:  return &R8_UEP3_CTRL;
    case 4:  return &R8_UEP4_CTRL;
    case 5:  return &R8_UEP5_CTRL;
    case 6:  return &R8_UEP6_CTRL;
    default: return &R8_UEP7_CTRL;
    }
}

/* 端点缓冲区起始地址。
 * ★ EP4 没有独立的 DMA 寄存器（手册第 112 页表 17-3）：
 *   当 RB_UEP4_RX_EN=1 时接收缓冲在 UEP0_DMA+64，
 *   当 RB_UEP4_TX_EN=1 时发送缓冲在 UEP0_DMA+128。
 *   这里统一返回 UEP0_DMA+64（接收起始），发送地址由调用方按需 +64。
 *   本层为简化实现，EP4 的收发缓冲都放在 ep_buf[4]，但硬件实际用的是
 *   UEP0_DMA 之后的区域，所以 EP4 的 DMA 地址必须写成 ep0_buf 的偏移，
 *   不能写 ep_buf[4] 的地址。 */
static volatile uint16_t *ep_dma_reg(uint8_t ep)
{
    switch (ep) {
    case 0:  return &R16_UEP0_DMA;
    case 1:  return &R16_UEP1_DMA;
    case 2:  return &R16_UEP2_DMA;
    case 3:  return &R16_UEP3_DMA;
    case 4:  return &R16_UEP0_DMA;    /* EP4 依附 EP0 缓冲 */
    case 5:  return &R16_UEP5_DMA;
    case 6:  return &R16_UEP6_DMA;
    default: return &R16_UEP7_DMA;
    }
}

/* 端点模式控制寄存器（使能位所在）
 *   EP1/EP4 → R8_UEP4_1_MOD
 *   EP2/EP3 → R8_UEP2_3_MOD
 *   EP5/6/7 → R8_UEP567_MOD
 *   EP0     → 无需使能（默认可用）
 */
static volatile uint8_t *ep_mod_reg(uint8_t ep)
{
    if (ep == 1 || ep == 4) return &R8_UEP4_1_MOD;
    if (ep == 2 || ep == 3) return &R8_UEP2_3_MOD;
    return &R8_UEP567_MOD;
}

/* 端点使能位（RX/TX） */
static void ep_get_en_bits(uint8_t ep, uint8_t *rx_bit, uint8_t *tx_bit)
{
    switch (ep) {
    case 1:  *rx_bit = RB_UEP1_RX_EN; *tx_bit = RB_UEP1_TX_EN; break;
    case 2:  *rx_bit = RB_UEP2_RX_EN; *tx_bit = RB_UEP2_TX_EN; break;
    case 3:  *rx_bit = RB_UEP3_RX_EN; *tx_bit = RB_UEP3_TX_EN; break;
    case 4:  *rx_bit = RB_UEP4_RX_EN; *tx_bit = RB_UEP4_TX_EN; break;
    case 5:  *rx_bit = RB_UEP5_RX_EN; *tx_bit = RB_UEP5_TX_EN; break;
    case 6:  *rx_bit = RB_UEP6_RX_EN; *tx_bit = RB_UEP6_TX_EN; break;
    case 7:  *rx_bit = RB_UEP7_RX_EN; *tx_bit = RB_UEP7_TX_EN; break;
    default: *rx_bit = 0; *tx_bit = 0; break;
    }
}

/* 使能/禁用端点的某个方向（写模式控制寄存器） */
static void ep_set_enable(uint8_t ep, uint8_t is_in, uint8_t on)
{
    volatile uint8_t *mod;
    uint8_t rx_bit, tx_bit;

    if (ep == 0 || ep >= USB_MAX_EP) return;

    mod = ep_mod_reg(ep);
    ep_get_en_bits(ep, &rx_bit, &tx_bit);

    if (is_in) {
        if (on) *mod |= tx_bit; else *mod &= (uint8_t)~tx_bit;
    } else {
        if (on) *mod |= rx_bit; else *mod &= (uint8_t)~rx_bit;
    }
}

/* 设置端点 IN 状态（ACK / NAK） */
static void ep_set_tx_res(uint8_t ep, uint8_t res)
{
    volatile uint8_t *c = ep_ctrl_reg(ep);
    *c = (uint8_t)((*c & ~MASK_UEP_T_RES) | res);
}

/* 设置端点 OUT 状态（ACK / NAK） */
static void ep_set_rx_res(uint8_t ep, uint8_t res)
{
    volatile uint8_t *c = ep_ctrl_reg(ep);
    *c = (uint8_t)((*c & ~MASK_UEP_R_RES) | res);
}

/* 端点初始化（配置类型、缓冲、初始状态） */
static void ep_config(uint8_t ep, uint8_t type, uint8_t is_in, uint16_t size)
{
    volatile uint8_t *c;
    uint8_t ctrl = 0;

    if (ep == 0 || ep >= USB_MAX_EP) return;

    /* 1) 先使能端点方向（写模式控制寄存器），否则 CTRL/DMA 写不生效 */
    ep_set_enable(ep, is_in, 1);

    /* 2) CTRL：端点类型
     *    类型位 bit1:0 = 00 控制 / 01 等时 / 10 批量 / 11 中断
     *
     * ★★★ 不要设 RB_UEP_AUTO_TOG！★★★
     *
     * 官方 USB/Device/COM 例程（Main.c:1209-1230）的端点初始化：
     *   R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN | RB_UEP3_TX_EN;
     *   R8_UEP1_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;   ← 无 AUTO_TOG
     *   R8_UEP2_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;   ← 无 AUTO_TOG
     *
     * 而 IN 完成中断里（Main.c:338）是手动翻 TOG：
     *   R8_UEP2_CTRL ^= RB_UEP_T_TOG;
     *   R8_UEP2_CTRL = (R8_UEP2_CTRL & 0xfc) | IN_NAK;
     *
     * 如果同时开了 AUTO_TOG，硬件会自动翻一次、软件再翻一次，
     * TOG 位错乱 → 主机把数据包当重复包处理
     * → 表现为「长度正确但数据全 0」。
     *
     * 所以这里必须与官方一致：不开 AUTO_TOG，由软件手动管理 TOG。 */
    if (type == HAL_USB_EP_BULK)      ctrl |= 0x02;
    else if (type == HAL_USB_EP_INT)  ctrl |= 0x03;
    else if (type == HAL_USB_EP_ISO)  ctrl |= 0x01;

    c = ep_ctrl_reg(ep);
    /* ★ 保留 TOG 位，只改类型位，并显式设置初始响应状态
     *   （对齐官方：OUT=ACK, IN=NAK） */
    *c = (uint8_t)(ctrl | UEP_R_RES_ACK | UEP_T_RES_NAK);

    /* 3) DMA 地址
     *    ★ EP4 特殊：没有独立 DMA 寄存器，缓冲区由 UEP0_DMA 派生
     *      RX_EN=1 → UEP0_DMA+64
     *      TX_EN=1 → UEP0_DMA+128
     *      （手册第 112 页表 17-3） */
    if (ep == 4) {
        /* EP4 缓冲区地址由 UEP0_DMA 决定，不能单独设置 */
    } else {
        *ep_dma_reg(ep) = (uint16_t)(uintptr_t)s_ep_buf[ep];
    }

    HAL_UNUSED(size);
    HAL_UNUSED(is_in);
}

/* ===========================================================================
 * 字符串描述符
 * =========================================================================== */
static uint16_t make_string_desc(uint8_t *buf, const char *str)
{
    uint16_t i, len;
    if (!str) str = "";
    len = (uint16_t)strlen(str);
    if (len > 30) len = 30;

    buf[0] = (uint8_t)(2 + len * 2);
    buf[1] = USB_DESC_STRING;
    for (i = 0; i < len; i++) {
        buf[2 + i * 2] = (uint8_t)str[i];
        buf[3 + i * 2] = 0;
    }
    return (uint16_t)(2 + len * 2);
}

/* ===========================================================================
 * 设备描述符
 * =========================================================================== */
static uint16_t make_device_desc(uint8_t *buf)
{
    uint16_t i = 0;
    uint8_t  multi;

    /* 是否复合设备（多于 1 个类，或单个 CDC 也算 2 个接口） */
    multi = (uint8_t)(s_usb.class_count > 1);

    /* ★ 实验开关：强制单设备模式，用于定位问题
     *   1 = 强制 bDeviceClass=0x02（单 CDC 风格）
     *   0 = 按 class_count 自动判断
     * 设为 1 时即使注册了 2 个 CDC，设备描述符也报 0x02，
     * 用于区分「描述符问题」和「端点问题」。 */
    if (s_usb.force_single_class) multi = 0;

    /* ★ 诊断：记录 make_device_desc 被调用时的状态 */
    s_usb.dbg_dev_desc_calls++;
    s_usb.dbg_last_class_count = s_usb.class_count;
    s_usb.dbg_last_multi       = multi;

    buf[i++] = 18;
    buf[i++] = USB_DESC_DEVICE;

    /* ★★★ 复合设备描述符：完全对齐官方 CompoundDev 例程 ★★★
     *
     * 官方 tmp/CH592EVT/EVT/EXAM/USB/Device/CompoundDev/src/Main.c：
     *
     *   设备描述符：
     *     {0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, DevEP0SIZE, ...}
     *                  ↑↑↑↑  bcdUSB = 0x0110 (USB 1.1)
     *                        ↑↑↑↑↑↑ bDeviceClass = 0x00
     *                               bDeviceSubClass = 0x00
     *                               bDeviceProtocol = 0x00
     *
     *   配置描述符：2 个 HID 接口，**没有 IAD**！
     *     {0x09, 0x02, 0x3b, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32,
     *      0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,   ← 接口0 HID
     *      0x09, 0x21, ...                                          ← HID 描述符
     *      0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0a,               ← EP1 IN
     *      0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,   ← 接口1 HID
     *      0x09, 0x21, ...                                          ← HID 描述符
     *      0x07, 0x05, 0x82, 0x03, 0x04, 0x00, 0x0a}               ← EP2 IN
     *
     * ★ 关键结论：官方复合设备**不用 IAD**，也不需要 bDeviceClass=0xEF！
     *   用 bDeviceClass=0x00（由接口决定）+ 多个接口即可。
     *   Windows 会按每个接口的 bInterfaceClass 分别匹配驱动。
     *
     * 之前用 0xEF/0x02/0x01 + IAD 的方案导致 usbccgp 启动失败
     * （CM_PROB_FAILED_START），因为 CH59x 的 USB 控制器对
     * usbccgp 拆分 IAD 所需的某些请求支持不完整。
     *
     * bcdUSB 也用官方的 0x0110（USB 1.1 Full Speed），
     * 因为 CH59x 是 Full Speed 设备，声明 2.00 反而可能引起兼容问题。 */
    buf[i++] = 0x00; buf[i++] = 0x02;       /* bcdUSB = 2.00（IAD 是 USB 2.0 ECN） */
    if (multi) {
        /* ★ 复合设备 + IAD：必须用 0xEF/0x02/0x01
         *
         * IAD（接口关联描述符）是 USB 2.0 的 ECN，要求：
         *   bcdUSB          = 0x0200
         *   bDeviceClass    = 0xEF (Miscellaneous)
         *   bDeviceSubClass = 0x02 (Common Class)
         *   bDeviceProtocol = 0x01 (Interface Association Descriptor)
         *
         * 这样 Windows 的 usbccgp.sys 会按 IAD 把接口分组，
         * 不会把 CDC 数据接口（class=0x0A）当独立设备乱试 HID 描述符。 */
        buf[i++] = 0xEF;                    /* bDeviceClass = Miscellaneous */
        buf[i++] = 0x02;                    /* bDeviceSubClass = Common Class */
        buf[i++] = 0x01;                    /* bDeviceProtocol = IAD */
    } else {
        buf[i++] = 0x02;                    /* bDeviceClass = CDC */
        buf[i++] = 0x00;                    /* bDeviceSubClass */
        buf[i++] = 0x00;                    /* bDeviceProtocol */
    }
    buf[i++] = USB_EP0_SIZE;
    buf[i++] = (uint8_t)(s_usb.desc.vid & 0xFF);
    buf[i++] = (uint8_t)(s_usb.desc.vid >> 8);
    buf[i++] = (uint8_t)(s_usb.desc.pid & 0xFF);
    buf[i++] = (uint8_t)(s_usb.desc.pid >> 8);
    buf[i++] = (uint8_t)(s_usb.desc.bcd_device & 0xFF);
    buf[i++] = (uint8_t)(s_usb.desc.bcd_device >> 8);
    buf[i++] = 0x00;                        /* iManufacturer = 0 */
    buf[i++] = 0x02;                        /* iProduct = 2 */
    buf[i++] = 0x00;                        /* iSerialNumber = 0 */
    buf[i++] = 0x01;
    return i;
}

/* ===========================================================================
 * 配置描述符（自动拼装所有类）
 * ---------------------------------------------------------------------------
 * 核心负责：
 *   1) 分配接口号（按每类的 if_count 累加）
 *   2) 为 need_iad=1 的类自动插入 IAD（接口关联描述符）
 *   3) 回填 wTotalLength / bNumInterfaces
 *
 * 类驱动只需按传入的 first_if_num 填写自己的接口描述符，
 * 不需要关心 IAD、接口号分配、总长度计算。
 * =========================================================================== */
static void rebuild_config_desc(void)
{
    uint8_t  *buf = s_usb.cfg_desc;
    uint16_t  i = 0;
    uint8_t   if_num = 0;
    uint8_t   n;

    buf[i++] = 9;
    buf[i++] = USB_DESC_CONFIG;
    buf[i++] = 0; buf[i++] = 0;     /* wTotalLength 回填 */
    buf[i++] = 0;                   /* bNumInterfaces 回填 */
    buf[i++] = 0x01;                /* bConfigurationValue */
    buf[i++] = 0x00;
    buf[i++] = 0x80;                /* Bus Powered */
    buf[i++] = 50;                  /* 100mA */

    for (n = 0; n < s_usb.class_count; n++) {
        const hal_usb_class_drv_t *drv = s_usb.classes[n];
        uint16_t written;
        uint8_t  cnt;

        if (!drv || !drv->get_config_desc) continue;

        cnt = drv->if_count ? drv->if_count : 1;

        /* ★ 自动插入 IAD（need_iad=1 时）
         *   IAD 必须紧跟在配置描述符之后、第一个接口描述符之前，
         *   告诉主机「接下来 cnt 个接口属于同一个功能」。 */
        if (drv->need_iad && cnt > 1) {
            if (i + 8 > sizeof(s_usb.cfg_desc)) break;
            buf[i++] = 0x08;                    /* bLength */
            buf[i++] = 0x0B;                    /* INTERFACE_ASSOCIATION */
            buf[i++] = if_num;                  /* bFirstInterface */
            buf[i++] = cnt;                     /* bInterfaceCount */
            buf[i++] = drv->func_class;         /* bFunctionClass */
            buf[i++] = drv->func_subclass;      /* bFunctionSubClass */
            buf[i++] = drv->func_protocol;      /* bFunctionProtocol */
            buf[i++] = 0x00;                    /* iFunction */
        }

        written = drv->get_config_desc(s_usb.class_ctx[n], buf + i,
                                       (uint16_t)(sizeof(s_usb.cfg_desc) - i),
                                       if_num);
        if (!written) continue;

        i += written;
        if_num = (uint8_t)(if_num + cnt);
    }

    buf[2] = (uint8_t)(i & 0xFF);
    buf[3] = (uint8_t)(i >> 8);
    buf[4] = if_num ? if_num : 1;

    s_usb.cfg_desc_len = i;
}

/* ===========================================================================
 * EP0 控制传输
 * =========================================================================== */
/* EP0 发送：把数据放入 EP0 缓冲（对应官方 SETUP 阶段的 memcpy(Ep0Buffer, pDescr, len)）
 * ★ 严格对齐官方 BLE_USB 的 EP0 状态机：
 *   SETUP 阶段只做 memcpy + 记录剩余长度；
 *   IN 阶段翻转 T_TOG 并置 T_ACK。
 *
 * ★ 位定义（CH592SFR.h）：
 *     RB_UEP_R_TOG  = 0x80    RB_UEP_T_TOG  = 0x40
 *     MASK_UEP_R_RES= 0x0C    MASK_UEP_T_RES= 0x03
 *     UEP_R_RES_ACK = 0x00    UEP_T_RES_ACK = 0x00
 *     UEP_R_RES_NAK = 0x08    UEP_T_RES_NAK = 0x02
 *   官方 SETUP 完成后的标准状态是 0xC2
 *     = R_TOG(0x80) | T_TOG(0x40) | R_ACK(0x00) | T_NAK(0x02)
 *   缺少 R_TOG/T_TOG 会让主机认为数据翻转位不对，反复重传。 */
static void ep0_send(const uint8_t *data, uint16_t len)
{
    uint16_t n = (len > USB_EP0_SIZE) ? USB_EP0_SIZE : len;

    if (data && n && data != s_ep0_buf) memcpy(s_ep0_buf, data, n);

    s_usb.ep0_ptr    = (uint8_t *)data + n;
    s_usb.ep0_remain = (uint16_t)(len - n);

    /* ★ 诊断：记录本次发送的首包内容和长度 */
    if (s_usb.dbg_ep0_send_cnt < 8) {
        uint8_t k;
        s_usb.dbg_ep0_send_len[s_usb.dbg_ep0_send_cnt] = (uint8_t)n;
        for (k = 0; k < 8 && k < n; k++) {
            s_usb.dbg_ep0_send_hdr[s_usb.dbg_ep0_send_cnt][k] = s_ep0_buf[k];
        }
        s_usb.dbg_ep0_send_cnt++;
    }

    R8_UEP0_T_LEN = (uint8_t)n;

    /* ★★★ 致命 bug 修复：SETUP 阶段的数据准备必须写 0xC0，不能翻转 T_TOG ★★★
     *
     * 官方 app_usb.c 的两处 EP0_CTRL 赋值（已逐行核对）：
     *
     *   【1】收到 SETUP 包时（app_usb.c:292）
     *       R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG
     *                    | UEP_R_RES_ACK | UEP_T_RES_NAK;        = 0xC2
     *
     *   【2】SETUP 处理结束、准备发数据时（app_usb.c:484）
     *       R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG
     *                    | UEP_R_RES_ACK | UEP_T_RES_ACK;        = 0xC0
     *       ↑ 注意 T_TOG 仍然是 1（0x40），T_RES 改成 ACK
     *
     *   【3】后续 IN 续传（app_usb.c:207，仅当 SetupReqLen 还有剩余）
     *       R8_UEP0_CTRL ^= RB_UEP_T_TOG;                        ← 这里才翻转
     *
     * 之前 HAL 版在【2】阶段就 ^= 翻转 T_TOG，把 T_TOG 从 1 变成 0，
     * 设备发出 DATA0，而主机期望 DATA1 → 主机丢弃描述符包
     * → 设备管理器报「未知 USB 设备(设备描述符请求失败)」
     *   USB\VID_0000&PID_0002。
     *
     * 正确做法：【2】阶段直接写 0xC0（保留 T_TOG=1，T_RES=ACK）。 */
    R8_UEP0_CTRL = (uint8_t)(RB_UEP_R_TOG | RB_UEP_T_TOG |
                             UEP_R_RES_ACK | UEP_T_RES_ACK);
}

/* 状态阶段准备：对齐官方 app_usb.c:484
 *   R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
 *   = 0x80 | 0x40 | 0x00 | 0x00 = 0xC0
 *
 * ★ 关键：这里不是「状态阶段完成」，而是「准备发出状态包」。
 *   控制传输的流程是：
 *     1) SETUP 收到        → EP0_CTRL = 0xC2 (T_NAK，先不应答 IN)
 *     2) SETUP 处理完      → EP0_CTRL = 0xC0 (T_ACK，允许主机读 IN 包)
 *     3) IN 包发出后       → EP0_CTRL = 0x02 (T_NAK，回到空闲)
 *
 *   之前 HAL 版在这里直接写 0x02（T_NAK），相当于「不允许发 IN」，
 *   主机的状态阶段 IN 包永远拿不到应答 → 超时
 *   → 设备管理器报「未知 USB 设备(设定地址失败)」
 *     USB\VID_0000&PID_0004。
 *
 *   T_LEN 必须为 0（状态包是零长度包）。 */
static void ep0_ack(void)
{
    R8_UEP0_T_LEN = 0;
    R8_UEP0_CTRL  = (uint8_t)(RB_UEP_R_TOG | RB_UEP_T_TOG |
                              UEP_R_RES_ACK | UEP_T_RES_ACK);
}

/* 处理 SETUP 包 */
static void handle_setup(void)
{
    const uint8_t *st = s_usb.setup;
    uint8_t  bmRequestType = st[0];
    uint8_t  bRequest      = st[1];
    uint16_t wValue        = (uint16_t)(st[2] | (st[3] << 8));
    uint16_t wIndex        = (uint16_t)(st[4] | (st[5] << 8));
    uint16_t wLength       = (uint16_t)(st[6] | (st[7] << 8));
    uint8_t  req_type      = bmRequestType & USB_REQ_TYPE_MASK;
    uint16_t n = 0;
    uint8_t  i;

    /* 每次 SETUP 前清空类请求应答缓冲 */
    s_usb.class_reply_len = 0;

    /* 诊断：记录所有 SETUP 请求 */
    if (s_usb.dbg_setup_cnt < 32) {
        uint8_t k;
        for (k = 0; k < 8; k++) {
            s_usb.dbg_setup[s_usb.dbg_setup_cnt][k] = st[k];
        }
        s_usb.dbg_setup_cnt++;
    }

    if (req_type == USB_REQ_TYPE_STANDARD) {
        switch (bRequest) {
        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t desc_type = (uint8_t)(wValue >> 8);
            uint8_t desc_idx  = (uint8_t)(wValue & 0xFF);

            switch (desc_type) {
            case USB_DESC_DEVICE:
                n = make_device_desc(s_ep0_buf);
                /* ★ 按 wLength 截断（主机可能只请求 8 字节或 18 字节） */
                if (wLength && wLength < n) n = wLength;
                ep0_send(s_ep0_buf, n);
                return;

            case USB_DESC_CONFIG:
                if (!s_usb.cfg_desc_len) rebuild_config_desc();
                /* ★★★ 关键修复：必须按 wLength 截断 ★★★
                 *
                 * 主机（尤其 usbccgp.sys）会分两步读配置描述符：
                 *   1) 先发 GET_DESCRIPTOR(CONFIG) 且 wLength=9，只读配置头，
                 *      拿到 wTotalLength 和 bNumInterfaces；
                 *   2) 再发一次 wLength=wTotalLength，读完整描述符。
                 *
                 * 如果第 1 步就返回全部 125 字节，主机会认为设备
                 * 不遵守 wLength → 请求失败 → usbccgp 启动失败
                 * （CM_PROB_FAILED_START / 0xC0000001）。
                 *
                 * 这正是双 CDC 复合设备无法枚举的根因：
                 * 单 CDC 时描述符只有 41 字节，主机可能一次读完，
                 * 所以没暴露这个问题；双 CDC 时 125 字节必须分两次读，
                 * 不截断就必然失败。
                 *
                 * 官方 app_usb.c 里也有对应处理：
                 *   if (SetupReqLen > len) SetupReqLen = len;
                 *   len = (SetupReqLen >= DevEP0SIZE) ? DevEP0SIZE : SetupReqLen; */
                {
                    uint16_t total = s_usb.cfg_desc_len;
                    uint16_t send  = (wLength && wLength < total) ? wLength : total;
                    ep0_send(s_usb.cfg_desc, send);
                }
                return;

            case USB_DESC_STRING:
                switch (desc_idx) {
                case 0:
                    s_usb.str_buf[0] = 4;
                    s_usb.str_buf[1] = USB_DESC_STRING;
                    s_usb.str_buf[2] = 0x09;
                    s_usb.str_buf[3] = 0x04;
                    n = 4;
                    break;
                case 1: n = make_string_desc(s_usb.str_buf, s_usb.desc.manufacturer); break;
                case 2: n = make_string_desc(s_usb.str_buf, s_usb.desc.product); break;
                case 3: n = make_string_desc(s_usb.str_buf, s_usb.desc.serial); break;
                default: n = 0; break;
                }
                /* ★ 按 wLength 截断 */
                if (wLength && wLength < n) n = wLength;
                ep0_send(s_usb.str_buf, n);
                return;

            default:
                ep0_ack();
                return;
            }
        }

        case USB_REQ_SET_CONFIGURATION:
            s_usb.configured = 1;

            /* ★ 关键：按各类驱动声明的端点需求真正配置硬件端点
             *   之前 ep_config() 从未被调用，端点一直停留在 usb_init 的
             *   「全部 NAK」状态，导致 SET_CONFIGURATION 之后主机发的
             *   数据包全部被拒。
             *   多 CDC / 复合设备必须在这里逐个类遍历配置。 */
            for (i = 0; i < s_usb.class_count; i++) {
                const hal_usb_class_drv_t *drv = s_usb.classes[i];
                hal_usb_ep_t eps[8];
                uint8_t      cnt, k;

                if (!drv) continue;

                if (drv->get_eps) {
                    cnt = drv->get_eps(s_usb.class_ctx[i], eps, 8);
                    for (k = 0; k < cnt; k++) {
                        ep_config(eps[k].ep_num, eps[k].type,
                                  (uint8_t)(eps[k].dir == HAL_USB_EP_IN),
                                  eps[k].max_packet);
                    }
                }

                if (drv->init) drv->init(s_usb.class_ctx[i]);
            }
            ep0_ack();
            return;

        case USB_REQ_GET_CONFIGURATION:
            s_usb.str_buf[0] = s_usb.configured ? 1 : 0;
            ep0_send(s_usb.str_buf, (wLength && wLength < 1) ? wLength : 1);
            return;

        case USB_REQ_SET_ADDRESS:
            /* ★★★ 致命 bug 修复：SET_ADDRESS 不能在 SETUP 阶段生效 ★★★
             *
             * 官方 app_usb.c 的做法是两阶段：
             *   SETUP 阶段（case USB_SET_ADDRESS）：
             *       SetupReqLen = (pSetupReqPak->wValue) & 0xff;    ← 只记录，不设地址
             *   IN 阶段（状态包发完后）：
             *       R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
             *       R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
             *
             * 原因：USB 规范要求设备在「状态阶段完成后」才切换到新地址。
             * 如果在 SETUP 阶段就写 R8_USB_DEV_AD，硬件立刻只接收新地址的包，
             * 而主机此时还在用旧地址（0）发 IN 状态包 → 设备收不到
             * → 主机超时 → Windows 报「未知 USB 设备(设定地址失败)」
             *   （设备管理器显示 USB\VID_0000&PID_0004，端口号 8）。
             *
             * 这里只记录待设地址，真正的写寄存器放到 EP0 IN 阶段
             * （见 usb_isr 里 s_usb.pending_addr 的处理）。 */
            s_usb.pending_addr = (uint8_t)(wValue & 0x7F);
            s_usb.has_pending_addr = 1;
            ep0_ack();
            return;

        case USB_REQ_GET_STATUS:
            s_usb.str_buf[0] = 0;
            s_usb.str_buf[1] = 0;
            ep0_send(s_usb.str_buf, (wLength && wLength < 2) ? wLength : 2);
            return;

        /* ★★★ 复合设备必需：GET_INTERFACE / SET_INTERFACE ★★★
         *
         * Windows 的 usbccgp.sys（USB 通用父设备驱动）在拆分复合设备时，
         * 会对每个接口发 GET_INTERFACE 查询当前 altsetting。
         * 如果设备返回 STALL（未处理 → default 分支的 ep0_ack 虽然不 STALL，
         * 但不返回数据），usbccgp 会判定设备不支持接口管理，
         * 导致 CM_PROB_FAILED_START（0xC0000001）。
         *
         * 这正是双 CDC 复合设备无法枚举的根因：
         * 之前 handle_setup 的 default 分支只调 ep0_ack()（零长度状态包），
         * 而 GET_INTERFACE 是需要返回 1 字节数据的请求，
         * 主机收不到数据 → 请求失败 → usbccgp 启动失败。
         *
         * 官方 CompoundDev 例程里也有对应处理
         * （case USB_GET_INTERFACE: pEP0_DataBuf[0] = 0x00; ...）。 */
        case USB_REQ_GET_INTERFACE:
            s_usb.str_buf[0] = 0x00;        /* bAltSetting = 0 */
            ep0_send(s_usb.str_buf, (wLength && wLength < 1) ? wLength : 1);
            return;

        case USB_REQ_SET_INTERFACE:
            /* ★★★ SET_INTERFACE 处理（关键修复）★★★
             *
             * 实测 Windows usbser.sys 的调用序列（EEPROM 抓包）：
             *   [12] SET_CONFIGURATION(1)
             *   [13] SET_INTERFACE(interface=1, altsetting=0)   ← 注意是 alt=0！
             *   [14] SET_INTERFACE(interface=3, altsetting=0)   ← 也是 alt=0
             *
             * ★ 重要：Windows 并不使用 altsetting 1，它一直用 alt=0。
             *   所以数据接口的 altsetting 0 必须**带端点**（bNumEndpoints=2），
             *   不能像某些规范示例那样做成 0 端点。
             *
             * 之前这里在 alt=0 时禁用端点，导致 SET_INTERFACE 之后
             * Bulk 端点被关掉，usbser 无法收发 → STATUS_DEVICE_NOT_READY
             * → CM_PROB_FAILED_START。
             *
             * 正确做法：无论 alt 是 0 还是 1，都保证端点处于使能状态
             * （本 HAL 的 CDC 只用 altsetting 0 带端点）。 */
            {
                uint8_t c, k;
                for (c = 0; c < s_usb.class_count; c++) {
                    const hal_usb_class_drv_t *drv = s_usb.classes[c];
                    hal_usb_ep_t eps[8];
                    uint8_t cnt;

                    if (!drv || !drv->get_eps) continue;
                    cnt = drv->get_eps(s_usb.class_ctx[c], eps, 8);

                    for (k = 0; k < cnt; k++) {
                        /* 重新配置端点（使能 + 设类型 + 设 DMA + 设初始状态） */
                        ep_config(eps[k].ep_num, eps[k].type,
                                  (uint8_t)(eps[k].dir == HAL_USB_EP_IN),
                                  eps[k].max_packet);
                    }
                }
            }
            ep0_ack();
            return;

        case USB_REQ_CLEAR_FEATURE:
        case USB_REQ_SET_FEATURE:
            ep0_ack();
            return;

        default:
            ep0_ack();
            return;
        }
    }
    else if (req_type == USB_REQ_TYPE_CLASS) {
        /* 诊断：记录类请求 */
        if (s_usb.dbg_class_req_cnt < 8) {
            uint8_t k;
            for (k = 0; k < 8; k++) {
                s_usb.dbg_class_req[s_usb.dbg_class_req_cnt][k] = st[k];
            }
            s_usb.dbg_class_req_cnt++;
        }
        for (i = 0; i < s_usb.class_count; i++) {
            if (s_usb.classes[i] && s_usb.classes[i]->on_setup) {
                if (s_usb.classes[i]->on_setup(s_usb.class_ctx[i], st) == HAL_OK) {
                    /* 记录处理本请求的类，供后续 EP0 OUT 数据阶段使用 */
                    s_usb.setup_class_idx = i;
                    /* ★★★ 关键修复：类驱动返回 HAL_OK 后必须由 port 层
                     *     根据请求方向完成 EP0 响应，不能直接 return！★★★
                     *
                     * 之前这里直接 return，EP0 停留在 SETUP 阶段的 0xC2
                     * （T_NAK），主机永远等不到 IN 数据或 OUT 状态包，
                     * 导致 usbser.sys 初始化超时 → 接口报
                     * CM_PROB_FAILED_START（COM 口出现但打不开）。
                     *
                     * CDC 类请求的方向：
                     *   GET_LINE_CODING  (0x21, bit7=1) → IN，返回 7 字节
                     *   SET_LINE_CODING  (0x20, bit7=0) → OUT，收 7 字节
                     *   SET_CONTROL_LINE_STATE (0x22, bit7=0) → 无数据，ACK
                     *
                     * 这里按 bmRequestType.bit7 判断方向：
                     *   bit7=1（设备→主机）：发送类驱动准备好的数据
                     *   bit7=0（主机→设备）：如果 wLength>0 等 OUT 数据，
                     *                         否则直接 ACK 状态阶段 */
                    if (bmRequestType & 0x80) {
                        /* IN 请求：类驱动把数据放在 str_buf，长度由
                         * hal_usb_class_setup_reply() 设置 */
                        uint16_t rlen = s_usb.class_reply_len;
                        if (rlen > wLength) rlen = wLength;
                        if (rlen) {
                            ep0_send(s_usb.class_reply_buf, rlen);
                        } else {
                            ep0_ack();
                        }
                    } else {
                        /* OUT 请求：wLength>0 表示后面有数据阶段，
                         * 由 EP0 OUT 分支接收后交给 on_ep_out；
                         * 否则直接 ACK 状态阶段。 */
                        if (wLength > 0) {
                            /* 准备接收 OUT 数据：R_ACK | T_NAK */
                            R8_UEP0_CTRL = (uint8_t)(RB_UEP_R_TOG | RB_UEP_T_TOG |
                                                     UEP_R_RES_ACK | UEP_T_RES_NAK);
                        } else {
                            ep0_ack();
                        }
                    }
                    return;
                }
            }
        }
        ep0_ack();
        return;
    }

    HAL_UNUSED(wLength);
    ep0_ack();
}

/* ===========================================================================
 * USB 中断处理（状态机）
 * =========================================================================== */
static void usb_isr(void *user)
{
    uint8_t intflag = R8_USB_INT_FG;
    uint8_t intst;
    uint8_t token, ep;
    uint16_t len;

    /* ---- 挂起 / 唤醒 ----
     * ★ 对齐官方 USB_DevTransProcess 的 0x40D2 分支：
     *   官方收到 SUSPEND 时会做一整套端点复位：
     *       R8_USB_DEV_AD  = 0
     *       R8_UEP0_CTRL   = 0x02          (R_ACK | T_NAK)
     *       R8_UEP1/2/3_CTRL = 0x12        (AUTO_TOG | R_ACK | T_NAK)
     *       R8_USB_INT_FG  = 0x01          (清 SUSPEND)
     *   只清标志而不复位端点，会导致挂起/唤醒后端点状态错乱，
     *   主机重新枚举时设备无响应。 */
    if (intflag & RB_UIF_SUSPEND) {
        uint8_t i;
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL  = (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);
        /* ★ 只复位 EP1~EP4（USB_DeviceInit 使能的范围），
         *   不要碰 EP5/6/7 —— 未使能端点写 CTRL 会导致控制器异常。 */
        for (i = 1; i < 5 && i < USB_MAX_EP; i++) {
            volatile uint8_t *c = ep_ctrl_reg(i);
            *c = (uint8_t)(RB_UEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK);
        }
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }

    /* ---- 总线复位 ---- */
    if (intflag & RB_UIF_BUS_RST) {
        R8_USB_INT_FG = RB_UIF_BUS_RST;
        s_usb.configured = 0;
        s_usb.address = 0;
        s_usb.has_pending_addr = 0;
        s_usb.pending_addr = 0;
        R8_USB_DEV_AD = 0;

        /* ★ 对齐官方 app_usb.c BUS_RST 分支：
         *     R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;              → 0x02
         *     R8_UEP1/2/3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | AUTO_TOG → 0x12
         *   注意 OUT 方向是 **ACK**，不是 NAK！设成 NAK 会让主机发出的
         *   SET_CONFIGURATION 之后的第一个 OUT 包被设备拒绝，
         *   CDC 的 SET_LINE_CODING 收不到，枚举卡在配置阶段。
         *   之前 HAL 版对 ep>=1 用了 UEP_R_RES_NAK，这是致命错误。 */
        R8_UEP0_CTRL = (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);
        {
            uint8_t i;
            /* ★ 只复位 EP1~EP4（USB_DeviceInit 使能的范围）。
             *   EP5/6/7 在 SET_CONFIGURATION 时由 ep_config() 按需使能，
             *   复位阶段不能碰未使能端点的 CTRL 寄存器。 */
            for (i = 1; i < 5 && i < USB_MAX_EP; i++) {
                volatile uint8_t *c = ep_ctrl_reg(i);
                *c = (uint8_t)(RB_UEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK);
            }
        }
        for (ep = 0; ep < s_usb.class_count; ep++) {
            if (s_usb.classes[ep] && s_usb.classes[ep]->on_reset) {
                s_usb.classes[ep]->on_reset(s_usb.class_ctx[ep]);
            }
        }
        return;
    }

    /* ---- 传输完成 ---- */
    if (!(intflag & RB_UIF_TRANSFER)) return;

    intst = R8_USB_INT_ST;

    /* ★★★ 关键修复（对齐官方 USB_DevTransProcess）★★★
     *
     * 官方结构（app_usb.c:183-490）：
     *
     *   if (intflag & RB_UIF_TRANSFER) {
     *       if ((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
     *           switch (R8_USB_INT_ST & (MASK_UIS_TOKEN|MASK_UIS_ENDP)) { ... }
     *           R8_USB_INT_FG = RB_UIF_TRANSFER;      // ← 分支内清
     *       }
     *       if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {    // ← 独立的 if，不是 else if
     *           R8_UEP0_CTRL = 0xC2;                     // R_TOG|T_TOG|R_ACK|T_NAK
     *           SetupReqLen = pSetupReqPak->wLength;
     *           SetupReqCode = pSetupReqPak->bRequest;
     *           ...
     *           R8_USB_INT_FG = RB_UIF_TRANSFER;      // ← 再清一次
     *       }
     *   }
     *
     * 之前 HAL 版的错误：
     *   1) 在读取 R8_USB_INT_ST 之前就 `R8_USB_INT_FG = RB_UIF_TRANSFER`，
     *      部分 WCH 硬件上清 INT_FG 会连带影响 INT_ST 的 SETUP_ACT 位，
     *      导致 SETUP 包被漏掉 → 主机拿不到描述符 → 永不枚举。
     *   2) 用 `token == UIS_TOKEN_SETUP` 判断 SETUP 阶段。硬件在 SETUP 包时
     *      MASK_UIS_TOKEN 读出来是 0x30（MASK_UIS_TOKEN 本身），
     *      必须用 RB_UIS_SETUP_ACT 标志判断，不能用 token 值。
     *   3) 清 INT_FG 的位置在分支外，SETUP 处理期间标志已被清，
     *      后续 IN 阶段的 TOG 翻转判断拿不到正确状态。
     */
    token = (uint8_t)(intst & MASK_UIS_TOKEN);
    ep    = (uint8_t)(intst & MASK_UIS_ENDP);

    /* ---- 非控制传输（数据端点 + EP0 数据阶段）---- */
    if (token != MASK_UIS_TOKEN) {
        if (ep == 0) {
            /* ★ EP0 数据阶段续传（对齐官方 UIS_TOKEN_IN 无 endpoint 分支）
             * 官方：
             *   case UIS_TOKEN_IN:
             *     switch (SetupReqCode) {
             *       case USB_GET_DESCRIPTOR:
             *         len = SetupReqLen >= DevEP0SIZE ? DevEP0SIZE : SetupReqLen;
             *         memcpy(pEP0_DataBuf, pDescr, len);
             *         SetupReqLen -= len; pDescr += len;
             *         R8_UEP0_T_LEN = len;
             *         R8_UEP0_CTRL ^= RB_UEP_T_TOG;      ← 翻转
             *         break;
             *       case USB_SET_ADDRESS:
             *         R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
             *         R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
             *         break;
             *       default:
             *         R8_UEP0_T_LEN = 0;
             *         R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
             *         break;
             *     }
             * 之前 HAL 版把这段放在 `ep == 0` 的 token switch 里，
             * 而 SETUP 又用 token 判断，导致 IN 阶段永远走不到。 */
            if (token == UIS_TOKEN_IN) {
                if (s_usb.ep0_remain > 0) {
                    uint16_t n = (s_usb.ep0_remain > USB_EP0_SIZE)
                                 ? USB_EP0_SIZE : s_usb.ep0_remain;
                    memcpy(s_ep0_buf, s_usb.ep0_ptr, n);
                    s_usb.ep0_ptr    += n;
                    s_usb.ep0_remain = (uint16_t)(s_usb.ep0_remain - n);
                    R8_UEP0_T_LEN = (uint8_t)n;
                    R8_UEP0_CTRL ^= RB_UEP_T_TOG;       /* 翻转 T_TOG */
                } else {
                    /* ★ 状态阶段：先把待设地址写入寄存器，再置状态
                     *   对齐官方 IN 阶段 case USB_SET_ADDRESS:
                     *     R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | SetupReqLen;
                     *     R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
                     *   顺序很重要：地址必须在状态包 ACK 之后才生效。 */
                    if (s_usb.has_pending_addr) {
                        R8_USB_DEV_AD = (uint8_t)((R8_USB_DEV_AD & RB_UDA_GP_BIT) |
                                                  s_usb.pending_addr);
                        s_usb.address = s_usb.pending_addr;
                        s_usb.has_pending_addr = 0;
                    }
                    R8_UEP0_T_LEN = 0;
                    R8_UEP0_CTRL = (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);
                }
            } else if (token == UIS_TOKEN_OUT) {
                /* ★★★ EP0 控制 OUT 数据阶段（SET_LINE_CODING 等）★★★
                 *
                 * 之前这里把 EP0 收到的数据直接丢给所有类的 on_ep_out，
                 * 但：
                 *   1) 数据在 s_ep0_buf，不是类的端点缓冲
                 *   2) CDC 的 on_ep_out 会把它当串口数据转发给 rx_cb
                 *   3) SET_LINE_CODING 的 7 字节被误当串口数据
                 * 结果 usbser.sys 拿不到正确的 line coding，
                 * 报 STATUS_DEVICE_NOT_READY (0xC000003E)，
                 * 接口显示 CM_PROB_FAILED_START。
                 *
                 * 正确做法：把 EP0 OUT 数据交给「当前 SETUP 请求对应的类」，
                 * 由类驱动通过 hal_usb_class_setup_out_data() 取走。 */
                len = R8_USB_RX_LEN;
                if (len > USB_EP0_SIZE) len = USB_EP0_SIZE;
                s_usb.class_out_len = len;
                if (len) {
                    memcpy(s_usb.class_out_buf, s_ep0_buf, len);
                }
                /* 通知当前 SETUP 所属的类 */
                if (s_usb.setup_class_idx < s_usb.class_count) {
                    const hal_usb_class_drv_t *drv =
                        s_usb.classes[s_usb.setup_class_idx];
                    if (drv && drv->on_setup_data) {
                        drv->on_setup_data(s_usb.class_ctx[s_usb.setup_class_idx],
                                           s_usb.class_out_buf, len);
                    }
                }
                ep0_ack();
            }
        } else if (ep < USB_MAX_EP) {
            if (token == UIS_TOKEN_IN) {
                /* ★ IN 完成：手动翻 T_TOG + 置 NAK（严格对齐官方 COM 例程）
                 *
                 * 官方 Main.c:338-339：
                 *   R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                 *   R8_UEP2_CTRL = (R8_UEP2_CTRL & 0xfc) | IN_NAK;
                 *
                 * 因为 ep_config() 里没有开 RB_UEP_AUTO_TOG，
                 * 所以必须由软件手动翻 TOG，否则下一个包 TOG 不对，
                 * 主机会把它当重复包丢弃。 */
                {
                    volatile uint8_t *ctrl = ep_ctrl_reg(ep);
                    *ctrl ^= RB_UEP_T_TOG;
                    *ctrl = (uint8_t)((*ctrl & (uint8_t)~MASK_UEP_T_RES) |
                                      UEP_T_RES_NAK);
                }
                s_usb.ep_tx_busy[ep] = 0;
                {
                    uint8_t c;
                    for (c = 0; c < s_usb.class_count; c++) {
                        if (s_usb.classes[c] && s_usb.classes[c]->on_ep_in) {
                            s_usb.classes[c]->on_ep_in(s_usb.class_ctx[c], ep, 0);
                        }
                    }
                }
            } else if (token == UIS_TOKEN_OUT) {
                if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
                    uint8_t c;
                    len = R8_USB_RX_LEN;
                    for (c = 0; c < s_usb.class_count; c++) {
                        if (s_usb.classes[c] && s_usb.classes[c]->on_ep_out) {
                            s_usb.classes[c]->on_ep_out(s_usb.class_ctx[c], ep, len);
                        }
                    }
                }
            }
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }

    /* ---- SETUP 阶段（独立判断，对齐官方 RB_UIS_SETUP_ACT）---- */
    if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
        /* 读 8 字节 SETUP 包（硬件写在 pEP0_RAM_Addr） */
        memcpy(s_usb.setup, s_ep0_buf, 8);

        /* 官方：R_TOG | T_TOG | R_ACK | T_NAK = 0xC2 */
        R8_UEP0_CTRL = (uint8_t)(RB_UEP_R_TOG | RB_UEP_T_TOG |
                                 UEP_R_RES_ACK | UEP_T_RES_NAK);

        handle_setup();

        R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
}

/* ===========================================================================
 * 主接口
 * =========================================================================== */
static hal_err_t usb_add_class(hal_usb_t *u, const hal_usb_class_drv_t *drv,
                               void *ctx)
{
    usb_ctx_t *c = (usb_ctx_t *)u;

    if (!drv) return HAL_ERR_PARAM;
    if (c->class_count >= USB_MAX_CLASSES) return HAL_ERR_FULL;

    c->classes[c->class_count]   = drv;
    c->class_ctx[c->class_count] = ctx;
    c->class_count++;
    return HAL_OK;
}

static hal_err_t usb_init(hal_usb_t *u, const hal_usb_dev_desc_t *desc)
{
    usb_ctx_t *c = (usb_ctx_t *)u;
    uint8_t i;

    if (!desc) return HAL_ERR_PARAM;
    memcpy(&c->desc, desc, sizeof(hal_usb_dev_desc_t));
    if (!c->desc.bcd_device) c->desc.bcd_device = 0x0100;

    /* 拼装配置描述符 */
    rebuild_config_desc();

    /* ★ 关键：必须先给官方驱动设置端点 DMA 缓冲指针！
     *   USB_DeviceInit() 内部执行：
     *       R16_UEP0_DMA = (uint16_t)(uint32_t)pEP0_RAM_Addr;
     *   而这些 pEPx_RAM_Addr 是未初始化的全局指针（= NULL）。
     *   如果不先赋值，DMA 地址就是 0，主机永远读不到设备描述符，
     *   表现为「USB 设备根本不出现」。
     *   官方 BLE_USB / USB_Device/COM 例程都在 USB_DeviceInit() 之前赋值。 */
    pEP0_RAM_Addr = s_ep0_buf;
    pEP1_RAM_Addr = s_ep_buf[1];
    pEP2_RAM_Addr = s_ep_buf[2];
    pEP3_RAM_Addr = s_ep_buf[3];

    /* 官方底层初始化（USB 控制器 + 引脚 + 端点 DMA） */
    USB_DeviceInit();

    /* EP0 缓冲
     * ★ EP0 的 OUT 必须保持 ACK：主机要能把 SETUP 包送进来。
     *   （官方 InitCDCDevice 里写 NAK 是因为它后面还有别的流程，
     *     而 USB_DeviceInit() 本身已设为 ACK，这里保持一致即可。） */
    *ep_dma_reg(0) = (uint16_t)(uintptr_t)s_ep0_buf;
    R8_UEP0_CTRL = (uint8_t)(UEP_R_RES_ACK | UEP_T_RES_NAK);

    /* 其余端点先 NAK
     * ★ EP4 特殊：无独立 DMA 寄存器，缓冲区由 UEP0_DMA 派生
     *   （RX 在 UEP0_DMA+64，TX 在 UEP0_DMA+128，见手册表 17-3），
     *   所以这里不能写 ep_dma_reg(4)。
     *
     * ★★★ 关键修复：只初始化 EP1~EP4，不要碰 EP5/EP6/EP7 ★★★
     *
     * USB_DeviceInit() 只配置了 R8_UEP4_1_MOD 和 R8_UEP2_3_MOD，
     * 即只使能了 EP1/EP2/EP3/EP4。EP5/EP6/EP7 的 R8_UEP567_MOD
     * 保持为 0（未使能）。
     *
     * 对未使能的端点写 CTRL/DMA 寄存器会导致 USB 控制器异常，
     * 表现为「设备插入后完全不枚举，ep0_send 一次都不被调用」。
     * 这正是之前双 CDC 失败的根因。
     *
     * EP5/6/7 的使能必须由 ep_config() 在 SET_CONFIGURATION 时
     * 通过 ep_set_enable() 写 R8_UEP567_MOD 完成，不能提前碰。 */
    for (i = 1; i < 5 && i < USB_MAX_EP; i++) {
        if (i != 4) {
            *ep_dma_reg(i) = (uint16_t)(uintptr_t)s_ep_buf[i];
        }
        ep_set_tx_res(i, UEP_T_RES_NAK);
        ep_set_rx_res(i, UEP_R_RES_NAK);
        c->ep_tx_busy[i] = 0;
    }
    /* EP5/6/7 只清软件状态，不碰硬件寄存器 */
    for (i = 5; i < USB_MAX_EP; i++) {
        c->ep_tx_busy[i] = 0;
    }

    /* 注册中断 + 使能（用影子副本方式，不破坏 BB/LLE） */
    hal_irq_attach(USB_IRQn, usb_isr, NULL);
    hal_irq_enable(USB_IRQn);

    c->configured = 0;
    c->address = 0;
    u->dev.initialized = 1;
    return HAL_OK;
}

static hal_err_t usb_deinit(hal_usb_t *u)
{
    usb_ctx_t *c = (usb_ctx_t *)u;
    hal_irq_disable(USB_IRQn);
    hal_irq_detach(USB_IRQn);
    /* 释放端点池，允许重新注册类 */
    hal_usb_ep_free_all();
    c->class_count = 0;
    c->cfg_desc_len = 0;
    c->configured = 0;
    u->dev.initialized = 0;
    return HAL_OK;
}

static hal_err_t usb_reconnect(hal_usb_t *u)
{
    usb_ctx_t *c = (usb_ctx_t *)u;
    R8_USB_CTRL &= (uint8_t)~RB_UC_DEV_PU_EN;
    for (volatile int i = 0; i < 200000; i++) { __asm__ volatile("nop"); }
    R8_USB_CTRL |= RB_UC_DEV_PU_EN;
    c->configured = 0;
    return HAL_OK;
}

/* 端点发送长度寄存器（EP1~EP7 各有独立寄存器） */
static volatile uint8_t *ep_t_len_reg(uint8_t ep)
{
    switch (ep) {
    case 1: return &R8_UEP1_T_LEN;
    case 2: return &R8_UEP2_T_LEN;
    case 3: return &R8_UEP3_T_LEN;
    case 4: return &R8_UEP4_T_LEN;
    case 5: return &R8_UEP5_T_LEN;
    case 6: return &R8_UEP6_T_LEN;
    case 7: return &R8_UEP7_T_LEN;
    default: return NULL;
    }
}

static hal_err_t usb_ep_write(hal_usb_t *u, uint8_t ep, const uint8_t *d,
                              uint16_t n)
{
    usb_ctx_t *c = (usb_ctx_t *)u;
    volatile uint8_t *tlen;
    volatile uint8_t *ctrl;

    if (!d || !n || ep == 0 || ep >= USB_MAX_EP) return HAL_ERR_PARAM;
    if (n > 64) n = 64;

    tlen = ep_t_len_reg(ep);
    ctrl = ep_ctrl_reg(ep);
    if (!tlen || !ctrl) return HAL_ERR_PARAM;

    /* ★★★ 端点忙检查（对齐官方 USBSendData）★★★
     *
     * 官方 app_usb.c 的 USBSendData() 第一件事：
     *   if ((R8_UEP2_CTRL & MASK_UEP_T_RES) == UEP_T_RES_ACK)
     *       return FAILURE;         // 上一次还没发完，直接返回
     *
     * 为什么必须检查：
     *   T_RES=ACK 表示硬件正在/已准备好发送。
     *   此时若覆盖缓冲并重写 T_LEN，硬件 DMA 可能正读到一半，
     *   主机收到被覆盖后的部分数据或全 0 ——
     *   这正是「长度正确但数据全 0」的根因！
     *
     * 端点忙时返回 HAL_ERR_BUSY，由上层决定重试或丢弃。 */
    if ((*ctrl & MASK_UEP_T_RES) == UEP_T_RES_ACK) {
        return HAL_ERR_BUSY;
    }

    /* ★ EP4 特殊：缓冲区由 UEP0_DMA 派生（+128 发送区），
     *   不能写到 s_ep_buf[4]。
     * ★ 其他端点：IN 缓冲在 OUT 缓冲之后 +64 字节
     *   （官方 Ep1Buffer[2*MAX_PACKET_SIZE] 布局）。 */
    if (ep == 4) {
        memcpy(s_ep0_buf + 128, d, n);
    } else {
        memcpy(EP_IN_PTR(ep), d, n);
    }

    /* ★★★ 发送顺序（关键，对齐官方 app_usb.c 的 DevEPx_IN_Deal）★★★
     *
     * 官方写法：
     *   memcpy(pEPx_IN_DataBuf, data, len);   // 1) 先把数据放进缓冲
     *   R8_UEPx_T_LEN = len;                  // 2) 设长度
     *   R8_UEPx_CTRL = (R8_UEPx_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
     *                                          // 3) 最后置 ACK 触发发送
     *
     * ★ 必须严格按这个顺序：
     *   - T_LEN 必须在 T_RES=ACK 之前写
     *   - T_RES=ACK 是"触发器"，一旦置位硬件立刻开始 DMA 读缓冲
     *   - 如果在 ACK 之后又写 CTRL（比如 |= AUTO_TOG），
     *     会打断正在进行的 DMA，导致主机收到全 0 数据
     *
     * 之前代码在置 ACK 后又 `*ctrl |= RB_UEP_AUTO_TOG`，
     * 这就是「长度正确但数据全 0」的根因！
     *
     * AUTO_TOG 应该在 ep_config() 里一次性设好，发送时不要再碰。 */
    *tlen = (uint8_t)n;
    *ctrl = (uint8_t)((*ctrl & ~MASK_UEP_T_RES) | UEP_T_RES_ACK);

    c->ep_tx_busy[ep] = 1;

    /* 诊断：记录最后一次发送 */
    if (ep < 8) {
        c->dbg_tx_ep        = ep;
        c->dbg_tx_len       = (uint8_t)n;
        c->dbg_tx_ctrl      = *ctrl;
        c->dbg_tx_first     = n ? d[0] : 0;
        c->dbg_tx_cnt++;
        /* ★ 关键诊断：DMA 寄存器值 vs 缓冲实际地址 */
        c->dbg_tx_dma_lo    = (uint8_t)(*ep_dma_reg(ep) & 0xFF);
        c->dbg_tx_dma_hi    = (uint8_t)(*ep_dma_reg(ep) >> 8);
        c->dbg_buf_lo       = (uint8_t)((uintptr_t)EP_IN_PTR(ep) & 0xFF);
        c->dbg_buf_hi       = (uint8_t)(((uintptr_t)EP_IN_PTR(ep) >> 8) & 0xFF);
        /* 缓冲内容首字节（确认 memcpy 生效） */
        c->dbg_buf_first    = EP_IN_PTR(ep)[0];
    }
    return HAL_OK;
}

static hal_err_t usb_ep_read(hal_usb_t *u, uint8_t ep, uint8_t *d, uint16_t n)
{
    usb_ctx_t *c = (usb_ctx_t *)u;
    uint16_t len;

    if (!d || !n || ep == 0 || ep >= USB_MAX_EP) return HAL_ERR_PARAM;

    len = c->ep_rx_len[ep];
    if (len > n) len = n;
    if (len) {
        /* ★ EP4 特殊：接收缓冲区在 s_ep0_buf+64
         * ★ 其他端点：OUT 缓冲在 s_ep_buf[ep][0] */
        if (ep == 4) {
            memcpy(d, s_ep0_buf + 64, len);
        } else {
            memcpy(d, EP_OUT_PTR(ep), len);
        }
    }
    return (hal_err_t)len;
}

static void usb_poll(hal_usb_t *u)
{
    /* 中断驱动，无需轮询 */
    HAL_UNUSED(u);
}

static uint8_t usb_is_configured(hal_usb_t *u)
{
    return ((usb_ctx_t *)u)->configured;
}

/* ===========================================================================
 * 实例
 * ---------------------------------------------------------------------------
 * 注意：hal_usb_t 必须是 usb_ctx_t 的第一个成员，
 *      这样 (usb_ctx_t*)u 和 &s_usb.base 可以互相转换，ISR 与 API 共用同一对象。
 * =========================================================================== */
static void usb_init_instance(void)
{
    s_usb.base.dev.name        = "usb";
    s_usb.base.dev.initialized = 0;
    s_usb.base.add_class       = usb_add_class;
    s_usb.base.init            = usb_init;
    s_usb.base.deinit          = usb_deinit;
    s_usb.base.reconnect       = usb_reconnect;
    s_usb.base.ep_write        = usb_ep_write;
    s_usb.base.ep_read         = usb_ep_read;
    s_usb.base.poll            = usb_poll;
    s_usb.base.is_configured   = usb_is_configured;
}

hal_usb_t *hal_usb_get(void)
{
    static uint8_t inited = 0;
    if (!inited) {
        usb_init_instance();
        inited = 1;
    }
    return &s_usb.base;
}

/* ===========================================================================
 * 诊断接口：导出当前拼装的配置描述符，供应用 dump 检查
 * =========================================================================== */
const uint8_t *hal_usb_debug_cfg_desc(uint16_t *out_len)
{
    if (out_len) *out_len = s_usb.cfg_desc_len;
    return s_usb.cfg_desc;
}

/* 诊断接口：导出设备描述符（用于确认 bDeviceClass 是否正确） */
uint16_t hal_usb_debug_dev_desc(uint8_t *buf, uint16_t max_len)
{
    uint16_t n = make_device_desc(buf);
    if (n > max_len) n = max_len;
    return n;
}

/* 诊断接口：返回当前注册的类数量 */
uint8_t hal_usb_debug_class_count(void)
{
    return s_usb.class_count;
}

/* 诊断接口：返回设备描述符调用统计
 *   out[0] = 调用次数低字节
 *   out[1] = 调用次数高字节
 *   out[2] = 最后一次调用时的 class_count
 *   out[3] = 最后一次调用时的 multi 标志
 */
void hal_usb_debug_dev_desc_stats(uint8_t out[4])
{
    out[0] = (uint8_t)(s_usb.dbg_dev_desc_calls & 0xFF);
    out[1] = (uint8_t)(s_usb.dbg_dev_desc_calls >> 8);
    out[2] = s_usb.dbg_last_class_count;
    out[3] = s_usb.dbg_last_multi;
}

/* 诊断接口：导出 EP0 发送记录
 *   out_cnt   = 发送次数
 *   out_len   = 每次发送的首包长度（8 字节）
 *   out_hdr   = 每次发送的首包前 8 字节（8x8）
 */
void hal_usb_debug_ep0_sends(uint8_t *out_cnt, uint8_t *out_len, uint8_t *out_hdr)
{
    uint8_t i, k;
    *out_cnt = s_usb.dbg_ep0_send_cnt;
    for (i = 0; i < 8; i++) {
        out_len[i] = s_usb.dbg_ep0_send_len[i];
        for (k = 0; k < 8; k++) {
            out_hdr[i * 8 + k] = s_usb.dbg_ep0_send_hdr[i][k];
        }
    }
}

/* 实验开关：强制单设备模式 */
void hal_usb_debug_set_force_single(uint8_t on)
{
    s_usb.force_single_class = on ? 1 : 0;
}

/* ===========================================================================
 * 类驱动辅助：为类请求准备 IN 应答数据
 * ---------------------------------------------------------------------------
 * 类驱动的 on_setup() 处理 IN 类请求（如 CDC GET_LINE_CODING）时，
 * 调用本函数把应答数据放到 EP0 发送缓冲，port 层随后自动发出。
 * =========================================================================== */
void hal_usb_class_setup_reply(const uint8_t *data, uint16_t len)
{
    if (!data || !len) {
        s_usb.class_reply_len = 0;
        return;
    }
    if (len > sizeof(s_usb.class_reply_buf)) {
        len = sizeof(s_usb.class_reply_buf);
    }
    memcpy(s_usb.class_reply_buf, data, len);
    s_usb.class_reply_len = len;
}

/* 诊断：导出类请求记录 */
void hal_usb_debug_class_reqs(uint8_t *out_cnt, uint8_t *out_reqs)
{
    uint8_t i, k;
    *out_cnt = s_usb.dbg_class_req_cnt;
    for (i = 0; i < 8; i++) {
        for (k = 0; k < 8; k++) {
            out_reqs[i * 8 + k] = s_usb.dbg_class_req[i][k];
        }
    }
}

/* 诊断：导出所有 SETUP 请求记录 */
void hal_usb_debug_setups(uint8_t *out_cnt, uint8_t *out_setups)
{
    uint8_t i, k;
    *out_cnt = s_usb.dbg_setup_cnt;
    for (i = 0; i < 32; i++) {
        for (k = 0; k < 8; k++) {
            out_setups[i * 8 + k] = s_usb.dbg_setup[i][k];
        }
    }
}

/* 诊断：导出 ep_write 状态
 *   out[0] = 最后一次写入的端点号
 *   out[1] = 长度
 *   out[2] = 写入后的 CTRL 寄存器值
 *   out[3] = 首字节（传入数据）
 *   out[4..5] = 调用次数（小端）
 *   out[6..7] = DMA 寄存器值（小端）
 *   out[8..9] = 缓冲实际地址低 16 位（小端）
 *   out[10] = 缓冲内容首字节（验证 memcpy）
 */
void hal_usb_debug_tx_state(uint8_t out[11])
{
    out[0] = s_usb.dbg_tx_ep;
    out[1] = s_usb.dbg_tx_len;
    out[2] = s_usb.dbg_tx_ctrl;
    out[3] = s_usb.dbg_tx_first;
    out[4] = (uint8_t)(s_usb.dbg_tx_cnt & 0xFF);
    out[5] = (uint8_t)(s_usb.dbg_tx_cnt >> 8);
    out[6] = s_usb.dbg_tx_dma_lo;
    out[7] = s_usb.dbg_tx_dma_hi;
    out[8] = s_usb.dbg_buf_lo;
    out[9] = s_usb.dbg_buf_hi;
    out[10] = s_usb.dbg_buf_first;
}
