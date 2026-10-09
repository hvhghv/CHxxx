/********************************** (C) COPYRIGHT *******************************
 * File Name          : pinmux.h
 * Description        : 引脚复用管理器
 *
 * 管理 CH591/CH592 每个 GPIO 的功能分配（GPIO/UART/PWM/ADC/SPI/I2C/SWIO），
 * 以及每个功能的参数。配置可保存到 Flash，上电自动加载。
 *******************************************************************************/

#ifndef _PINMUX_H
#define _PINMUX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO 引脚编号（0..23 表示 PA0..PA15, PB0..PB7；24..47 表示 PB8..PB23）
 * 简化：用 0..15 = PA0..PA15，16..39 = PB0..PB23 */
#define PIN_PA(n)   ((n) + 0)
#define PIN_PB(n)   ((n) + 16)
#define PIN_COUNT   40

/* 引脚功能类型 */
typedef enum {
	PIN_FUNC_NONE = 0,   /* 未使用 */
	PIN_FUNC_GPIO_IN,    /* GPIO 输入 */
	PIN_FUNC_GPIO_OUT,   /* GPIO 输出 */
	PIN_FUNC_UART_TX,    /* 串口 TX */
	PIN_FUNC_UART_RX,    /* 串口 RX */
	PIN_FUNC_PWM,        /* PWM 输出 */
	PIN_FUNC_ADC,        /* ADC 输入 */
	PIN_FUNC_SPI,        /* SPI */
	PIN_FUNC_I2C,        /* I2C */
	PIN_FUNC_SWIO,       /* 软件 SWIO（烧录 CH32V003） */
} pin_func_t;

/* GPIO 输入配置 */
typedef enum {
	PIN_GPIO_IN_FLOAT = 0,
	PIN_GPIO_IN_PU,
	PIN_GPIO_IN_PD,
} pin_gpio_in_t;

/* GPIO 输出配置 */
typedef enum {
	PIN_GPIO_OUT_PP = 0,   /* 推挽 */
	PIN_GPIO_OUT_OD,       /* 开漏 */
} pin_gpio_out_t;

/* GPIO 中断触发方式 */
typedef enum {
	PIN_IRQ_NONE = 0,
	PIN_IRQ_RISING,
	PIN_IRQ_FALLING,
	PIN_IRQ_BOTH,
} pin_irq_t;

/* 串口模式 */
typedef enum {
	PIN_UART_MODE_FORWARD = 0,   /* 转发模式（绑定 CDC） */
	PIN_UART_MODE_FRAME,         /* 帧模式 */
} pin_uart_mode_t;

/* 单个引脚的配置 */
typedef struct {
	uint8_t  func;          /* pin_func_t */
	uint8_t  flags;         /* 功能相关标志 */
	uint8_t  param1;        /* 通用参数（如 UART 号、PWM 通道） */
	uint8_t  param2;        /* 通用参数 */
	uint8_t  arg1;          /* 扩展参数（GPIO 输出电平 0/1） */
} pin_cfg_t;

/* 全局引脚配置表：别名到统一配置 g_cfg.pins
 * （使用前需包含 settings.h；见 settings.h 的 gateway_cfg_t） */
#define g_pin_cfg   (g_cfg.pins)

/* 初始化（加载配置并应用） */
void pinmux_init(void);

/* 应用单个引脚的配置（硬件层面） */
int pinmux_apply(int pin);

/* 应用全部引脚配置 */
void pinmux_apply_all(void);

/* 设置引脚功能（不立即应用） */
int pinmux_set_func(int pin, pin_func_t func);

/* 读取 GPIO 电平（0/1，-1 错误） */
int pinmux_gpio_read(int pin);

/* 设置 GPIO 输出电平 */
int pinmux_gpio_write(int pin, int level);

/* 读 ADC 值（0..4095，-1 错误） */
int pinmux_adc_read(int pin);

/* 配置存储 */
int  pinmux_save(void);      /* 保存到 Flash */
int  pinmux_load(void);      /* 从 Flash 加载 */
void pinmux_reset_default(void);  /* 恢复默认配置 */

/* 引脚名（如 "PA0"） */
const char *pinmux_pin_name(int pin);
/* 功能名（如 "GPIO_OUT"） */
const char *pinmux_func_name(int func);

#ifdef __cplusplus
}
#endif

#endif /* _PINMUX_H */
