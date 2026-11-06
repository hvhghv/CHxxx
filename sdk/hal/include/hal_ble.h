/**
 * @file    hal_ble.h
 * @brief   BLE 通用接口（多服务 / 多特征值 / 预留经典蓝牙）
 *
 * 设计要点：
 *   1. 声明式 GATT 定义：应用提供静态表，协议栈按表注册
 *   2. 运行时句柄：注册后返回 hal_ble_char_t，用于 notify / 读写
 *   3. 事件驱动：连接/断开/MTU 等通过回调通知
 *   4. poll() 必须在应用主循环中周期调用
 *
 * 典型用法：
 *   hal_ble_t *b = hal_ble_get();
 *   hal_ble_cfg_t cfg = { .device_name = "CH592-BLE", .mtu = 247 };
 *   b->init(b, &cfg);
 *
 *   hal_ble_char_t rx, tx;
 *   hal_ble_char_t tmp[2];
 *   b->add_service(b, &services[0], tmp, NULL);
 *   rx = tmp[0]; tx = tmp[1];
 *
 *   b->bind_cb(b, &rx, &cbs);
 *   b->adv_start(b);
 *   while (1) { b->poll(b); }
 */
#ifndef HAL_BLE_H
#define HAL_BLE_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 特征值属性与权限
 * =========================================================================== */
#define HAL_BLE_PROP_READ        0x02
#define HAL_BLE_PROP_WRITE_NR    0x04   /**< Write Without Response */
#define HAL_BLE_PROP_WRITE       0x08
#define HAL_BLE_PROP_NOTIFY      0x10
#define HAL_BLE_PROP_INDICATE    0x20

#define HAL_BLE_PERM_READ        0x01
#define HAL_BLE_PERM_WRITE       0x02

/* ===========================================================================
 * UUID
 * =========================================================================== */
typedef struct {
    uint8_t len;            /**< 2（16 位）或 16（128 位） */
    uint8_t bytes[16];      /**< 小端存放 */
} hal_ble_uuid_t;

/* 便捷宏：16 位 UUID */
#define HAL_BLE_UUID16(v)  { 2, { (uint8_t)((v) & 0xFF), (uint8_t)((v) >> 8) } }

/* ===========================================================================
 * GATT 声明
 * =========================================================================== */
typedef struct {
    const char     *name;        /**< 调试用，如 "TX" */
    hal_ble_uuid_t  uuid;
    uint8_t         props;       /**< HAL_BLE_PROP_* */
    uint8_t         perms;       /**< HAL_BLE_PERM_* */
    uint16_t        max_len;     /**< 特征值最大长度 */
    const uint8_t  *init_value;  /**< 初始值，可为 NULL */
    uint16_t        init_len;
} hal_ble_char_def_t;

typedef struct {
    const char               *name;      /**< 调试用，如 "Echo" */
    hal_ble_uuid_t            uuid;
    uint8_t                   char_count;
    const hal_ble_char_def_t *chars;
} hal_ble_service_def_t;

/* ===========================================================================
 * 运行时句柄
 * =========================================================================== */
typedef struct {
    uint16_t service_idx;   /**< 应用侧服务序号 */
    uint16_t char_idx;      /**< 服务内特征值序号 */
    uint16_t value_handle;  /**< 协议栈分配的属性句柄 */
    uint16_t cccd_handle;   /**< CCCD 句柄（通知用），0 = 无 */
} hal_ble_char_t;

/* ===========================================================================
 * 回调
 * =========================================================================== */
typedef struct {
    uint16_t conn_handle;
    uint8_t  addr[6];       /**< 对端地址（小端） */
    uint8_t  addr_type;
    uint16_t mtu;
} hal_ble_conn_t;

typedef enum {
    HAL_BLE_EVT_ADV_STARTED,
    HAL_BLE_EVT_ADV_STOPPED,
    HAL_BLE_EVT_CONNECTED,        /**< param = hal_ble_conn_t* */
    HAL_BLE_EVT_DISCONNECTED,     /**< param = hal_ble_conn_t* */
    HAL_BLE_EVT_MTU_CHANGED,      /**< param = hal_ble_conn_t* */
    HAL_BLE_EVT_PASSKEY,          /**< param = uint32_t* */
    HAL_BLE_EVT_PAIRING_DONE,
} hal_ble_evt_t;

typedef void (*hal_ble_evt_cb_t)(hal_ble_evt_t evt, const void *param,
                                 void *user);

