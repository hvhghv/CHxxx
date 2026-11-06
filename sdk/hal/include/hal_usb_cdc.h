/**
 * @file    hal_usb_cdc.h
 * @brief   USB CDC（虚拟串口）便捷封装 —— 支持多实例
 *
 * 用法（单 CDC，带通知端点）：
 *   hal_usb_cdc_cfg_t cfg = { .ep_notify=1, .ep_out=2, .ep_in=3 };
 *   hal_usb_cdc_handle_t h = hal_usb_cdc_add(&cfg, on_rx, NULL);
 *   hal_usb_cdc_write(h, (const uint8_t*)"hi\r\n", 4);
 *
 * 用法（2 个 CDC，带通知端点，CH59x 上限）：
 *   hal_usb_cdc_cfg_t c1 = { .ep_notify=1, .ep_out=2, .ep_in=3 };
 *   hal_usb_cdc_cfg_t c2 = { .ep_notify=4, .ep_out=5, .ep_in=6 };
 *   hal_usb_cdc_handle_t h1 = hal_usb_cdc_add(&c1, on_rx1, NULL);
 *   hal_usb_cdc_handle_t h2 = hal_usb_cdc_add(&c2, on_rx2, NULL);
 *
 * 用法（3 个 CDC，省略通知端点，CH59x 上限）：
 *   hal_usb_cdc_cfg_t c1 = { .ep_notify=0, .ep_out=1, .ep_in=2 };
 *   hal_usb_cdc_cfg_t c2 = { .ep_notify=0, .ep_out=3, .ep_in=4 };
 *   hal_usb_cdc_cfg_t c3 = { .ep_notify=0, .ep_out=5, .ep_in=6 };
 *   hal_usb_cdc_add(&c1, on_rx1, NULL);
 *   hal_usb_cdc_add(&c2, on_rx2, NULL);
 *   hal_usb_cdc_add(&c3, on_rx3, NULL);
 *
 * 用法（全自动分配端点）：
 *   hal_usb_cdc_handle_t h = hal_usb_cdc_add(NULL, on_rx, NULL);
 *   // 或显式省略通知端点 + 自动分配数据端点：
 *   hal_usb_cdc_cfg_t c = { .ep_notify=0 };   // ep_out/ep_in = 0 → 自动
 *   hal_usb_cdc_add(&c, on_rx, NULL);
 */
#ifndef HAL_USB_CDC_H
#define HAL_USB_CDC_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 实例存储
 * ---------------------------------------------------------------------------
 * ★ 通用层不限制 CDC 实例数量，也不假设芯片端点能力。
 *   实例数量上限由两个芯片相关因素决定，都在 port 层：
 *     1) 端点池是否还有空闲端点（hal_usb_ep_alloc 返回 HAL_ERR_FULL）
 *     2) 配置描述符缓冲是否够大（port 层 cfg_desc[]）
 *   通用层只管按需分配，不够就返回 HAL_USB_CDC_HANDLE_INVALID。
 *
 * 实例上下文（含 rx/tx 环形缓冲）由应用提供，有两种方式：
 *
 *   方式 A（推荐）：用下面的 HAL_USB_CDC_INSTANCE() 宏静态定义
 *     static HAL_USB_CDC_INSTANCE(my_cdc, 128, 128);
 *     hal_usb_cdc_add_ex(&my_cdc, NULL, on_rx, NULL);
 *
 *   方式 B：自己分配内存
 *     static uint8_t rx[128], tx[128];
 *     static hal_usb_cdc_inst_t inst;
 *     hal_usb_cdc_inst_init(&inst, rx, sizeof(rx), tx, sizeof(tx));
 *     hal_usb_cdc_add_ex(&inst, NULL, on_rx, NULL);
 *
 *   方式 C（最简）：hal_usb_cdc_add(NULL, on_rx, NULL)
 *     由 port 层从内部静态池切分，池大小见 HAL_USB_CDC_STATIC_POOL。
 * =========================================================================== */

/** CDC 实例存储（应用侧定义，不要直接访问成员） */
typedef struct {
    void    *priv[16];      /**< 内部使用，勿访问 */
} hal_usb_cdc_inst_t;

/**
 * 静态定义一个 CDC 实例（含 rx/tx 缓冲）。
 * @param name  变量名
 * @param rxsz  接收缓冲字节数
 * @param txsz  发送缓冲字节数
 */
