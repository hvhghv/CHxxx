/**
 * @file    hal_usb.h
 * @brief   USB 核心接口（类驱动框架，支持复合设备）
 *
 * 设计要点：
 *   1. 类驱动（Class Driver）可插拔：CDC / HID / MSC / Audio / Vendor
 *   2. 描述符动态拼装：按启用的类自动生成 Configuration 描述符
 *   3. 端点资源统一分配，避免冲突
 *   4. 复合设备：多个类共存于一个 Configuration
 *
 * 典型用法（CDC + HID 复合设备）：
 *   hal_usb_t *u = hal_usb_get();
 *   hal_usb_cdc_cfg_t cdc = { .ep_in=3, .ep_out=2, .ep_notify=1 };
 *   hal_usb_cdc_add(&cdc, on_rx, NULL);
 *   hal_usb_hid_cfg_t hid = { .ep_in=4, .report_desc=..., .report_len=... };
 *   hal_usb_hid_add(&hid, NULL);
 *   hal_usb_dev_desc_t desc = { .vid=0x1A86, .pid=0xFE07, ... };
 *   u->init(u, &desc);
 *   while (1) { u->poll(u); }
 */
#ifndef HAL_USB_H
#define HAL_USB_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 功能类
 * =========================================================================== */
typedef enum {
    HAL_USB_CLASS_CDC    = 0,   /**< 通信设备类：虚拟串口 */
    HAL_USB_CLASS_HID    = 1,   /**< 人机接口：键盘/鼠标/自定义 */
    HAL_USB_CLASS_MSC    = 2,   /**< 大容量存储 */
    HAL_USB_CLASS_AUDIO  = 3,   /**< 音频 */
    HAL_USB_CLASS_VENDOR = 4,   /**< 厂商自定义 */
    HAL_USB_CLASS_RNDIS  = 5,   /**< 微软 RNDIS 网络设备（Windows 免驱） */
    HAL_USB_CLASS_NCM    = 6,   /**< USB-IF NCM 网络设备（跨平台标准） */
} hal_usb_class_t;

/* 端点类型 */
#define HAL_USB_EP_CTRL   0
#define HAL_USB_EP_ISO    1
#define HAL_USB_EP_BULK   2
#define HAL_USB_EP_INT    3

/* 端点方向 */
#define HAL_USB_EP_OUT    0
#define HAL_USB_EP_IN     1

/* 端点号上限（不含方向位）
 *
 * 芯片实际能力由 port 层决定，这里是各芯片的保守最小值：
 *   CH59x (CH591/CH592) : EP0~EP7  → 8
 *   CH32 / 其他         : 通常 4~8
 *
 * 通用层用它做端点号越界检查，避免类驱动配出不存在的端点。
 * port 层可在自己的头文件里定义更大的值覆盖。 */
#ifndef HAL_USB_EP_MAX
#define HAL_USB_EP_MAX    8
#endif

/* 端点描述 */
typedef struct {
    uint8_t  ep_num;        /**< 端点号（不含方向位） */
    uint8_t  type;          /**< HAL_USB_EP_* */
    uint8_t  dir;           /**< HAL_USB_EP_IN / OUT */
    uint16_t max_packet;    /**< 最大包长 */
    uint8_t  interval;      /**< 轮询间隔（ms，仅中断/等时） */
} hal_usb_ep_t;

/* ===========================================================================
 * 类驱动接口
 * ---------------------------------------------------------------------------
 * 每个 USB 功能类实现这组回调，由 hal_usb 核心统一调度。
 * =========================================================================== */
typedef struct {
    hal_usb_class_t type;
    const char     *name;

    /* ---- 接口布局（供核心自动分配接口号 / 生成 IAD）---- */

    /** 该功能占用的 USB 接口数量（CDC = 2，HID = 1，MSC = 1，NCM = 2）
     *  为 0 时核心按 1 处理。 */
    uint8_t  if_count;

    /** 是否需要 IAD（接口关联描述符）
     *  if_count > 1 时通常需要，用于告诉主机这些接口属于同一功能。
     *  核心会在第一个接口前自动插入 IAD。 */
    uint8_t  need_iad;

    /** IAD 中声明的功能类/子类/协议（need_iad=1 时有效）
     *  例：CDC ACM → 0x02 / 0x02 / 0x01 */
    uint8_t  func_class;
    uint8_t  func_subclass;
    uint8_t  func_protocol;

    /**
     * 返回该类的接口描述符（含端点、类特定描述符）。
     * @param ctx           类实例上下文（多实例时必须用它区分）
     * @param buf           输出缓冲
     * @param max_len       缓冲大小
     * @param first_if_num  核心分配的起始接口号（类驱动据此填写
     *                      bInterfaceNumber，不要再自己算）
     * @return 写入字节数
     */
    uint16_t (*get_config_desc)(void *ctx, uint8_t *buf, uint16_t max_len,
                                uint8_t first_if_num);

    /** 端点需求（返回需要的端点列表，核心负责分配） */
    uint8_t  (*get_eps)(void *ctx, hal_usb_ep_t *eps, uint8_t max_eps);

    /** 初始化（分配端点、准备缓冲） */
    hal_err_t (*init)(void *ctx);

    /** 标准请求处理（SETUP 阶段）。返回 HAL_OK 表示已处理 */
    hal_err_t (*on_setup)(void *ctx, const uint8_t setup[8]);

    /**
     * 类请求的 OUT 数据阶段（如 CDC SET_LINE_CODING 的 7 字节）。
     * 仅当 on_setup 返回 HAL_OK 且该请求 wLength > 0 时调用。
     */
    void      (*on_setup_data)(void *ctx, const uint8_t *data, uint16_t len);

    /** 数据收发完成 */
    void      (*on_ep_in )(void *ctx, uint8_t ep, uint16_t len);
    void      (*on_ep_out)(void *ctx, uint8_t ep, uint16_t len);

    /** 总线事件 */
    void      (*on_reset)(void *ctx);
    void      (*on_suspend)(void *ctx);
    void      (*on_resume)(void *ctx);
} hal_usb_class_drv_t;

