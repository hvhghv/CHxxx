/**
 * @file    hal_sys_ch59x.c
 * @brief   CH59x 系统层：时钟、复位、UID、低功耗
 */
#include "hal_core.h"
#include "CH59x_common.h"
#include <string.h>

/* ===========================================================================
 * 时钟
 * =========================================================================== */
static hal_err_t sys_init(hal_sys_t *s)
{
    /* 官方默认时钟：PLL 60MHz（BLE 协议栈要求） */
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    s->dev.initialized = 1;
    return HAL_OK;
}

static uint32_t sys_get_clock_hz(hal_sys_t *s)
{
    /* CH59x 的 GetSysClock() 返回 Hz */
    return GetSysClock();
}

static hal_err_t sys_set_clock_hz(hal_sys_t *s, uint32_t hz)
{
    SYS_CLKTypeDef src;

    switch (hz) {
    case 60000000: src = CLK_SOURCE_PLL_60MHz; break;
    case 48000000: src = CLK_SOURCE_PLL_48MHz; break;
    case 24000000: src = CLK_SOURCE_PLL_24MHz; break;
    case 16000000: src = CLK_SOURCE_HSE_16MHz; break;
    default: return HAL_ERR_PARAM;
    }
    SetSysClock(src);
    return HAL_OK;
}

static void sys_reset(hal_sys_t *s)
{
    PFIC_SystemReset();
}

/* ===========================================================================
 * 唯一 ID
 * =========================================================================== */
static void sys_get_uid(hal_sys_t *s, uint8_t uid[8])
{
    /* CH59x 的 UID 在 Flash 信息区，地址 0x3F000 附近。
     * 官方 GetMACAddress() 返回 6 字节 MAC，可作 UID 用。 */
    uint8_t mac[6];
    GetMACAddress(mac);
    memcpy(uid, mac, 6);
    uid[6] = 0;
    uid[7] = 0;
}

/* ===========================================================================
 * 低功耗
 * =========================================================================== */
static uint32_t s_wake_reason = 0;

static uint32_t sys_sleep(hal_sys_t *s, const hal_sleep_cfg_t *cfg)
{
    uint32_t reason = 0;

    if (!cfg) return 0;

    /* 配置唤醒源 */
    if (cfg->wake_sources & HAL_WAKE_RTC && cfg->rtc_wake_ms) {
        /* RTC 定时唤醒：官方 RTC_SetCycle32k 单位 1/32768 s */
        uint32_t ticks = (uint32_t)((uint64_t)cfg->rtc_wake_ms * 32768u / 1000u);
        RTC_SetCycle32k(ticks);
        reason |= HAL_WAKE_RTC;
    }

    if (cfg->wake_sources & HAL_WAKE_GPIO && cfg->gpio_wake_pin != HAL_PIN_NONE) {
        uint8_t port = HAL_PIN_PORT(cfg->gpio_wake_pin);
        uint8_t num  = HAL_PIN_NUM(cfg->gpio_wake_pin);
        uint32_t mask = 1u << num;
        GPIOITModeTpDef m = cfg->gpio_wake_level ? GPIO_ITMode_HighLevel
                                                 : GPIO_ITMode_LowLevel;
        if (port == 0) {
            GPIOA_ITModeCfg(mask, m);
        } else {
            GPIOB_ITModeCfg(mask, m);
        }
        reason |= HAL_WAKE_GPIO;
    }

    if (cfg->wake_sources & HAL_WAKE_USB) {
        R8_USB_INT_EN |= RB_UC_DEV_PU_EN;
        reason |= HAL_WAKE_USB;
    }

    /* 进入低功耗 */
    switch (cfg->mode) {
    case HAL_SLEEP_MODE_SLEEP:
        /* 保留全部 SRAM + USB/BLE 供电，仅停 CPU */
        LowPower_Sleep(RB_PWR_RAM2K | RB_PWR_RAM24K | RB_PWR_EXTEND);
        break;
    case HAL_SLEEP_MODE_STANDBY:
        /* 仅保留 2K retention SRAM */
        LowPower_Sleep(RB_PWR_RAM2K);
        break;
    case HAL_SLEEP_MODE_SHUTDOWN:
        LowPower_Shutdown(0);
        break;
    default:
        return 0;
    }

    s_wake_reason = reason;
    return reason;
}

static uint32_t sys_get_wake_reason(hal_sys_t *s)
{
    return s_wake_reason;
}

static void sys_clear_wake_reason(hal_sys_t *s)
{
    s_wake_reason = 0;
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static hal_sys_t s_sys = {
    .dev = { .name = "sys", .initialized = 0 },
    .init              = sys_init,
    .get_clock_hz      = sys_get_clock_hz,
    .set_clock_hz      = sys_set_clock_hz,
    .reset             = sys_reset,
    .get_uid           = sys_get_uid,
    .sleep             = sys_sleep,
    .get_wake_reason   = sys_get_wake_reason,
    .clear_wake_reason = sys_clear_wake_reason,
};

hal_sys_t *hal_sys_get(void)
{
    return &s_sys;
}
