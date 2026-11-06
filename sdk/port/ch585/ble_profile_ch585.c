/**
 * @file    ble_profile_ch585.c
 * @brief   CH585 动态 GATT 服务注册（把 hal_ble_service_def_t 转成协议栈属性表）
 *
 * 设计：
 *   应用用声明式 hal_ble_service_def_t 描述服务，本文件在运行时把它展开成
 *   官方协议栈要求的 gattAttribute_t 数组，并注册到 GATTServApp。
 *
 * 每个特征值展开为 3~4 个属性：
 *   1. Characteristic Declaration (0x2803) —— 属性 + 值句柄
 *   2. Characteristic Value       (自定义 UUID)
 *   3. Client Char Config (0x2902) —— 仅当 props 含 NOTIFY/INDICATE
 *   4. User Description   (0x2901) —— 可选，这里统一生成
 *
 * 回调桥接：
 *   协议栈 simpleProfile_ReadAttrCB / WriteAttrCB 风格
 *   → 查表找到对应 hal_ble_char_t
 *   → 调应用注册的 hal_ble_char_cbs_t.read_cb / write_cb / cccd_cb
 *
 * ★ 参考官方 tmp/CH592EVT/EVT/EXAM/BLE/BLE_USB/Profile/gattprofile.c
 */
#include "hal_core.h"
#include "CH58x_common.h"
#include "CH58xBLE_LIB.h"
#include <string.h>

/* ===========================================================================
 * 配置
 * =========================================================================== */
#define BLE_PROFILE_MAX_SERVICES     1
#define BLE_PROFILE_MAX_ATTRS        10    /* 所有服务属性总数上限（省 RAM） */
#define BLE_PROFILE_MAX_CHARS        2
#define BLE_PROFILE_MAX_CONN         1

/* 每个特征值最多占 4 个属性 */
#define ATTR_PER_CHAR                4
/* ===========================================================================
 * 内部类型
 * =========================================================================== */
typedef struct {
    /* 服务定义（应用提供，静态生命周期） */
    const hal_ble_service_def_t *def;

    /* ★ 服务声明属性值：必须是 gattAttrType_t 结构体（协议栈按此解析） */
    gattAttrType_t   svc_decl;

    /* 运行时属性表（本文件管理） */
    gattAttribute_t *attrs;
    uint16_t         attr_count;

    /* 每个特征值在 attrs[] 中的起始下标（char 声明） */
    uint16_t         char_attr_idx[BLE_PROFILE_MAX_CHARS];

    /* 每个特征值的值缓冲（应用可读写） */
    uint8_t         *value_buf[BLE_PROFILE_MAX_CHARS];
    uint16_t         value_max[BLE_PROFILE_MAX_CHARS];
    uint16_t         value_len[BLE_PROFILE_MAX_CHARS];

    /* CCCD 表（每连接一份） */
    gattCharCfg_t   *cccd[BLE_PROFILE_MAX_CHARS];

    /* 应用回调 */
    hal_ble_char_cbs_t cbs[BLE_PROFILE_MAX_CHARS];

    /* 运行时句柄（回填给应用） */
    hal_ble_char_t   *out_chars;
} profile_svc_t;

/* ===========================================================================
 * 全局状态
 * =========================================================================== */
/* 诊断（应用可读） */
volatile uint8_t  g_ble_dbg_gatt_reg_rc   = 0xFF;
volatile uint16_t g_ble_dbg_gatt_attr_cnt = 0;
volatile uint16_t g_ble_dbg_gatt_first_handle = 0;
volatile uint16_t g_ble_dbg_gatt_val_handle   = 0;
volatile uint16_t g_ble_dbg_read_cb_cnt  = 0;
volatile uint16_t g_ble_dbg_write_cb_cnt = 0;

static profile_svc_t s_svc[BLE_PROFILE_MAX_SERVICES];
static uint8_t       s_svc_count = 0;

/* 属性池（所有服务共享） */
static gattAttribute_t s_attr_pool[BLE_PROFILE_MAX_ATTRS];
static uint16_t        s_attr_used = 0;

/* 值缓冲池（每特征值最多 32 字节，省 RAM） */
#define VALUE_BUF_SIZE 32
static uint8_t  s_value_pool[BLE_PROFILE_MAX_CHARS][VALUE_BUF_SIZE];
static uint8_t  s_value_used = 0;

