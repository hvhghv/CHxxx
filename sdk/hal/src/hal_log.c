/**
 * @file    hal_log.c
 * @brief   日志核心实现（后端可切换）
 *
 * 设计：
 *   - 所有输出先格式化到栈上缓冲，再交给当前 sink
 *   - sink 可运行时切换（UART / USB CDC / EEPROM / RAM / 自定义）
 *   - 不依赖芯片，纯通用层实现
 *
 * ★ RAM 优化：不使用 newlib 的 vsnprintf（会拖进 __sf / malloc / locale
 *   等大量 RAM），改用自带的轻量格式化，支持 %s %d %u %x %c %p %%。
 */
#include "hal_log.h"
#include "hal_uart.h"
#include <string.h>
#include <stdarg.h>

/* ★ RAM 优化：格式化缓冲与 RAM 后端缓冲都可编译期覆盖。
 *   LOG_RAM_BUF_SIZE=0 时完全关闭 RAM 后端（省 256B RAM）。 */
#ifndef LOG_FMT_BUF_SIZE
#define LOG_FMT_BUF_SIZE   128
#endif
#ifndef LOG_RAM_BUF_SIZE
#define LOG_RAM_BUF_SIZE   256
#endif

static hal_log_sink_t   s_sink = HAL_LOG_SINK_NONE;
static void            *s_sink_arg = NULL;
static hal_log_level_t  s_level = HAL_LOG_LVL_INFO;

/* RAM 后端环形缓冲（LOG_RAM_BUF_SIZE=0 时不分配） */
#if LOG_RAM_BUF_SIZE > 0
static uint8_t  s_ram_buf[LOG_RAM_BUF_SIZE];
#endif
static uint16_t s_ram_head = 0;
static uint16_t s_ram_tail = 0;

/* ===========================================================================
 * 轻量格式化（避免 newlib）
 * =========================================================================== */
static int fmt_uint(char *out, uint32_t v, uint8_t base, uint8_t upper,
                    uint8_t width, char pad)
{
    char tmp[12];
    int  n = 0, i, len;

    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v) {
            uint8_t d = (uint8_t)(v % base);
            tmp[n++] = (char)(d < 10 ? ('0' + d)
                                     : ((upper ? 'A' : 'a') + d - 10));
            v /= base;
        }
    }
    len = n;
    while (len < width) { *out++ = pad; width--; }
    for (i = n - 1; i >= 0; i--) *out++ = tmp[i];
    return n > len ? n : len;
}

static int log_vformat(char *buf, int size, const char *fmt, va_list ap)
{
    int pos = 0;

    #define PUT(c) do { if (pos < size - 1) buf[pos++] = (char)(c); } while (0)

    while (*fmt && pos < size - 1) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;

        /* 解析宽度 */
        uint8_t width = 0;
        char    pad   = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = (uint8_t)(width * 10 + (*fmt - '0')); fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) { PUT('-'); v = -v; }
            pos += fmt_uint(buf + pos, (uint32_t)v, 10, 0, width, pad);
            break;
        }
        case 'u':
            pos += fmt_uint(buf + pos, va_arg(ap, uint32_t), 10, 0, width, pad);
            break;
        case 'x':
            pos += fmt_uint(buf + pos, va_arg(ap, uint32_t), 16, 0, width, pad);
            break;
        case 'X':
            pos += fmt_uint(buf + pos, va_arg(ap, uint32_t), 16, 1, width, pad);
            break;
        case 'c':
            PUT((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) buf[pos++] = *s++;
            break;
        }
        case 'p':
            PUT('0'); PUT('x');
            pos += fmt_uint(buf + pos, (uint32_t)(uintptr_t)va_arg(ap, void *),
                            16, 0, 8, '0');
            break;
        case '%':
            PUT('%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            PUT('%');
            PUT(*fmt);
            break;
        }
        if (*fmt) fmt++;
    }
    buf[pos] = '\0';
    #undef PUT
    return pos;
}

/* ===========================================================================
 * 初始化
 * =========================================================================== */
hal_err_t hal_log_init(void)
{
    s_sink = HAL_LOG_SINK_NONE;
    s_sink_arg = NULL;
    s_level = HAL_LOG_LVL_INFO;
    s_ram_head = s_ram_tail = 0;
    return HAL_OK;
}

/* ===========================================================================
 * 后端管理
 * =========================================================================== */
hal_err_t hal_log_set_sink(hal_log_sink_t sink, void *arg)
{
    switch (sink) {
    case HAL_LOG_SINK_NONE:
    case HAL_LOG_SINK_USB_CDC:
    case HAL_LOG_SINK_RAM:
        s_sink_arg = NULL;
        break;
    case HAL_LOG_SINK_UART:
        if (!arg) return HAL_ERR_PARAM;
        s_sink_arg = arg;
        break;
    case HAL_LOG_SINK_EEPROM:
    case HAL_LOG_SINK_CUSTOM:
        if (!arg) return HAL_ERR_PARAM;
        s_sink_arg = arg;
        break;
    default:
        return HAL_ERR_PARAM;
    }
    s_sink = sink;
    return HAL_OK;
}