/* 特征值写事件 */
typedef void (*hal_ble_write_cb_t)(const hal_ble_char_t *ch,
                                   const uint8_t *data, uint16_t len,
                                   uint16_t conn_handle, void *user);
/* 特征值读事件（可选，用于动态值） */
typedef uint16_t (*hal_ble_read_cb_t)(const hal_ble_char_t *ch,
                                      uint8_t *buf, uint16_t max_len,
                                      uint16_t conn_handle, void *user);
/* CCCD 变更（手机订阅/取消订阅） */
typedef void (*hal_ble_cccd_cb_t)(const hal_ble_char_t *ch, uint8_t enabled,
                                  uint16_t conn_handle, void *user);

typedef struct {
    hal_ble_write_cb_t write_cb;
    hal_ble_read_cb_t  read_cb;
    hal_ble_cccd_cb_t  cccd_cb;
    void              *user;
} hal_ble_char_cbs_t;

/* ===========================================================================
 * 配置
 * =========================================================================== */
typedef struct {
    const char *device_name;
    uint8_t     adv_data[31];       /**< 自定义广播包，adv_len=0 时用默认 */
    uint8_t     adv_len;
    uint8_t     scan_rsp_data[31];
    uint8_t     scan_rsp_len;
    uint16_t    adv_interval_ms;    /**< 默认 100ms */
    uint16_t    min_conn_interval_ms;
    uint16_t    max_conn_interval_ms;
    uint16_t    mtu;                /**< 期望 MTU，默认 23，可设 247 */
    uint8_t     max_connections;    /**< 1..4 */
    uint8_t     enable_pairing;     /**< 1 = 启用绑定 */
} hal_ble_cfg_t;

/* ===========================================================================
 * 主接口
 * =========================================================================== */
typedef struct hal_ble hal_ble_t;

struct hal_ble {
    hal_dev_t dev;

    /** 初始化协议栈（内部完成 BLEInit / HAL_Init / GAPRole 初始化） */
    hal_err_t (*init)  (hal_ble_t *b, const hal_ble_cfg_t *cfg);
    hal_err_t (*deinit)(hal_ble_t *b);

    /* ---- GATT 动态注册（必须在 init 之后、adv_start 之前调用） ---- */
    /**
     * 注册一个服务。
     * @param def        服务定义（静态表）
     * @param out_chars  输出：该服务内所有特征值的运行时句柄，
     *                   长度必须 >= def->char_count
     * @param cbs        回调（可为 NULL，后续用 bind_cb 绑定）
     */
    hal_err_t (*add_service)(hal_ble_t *b,
                             const hal_ble_service_def_t *def,
                             hal_ble_char_t *out_chars,
                             const hal_ble_char_cbs_t *cbs);

    /** 为已注册特征值绑定/更新回调 */
    hal_err_t (*bind_cb)(hal_ble_t *b, const hal_ble_char_t *ch,
                         const hal_ble_char_cbs_t *cbs);

    /* ---- 广播 ---- */
    hal_err_t (*adv_start)(hal_ble_t *b);
    hal_err_t (*adv_stop) (hal_ble_t *b);

    /* ---- 数据收发 ---- */
    /** 发送通知（需对端已订阅 CCCD） */
    hal_err_t (*notify)(hal_ble_t *b, const hal_ble_char_t *ch,
                        uint16_t conn_handle,
                        const uint8_t *data, uint16_t len);
    /** 发送指示（需对端确认） */
    hal_err_t (*indicate)(hal_ble_t *b, const hal_ble_char_t *ch,
                          uint16_t conn_handle,
                          const uint8_t *data, uint16_t len);
    /** 主动更新特征值（读请求会返回新值） */
    hal_err_t (*set_value)(hal_ble_t *b, const hal_ble_char_t *ch,
                           const uint8_t *data, uint16_t len);

    /* ---- 事件 ---- */
    hal_err_t (*set_evt_cb)(hal_ble_t *b, hal_ble_evt_cb_t cb, void *user);

    /* ---- 协议栈主循环（应用 while(1) 中周期调用） ---- */
    void      (*poll)(hal_ble_t *b);

    /* ---- 查询 ---- */
    uint8_t   (*is_connected)(hal_ble_t *b);

    /* ---- 重新使能 BLE 中断（芯片无关） ----
     * USB 初始化等操作可能影响 PFIC 中断使能位，
     * 应用层在 USB init 后调用此函数即可，无需知道芯片中断号。 */
    void      (*irq_reenable)(hal_ble_t *b);
};

hal_ble_t *hal_ble_get(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BLE_H */
