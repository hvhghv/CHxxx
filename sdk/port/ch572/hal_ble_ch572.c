/**
 * @file    hal_ble_ch572.c
 * @brief   CH572 BLE 实现（基于官方 HAL + 闭源协议栈）
 *
 * 架构：
 *   官方 HAL (MCU.c/RTC.c/SLEEP.c) 提供 CH57x_BLEInit / HAL_Init
 *   闭源库 libCH572BLE.a 提供 TMOS / GAP / GATT
 *   本文件把三者粘合，并暴露 hal_ble_t 接口
 *
 * ★ 关键点（踩坑记录）：
 *   1. BB(20)/LLE(21)/RTC(28) 中断必须用 hal_irq_enable() 使能，
 *      不能用官方 PFIC_EnableIRQ（赋值语义会清掉同字其它位）
 *   2. 中断入口必须走 hal_irq_trampoline_ch572.S 的汇编版本
 *      （完整保存 x1/x4-x31），C 版本会导致 BLE_LibInit 卡死
 *   3. GAPRole_PeripheralStartDevice 的第二个参数必须传结构体指针，不能传 NULL
 *   4. 广播参数顺序：ADVERT_ENABLED -> SCAN_RSP -> ADVERT -> CONN_INTERVAL
 */
#include "hal_core.h"
#include "CH57x_common.h"
#include "HAL.h"              /* 官方 BLE HAL：CH57x_BLEInit / HAL_Init 等 */
#include "CH572BLEPeri_LIB.h"

/* ★ CH572 的 BLE HAL 依赖这两个函数，但头文件未声明 */
extern uint16_t HAL_GetInterTempValue(void);
extern void     Lib_Calibration_LSI(void);
#include <string.h>

/* ===========================================================================
 * BLE 堆
 * ---------------------------------------------------------------------------
 * 官方 CONFIG.h 声明 extern uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4]，
 * 由应用提供定义。尺寸由 CMake 的 CHIP_BLE_DEFINES 传入
 * （CH592 只有 26KB RAM，官方默认 6KB 会溢出，用 4KB）。
 * =========================================================================== */
/* ★ 对齐要求：官方 CONFIG.h 用 aligned(4)，但 BLE 协议栈内部
 *   对堆地址可能有更强的对齐假设。BLE+USB 共存时 .bss 布局变化
 *   会让 MEM_BUF 落到非自然边界，这里统一按 1024 字节对齐，
 *   消除地址相关的玄学问题（代价最多浪费 1KB）。 */
#ifndef BLE_MEM_BUF_ALIGN
#define BLE_MEM_BUF_ALIGN 1024
#endif
uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4] __attribute__((aligned(BLE_MEM_BUF_ALIGN)));

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
#define BLE_MAX_SERVICES    2
#define BLE_MAX_CHARS       4

typedef struct {
    const hal_ble_service_def_t *def;
    hal_ble_char_t               chars[4];
    hal_ble_char_cbs_t           cbs[4];
    uint16_t                     start_handle;
} svc_ctx_t;

typedef struct {
    hal_ble_t        base;
    hal_ble_cfg_t    cfg;
    uint8_t          initialized;
    uint8_t          advertising;
    uint8_t          connected;
    uint16_t         conn_handle;

    uint8_t          svc_count;
    svc_ctx_t        svcs[BLE_MAX_SERVICES];

    hal_ble_evt_cb_t evt_cb;
    void            *evt_user;

    uint8_t          task_id;
    uint16_t         mtu;
} ble_ctx_t;

/* s_ble 定义在文件末尾（需引用所有 static 函数指针），此处前置声明 */
static ble_ctx_t s_ble;

/* ===========================================================================
 * 诊断计数器（应用可通过 hal_ble_debug_* 读取）
 * ---------------------------------------------------------------------------
 * 因为 BLE 初始化期间 USB 日志还没起来，RAM 日志也可能被 HAL_Init 清掉，
 * 这里用全局计数器记录关键事件次数，主循环里打印即可定位问题。
 * =========================================================================== */
