/**
 * @file    flash_demo.c
 * @brief   Flash / ROM 读写验证
 *
 * 验证内容：
 *   1. 几何信息（擦除/写入粒度）
 *   2. 读芯片 UID / MAC
 *   3. 擦 → 写 → 读 回环
 *   4. 掉电保持（复位后读回上次写入的计数）
 *
 * ★ 区域选择
 *   CH5xx（CH59x/CH572/CH585）：用 Data Flash（独立 32KB，不影响程序）
 *   CH32（X035/L103/V20x）：没有独立 Data Flash，只能写程序区，
 *                            默认关闭写入，只做只读验证。
 *                            需要写时定义 FLASH_DEMO_WRITE=1。
 */

#include "hal_core.h"
#include <string.h>

/* CH5xx 有独立 Data Flash；CH32 没有 */
#if defined(CH59x) || defined(CH572) || defined(CH585)
  #define FLASH_HAS_DATA_REGION   1
#else
  #define FLASH_HAS_DATA_REGION   0
#endif

/* 是否允许写（CH32 上默认关闭，避免破坏程序区） */
#ifndef FLASH_DEMO_WRITE
#define FLASH_DEMO_WRITE        FLASH_HAS_DATA_REGION
#endif

#define TEST_OFFSET     0
#define TEST_MAGIC      0x5AA5C33CUL

typedef struct {
    uint32_t magic;
    uint32_t counter;
    uint8_t  payload[8];
} test_blob_t;

int main(void)
{
    hal_err_t       err;
    hal_uart_t     *u;
    hal_uart_cfg_t  ucfg;
    hal_flash_cfg_t fcfg;
    hal_flash_geo_t geo;
    test_blob_t     blob;
    test_blob_t     readback;
    uint8_t         uid[16];
    uint8_t         n;
    uint8_t         mac[6];
    uint8_t         have_prev;

    /* ---- 系统 + 串口 ---- */
    err = hal_init();
    if (err != HAL_OK) { while (1) { } }

    u = hal_uart_get(1);
    if (!u) { while (1) { } }

    memset(&ucfg, 0, sizeof(ucfg));
    ucfg.baudrate  = 115200;
    ucfg.data_bits = 8;
    ucfg.stop_bits = 1;
    ucfg.parity    = 0;
    ucfg.tx_pin    = HAL_PA9;
    ucfg.rx_pin    = HAL_PA8;
    ucfg.use_dma   = 0;

    err = u->init(u, &ucfg);
    if (err != HAL_OK) { while (1) { } }

    hal_log_init();
    hal_log_set_sink(HAL_LOG_SINK_UART, u);
    hal_log_set_level(HAL_LOG_LVL_INFO);

    LOG_I("\r\n===== flash_demo =====\r\n");

    /* ---- 1. 初始化 flash ---- */
    fcfg.region = FLASH_HAS_DATA_REGION ? HAL_FLASH_REGION_DATA
                                        : HAL_FLASH_REGION_CODE;
    fcfg.base             = 0;
    fcfg.size             = 0;              /* 用默认 */
    fcfg.allow_code_write = FLASH_DEMO_WRITE && !FLASH_HAS_DATA_REGION;

    err = hal_flash_init(&fcfg);
    LOG_I("flash_init : %s\r\n", err == HAL_OK ? "OK" : "FAIL");
    if (err != HAL_OK) { while (1) { } }

    err = hal_flash_get_geo(&geo);
    if (err == HAL_OK) {
        LOG_I("total_size : %u\r\n", (unsigned)geo.total_size);
        LOG_I("erase_size : %u\r\n", (unsigned)geo.erase_size);
        LOG_I("write_size : %u\r\n", (unsigned)geo.write_size);
        LOG_I("block_size : %u\r\n", (unsigned)geo.block_size);
    }

    /* ---- 2. 芯片 UID / MAC ---- */
    n = hal_flash_read_uid(uid, sizeof(uid));
    if (n) {
        uint8_t i;
        LOG_I("UID        : ");
        for (i = 0; i < n; i++) LOG_I("%02X ", uid[i]);
        LOG_I("\r\n");
    }

    if (hal_flash_read_mac(mac) == HAL_OK) {
        LOG_I("MAC        : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        LOG_I("MAC        : (不支持)\r\n");
    }

    /* ---- 3. 读上次写入的内容（掉电保持验证） ---- */
    memset(&readback, 0, sizeof(readback));
    err = hal_flash_read(TEST_OFFSET, &readback, sizeof(readback));

    have_prev = (err == HAL_OK && readback.magic == TEST_MAGIC) ? 1 : 0;
    if (have_prev) {
        LOG_I("上次 counter : %u\r\n", (unsigned)readback.counter);
    } else {
        LOG_I("首次运行（无历史数据）\r\n");
    }

    /* ---- 4. 擦 → 写 → 读 ---- */
    if (FLASH_DEMO_WRITE) {
        blob.magic      = TEST_MAGIC;
        blob.counter    = have_prev ? (readback.counter + 1) : 1;
        blob.payload[0] = 0xDE;
        blob.payload[1] = 0xAD;
        blob.payload[2] = 0xBE;
        blob.payload[3] = 0xEF;

        err = hal_flash_update(TEST_OFFSET, &blob, sizeof(blob));
        LOG_I("写入       : %s\r\n", err == HAL_OK ? "OK" : "FAIL");

        if (err == HAL_OK) {
            memset(&readback, 0, sizeof(readback));
            err = hal_flash_read(TEST_OFFSET, &readback, sizeof(readback));

            if (err == HAL_OK &&
                readback.magic   == TEST_MAGIC &&
                readback.counter == blob.counter) {
                LOG_I("读回校验   : OK  counter=%u\r\n",
                      (unsigned)readback.counter);
            } else {
                LOG_I("读回校验   : 失败！\r\n");
            }
        }
    } else {
        LOG_I("写入       : 跳过（CH32 无独立 Data Flash）\r\n");
        LOG_I("             需写入请定义 FLASH_DEMO_WRITE=1\r\n");
    }

    LOG_I("===== done =====\r\n");

    /* ---- 主循环：闪灯表示存活 ---- */
    {
        hal_gpio_t *g = hal_gpio_get();
        if (g) {
            /* pull: 0=none 1=up 2=down */
            g->init(g, HAL_PA8, HAL_GPIO_OUT_PP, 0);
            for (;;) {
                g->toggle(g, HAL_PA8);
                hal_time_get()->delay_ms(hal_time_get(), 500);
            }
        }
    }
    for (;;) { }
}