#define HAL_USB_CDC_INSTANCE(name, rxsz, txsz)                              \
    static uint8_t name##_rxbuf[(rxsz)];                                    \
    static uint8_t name##_txbuf[(txsz)];                                    \
    static hal_usb_cdc_inst_t name = {                                      \
        { (void *)name##_rxbuf, (void *)(rxsz),                             \
          (void *)name##_txbuf, (void *)(txsz) } }

/**
 * 用外部缓冲初始化实例。
 * @param inst     实例存储
 * @param rx       接收缓冲
 * @param rx_size  接收缓冲大小
 * @param tx       发送缓冲
 * @param tx_size  发送缓冲大小
 */
void hal_usb_cdc_inst_init(hal_usb_cdc_inst_t *inst,
                           uint8_t *rx, uint16_t rx_size,
                           uint8_t *tx, uint16_t tx_size);

typedef struct {
    uint8_t  ep_in;          /**< 数据 IN 端点号（0 = 自动分配） */
    uint8_t  ep_out;         /**< 数据 OUT 端点号（0 = 自动分配） */
    uint8_t  ep_notify;      /**< 中断 IN 端点号
                              *   0 = 不声明通知端点（省 1 个端点，
                              *       每个 CDC 只占 2 个端点）
                              *   非 0 = 声明通知端点（每个 CDC 占 3 个端点） */
    uint16_t rx_buf_size;    /**< 接收缓冲大小（仅 cfg=NULL 时有效） */
    uint16_t tx_buf_size;    /**< 发送缓冲大小（仅 cfg=NULL 时有效） */
} hal_usb_cdc_cfg_t;

/* 接收回调（在中断上下文，应尽量短） */
typedef void (*hal_usb_cdc_rx_cb_t)(const uint8_t *data, uint16_t len,
                                    void *user);

/** CDC 实例句柄（= 实例索引 + 1，0 表示无效） */
typedef uint8_t hal_usb_cdc_handle_t;
#define HAL_USB_CDC_HANDLE_INVALID  0

/**
 * 注册一个 CDC 类实例（必须在 hal_usb_init 之前调用）。
 * 实例存储由内部静态池提供，池大小见 HAL_USB_CDC_STATIC_POOL。
 *
 * @param cfg   配置（端点号、缓冲大小），可为 NULL（全自动）
 * @param cb    接收回调
 * @param user  回调用户数据
 * @return 实例句柄（>0），失败返回 HAL_USB_CDC_HANDLE_INVALID
 */
hal_usb_cdc_handle_t hal_usb_cdc_add(const hal_usb_cdc_cfg_t *cfg,
                                     hal_usb_cdc_rx_cb_t cb, void *user);

/**
 * 注册一个 CDC 类实例，使用应用提供的实例存储。
 * 适合实例数量或缓冲大小需要精确控制的场景。
 *
 * @param inst  应用定义的实例（HAL_USB_CDC_INSTANCE 宏或 hal_usb_cdc_inst_init）
 * @param cfg   配置（端点号），可为 NULL（全自动）
 * @param cb    接收回调
 * @param user  回调用户数据
 * @return 实例句柄（>0），失败返回 HAL_USB_CDC_HANDLE_INVALID
 */
hal_usb_cdc_handle_t hal_usb_cdc_add_ex(hal_usb_cdc_inst_t *inst,
                                        const hal_usb_cdc_cfg_t *cfg,
                                        hal_usb_cdc_rx_cb_t cb, void *user);

/** 发送（非阻塞，返回入队字节数） */
int       hal_usb_cdc_write(hal_usb_cdc_handle_t h,
                            const uint8_t *data, uint16_t len);

/** 是否已连接（主机置位 DTR） */
uint8_t   hal_usb_cdc_connected(hal_usb_cdc_handle_t h);

/** 供 hal_log 使用：日志后端切到 USB CDC 时调用 */
int       hal_usb_cdc_log_write(hal_usb_cdc_handle_t h,
                                const uint8_t *data, uint16_t len);

/* ---- 兼容旧接口（单实例场景，等价于句柄 1）---- */
#define hal_usb_cdc_write_0(d, n)   hal_usb_cdc_write(1, (d), (n))
#define hal_usb_cdc_connected_0()   hal_usb_cdc_connected(1)

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_CDC_H */