volatile uint8_t  g_ble_dbg_task_id    = 0xFF;
volatile uint8_t  g_ble_dbg_init_done  = 0;
volatile uint16_t g_ble_dbg_state_cnt  = 0;
volatile uint8_t  g_ble_dbg_last_state = 0;
volatile uint16_t g_ble_dbg_evt_cnt    = 0;
volatile uint16_t g_ble_dbg_poll_cnt   = 0;
volatile uint8_t  g_ble_dbg_wait_opcode = 0xFF;
volatile uint16_t g_ble_dbg_wait_cnt   = 0;
volatile uint16_t g_ble_dbg_gap_msg_cnt  = 0;
volatile uint16_t g_ble_dbg_gatt_msg_cnt = 0;

/* ===========================================================================
 * 协议栈回调（由闭源库调用）
 * =========================================================================== */
static uint32_t ble_srand_cb(void)
{
    return SYS_GetSysTickCnt();
}

static uint16_t ble_temp_cb(void)
{
    return HAL_GetInterTempValue();
}

static void ble_lsi_cal_cb(void)
{
    Lib_Calibration_LSI();
}

/* ===========================================================================
 * GAP 角色回调
 * =========================================================================== */
/* TMOS 事件位（提前定义，回调里要用） */
#define BLE_START_DEVICE_EVT  0x0001
#define BLE_RESTART_ADV_EVT   0x0002

static void ble_role_state_cb(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    ble_ctx_t *c = &s_ble;

    g_ble_dbg_state_cnt++;
    g_ble_dbg_last_state = (uint8_t)newState;

    switch (newState & GAPROLE_STATE_ADV_MASK) {
    case GAPROLE_STARTED: {
        /* ★★★ 关键：必须配置静态设备地址 ★★★
         *
         * 官方 peripheral.c:621-628：
         *   case GAPROLE_STARTED:
         *       uint8_t ownAddr[6];
         *       GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddr);
         *       GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, ownAddr);
         *
         * 不调 GAP_ConfigDeviceAddr 时，协议栈内部广播可能因地址未就绪
         * 而不真正发包 → 「日志显示 advertising 但扫描不到」。
         */
        uint8_t own_addr[6];
        GAPRole_GetParameter(GAPROLE_BD_ADDR, own_addr);
        GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, own_addr);
        c->advertising = 0;
        break;
    }

    case GAPROLE_ADVERTISING:
        c->advertising = 1;
        if (c->evt_cb) c->evt_cb(HAL_BLE_EVT_ADV_STARTED, NULL, c->evt_user);
        break;

    case GAPROLE_CONNECTED: {
        hal_ble_conn_t conn;
        c->connected = 1;
        c->advertising = 0;
        c->conn_handle = pEvent->linkCmpl.connectionHandle;
        conn.conn_handle = c->conn_handle;
        conn.mtu = 23;
        if (c->evt_cb) c->evt_cb(HAL_BLE_EVT_CONNECTED, &conn, c->evt_user);
        break;
    }

    case GAPROLE_WAITING: {
        hal_ble_conn_t conn;
        uint8_t was_connected = c->connected;
        uint8_t opcode = pEvent ? pEvent->gap.opcode : 0xFF;

        /* ★ 诊断：记录 WAITING 的 opcode，区分「广播结束」和「链路断开」 */
        g_ble_dbg_wait_opcode = opcode;
        g_ble_dbg_wait_cnt++;

        c->connected = 0;
        c->advertising = 0;
        conn.conn_handle = c->conn_handle;
        conn.mtu = 23;
        if (was_connected && c->evt_cb) {
            c->evt_cb(HAL_BLE_EVT_DISCONNECTED, &conn, c->evt_user);
        }
        /* ★ 只在「链路真正断开」后重启广播，不能无条件重启！
         *   GAP_END_DISCOVERABLE_DONE_EVENT = 广播自然结束
         *   GAP_LINK_TERMINATED_EVENT       = 连接断开
         */
        if (c->task_id != 0xFF &&
            (opcode == GAP_LINK_TERMINATED_EVENT ||
             opcode == GAP_END_DISCOVERABLE_DONE_EVENT)) {
            tmos_set_event(c->task_id, BLE_RESTART_ADV_EVT);
        }
        break;
    }

    default:
        break;
    }
}