/* CCCD 池 */
static gattCharCfg_t s_cccd_pool[BLE_PROFILE_MAX_CHARS][BLE_PROFILE_MAX_CONN];
static uint8_t       s_cccd_used = 0;

/* 用户描述字符串池（"Char N\0" 之类） */
static char s_desc_pool[BLE_PROFILE_MAX_CHARS][16];
static uint8_t s_desc_used = 0;

/* 特征值属性字节池（0x2803 声明指向它，必须持久化） */
static uint8_t s_props_pool[BLE_PROFILE_MAX_CHARS];
static uint8_t s_props_used = 0;

/* ===========================================================================
 * 协议栈标准 UUID（★ 必须自己定义字节数组！）
 * ---------------------------------------------------------------------------
 * 坑：CH58xBLE_LIB.h 里的 s_uuid_primary_service / s_uuid_character 等
 *     在 ROM 版本里是 BLE_LIB_JT() 跳转表宏（函数指针），
 *     不是 UUID 数据数组。直接拿来当 gattAttribute_t.type.uuid 用，
 *     协议栈会读到函数机器码 → 服务发现全部失败。
 *
 * 所以我们自己定义标准 UUID 的小端字节数组。
 * =========================================================================== */
static const uint8_t s_uuid_primary_service[ATT_BT_UUID_SIZE] = { 0x00, 0x28 }; /* 0x2800 */
static const uint8_t s_uuid_character[ATT_BT_UUID_SIZE]       = { 0x03, 0x28 }; /* 0x2803 */
static const uint8_t s_uuid_char_user_desc[ATT_BT_UUID_SIZE]  = { 0x01, 0x29 }; /* 0x2901 */
static const uint8_t s_uuid_client_char_cfg[ATT_BT_UUID_SIZE] = { 0x02, 0x29 }; /* 0x2902 */

/* ===========================================================================
 * 工具
 * =========================================================================== */
static profile_svc_t *svc_by_attr(gattAttribute_t *pAttr)
{
    uint8_t i;
    for (i = 0; i < s_svc_count; i++) {
        profile_svc_t *s = &s_svc[i];
        if (!s->attrs) continue;
        if (pAttr >= s->attrs && pAttr < (s->attrs + s->attr_count)) {
            return s;
        }
    }
    return NULL;
}

/* 由属性指针反查特征值序号 */
static int char_idx_by_attr(profile_svc_t *s, gattAttribute_t *pAttr)
{
    uint8_t i;
    uint16_t idx = (uint16_t)(pAttr - s->attrs);
    for (i = 0; i < s->def->char_count && i < 8; i++) {
        uint16_t base = s->char_attr_idx[i];
        /* 特征值占 [base, base+3]（声明 / 值 / CCCD / 描述） */
        if (idx >= base && idx <= (uint16_t)(base + 3)) {
            return (int)i;
        }
    }
    return -1;
}

/* HAL props → 协议栈 GATT_PROP_* */
static uint8_t hal_props_to_gatt(uint8_t props)
{
    uint8_t p = 0;
    if (props & HAL_BLE_PROP_READ)      p |= GATT_PROP_READ;
    if (props & HAL_BLE_PROP_WRITE)     p |= GATT_PROP_WRITE;
    if (props & HAL_BLE_PROP_WRITE_NR)  p |= GATT_PROP_WRITE_NO_RSP;
    if (props & HAL_BLE_PROP_NOTIFY)    p |= GATT_PROP_NOTIFY;
    if (props & HAL_BLE_PROP_INDICATE)  p |= GATT_PROP_INDICATE;
    return p;
}

/* HAL perms → 协议栈 GATT_PERMIT_* */
static uint8_t hal_perms_to_gatt(uint8_t perms)
{
    uint8_t p = 0;
    if (perms & HAL_BLE_PERM_READ)  p |= GATT_PERMIT_READ;
    if (perms & HAL_BLE_PERM_WRITE) p |= GATT_PERMIT_WRITE;
    return p;
}

/* ===========================================================================
 * 协议栈回调（Read / Write）
 * =========================================================================== */
