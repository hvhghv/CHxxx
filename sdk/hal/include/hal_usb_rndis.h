/**
 * @file    hal_usb_rndis.h
 * @brief   USB RNDIS（Remote NDIS）类驱动 —— 芯片无关
 *
 * RNDIS 是微软定义的 USB 网络设备协议，Windows 无需驱动即可识别为
 * "Remote NDIS Compatible Device"，Linux/macOS 需 rndis_host 驱动。
 *
 * 与 CDC-ECM/NCM 的区别：
 *   - RNDIS 用私有控制协议（OID 请求）配置网络参数
 *   - 数据包带 44 字节 RNDIS 头（含 IEEE 802.3 以太网帧）
 *   - Windows 原生支持，Linux 需内核模块
 *
 * 典型用法：
 *   hal_usb_rndis_cfg_t cfg = {
 *       .mac        = {0x02,0x00,0x00,0x00,0x00,0x01},
 *       .ipv4       = {192,168,7,1},
 *       .netmask    = {255,255,255,0},
 *   };
 *   hal_usb_rndis_handle_t h = hal_usb_rndis_add(&cfg, on_rx, user);
 *   ...
 *   hal_usb_rndis_write(h, frame, len);   // 发送以太网帧
 */
#ifndef HAL_USB_RNDIS_H
#define HAL_USB_RNDIS_H

#include "hal_types.h"
#include "hal_usb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 配置
 * =========================================================================== */
typedef struct {
    uint8_t  mac[6];        /**< 本机 MAC（建议 02:xx 本地管理地址） */
    uint8_t  ipv4[4];       /**< 本机 IPv4（DHCP 服务器地址） */
    uint8_t  netmask[4];    /**< 子网掩码 */
    uint8_t  gateway[4];    /**< 网关（通常同 ipv4） */
    uint16_t mtu;           /**< MTU，默认 1500 */
    uint8_t  ep_notify;     /**< 0 = 不声明中断端点（省 1 个端点） */
    uint8_t  ep_out;        /**< 0 = 自动分配 */
    uint8_t  ep_in;         /**< 0 = 自动分配 */
} hal_usb_rndis_cfg_t;

/* ===========================================================================
 * 句柄
 * =========================================================================== */
typedef uint8_t hal_usb_rndis_handle_t;
#define HAL_USB_RNDIS_HANDLE_INVALID   0xFF

/* 收到以太网帧（在 USB 中断上下文，应尽快拷贝走） */
typedef void (*hal_usb_rndis_rx_cb_t)(const uint8_t *frame, uint16_t len,
                                      void *user);

/* ===========================================================================
 * 接口
 * =========================================================================== */

/**
 * 注册一个 RNDIS 实例（必须在 hal_usb->init() 之前调用）
 * @return 句柄，失败返回 HAL_USB_RNDIS_HANDLE_INVALID
 */
hal_usb_rndis_handle_t hal_usb_rndis_add(const hal_usb_rndis_cfg_t *cfg,
                                         hal_usb_rndis_rx_cb_t cb,
                                         void *user);

/**
 * 发送一个以太网帧
 * @return 实际写入字节数，<0 表示错误
 */
int hal_usb_rndis_write(hal_usb_rndis_handle_t h,
                        const uint8_t *frame, uint16_t len);

/** 查询链路是否已就绪（主机已配置并打开数据通道） */
uint8_t hal_usb_rndis_link_up(hal_usb_rndis_handle_t h);

/** 获取主机分配的对端 MAC（DHCP 协商后可用） */
void hal_usb_rndis_peer_mac(hal_usb_rndis_handle_t h, uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_RNDIS_H */