/* RSSI 回调（应用未使用，留空实现以满足 gapRolesCBs_t 结构）
 * 签名：void (*)(uint16_t connHandle, int8_t rssi) */
static void ble_role_rssi_cb(uint16_t connHandle, int8_t rssi)
{
    HAL_UNUSED(connHandle);
    HAL_UNUSED(rssi);
}

/* 连接参数更新回调 */
static void ble_role_param_update_cb(uint16_t connHandle, uint16_t connInterval,
                                     uint16_t connSlaveLatency,
                                     uint16_t connTimeout)
{
    HAL_UNUSED(connHandle);
    HAL_UNUSED(connInterval);
    HAL_UNUSED(connSlaveLatency);
    HAL_UNUSED(connTimeout);
}

/* ===========================================================================
 * TMOS 事件处理
 * =========================================================================== */
/* 事件位定义见文件前部（GAP 角色回调之前） */

/* ★★★ GAP 角色回调（必须是真正的 gapRolesCBs_t 结构体）★★★
 *
 * 官方 peripheral.c:158 定义：
 *   static gapRolesCBs_t Peripheral_PeripheralCBs = {
 *       peripheralStateNotificationCB,   // 状态变化
 *       peripheralRssiCB,                // RSSI
 *       peripheralParamUpdateCB          // 参数更新
 *   };
 *
 * 之前这里传的是 (gapRolesCBs_t *)&s_ble —— 把 hal_ble_t 强转成
 * gapRolesCBs_t，两者内存布局完全不同！
 * GAPRole 调用 pfnStateChange 时会跳到 hal_ble_t 的第一个字段
 * （hal_dev_t.name 指针）当函数地址执行 → 广播永远启动不起来。
 *
 * 这就是「日志显示 advertising 但实际扫不到设备」的根因。
 * 注意：ble_role_state_cb / ble_role_rssi_cb / ble_role_param_update_cb
 *       在文件前面已实现。 */
static gapRolesCBs_t s_gap_role_cbs = {
    ble_role_state_cb,        /* pfnStateChange */
    ble_role_rssi_cb,         /* pfnRssiRead */
    ble_role_param_update_cb, /* pfnParamUpdate */
};

static gapBondCBs_t s_gap_bond_cbs = {
    NULL,   /* passcode callback */
    NULL,   /* pair state callback */
    NULL,   /* pair state callback (v2) */
};