static bStatus_t profile_read_cb(uint16_t conn_handle, gattAttribute_t *pAttr,
                                 uint8_t *pValue, uint16_t *pLen,
                                 uint16_t offset, uint16_t max_len,
                                 uint8_t method);

static bStatus_t profile_write_cb(uint16_t conn_handle, gattAttribute_t *pAttr,
                                  uint8_t *pValue, uint16_t len,
                                  uint16_t offset, uint8_t method);

static gattServiceCBs_t s_svc_cbs = {
    profile_read_cb,
    profile_write_cb,
    NULL,
};

static bStatus_t profile_read_cb(uint16_t conn_handle, gattAttribute_t *pAttr,
                                 uint8_t *pValue, uint16_t *pLen,
                                 uint16_t offset, uint16_t max_len,
                                 uint8_t method)
{
    profile_svc_t *s;
    int            ci;
    hal_ble_char_t ch;

    (void)method;

    g_ble_dbg_read_cb_cnt++;

    if (offset > 0) return ATT_ERR_ATTR_NOT_LONG;

    s = svc_by_attr(pAttr);
    if (!s) { *pLen = 0; return ATT_ERR_ATTR_NOT_FOUND; }

    ci = char_idx_by_attr(s, pAttr);
    if (ci < 0) { *pLen = 0; return ATT_ERR_ATTR_NOT_FOUND; }

    /* 只有"值"属性需要应用参与；CCCD / 声明 / 描述由协议栈处理 */
    if (pAttr->type.len == ATT_BT_UUID_SIZE) {
        uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
        if (uuid == GATT_CLIENT_CHAR_CFG_UUID ||
            uuid == GATT_CHAR_USER_DESC_UUID ||
            uuid == GATT_CHAR_EXT_PROPS_UUID) {
            /* 交给协议栈默认处理 */
            *pLen = 0;
            return ATT_ERR_ATTR_NOT_FOUND;
        }
    }

    ch.service_idx   = 0;   /* 由调用方填，这里只用 char_idx */
    ch.char_idx      = (uint16_t)ci;
    ch.value_handle  = s->attrs[s->char_attr_idx[ci] + 1].handle;
    ch.cccd_handle   = 0;

    /* 应用是否提供动态读回调 */
    if (s->cbs[ci].read_cb) {
        uint16_t n = s->cbs[ci].read_cb(&ch, pValue, max_len, conn_handle,
                                        s->cbs[ci].user);
        *pLen = n;
        return SUCCESS;
    }

    /* 否则返回内部缓冲 */
    {
        uint16_t n = s->value_len[ci];
        if (n > max_len) n = max_len;
        if (n) memcpy(pValue, s->value_buf[ci], n);
        *pLen = n;
    }
    return SUCCESS;
}

static bStatus_t profile_write_cb(uint16_t conn_handle, gattAttribute_t *pAttr,
                                  uint8_t *pValue, uint16_t len,
                                  uint16_t offset, uint8_t method)
{
    profile_svc_t *s;
    int            ci;
    hal_ble_char_t ch;

    (void)method;

    g_ble_dbg_write_cb_cnt++;

    if (gattPermitAuthorWrite(pAttr->permissions)) {
        return ATT_ERR_INSUFFICIENT_AUTHOR;
    }

    s = svc_by_attr(pAttr);
    if (!s) return ATT_ERR_ATTR_NOT_FOUND;

    /* CCCD 写入：交给协议栈处理（会自动回调 cccd_cb） */
    if (pAttr->type.len == ATT_BT_UUID_SIZE) {
        uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
        if (uuid == GATT_CLIENT_CHAR_CFG_UUID) {
            bStatus_t st = GATTServApp_ProcessCCCWriteReq(
                conn_handle, pAttr, pValue, len, offset, GATT_CLIENT_CFG_NOTIFY);
            /* 找到对应特征值，通知应用 */
            ci = char_idx_by_attr(s, pAttr);
            if (ci >= 0 && s->cbs[ci].cccd_cb) {
                uint16_t cfg = GATTServApp_ReadCharCfg(conn_handle, s->cccd[ci]);
                ch.service_idx  = 0;
                ch.char_idx     = (uint16_t)ci;
                ch.value_handle = s->attrs[s->char_attr_idx[ci] + 1].handle;
                ch.cccd_handle  = s->attrs[s->char_attr_idx[ci] + 2].handle;
                s->cbs[ci].cccd_cb(&ch,
                                   (cfg & GATT_CLIENT_CFG_NOTIFY) ? 1 : 0,
                                   conn_handle, s->cbs[ci].user);
            }
            return st;
        }
    }

    ci = char_idx_by_attr(s, pAttr);
    if (ci < 0) return ATT_ERR_ATTR_NOT_FOUND;

    if (offset != 0) return ATT_ERR_ATTR_NOT_LONG;
    if (len > s->value_max[ci]) return ATT_ERR_INVALID_VALUE_SIZE;

    /* 存到内部缓冲 */
    memcpy(s->value_buf[ci], pValue, len);
    s->value_len[ci] = len;

    ch.service_idx  = 0;
    ch.char_idx     = (uint16_t)ci;
    ch.value_handle = s->attrs[s->char_attr_idx[ci] + 1].handle;
    ch.cccd_handle  = 0;

    /* 通知应用 */
    if (s->cbs[ci].write_cb) {
        s->cbs[ci].write_cb(&ch, pValue, len, conn_handle, s->cbs[ci].user);
    }
    return SUCCESS;
}

