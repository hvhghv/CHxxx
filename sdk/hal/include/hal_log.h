/**
 * @file    hal_log.h
 * @brief   日志接口（后端可运行时切换）
 *
 * 需求：默认 UART，可一键切换 USB CDC / 其他 UART / EEPROM。
 *
 * 用法：
 *   hal_log_set_sink(HAL_LOG_SINK_UART, hal_uart_get(1));
 *   hal_log_set_level(HAL_LOG_LVL_INFO);
 *   LOG_I("boot ok, clock=%u\r\n", hal_sys_get()->get_clock_hz(hal_sys_get()));
 *
 *   // USB 枚举完成后再切换
 *   hal_log_set_sink(HAL_LOG_SINK_USB_CDC, NULL);
 */
#ifndef HAL_LOG_H
#define HAL_LOG_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_LOG_SINK_NONE = 0,
    HAL_LOG_SINK_UART,      /**< arg = hal_uart_t* */
    HAL_LOG_SINK_USB_CDC,   /**< arg = NULL */
    HAL_LOG_SINK_EEPROM,    /**< arg = uint32_t* 起始逻辑地址 */
    HAL_LOG_SINK_CUSTOM,    /**< arg = hal_log_write_fn */
    HAL_LOG_SINK_RAM,       /**< arg = NULL，写入内部 RAM 环形缓冲（诊断用） */
} hal_log_sink_t;

typedef enum {
    HAL_LOG_LVL_NONE = 0,
    HAL_LOG_LVL_ERROR,
    HAL_LOG_LVL_WARN,
    HAL_LOG_LVL_INFO,
    HAL_LOG_LVL_DEBUG,
} hal_log_level_t;

/* 自定义后端：返回实际写入字节数 */
typedef int (*hal_log_write_fn)(const uint8_t *data, uint16_t len, void *user);

/** 切换后端。返回 HAL_OK 或错误码 */
hal_err_t hal_log_set_sink(hal_log_sink_t sink, void *arg);

/** 查询当前后端 */
hal_log_sink_t hal_log_get_sink(void);

/** 设置日志级别 */
void hal_log_set_level(hal_log_level_t lvl);
hal_log_level_t hal_log_get_level(void);
uint8_t hal_log_enabled(hal_log_level_t lvl);

/** 格式化输出 */
void hal_log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void hal_log_write(const uint8_t *data, uint16_t len);
void hal_log_hexdump(const void *data, uint16_t len);

/** RAM 后端：读取内部环形缓冲（用于无输出通道时诊断） */
uint16_t hal_log_ram_read(uint8_t *buf, uint16_t max_len);
void     hal_log_ram_clear(void);

/** 初始化（默认后端 = NONE，级别 = INFO） */
hal_err_t hal_log_init(void);

/* ---- 便捷宏 ---- */
#define LOG_E(...) do { if (hal_log_enabled(HAL_LOG_LVL_ERROR)) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_W(...) do { if (hal_log_enabled(HAL_LOG_LVL_WARN )) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_I(...) do { if (hal_log_enabled(HAL_LOG_LVL_INFO )) hal_log_printf(__VA_ARGS__); } while (0)
#define LOG_D(...) do { if (hal_log_enabled(HAL_LOG_LVL_DEBUG)) hal_log_printf(__VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* HAL_LOG_H */
