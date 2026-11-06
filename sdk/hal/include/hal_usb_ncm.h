/**
 * @file    hal_usb_ncm.h
 * @brief   USB NCM（Network Control Model）类驱动 —— 芯片无关
 *
 * NCM 是 USB-IF 标准网络类（CDC-NCM），Linux/macOS 原生支持，
 * Windows 10+ 也内置驱动（usbnet / Ncm.sys）。
 *
 * 相比 RNDIS：
 *   - 标准协议，跨平台兼容性更好
 *   - 用 NTB（NCM Transfer Block）聚合多个以太网帧，吞吐更高
 *   - 控制接口用 CDC 标准请求（SET_ETHERNET_PACKET_FILTER 等）
 *
 * 典型用法：
 *   hal_usb_ncm_cfg_t cfg = {
 *       .mac     = {0x02,0x00,0x00,0x00,0x00,0x02},
 *       .ipv4    = {192,168,8,1},
 *       .netmask = {255,255,255,0},
 *   };
 *   hal_usb_ncm_handle_t h = hal_usb_ncm_add(&cfg, on_rx, user);
 *   ...
 *   hal_usb_ncm_write(h, frame, len);
 */
#ifndef HAL_USB_NCM_H
#define HAL_USB_NCM_H

#include "hal_types.h"
#include "hal_usb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 配置
 * =========================================================================== */
typedef struct {
    uint8_t  mac[6];        /**< 本机 MAC */
    uint8_t  ipv4[4];       /**< 本机 IPv4 */
    uint8_t  netmask[4];    /**< 子网掩码 */
    uint8_t  gateway[4];    /**< 网关 */
    uint16_t mtu;           /**< MTU，默认 1500 */
    uint16_t max_ntb;       /**< 单个 NTB 最大字节数，默认 2048 */

    uint8_t  ep_notify;     /**< 0 = 不声明中断端点 */
    uint8_t  ep_out;        /**< 0 = 自动分配 */
    uint8_t  ep_in;         /**< 0 = 自动分配 */
} hal_usb_ncm_cfg_t;

/* ===========================================================================
 * 句柄
 * =========================================================================== */
typedef uint8_t hal_usb_ncm_handle_t;
#define HAL_USB_NCM_HANDLE_INVALID     0xFF

/* 收到以太网帧（在 USB 中断上下文） */
typedef void (*hal_usb_ncm_rx_cb_t)(const uint8_t *frame, uint16_t len,
                                    void *user);

/* ===========================================================================
 * 接口
 * =========================================================================== */

/**
 * 注册一个 NCM 实例（必须在 hal_usb->init() 之前调用）
 * @return 句柄，失败返回 HAL_USB_NCM_HANDLE_INVALID
 */
hal_usb_ncm_handle_t hal_usb_ncm_add(const hal_usb_ncm_cfg_t *cfg,
                                     hal_usb_ncm_rx_cb_t cb,
                                     void *user);

/**
 * 发送一个以太网帧（内部打包成 NTB）
 * @return 实际写入字节数，<0 表示错误
 */
int hal_usb_ncm_write(hal_usb_ncm_handle_t h,
                      const uint8_t *frame, uint16_t len);

/** 查询链路是否已就绪 */
uint8_t hal_usb_ncm_link_up(hal_usb_ncm_handle_t h);

/** 获取主机分配的 MAC */
void hal_usb_ncm_peer_mac(hal_usb_ncm_handle_t h, uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_NCM_H */