/* ===========================================================================
 * 连接状态回调（断线重置 CCCD）
 * =========================================================================== */
static void profile_conn_status_cb(uint16_t conn_handle, uint8_t change_type)
{
    uint8_t i, j;

    if (conn_handle == LOOPBACK_CONNHANDLE) return;

    if ((change_type == LINKDB_STATUS_UPDATE_REMOVED) ||
        ((change_type == LINKDB_STATUS_UPDATE_STATEFLAGS) &&
         (!linkDB_Up(conn_handle)))) {
        for (i = 0; i < s_svc_count; i++) {
            profile_svc_t *s = &s_svc[i];
            for (j = 0; j < s->def->char_count && j < 8; j++) {
                if (s->cccd[j]) {
                    GATTServApp_InitCharCfg(conn_handle, s->cccd[j]);
                }
            }
        }
    }
}

static uint8_t s_linkdb_registered = 0;

/* ===========================================================================
 * 对外接口
 * =========================================================================== */

/**
 * 注册一个服务（把 hal_ble_service_def_t 展开成协议栈属性表）
 * @return 0 成功，<0 失败
 */
int hal_ble_profile_add(const hal_ble_service_def_t *def,
                        hal_ble_char_t *out_chars,
                        const hal_ble_char_cbs_t *cbs)
{
    profile_svc_t *s;
    uint16_t       need_attrs;
    uint8_t        i;

    if (!def || !out_chars) return -1;
    if (def->char_count == 0 || def->char_count > 8) return -1;
    if (s_svc_count >= BLE_PROFILE_MAX_SERVICES) return -2;

    /* 计算属性数：1(服务) + 每特征值 4（声明/值/CCCD/描述） */
    need_attrs = (uint16_t)(1 + def->char_count * ATTR_PER_CHAR);
    if ((s_attr_used + need_attrs) > BLE_PROFILE_MAX_ATTRS) return -3;
    if ((s_value_used + def->char_count) > BLE_PROFILE_MAX_CHARS) return -4;
    if ((s_cccd_used + def->char_count) > BLE_PROFILE_MAX_CHARS) return -5;
    if ((s_desc_used + def->char_count) > BLE_PROFILE_MAX_CHARS) return -6;

    s = &s_svc[s_svc_count];
    memset(s, 0, sizeof(*s));
    s->def        = def;
    s->attrs      = &s_attr_pool[s_attr_used];
    s->attr_count = need_attrs;
    s->out_chars  = out_chars;

    {
        uint16_t ai = 0;

        /* --- 服务声明 --- */
        s->attrs[ai].type.len     = ATT_BT_UUID_SIZE;
        s->attrs[ai].type.uuid    = s_uuid_primary_service;   /* ★ uuid 是指针 */
        s->attrs[ai].permissions  = GATT_PERMIT_READ;
        s->attrs[ai].handle       = 0;
        /* ★★★ pValue 必须指向 gattAttrType_t 结构体，不是纯 UUID 字节！★★★
         *
         * 官方 gattprofile.c:
         *   static const gattAttrType_t simpleProfileService =
         *       {ATT_BT_UUID_SIZE, simpleProfileServUUID};
         *   { {ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0,
         *     (uint8_t *)&simpleProfileService }
         *
         * 协议栈读服务声明时会把这个 pValue 当作 gattAttrType_t 解析
         * （前 1 字节 len + 后面 uuid 指针），传纯字节数组会解析出垃圾长度
         * → 整个服务发现失败（连接成功但 ATT 无响应）。
         */
        s->svc_decl.len       = def->uuid.len;
        s->svc_decl.uuid      = def->uuid.bytes;
        s->attrs[ai].pValue   = (uint8_t *)&s->svc_decl;
        ai++;

        for (i = 0; i < def->char_count; i++) {
            const hal_ble_char_def_t *cd = &def->chars[i];
            uint8_t  props;
            uint8_t  perms;
            uint8_t *vbuf;
            uint8_t *pptr;
            char    *desc;
            uint8_t  has_cccd;

            s->char_attr_idx[i] = ai;

            props = hal_props_to_gatt(cd->props);
            perms = hal_perms_to_gatt(cd->perms);
            has_cccd = (cd->props & (HAL_BLE_PROP_NOTIFY |
                                     HAL_BLE_PROP_INDICATE)) ? 1 : 0;

            /* props 必须持久化（0x2803 声明的 pValue 指向它） */
            pptr = &s_props_pool[s_props_used++];
            *pptr = props;

            /* 值缓冲 */
            vbuf = s_value_pool[s_value_used++];
            s->value_buf[i] = vbuf;
            s->value_max[i] = (cd->max_len && cd->max_len <= VALUE_BUF_SIZE)
                              ? cd->max_len : VALUE_BUF_SIZE;
            s->value_len[i] = 0;
            if (cd->init_value && cd->init_len) {
                uint16_t n = cd->init_len;
                if (n > s->value_max[i]) n = s->value_max[i];
                memcpy(vbuf, cd->init_value, n);
                s->value_len[i] = n;
            }

            /* 描述字符串 */
            desc = s_desc_pool[s_desc_used++];
            if (cd->name) {
                strncpy(desc, cd->name, 15);
                desc[15] = '\0';
            } else {
                desc[0] = '\0';
            }

            /* 1) Characteristic Declaration (0x2803) */
            s->attrs[ai].type.len    = ATT_BT_UUID_SIZE;
            s->attrs[ai].type.uuid   = s_uuid_character;   /* ★ 指针赋值 */
            s->attrs[ai].permissions = GATT_PERMIT_READ;
            s->attrs[ai].handle      = 0;
            s->attrs[ai].pValue      = pptr;
            ai++;

            /* 2) Characteristic Value */
            s->attrs[ai].type.len    = cd->uuid.len;
            s->attrs[ai].type.uuid   = cd->uuid.bytes;  /* ★ 指针赋值 */
            s->attrs[ai].permissions = perms;
            s->attrs[ai].handle      = 0;
            s->attrs[ai].pValue      = vbuf;
            ai++;

            /* 3) CCCD (0x2902) */
            if (has_cccd) {
                s->cccd[i] = s_cccd_pool[s_cccd_used++];
                s->attrs[ai].type.len    = ATT_BT_UUID_SIZE;
                s->attrs[ai].type.uuid   = s_uuid_client_char_cfg;
                s->attrs[ai].permissions = GATT_PERMIT_READ | GATT_PERMIT_WRITE;
                s->attrs[ai].handle      = 0;
                s->attrs[ai].pValue      = (uint8_t *)s->cccd[i];
            } else {
                /* 无 CCCD：用 User Description 占位 */
                s->attrs[ai].type.len    = ATT_BT_UUID_SIZE;
                s->attrs[ai].type.uuid   = s_uuid_char_user_desc;
                s->attrs[ai].permissions = GATT_PERMIT_READ;
                s->attrs[ai].handle      = 0;
                s->attrs[ai].pValue      = (uint8_t *)desc;
            }
            ai++;

            /* 4) User Description (0x2901) */
            if (has_cccd) {
                s->attrs[ai].type.len    = ATT_BT_UUID_SIZE;
                s->attrs[ai].type.uuid   = s_uuid_char_user_desc;
                s->attrs[ai].permissions = GATT_PERMIT_READ;
                s->attrs[ai].handle      = 0;
                s->attrs[ai].pValue      = (uint8_t *)desc;
                ai++;
            }

            /* 保存回调 */
            if (cbs) s->cbs[i] = cbs[i];
        }

        /* 实际属性数（无 CCCD 的特征值少 1 个） */
        s->attr_count = ai;
    }

    /* 初始化 CCCD */
    for (i = 0; i < def->char_count; i++) {
        if (s->cccd[i]) {
            GATTServApp_InitCharCfg(INVALID_CONNHANDLE, s->cccd[i]);
        }
    }

    /* 注册 linkDB 回调（只注册一次） */
    if (!s_linkdb_registered) {
        linkDB_Register(profile_conn_status_cb);
        s_linkdb_registered = 1;
    }

    /* 注册到 GATT Server */
    {
        bStatus_t st = GATTServApp_RegisterService(
            s->attrs, s->attr_count, GATT_MAX_ENCRYPT_KEY_SIZE, &s_svc_cbs);
        g_ble_dbg_gatt_reg_rc = (uint8_t)st;
        g_ble_dbg_gatt_attr_cnt = s->attr_count;
        if (st != SUCCESS) return -7;
    }

    /* 回填运行时句柄给应用 */
    for (i = 0; i < def->char_count; i++) {
        out_chars[i].service_idx  = s_svc_count;
        out_chars[i].char_idx     = i;
        out_chars[i].value_handle = s->attrs[s->char_attr_idx[i] + 1].handle;
        out_chars[i].cccd_handle  = s->cccd[i]
            ? s->attrs[s->char_attr_idx[i] + 2].handle : 0;
    }

    /* 诊断：记录第一个属性的 handle（0 表示协议栈没分配 → 注册无效） */
    g_ble_dbg_gatt_first_handle = s->attrs[0].handle;
    g_ble_dbg_gatt_val_handle   = out_chars[0].value_handle;

    s_attr_used  = (uint16_t)(s_attr_used + s->attr_count);
    s_svc_count++;
    return 0;
}

