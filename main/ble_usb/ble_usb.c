/**
 * @file    ble_usb.c
 * @brief   ★ 核心验收：BLE + USB CDC 同时工作
 *
 * 这是旧架构（ch32fun + fsusb）完全做不到的场景：
 *   fsusb.c 调用 NVIC_EnableIRQ(USB_IRQn) 会清掉 BB/LLE 的中断使能位，
 *   导致 BLE 协议栈收不到链路层事件，射频永不发包。
 *
 * 新架构用 hal_irq_enable()（影子副本 + 整体回写）彻底解决这个问题：
 *   - USB 使能不会影响 BB/LLE
 *   - BLE 使能不会影响 USB
 *   - 每次 poll 都重新确保三者都打开
 *
 * 验证：
 *   1. 手机能扫到 CH592-BLE，能连接、能收发
 *   2. 同时 Windows 识别出 COM 口，能收发
 *   3. 两者互不干扰
 */
#include "hal_core.h"
#include <string.h>

/* ===========================================================================
 * GATT 定义：Echo 服务（0xFFF0）
 * =========================================================================== */
static const hal_ble_char_def_t echo_chars[] = {
    {
        .name  = "RX",
        .uuid  = HAL_BLE_UUID16(0xFFF1),
        .props = HAL_BLE_PROP_WRITE | HAL_BLE_PROP_WRITE_NR,
        .perms = HAL_BLE_PERM_WRITE,
        .max_len = 128,
    },
    {
        .name  = "TX",
        .uuid  = HAL_BLE_UUID16(0xFFF2),
        .props = HAL_BLE_PROP_NOTIFY,
        .perms = HAL_BLE_PERM_READ,
        .max_len = 128,
    },
};

static const hal_ble_service_def_t echo_service = {
    .name  = "Echo",
    .uuid  = HAL_BLE_UUID16(0xFFF0),
    .char_count = 2,
    .chars = echo_chars,
};

static hal_ble_char_t g_rx, g_tx;

/* ===========================================================================
 * 回调
 * =========================================================================== */
static void on_ble_write(const hal_ble_char_t *ch, const uint8_t *data,
                         uint16_t len, uint16_t conn, void *user)
{
    hal_ble_t *b = hal_ble_get();
    /* BLE -> BLE 回显 */
    b->notify(b, &g_tx, conn, data, len);
    /* BLE -> USB 转发（句柄 1 = 第一个 CDC 实例） */
    hal_usb_cdc_write(1, data, len);
    LOG_I("BLE rx %u bytes\r\n", len);
}

static void on_ble_evt(hal_ble_evt_t evt, const void *param, void *user)
{
    switch (evt) {
    case HAL_BLE_EVT_ADV_STARTED:
        LOG_I("BLE: advertising\r\n");
        break;
    case HAL_BLE_EVT_CONNECTED:
        LOG_I("BLE: connected\r\n");
        break;
    case HAL_BLE_EVT_DISCONNECTED:
        LOG_I("BLE: disconnected\r\n");
        break;
    default:
        break;
    }
}

static void on_usb_rx(const uint8_t *data, uint16_t len, void *user)
{
    /* USB -> BLE 转发（需要已连接） */
    hal_ble_t *b = hal_ble_get();
    if (b->is_connected(b)) {
        b->notify(b, &g_tx, 0, data, len);
    }
    LOG_I("USB rx %u bytes\r\n", len);
}

/* ===========================================================================
 * main
 * =========================================================================== */
int main(void)
{
    hal_err_t  err;
    hal_ble_t *b;
    hal_usb_t *u;
    uint32_t   cnt = 0;

    err = hal_init();
    if (err != HAL_OK) while (1) { }

    /* ---- 1. USB CDC ---- */
    u = hal_usb_get();
    {
        hal_usb_cdc_cfg_t cdc;
        cdc.ep_in     = 3;
        cdc.ep_out    = 2;
        cdc.ep_notify = 1;
        cdc.rx_buf_size = 128;
        cdc.tx_buf_size = 128;
        err = hal_usb_cdc_add(&cdc, on_usb_rx, NULL);
        if (err != HAL_OK) while (1) { }
    }
    {
        hal_usb_dev_desc_t desc;
        memset(&desc, 0, sizeof(desc));
        /* ★ 用 CH340 的 VID/PID：Windows 自带 CH341SER 驱动，
         *   能直接看到 COM 口，便于验证枚举是否成功。
         *   正式产品应换成自己的 VID/PID 并配 .inf 驱动。 */
        desc.vid = 0x1A86;
        desc.pid = 0x7523;
        desc.bcd_device = 0x0263;
        desc.manufacturer = "CHxxx";
        desc.product = "BLE+CDC";
        desc.serial = "0001";
        desc.max_power_ma = 100;
        err = u->init(u, &desc);
        if (err != HAL_OK) while (1) { }
    }

    /* ---- 2. 日志先走 USB CDC ---- */
    hal_log_init();
    hal_log_set_sink(HAL_LOG_SINK_USB_CDC, NULL);
    hal_log_set_level(HAL_LOG_LVL_INFO);

    /* ---- 3. BLE ---- */
    b = hal_ble_get();
    {
        hal_ble_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.device_name = "CH592-BLE";
        cfg.adv_interval_ms = 100;
        cfg.min_conn_interval_ms = 20;
        cfg.max_conn_interval_ms = 100;
        cfg.mtu = 128;
        cfg.max_connections = 1;
        err = b->init(b, &cfg);
        if (err != HAL_OK) {
            LOG_E("BLE init failed: %d\r\n", err);
            while (1) { }
        }
    }

    /* ---- 4. 注册 GATT ---- */
    {
        hal_ble_char_t tmp[2];
        hal_ble_char_cbs_t cbs;
        memset(&cbs, 0, sizeof(cbs));
        cbs.write_cb = on_ble_write;

        err = b->add_service(b, &echo_service, tmp, &cbs);
        if (err != HAL_OK) LOG_E("add service failed: %d\r\n", err);
        g_rx = tmp[0];
        g_tx = tmp[1];
    }

    b->set_evt_cb(b, on_ble_evt, NULL);
    err = b->adv_start(b);
    if (err != HAL_OK) LOG_E("adv_start failed: %d\r\n", err);

    LOG_I("\r\n=== BLE + USB CDC ready ===\r\n");

    /* ---- 5. 主循环：同时驱动 BLE 协议栈和 USB ---- */
    while (1) {
        b->poll(b);     /* 内部会重新确保 BB/LLE/RTC 中断使能 */
        u->poll(u);

        cnt++;
        if ((cnt % 1000) == 0) {
            LOG_I("[%u] ble=%u usb=%u\r\n", cnt / 1000,
                  b->is_connected(b), hal_usb_cdc_connected(1));
        }
        hal_time_get()->delay_ms(hal_time_get(), 10);
    }
}