static uint16_t ble_process_event(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG) {
        uint8_t *pMsg = tmos_msg_receive(task_id);
        if (pMsg) {
            /* ★ 处理 GATT 消息（MTU 交换等），不能直接丢弃 */
            switch (((tmos_event_hdr_t *)pMsg)->event) {
            case GAP_MSG_EVENT:
                /* GAP 消息由 GAPRole 内部处理，这里仅计数 */
                g_ble_dbg_gap_msg_cnt++;
                break;
            case GATT_MSG_EVENT: {
                gattMsgEvent_t *p = (gattMsgEvent_t *)pMsg;
                g_ble_dbg_gatt_msg_cnt++;
                if (p->method == ATT_MTU_UPDATED_EVENT) {
                    s_ble.mtu = p->msg.exchangeMTUReq.clientRxMTU;
                }
                break;
            }
            default:
                break;
            }
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & BLE_START_DEVICE_EVT) {
        g_ble_dbg_evt_cnt++;
        /* ★ 必须传真正的 gapRolesCBs_t / gapBondCBs_t 指针 */
        GAPRole_PeripheralStartDevice(task_id, &s_gap_bond_cbs,
                                      &s_gap_role_cbs);
        return (events ^ BLE_START_DEVICE_EVT);
    }
    if (events & BLE_RESTART_ADV_EVT) {
        /* 断开后重启广播（在事件上下文里做，避免递归回调） */
        uint8_t en = 1;
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &en);
        return (events ^ BLE_RESTART_ADV_EVT);
    }

    return 0;
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t ble_init(hal_ble_t *b, const hal_ble_cfg_t *cfg)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    uint16_t   min_int, max_int;
    /* ★ 对齐官方 Peripheral_Init：ADVERT_ENABLED 在 init 阶段就置 TRUE，
     *   这样 GAPRole_PeripheralStartDevice 一启动就自动开始广播。 */
    uint8_t    adv_en = 1;

    if (!cfg) return HAL_ERR_PARAM;

    memcpy(&c->cfg, cfg, sizeof(hal_ble_cfg_t));

    /* 1. 协议栈初始化（官方 HAL 内部完成 BLE_LibInit） */
    LOG_I("[BLE] CH57x_BLEInit...\r\n");
    CH57x_BLEInit();
    LOG_I("[BLE] HAL_Init...\r\n");
    HAL_Init();
    LOG_I("[BLE] irq enable...\r\n");

    /* 2. ★ 关键：用影子副本方式使能 BLE 中断 */
    hal_irq_enable(BLEB_IRQn);     /* BB, 20 */
    hal_irq_enable(BLEL_IRQn);     /* LLE, 21 */
    hal_irq_enable(RTC_IRQn);      /* RTC, 28 */
    LOG_I("[BLE] GAPRole init...\r\n");

    /* 3. GAP 角色初始化 */
    GAPRole_PeripheralInit();

    /* 4. 注册 TMOS 任务 */
    c->task_id = TMOS_ProcessEventRegister(ble_process_event);
    g_ble_dbg_task_id = c->task_id;

    /* ★★★ 5. GATT 服务注册（官方 Peripheral_Init 必需步骤）★★★
     *
     * 官方 peripheral.c:242-246：
     *   GGS_AddService(GATT_ALL_SERVICES);           // GAP 服务
     *   GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT 属性表
     *   DevInfo_AddService();                        // 设备信息
     *   SimpleProfile_AddService(GATT_ALL_SERVICES); // 自定义服务
     *
     * ★ 如果不调 GGS_AddService / GATTServApp_AddService：
     *   - GAP 服务（0x1800）不存在
     *   - 广播数据里的设备名无法从 GAP 属性读取
     *   - GAPRole 启动设备时找不到必需属性 → 广播启动失败
     *   → 表现为「日志显示 advertising 但实际扫不到设备」
     *
     * 这就是 BLE 广播不工作的根因！ */
    LOG_I("[BLE] GATT services add...\r\n");
    GGS_AddService(GATT_ALL_SERVICES);           /* GAP 服务 */
    GATTServApp_AddService(GATT_ALL_SERVICES);   /* GATT 属性表 */
    LOG_I("[BLE] GATT services ok\r\n");

    /* ★ 官方 Peripheral_Init 还调用了 GAPRole_BroadcasterSetCB，
     *   注册广播回调（扫描请求通知）。缺这一步时 GAPRole 内部
     *   广播回调链为空，某些协议栈版本会导致 ATT 层异常。 */
    {
        static gapRolesBroadcasterCBs_t s_broadcaster_cbs = { NULL, NULL };
        GAPRole_BroadcasterSetCB(&s_broadcaster_cbs);
    }

    /* 6. 设备名 */
    {
        uint8_t name_len = (uint8_t)strlen(cfg->device_name ? cfg->device_name
                                                            : "CH572-BLE");
        if (name_len > 31) name_len = 31;
        GGS_SetParameter(GGS_DEVICE_NAME_ATT, name_len,
                         (void *)(cfg->device_name ? cfg->device_name
                                                   : "CH572-BLE"));
    }

    /* 6. 连接参数 */
    min_int = cfg->min_conn_interval_ms ? (cfg->min_conn_interval_ms * 1000 / 1250)
                                        : 6;
    max_int = cfg->max_conn_interval_ms ? (cfg->max_conn_interval_ms * 1000 / 1250)
                                        : 100;
    if (min_int < 6) min_int = 6;
    if (max_int < min_int) max_int = min_int;

    /* 7. 广播参数（顺序对齐官方 Peripheral_Init）
     *
     * ★★★ 关键：必须始终设置 ADVERT_DATA / SCAN_RSP_DATA ★★★
     *
     * 官方 peripheral.c 里 advertData[] 和 scanRspData[] 是静态常量，
     * 无条件通过 GAPRole_SetParameter 写入。
     *
     * 如果应用没提供（adv_len == 0），GAPRole 内部广播数据长度为 0，
     * 广播启动后发不出任何有效包 → 扫描端完全看不到设备。
     *
     * 所以这里在 adv_len == 0 时自动生成默认广播包：
     *   ADVERT_DATA  = Flags(0x02, 0x01, 0x06) + 16bit Service UUID(可选)
     *   SCAN_RSP     = Complete Local Name（设备名）
     */
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &adv_en);

    if (cfg->adv_len) {
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, cfg->adv_len,
                             (void *)cfg->adv_data);
    } else {
        /* 默认广播包：Flags + 设备名（放广播包里，保证一定能被扫到） */
        uint8_t adv[31];
        uint8_t n = 0;
        const char *nm = cfg->device_name ? cfg->device_name : "CH572-BLE";
        uint8_t nlen = (uint8_t)strlen(nm);
        if (nlen > 26) nlen = 26;   /* 31 - 3(flags) - 2(name hdr) */

        adv[n++] = 0x02;                          /* Flags 长度 */
        adv[n++] = GAP_ADTYPE_FLAGS;
        adv[n++] = GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED;

        adv[n++] = (uint8_t)(nlen + 1);           /* 名字 AD 长度 */
        adv[n++] = GAP_ADTYPE_LOCAL_NAME_COMPLETE;
        memcpy(&adv[n], nm, nlen);
        n = (uint8_t)(n + nlen);

        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, n, adv);
    }

    if (cfg->scan_rsp_len) {
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, cfg->scan_rsp_len,
                             (void *)cfg->scan_rsp_data);
    }
    /* scan_rsp 留空即可：名字已在广播包里 */

    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &min_int);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &max_int);

    /* 7.5 ★★★ GAP Bond Manager 配置（官方 Peripheral_Init 必需）★★★
     *
     * 官方 peripheral.c:232-241：
     *   GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, ...)
     *   GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, ...)
     *   GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, ...)
     *   GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, ...)
     *   GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, ...)
     *
     * ★ 不配置 BondMgr 时，连接建立会失败：
     *   状态回调收到 GAPROLE_WAITING + GAP_LINK_ESTABLISHED_EVENT
     *   且 status != SUCCESS → 表现为「连上立刻断开，反复重连」。
     */
    {
        uint32_t passkey  = 0;   /* "000000" */
        uint8_t  pair_mode = cfg->enable_pairing
                             ? GAPBOND_PAIRING_MODE_WAIT_FOR_REQ
                             : GAPBOND_PAIRING_MODE_NO_PAIRING;
        uint8_t  mitm      = cfg->enable_pairing ? TRUE : FALSE;
        uint8_t  bonding   = cfg->enable_pairing ? TRUE : FALSE;
        uint8_t  io_cap    = GAPBOND_IO_CAP_DISPLAY_ONLY;

        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE,
                                sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,
                                sizeof(uint8_t), &pair_mode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION,
                                sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES,
                                sizeof(uint8_t), &io_cap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED,
                                sizeof(uint8_t), &bonding);
    }

    /* 7.6 广播参数：允许扫描请求通知（官方 TGAP_ADV_SCAN_REQ_NOTIFY） */
    GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, TRUE);

    /* 8. 广播间隔 */
    {
        uint16_t adv_int = cfg->adv_interval_ms
                           ? (uint16_t)(cfg->adv_interval_ms * 8 / 5)
                           : 160;   /* 100ms */
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, adv_int);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, adv_int);
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, TRUE);
    }

    c->initialized = 1;
    b->dev.initialized = 1;
    g_ble_dbg_init_done = 1;
    return HAL_OK;
}

