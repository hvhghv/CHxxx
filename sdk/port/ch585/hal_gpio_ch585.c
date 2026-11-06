/**
 * @file    hal_gpio_ch585.c
 * @brief   CH585 GPIO 实现（基于官方 StdPeriphDriver）
 *
 * 官方 API 特点：GPIOA_xxx / GPIOB_xxx 是两组独立的宏和函数，
 * 没有统一的 GPIOTypeDef* 抽象。这里用 port 分支封装掉。
 */
#include "hal_core.h"
#include "CH58x_common.h"
#include <string.h>

#define GPIO_PINS_PER_PORT  24

static hal_gpio_irq_cb_t s_irq_cb[2][GPIO_PINS_PER_PORT];
static void             *s_irq_user[2][GPIO_PINS_PER_PORT];

/* ===========================================================================
 * 端口分发辅助
 * =========================================================================== */
static void port_mode_cfg(uint8_t port, uint32_t pinmask, GPIOModeTypeDef mode)
{
    /* ★ 关键：官方 GPIOA_ModeCfg 内部用 SYS_SAFE_ACCESS 保护序列
     * （写 0x57/0xA8 到 R8_SAFE_ACCESS_SIG，然后改寄存器）。
     * 如果这个序列被中断打断，保护会失效，寄存器配置失败甚至死机。
     * 用 save/restore 保证不改变调用者的中断状态。 */
    uint32_t st = hal_irq_save();
    if (port == 0) GPIOA_ModeCfg(pinmask, mode);
    else           GPIOB_ModeCfg(pinmask, mode);
    hal_irq_restore(st);
}

static void port_set_bits(uint8_t port, uint32_t pinmask)
{
    /* ★ GPIOA_SetBits 内部走 SYS_SAFE_ACCESS，必须关中断保护 */
    uint32_t st = hal_irq_save();
    if (port == 0) GPIOA_SetBits(pinmask);
    else           GPIOB_SetBits(pinmask);
    hal_irq_restore(st);
}

static void port_reset_bits(uint8_t port, uint32_t pinmask)
{
    uint32_t st = hal_irq_save();
    if (port == 0) GPIOA_ResetBits(pinmask);
    else           GPIOB_ResetBits(pinmask);
    hal_irq_restore(st);
}

static uint32_t port_read_pin(uint8_t port)
{
    /* ★ 官方 GPIOx_ReadPortPin 内部同样走 SYS_SAFE_ACCESS，
     *   必须关中断保护，否则被 SysTick 打断会死锁。 */
    uint32_t st = hal_irq_save();
    uint32_t v  = (port == 0) ? GPIOA_ReadPortPin(0xFFFFFFFFu)
                              : GPIOB_ReadPortPin(0xFFFFFFFFu);
    hal_irq_restore(st);
    return v;
}

static void port_it_mode_cfg(uint8_t port, uint32_t pinmask, GPIOITModeTypeDef mode)
{
    uint32_t st = hal_irq_save();
    if (port == 0) GPIOA_ITModeCfg(pinmask, mode);
    else           GPIOB_ITModeCfg(pinmask, mode);
    hal_irq_restore(st);
}

static uint16_t port_read_it_flag(uint8_t port)
{
    return (port == 0) ? GPIOA_ReadITFlagPort() : GPIOB_ReadITFlagPort();
}