/* ===========================================================================
 * 设备描述
 * =========================================================================== */
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

/* ===========================================================================
 * 主接口
 * =========================================================================== */
typedef struct hal_usb hal_usb_t;

struct hal_usb {
    hal_dev_t dev;

    /** 注册一个功能类（必须在 init 之前调用） */
    hal_err_t (*add_class)(hal_usb_t *u, const hal_usb_class_drv_t *drv,
                           void *ctx);

    hal_err_t (*init)   (hal_usb_t *u, const hal_usb_dev_desc_t *desc);
    hal_err_t (*deinit) (hal_usb_t *u);
    /** 重新枚举（修改描述符后调用） */
    hal_err_t (*reconnect)(hal_usb_t *u);

    /** 底层传输（类驱动内部用，应用一般不直接调） */
    hal_err_t (*ep_write)(hal_usb_t *u, uint8_t ep, const uint8_t *d,
                          uint16_t n);
    hal_err_t (*ep_read) (hal_usb_t *u, uint8_t ep, uint8_t *d, uint16_t n);

    /** 主循环（处理 SETUP / 端点事件） */
    void      (*poll)(hal_usb_t *u);

    /** 是否已配置（枚举完成） */
    uint8_t   (*is_configured)(hal_usb_t *u);
};

/* ===========================================================================
 * 端点资源池
 * ---------------------------------------------------------------------------
 * 类驱动不再自己指定端点号，而是向核心"申请"端点。
 * 核心维护 EP1~EP(MAX-1) 的占用表，按类型/方向自动分配，避免冲突。
 *
 * 典型用法（类驱动的 get_eps 回调里）：
 *   hal_usb_ep_t *e = &eps[n++];
 *   if (hal_usb_ep_alloc(HAL_USB_EP_BULK, HAL_USB_EP_OUT, 64, e) != HAL_OK)
 *       return 0;   // 端点不够
 *
 * 注意：alloc 只是"预约"，真正配置硬件在 SET_CONFIGURATION 时进行。
 * =========================================================================== */

/** 端点池状态查询 */
typedef struct {
    uint8_t total;      /**< 可用端点总数（不含 EP0） */
    uint8_t used;       /**< 已分配数 */
    uint8_t free;       /**< 剩余数 */
} hal_usb_ep_pool_t;

/**
 * 申请一个端点（按需自动选号）。
 * @param type        HAL_USB_EP_BULK / INT / ISO
 * @param dir         HAL_USB_EP_IN / OUT
 * @param max_packet  最大包长（64 或 8 等）
 * @param out         输出：分配到的端点描述（含 ep_num）
 * @return HAL_OK / HAL_ERR_FULL（端点耗尽）
 */
hal_err_t hal_usb_ep_alloc(uint8_t type, uint8_t dir, uint16_t max_packet,
                           hal_usb_ep_t *out);

/**
 * 申请一个"指定端点号"的端点（兼容旧代码）。
 * 如果该端点已被占用返回 HAL_ERR_BUSY。
 */
hal_err_t hal_usb_ep_alloc_at(uint8_t ep_num, uint8_t type, uint8_t dir,
                              uint16_t max_packet, hal_usb_ep_t *out);

/** 释放一个端点（按端点号+方向） */
hal_err_t hal_usb_ep_free(uint8_t ep_num, uint8_t dir);

/** 释放所有端点（deinit 时调用） */
void      hal_usb_ep_free_all(void);

/** 查询端点池状态 */
void      hal_usb_ep_pool_info(hal_usb_ep_pool_t *info);

/** 查询某端点是否已被占用 */
uint8_t   hal_usb_ep_is_used(uint8_t ep_num, uint8_t dir);

hal_usb_t *hal_usb_get(void);

/**
 * 类驱动辅助：为类请求（bmRequestType bit7=1，IN 方向）准备应答数据。
 *
 * 类驱动的 on_setup() 处理 IN 类请求时调用本函数填入应答数据，
 * port 层随后按 wLength 截断并发出。
 *
 * 例（CDC GET_LINE_CODING）：
 *   hal_usb_class_setup_reply(c->line_coding, 7);
 *   return HAL_OK;
 *
 * @param data 应答数据
 * @param len  长度（会按 wLength 截断）
 */
void hal_usb_class_setup_reply(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_USB_H */