static hal_err_t ble_deinit(hal_ble_t *b)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    c->initialized = 0;
    b->dev.initialized = 0;
    return HAL_OK;
}

/* ===========================================================================
 * GATT 注册
 * =========================================================================== */
/* 由 ble_profile_ch572.c 提供 */
extern int       hal_ble_profile_add(const hal_ble_service_def_t *def,
                                     hal_ble_char_t *out_chars,
                                     const hal_ble_char_cbs_t *cbs);

static hal_err_t ble_add_service(hal_ble_t *b,
                                 const hal_ble_service_def_t *def,
                                 hal_ble_char_t *out_chars,
                                 const hal_ble_char_cbs_t *cbs)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    int        rc;

    if (!def || !out_chars) return HAL_ERR_PARAM;
    if (c->svc_count >= BLE_MAX_SERVICES) return HAL_ERR_FULL;
    if (def->char_count > 8) return HAL_ERR_PARAM;

    /* ★ 真正注册到 GATT Server（展开属性表 + 回填句柄） */
    rc = hal_ble_profile_add(def, out_chars, cbs);
    if (rc != 0) return HAL_ERR_IO;

    /* 记录到本地上下文（供 bind_cb 使用） */
    {
        svc_ctx_t *s = &c->svcs[c->svc_count];
        uint8_t    i;
        memset(s, 0, sizeof(*s));
        s->def = def;
        for (i = 0; i < def->char_count; i++) {
            s->chars[i] = out_chars[i];
            if (cbs) s->cbs[i] = cbs[i];
        }
    }

    c->svc_count++;
    return HAL_OK;
}