static void port_clear_it_flag(uint8_t port, uint16_t pinmask)
{
    uint32_t st = hal_irq_save();
    if (port == 0) GPIOA_ClearITFlagBit(pinmask);
    else           GPIOB_ClearITFlagBit(pinmask);
    hal_irq_restore(st);
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
static hal_err_t gpio_init(hal_gpio_t *g, uint8_t pin, hal_gpio_mode_t mode,
                           uint8_t pull)
{
    uint8_t  port = HAL_PIN_PORT(pin);
    uint8_t  num  = HAL_PIN_NUM(pin);
    uint32_t mask;
    GPIOModeTypeDef m;

    if (port > 1 || num > 23) return HAL_ERR_PARAM;
    mask = 1u << num;

    switch (mode) {
    case HAL_GPIO_IN:
    case HAL_GPIO_IN_ANALOG:
        m = GPIO_ModeIN_Floating;
        break;
    case HAL_GPIO_IN_PULLUP:
        m = GPIO_ModeIN_PU;
        break;
    case HAL_GPIO_IN_PULLDOWN:
        m = GPIO_ModeIN_PD;
        break;
    case HAL_GPIO_OUT_PP:
    case HAL_GPIO_OUT_AF_PP:
    case HAL_GPIO_OUT_OD:
    case HAL_GPIO_OUT_AF_OD:
        /* CH585 官方枚举无独立 OD 模式，用推挽近似 */
        m = GPIO_ModeOut_PP_5mA;
        break;
    default:
        return HAL_ERR_PARAM;
    }

    port_mode_cfg(port, mask, m);
    HAL_UNUSED(pull);
    return HAL_OK;
}

static hal_err_t gpio_deinit(hal_gpio_t *g, uint8_t pin)
{
    return gpio_init(g, pin, HAL_GPIO_IN, 0);
}

/* ===========================================================================
 * 读写
 * =========================================================================== */
static void gpio_write(hal_gpio_t *g, uint8_t pin, uint8_t level)
{
    uint8_t  port = HAL_PIN_PORT(pin);
    uint8_t  num  = HAL_PIN_NUM(pin);
    uint32_t mask;

    if (port > 1 || num > 23) return;
    mask = 1u << num;
    if (level) port_set_bits(port, mask);
    else       port_reset_bits(port, mask);
}

static uint8_t gpio_read(hal_gpio_t *g, uint8_t pin)
{
    uint8_t port = HAL_PIN_PORT(pin);
    uint8_t num  = HAL_PIN_NUM(pin);
    if (port > 1 || num > 23) return 0;
    return (port_read_pin(port) >> num) & 1u;
}

static void gpio_toggle(hal_gpio_t *g, uint8_t pin)
{
    gpio_write(g, pin, (uint8_t)!gpio_read(g, pin));
}

static void gpio_write_port(hal_gpio_t *g, uint8_t port, uint32_t value)
{
    if (port > 1) return;
    port_set_bits(port, value);
}

static uint32_t gpio_read_port(hal_gpio_t *g, uint8_t port)
{
    if (port > 1) return 0;
    return port_read_pin(port);
}

/* ===========================================================================
 * 中断
 * =========================================================================== */
static hal_err_t gpio_set_irq(hal_gpio_t *g, uint8_t pin, hal_gpio_irq_t edge,
                              hal_gpio_irq_cb_t cb, void *user)
{
    uint8_t  port = HAL_PIN_PORT(pin);
    uint8_t  num  = HAL_PIN_NUM(pin);
    uint32_t mask;
    GPIOITModeTypeDef m;

    if (port > 1 || num > 23) return HAL_ERR_PARAM;

    s_irq_cb[port][num]   = cb;
    s_irq_user[port][num] = user;

    if (edge == HAL_GPIO_IRQ_NONE) return HAL_OK;

    switch (edge) {
    case HAL_GPIO_IRQ_RISING:  m = GPIO_ITMode_RiseEdge; break;
    case HAL_GPIO_IRQ_FALLING: m = GPIO_ITMode_FallEdge; break;
    case HAL_GPIO_IRQ_BOTH:    m = GPIO_ITMode_RiseEdge; break; /* 单边沿近似 */
    default: return HAL_ERR_PARAM;
    }

    mask = 1u << num;
    port_it_mode_cfg(port, mask, m);
    hal_irq_enable(port == 0 ? GPIO_A_IRQn : GPIO_B_IRQn);
    return HAL_OK;
}

static void gpio_irq_dispatch(hal_gpio_t *g, uint8_t port)
{
    uint16_t status;
    uint8_t  i;

    if (port > 1) return;
    status = port_read_it_flag(port);
    port_clear_it_flag(port, status);

    for (i = 0; i < GPIO_PINS_PER_PORT; i++) {
        if ((status >> i) & 1u) {
            if (s_irq_cb[port][i]) {
                s_irq_cb[port][i](HAL_PIN(port, i), s_irq_user[port][i]);
            }
        }
    }
}

/* ===========================================================================
 * 实例
 * =========================================================================== */
static hal_gpio_t s_gpio = {
    .dev = { .name = "gpio", .initialized = 0 },
    .init         = gpio_init,
    .deinit       = gpio_deinit,
    .write        = gpio_write,
    .read         = gpio_read,
    .toggle       = gpio_toggle,
    .write_port   = gpio_write_port,
    .read_port    = gpio_read_port,
    .set_irq      = gpio_set_irq,
    .irq_dispatch = gpio_irq_dispatch,
};

hal_gpio_t *hal_gpio_get(void)
{
    return &s_gpio;
}
