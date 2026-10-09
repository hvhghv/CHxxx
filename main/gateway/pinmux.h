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

/* 引脚数量（由 CMake 传入 GW_PIN_COUNT；缺省按芯片）
 *   CH592（QFN32）：24 个 GPIO（PA4-15 + PB0/4/6/7/10-15/22-23）
 *   CH591（QFN28）：20 个 GPIO（PA4/5/8/9/10-15 + PB4/7/10-15/22-23）
 * 紧凑编号 0..GW_PIN_COUNT-1 → 物理引脚（见 pinmux.c 的 g_pin_map[]） */
#ifndef GW_PIN_COUNT
#  if defined(CH591)
#    define GW_PIN_COUNT 20
#  else
#    define GW_PIN_COUNT 24
#  endif
#endif

/* GPIO 引脚编号（紧凑编号，每芯片引脚表不同）
 * 编号 0..PIN_COUNT-1 通过 g_pin_map[] 映射到物理引脚（ch32fun 引脚号）。
 * 紧凑编号省 Flash/RAM（CH591 QFN28 引脚少于 CH592 QFN32）。 */
#define PIN_COUNT   GW_PIN_COUNT

/* 物理引脚号（ch32fun 格式：PA0-15 = 0..15，PB0-23 = PB|n）
 * 由 pinmux.c 的 g_pin_map[] 提供 */
#define PIN_PA(n)   ((n) + 0)
#define PIN_PB(n)   (16 + (n))   /* 紧凑编号空间内 PB 偏移（仅用于构建映射表） */

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

/* 串口波特率：存 16 位分频值（R16_UARTx_DL），支持任意波特率（最高 6Mbps）。
 * 波特率公式（数据手册 9.3.1）：baud = Fsys * 2 / DIV / 16 / DL
 * 其中 DIV = R8_UARTx_DIV（1..127），由 uart_baud_to_div() 自动选择。
 * 0 表示未设置（回退 115200）。 */
#define UART_BAUD_DEFAULT  115200
/* 波特率 → 16 位 DL 分频值（自动选 DIV 使误差最小）；baud=0 或非法返回默认值 */
uint16_t uart_baud_to_div(uint32_t baud);
/* 16 位 DL 分频值 → 波特率（用于显示） */
uint32_t uart_div_to_baud(uint16_t dl);

/* 串口校验 */
typedef enum {
	PIN_UART_PARITY_NONE = 0,
	PIN_UART_PARITY_ODD,
	PIN_UART_PARITY_EVEN,
} pin_uart_parity_t;

/* 串口停止位 */
typedef enum {
	PIN_UART_STOP_1 = 0,
	PIN_UART_STOP_2,
} pin_uart_stop_t;

/* UART 引脚配置字段约定（TX 与 RX 引脚共享同一 uart 号）：
 *   param1 = UART 号（0..3，后端从 UARTn_TX/RX 名自动解析）
 *   param2 = 编码参数：bit0=模式（pin_uart_mode_t）
 *                        bit1-2=校验（pin_uart_parity_t）
 *                        bit3=停止位（pin_uart_stop_t）
 *   uart_dl = 波特率分频值（uart_baud_to_div，0=默认 115200） */

/* 单个引脚的配置 */
typedef struct {
	uint8_t  func;          /* pin_func_t */
	uint8_t  flags;         /* 功能相关标志（UART：bit0-1=CDC 绑定号） */
	uint8_t  param1;        /* 通用参数（如 UART 号、PWM 通道） */
	uint8_t  param2;        /* 通用参数 */
	uint8_t  arg1;          /* 扩展参数（GPIO 输出电平 0/1） */
	uint16_t uart_dl;       /* UART 波特率分频值（0=默认 115200） */
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