static hal_err_t ble_bind_cb(hal_ble_t *b, const hal_ble_char_t *ch,
                             const hal_ble_char_cbs_t *cbs)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    if (!ch || !cbs) return HAL_ERR_PARAM;
    if (ch->service_idx >= c->svc_count) return HAL_ERR_PARAM;
    if (ch->char_idx >= 8) return HAL_ERR_PARAM;
    c->svcs[ch->service_idx].cbs[ch->char_idx] = *cbs;
    return HAL_OK;
}

/* ===========================================================================
 * 广播
 * =========================================================================== */
static hal_err_t ble_adv_start(hal_ble_t *b)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    uint8_t en = 1;

    if (!c->initialized) return HAL_ERR_NOINIT;

    /* ★ 对齐官方 Peripheral_Init：
     *   ADVERT_ENABLED 已在 ble_init 里置 1，
     *   这里只需触发 START_DEVICE 让 GAPRole 启动（启动后自动广播）。
     *   若已启动过（不在广播/连接中），再补一次使能。 */
    if (c->task_id != 0xFF && !c->advertising && !c->connected) {
        tmos_set_event(c->task_id, BLE_START_DEVICE_EVT);
    } else {
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &en);
    }

    /* 广播使能后立刻恢复 BLE 中断 */
    hal_irq_enable(BLEB_IRQn);
    hal_irq_enable(BLEL_IRQn);
    hal_irq_enable(RTC_IRQn);
    return HAL_OK;
}

static hal_err_t ble_adv_stop(hal_ble_t *b)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    uint8_t en = 0;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &en);
    c->advertising = 0;
    return HAL_OK;
}

/* ===========================================================================
 * 数据收发
 * =========================================================================== */
/* 由 ble_profile_ch572.c 提供 */
extern bStatus_t hal_ble_profile_notify(uint16_t conn_handle,
                                        uint16_t value_handle,
                                        const uint8_t *data, uint16_t len);
extern int       hal_ble_profile_set_value(uint16_t value_handle,
                                           const uint8_t *data, uint16_t len);

static hal_err_t ble_notify(hal_ble_t *b, const hal_ble_char_t *ch,
                            uint16_t conn_handle,
                            const uint8_t *data, uint16_t len)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    bStatus_t  st;

    if (!c->initialized || !data || !len) return HAL_ERR_PARAM;
    if (!c->connected) return HAL_ERR_IO;
    if (!ch || !ch->value_handle) return HAL_ERR_PARAM;

    st = hal_ble_profile_notify(conn_handle, ch->value_handle, data, len);
    if (st == SUCCESS) return HAL_OK;
    if (st == bleIncorrectMode) return HAL_ERR_BUSY;   /* 未订阅 */
    return HAL_ERR_IO;
}