hal_log_sink_t hal_log_get_sink(void)
{
    return s_sink;
}

void hal_log_set_level(hal_log_level_t lvl)
{
    s_level = lvl;
}

hal_log_level_t hal_log_get_level(void)
{
    return s_level;
}

uint8_t hal_log_enabled(hal_log_level_t lvl)
{
    return (s_level != HAL_LOG_LVL_NONE) && (lvl <= s_level);
}

/* ===========================================================================
 * 底层输出
 * =========================================================================== */
static void ram_put(const uint8_t *data, uint16_t len)
{
#if LOG_RAM_BUF_SIZE > 0
    uint16_t i;
    for (i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((s_ram_head + 1) % LOG_RAM_BUF_SIZE);
        if (next == s_ram_tail) {
            /* 满：丢弃最旧的一个字节 */
            s_ram_tail = (uint16_t)((s_ram_tail + 1) % LOG_RAM_BUF_SIZE);
        }
        s_ram_buf[s_ram_head] = data[i];
        s_ram_head = next;
    }
#else
    /* LOG_RAM_BUF_SIZE=0：RAM 后端关闭，直接丢弃 */
    (void)data;
    (void)len;
#endif
}

void hal_log_write(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;

    switch (s_sink) {
    case HAL_LOG_SINK_UART: {
        hal_uart_t *u = (hal_uart_t *)s_sink_arg;
        if (u && u->dev.initialized) {
            u->write(u, data, len);
        }
        break;
    }
    case HAL_LOG_SINK_USB_CDC:
        /* 由 USB 层提供弱符号实现，未链接 USB 时为 no-op。
         * s_sink_arg 可以是 hal_usb_cdc_handle_t 转成的指针，
         * NULL 表示用句柄 1（第一个 CDC 实例）。 */
        {
            extern int hal_usb_cdc_log_write(uint8_t h,
                                             const uint8_t *d, uint16_t n)
                __attribute__((weak));
            if (hal_usb_cdc_log_write) {
                uint8_t h = s_sink_arg ? (uint8_t)(uintptr_t)s_sink_arg : 1;
                hal_usb_cdc_log_write(h, data, len);
            }
        }
        break;
    case HAL_LOG_SINK_EEPROM:
        /* 简化：直接追加写（实际使用建议加环形管理） */
        {
            extern uint32_t hal_flash_log_append(uint32_t addr,
                                                 const uint8_t *d, uint16_t n)
                __attribute__((weak));
            if (hal_flash_log_append) {
                hal_flash_log_append(*(uint32_t *)s_sink_arg, data, len);
            }
        }
        break;
    case HAL_LOG_SINK_CUSTOM:
        {
            hal_log_write_fn fn = (hal_log_write_fn)s_sink_arg;
            if (fn) fn(data, len, NULL);
        }
        break;
    case HAL_LOG_SINK_RAM:
        ram_put(data, len);
        break;
    default:
        break;
    }
}

/* ===========================================================================
 * 格式化输出
 * =========================================================================== */
void hal_log_printf(const char *fmt, ...)
{
    char buf[LOG_FMT_BUF_SIZE];
    va_list ap;
    int n;

    if (!fmt) return;

    va_start(ap, fmt);
    n = log_vformat(buf, (int)sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    hal_log_write((const uint8_t *)buf, (uint16_t)n);
}

void hal_log_hexdump(const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    char line[80];
    uint16_t i, j;
    int pos;

    if (!p) return;

    for (i = 0; i < len; i += 16) {
        pos = 0;
        pos += fmt_uint(line + pos, i, 16, 1, 4, '0');
        line[pos++] = ':'; line[pos++] = ' ';
        for (j = 0; j < 16 && (i + j) < len; j++) {
            pos += fmt_uint(line + pos, p[i + j], 16, 1, 2, '0');
            line[pos++] = ' ';
        }
        line[pos++] = '\r'; line[pos++] = '\n';
        hal_log_write((const uint8_t *)line, (uint16_t)pos);
    }
}

/* ===========================================================================
 * RAM 后端读取
 * =========================================================================== */
uint16_t hal_log_ram_read(uint8_t *buf, uint16_t max_len)
{
#if LOG_RAM_BUF_SIZE > 0
    uint16_t n = 0;
    if (!buf) return 0;

    while (n < max_len && s_ram_tail != s_ram_head) {
        buf[n++] = s_ram_buf[s_ram_tail];
        s_ram_tail = (uint16_t)((s_ram_tail + 1) % LOG_RAM_BUF_SIZE);
    }
    return n;
#else
    (void)buf;
    (void)max_len;
    return 0;
#endif
}

void hal_log_ram_clear(void)
{
    s_ram_head = s_ram_tail = 0;
}
