/**
 * @file    usb_cdc.c
 * @brief   阶段 5 验收：USB CDC 虚拟串口
 *
 * 验证：
 *   1. 插 USB 后 Windows 识别为 COM 口
 *   2. 串口助手能收到周期输出
 *   3. 发送数据能回显
 *
 * 注意：本示例不启用 BLE，用于单独验证 USB 通路。
 *       确认 USB 可用后，再看 ble_usb_demo（USB + BLE 共存）。
 */
#include "hal_core.h"
#include <string.h>

static hal_usb_cdc_rx_cb_t dummy;

/* CDC 实例句柄 */
static hal_usb_cdc_handle_t s_cdc;

/* 接收回调：原样回显 */
static void on_cdc_rx(const uint8_t *data, uint16_t len, void *user)
{
    HAL_UNUSED(user);
    hal_usb_cdc_write(s_cdc, data, len);
}

int main(void)
{
    hal_err_t err;
    hal_usb_t *u;
    uint32_t   cnt = 0;

    err = hal_init();

    u = hal_usb_get();

    /* 1. 注册 CDC 类（必须在 init 之前） */
    {
        hal_usb_cdc_cfg_t cfg;
        cfg.ep_in     = 3;
        cfg.ep_out    = 2;
        cfg.ep_notify = 1;
        cfg.rx_buf_size = 128;
        cfg.tx_buf_size = 128;

        s_cdc = hal_usb_cdc_add(&cfg, on_cdc_rx, NULL);
        if (s_cdc == HAL_USB_CDC_HANDLE_INVALID) while (1) { }
    }

    /* 2. 初始化 USB（自动拼装描述符并枚举） */
    {
        hal_usb_dev_desc_t desc;
        memset(&desc, 0, sizeof(desc));
        desc.vid         = 0x1A86;      /* WCH */
        desc.pid         = 0x7523;      /* CH340（与官方 BLE_USB 一致） */
        desc.bcd_device  = 0x0263;      /* 与官方一致 */
        desc.manufacturer = "CHxxx";
        desc.product     = "HAL CDC Demo";
        desc.serial      = "0001";
        desc.max_power_ma = 100;
        desc.self_powered = 0;

        err = u->init(u, &desc);
        if (err != HAL_OK) while (1) { }
    }

    /* 3. 日志切到 USB CDC（枚举完成后才能输出） */
    hal_log_init();
    hal_log_set_sink(HAL_LOG_SINK_USB_CDC, NULL);
    hal_log_set_level(HAL_LOG_LVL_INFO);

    /* 4. 等枚举完成 */
    {
        uint32_t t0 = hal_time_get()->get_tick_ms(hal_time_get());
        uint32_t spins = 0;

        /* ★ 诊断：先记录 tick 是否在走 */

        while (!hal_usb_cdc_connected(s_cdc)) {
            u->poll(u);
            spins++;
            if (spins == 200000u) {
                /* 记录第一次超时检查时的 tick 值 */
            }
            if ((hal_time_get()->get_tick_ms(hal_time_get()) - t0) > 3000) break;
        }
    }

    LOG_I("\r\n=== CH59x HAL USB CDC Demo ===\r\n");
    LOG_I("clock = %u Hz\r\n", hal_sys_get()->get_clock_hz(hal_sys_get()));
    LOG_I("type something to echo\r\n");

    /* 5. 主循环 */
    while (1) {
        u->poll(u);

        cnt++;
        if ((cnt % 500) == 0 && hal_usb_cdc_connected(s_cdc)) {
            LOG_I("[%u] tick=%u\r\n", cnt / 500,
                  hal_time_get()->get_tick_ms(hal_time_get()));
        }
        hal_time_get()->delay_ms(hal_time_get(), 10);
    }
}