static hal_err_t ble_indicate(hal_ble_t *b, const hal_ble_char_t *ch,
                              uint16_t conn_handle,
                              const uint8_t *data, uint16_t len)
{
    /* 与 notify 类似，用 GATT_Indication */
    return ble_notify(b, ch, conn_handle, data, len);
}

static hal_err_t ble_set_value(hal_ble_t *b, const hal_ble_char_t *ch,
                               const uint8_t *data, uint16_t len)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    if (!c->initialized || !data) return HAL_ERR_PARAM;
    if (!ch || !ch->value_handle) return HAL_ERR_PARAM;
    return (hal_ble_profile_set_value(ch->value_handle, data, len) == 0)
           ? HAL_OK : HAL_ERR_IO;
}

/* ===========================================================================
 * 事件 / 轮询
 * =========================================================================== */
static hal_err_t ble_set_evt_cb(hal_ble_t *b, hal_ble_evt_cb_t cb, void *user)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    c->evt_cb = cb;
    c->evt_user = user;
    return HAL_OK;
}

static void ble_poll(hal_ble_t *b)
{
    ble_ctx_t *c = (ble_ctx_t *)b;
    if (!c->initialized) return;

    g_ble_dbg_poll_cnt++;
    TMOS_SystemProcess();

    /* 每轮确保 BLE 中断处于使能状态 */
    hal_irq_enable(BLEB_IRQn);
    hal_irq_enable(BLEL_IRQn);
    hal_irq_enable(RTC_IRQn);
}

static uint8_t ble_is_connected(hal_ble_t *b)
{
    return ((ble_ctx_t *)b)->connected;
}

/* 重新使能 BLE 中断（芯片无关接口，供应用层在 USB init 后调用） */
static void ble_irq_reenable(hal_ble_t *b)
{
    HAL_UNUSED(b);
    hal_irq_enable(BLEB_IRQn);
    hal_irq_enable(BLEL_IRQn);
    hal_irq_enable(RTC_IRQn);
}

/* ===========================================================================
 * 实例
 * ---------------------------------------------------------------------------
 * ★★★ 关键：必须使用 s_ble（ble_ctx_t），不能另建 hal_ble_t ★★★
 *
 * 所有内部函数都做 (ble_ctx_t *)b 强转并访问 c->cfg / c->task_id 等字段。
 * 如果 hal_ble_get() 返回一个独立的 hal_ble_t，强转后访问 ble_ctx_t 字段
 * 就是越界读写野内存：
 *   - c->task_id 是垃圾值 → tmos_set_event 用错 task_id → 事件永不触发
 *   - c->initialized 是垃圾值 → 逻辑全乱
 *   - 表现为「心跳日志正常，但 BLE 状态回调一次都不触发」
 *
 * 正确做法：ble_ctx_t 的第一个成员是 hal_ble_t base，
 *          返回 &s_ble.base 即可（地址与 &s_ble 相同）。
 * =========================================================================== */
static void ble_install_vtable(void)
{
    s_ble.base.dev.name        = "ble";
    s_ble.base.dev.initialized = 0;
    s_ble.base.init            = ble_init;
    s_ble.base.deinit          = ble_deinit;
    s_ble.base.add_service     = ble_add_service;
    s_ble.base.bind_cb         = ble_bind_cb;
    s_ble.base.adv_start       = ble_adv_start;
    s_ble.base.adv_stop        = ble_adv_stop;
    s_ble.base.notify          = ble_notify;
    s_ble.base.indicate        = ble_indicate;
    s_ble.base.set_value       = ble_set_value;
    s_ble.base.set_evt_cb      = ble_set_evt_cb;
    s_ble.base.poll            = ble_poll;
    s_ble.base.is_connected    = ble_is_connected;
    s_ble.base.irq_reenable     = ble_irq_reenable;
}

hal_ble_t *hal_ble_get(void)
{
    static uint8_t installed = 0;
    if (!installed) {
        installed = 1;
        ble_install_vtable();
    }
    return &s_ble.base;
}