/**
 * 发送通知
 */
bStatus_t hal_ble_profile_notify(uint16_t conn_handle, uint16_t value_handle,
                                 const uint8_t *data, uint16_t len)
{
    attHandleValueNoti_t noti;
    uint8_t i, j;

    if (!data || !len) return INVALIDPARAMETER;

    /* 找到该句柄所属服务与特征值，检查 CCCD 是否已订阅 */
    for (i = 0; i < s_svc_count; i++) {
        profile_svc_t *s = &s_svc[i];
        for (j = 0; j < s->def->char_count && j < 8; j++) {
            if (!s->cccd[j]) continue;
            if (s->attrs[s->char_attr_idx[j] + 1].handle != value_handle) continue;

            /* 检查订阅状态 */
            if (!(GATTServApp_ReadCharCfg(conn_handle, s->cccd[j])
                  & GATT_CLIENT_CFG_NOTIFY)) {
                return bleIncorrectMode;
            }

            noti.handle = value_handle;
            noti.len    = len;
            noti.pValue = (uint8_t *)data;   /* ★ pValue 是指针，直接指向调用方数据 */
            return GATT_Notification(conn_handle, &noti, FALSE);
        }
    }
    return ATT_ERR_INVALID_HANDLE;
}

/**
 * 更新特征值（读请求会返回新值）
 */
int hal_ble_profile_set_value(uint16_t value_handle,
                              const uint8_t *data, uint16_t len)
{
    uint8_t i, j;

    if (!data) return -1;

    for (i = 0; i < s_svc_count; i++) {
        profile_svc_t *s = &s_svc[i];
        for (j = 0; j < s->def->char_count && j < 8; j++) {
            if (s->attrs[s->char_attr_idx[j] + 1].handle != value_handle) continue;
            if (len > s->value_max[j]) return -2;
            memcpy(s->value_buf[j], data, len);
            s->value_len[j] = len;
            return 0;
        }
    }
    return -3;
}

/**
 * 查询已注册服务数
 */
uint8_t hal_ble_profile_service_count(void)
{
    return s_svc_count;
}
