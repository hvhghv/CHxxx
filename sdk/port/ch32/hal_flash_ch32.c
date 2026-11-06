/**
 * @file    hal_flash_ch32.c
 * @brief   CH32（X035 / L103 / V20x）Flash 实现
 *
 * 与 CH5xx 的差异：
 *   - 没有独立 Data Flash，只有 Code Flash（程序区）
 *   - 没有 FLASH_ROM_READ，读靠内存映射指针（0x08000000 + offset）
 *   - 擦除用 FLASH_ROM_ERASE(addr, len)，addr/len 必须 256 对齐
 *   - 写入用 FLASH_ROM_WRITE(addr, pbuf, len)，addr/len 必须 4 对齐
 *
 * ★ 地址约定
 *   本层对外用「区域内偏移」（从 0 开始），内部转换为绝对地址：
 *     绝对地址 = FLASH_BASE + offset
 *   这样应用代码和 CH5xx 的用法一致。
 *
 * ★ 安全
 *   默认 allow_code_write = 0，此时拒绝所有写/擦操作。
 *   因为 CH32 没有独立数据区，写 Flash 就是写程序区，
 *   误操作会直接破坏固件。需要保存配置时请显式开启并选好偏移。
 */

#include "hal_core.h"
#include "hal_flash.h"
#include <string.h>

/* ===========================================================================
 * 芯片头文件（由 CHIP_DEFINES 选择）
 * ---------------------------------------------------------------------------
 * 本 port 层同时支持 CH32X035 / CH32L103 / CH32V20x。
 * =========================================================================== */
#if defined(CH32X035)
  #include "ch32x035.h"
  #define CH32_FLASH_SIZE_BYTES   (62u * 1024u)
#elif defined(CH32L103)
  #include "ch32l103.h"
  #define CH32_FLASH_SIZE_BYTES   (64u * 1024u)
#elif defined(CH32V20x)
  #include "ch32v20x.h"
  #define CH32_FLASH_SIZE_BYTES   (128u * 1024u)
#else
  #error "未定义 CH32 芯片宏（CH32X035 / CH32L103 / CH32V20x）"
#endif

/* 擦除/写入粒度（三颗 CH32 都是 256B 页 / 4B 字） */
#define CH32_ERASE_SIZE     256u
#define CH32_WRITE_SIZE     4u

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
typedef struct {
    uint8_t  inited;
    uint32_t base;          /* 区域内起始偏移 */
    uint32_t size;          /* 区域可用长度 */
    uint8_t  allow_write;
} flash_ctx_t;

static flash_ctx_t s_flash;

/* 区域内偏移 → 绝对地址 */
static uint32_t to_abs(uint32_t off)
{
    return (uint32_t)(FLASH_BASE + off);
}

/* ===========================================================================
 * 生命周期
 * =========================================================================== */
hal_err_t hal_flash_init(const hal_flash_cfg_t *cfg)
{
    memset(&s_flash, 0, sizeof(s_flash));

    if (cfg) {
        s_flash.base        = cfg->base;
        s_flash.size        = cfg->size;
        s_flash.allow_write = cfg->allow_code_write;
    }

    if (!s_flash.size) {
        s_flash.size = CH32_FLASH_SIZE_BYTES;
    }

    /* 越界检查 */
    if ((uint64_t)s_flash.base + s_flash.size >
        (uint64_t)CH32_FLASH_SIZE_BYTES) {
        return HAL_ERR_PARAM;
    }

    s_flash.inited = 1;
    return HAL_OK;
}

hal_err_t hal_flash_get_geo(hal_flash_geo_t *geo)
{
    if (!geo) return HAL_ERR_PARAM;

    geo->total_size = s_flash.size;
    geo->erase_size = CH32_ERASE_SIZE;      /* 256 */
    geo->write_size = CH32_WRITE_SIZE;      /* 4 */
    geo->block_size = 0;                    /* 无大块擦除 */
    return HAL_OK;
}

/* ===========================================================================
 * 范围检查
 * =========================================================================== */
static hal_err_t check_range(uint32_t addr, uint32_t len)
{
    if (!s_flash.inited) return HAL_ERR_NOINIT;
    if (addr < s_flash.base) return HAL_ERR_PARAM;
    if (len == 0) return HAL_ERR_PARAM;
    if ((uint64_t)addr + len > (uint64_t)s_flash.base + s_flash.size) {
        return HAL_ERR_PARAM;
    }
    return HAL_OK;
}

/* ===========================================================================
 * 读
 * =========================================================================== */
hal_err_t hal_flash_read(uint32_t addr, void *buf, uint32_t len)
{
    hal_err_t e = check_range(addr, len);
    if (e != HAL_OK) return e;
    if (!buf) return HAL_ERR_PARAM;

    /* CH32 没有 FLASH_ROM_READ，直接内存映射读 */
    memcpy(buf, (const void *)to_abs(addr), len);
    return HAL_OK;
}

/* ===========================================================================
 * 擦除
 * =========================================================================== */
hal_err_t hal_flash_erase(uint32_t addr, uint32_t len)
{
    hal_err_t e;

    if (!s_flash.inited) return HAL_ERR_NOINIT;
    if (!s_flash.allow_write) return HAL_ERR_UNSUP;

    /* len == 0 表示擦到区域末尾 */
    if (len == 0) {
        len = (s_flash.base + s_flash.size) - addr;
    }

    e = check_range(addr, len);
    if (e != HAL_OK) return e;

    /* 256 字节对齐 */
    if ((addr % CH32_ERASE_SIZE) || (len % CH32_ERASE_SIZE)) {
        return HAL_ERR_PARAM;
    }

    if (FLASH_ROM_ERASE(to_abs(addr), len) != FLASH_COMPLETE) {
        return HAL_ERR_IO;
    }
    return HAL_OK;
}

hal_err_t hal_flash_erase_all(void)
{
    if (!s_flash.inited) return HAL_ERR_NOINIT;
    return hal_flash_erase(s_flash.base, s_flash.size);
}

/* ===========================================================================
 * 写
 * =========================================================================== */
hal_err_t hal_flash_write(uint32_t addr, const void *buf, uint32_t len)
{
    hal_err_t e = check_range(addr, len);
    if (e != HAL_OK) return e;
    if (!buf) return HAL_ERR_PARAM;
    if (!s_flash.allow_write) return HAL_ERR_UNSUP;

    /* 4 字节对齐 */
    if ((addr % CH32_WRITE_SIZE) || (len % CH32_WRITE_SIZE)) {
        return HAL_ERR_PARAM;
    }

    /* FLASH_ROM_WRITE 的 pbuf 是非 const 的 uint32_t* */
    if (FLASH_ROM_WRITE(to_abs(addr), (uint32_t *)(uintptr_t)buf, len)
        != FLASH_COMPLETE) {
        return HAL_ERR_IO;
    }
    return HAL_OK;
}

/* ===========================================================================
 * ROM 只读信息
 * =========================================================================== */

/**
 * CH32 的 UID 在系统存储区（0x1FFFF7E8 附近，各系列不同）。
 * X035/L103/V20x 均在 0x1FFFF7E8 起 12 字节（96 位）。
 */
#define CH32_UID_ADDR   0x1FFFF7E8u
#define CH32_UID_LEN    12u

uint8_t hal_flash_read_uid(uint8_t *buf, uint8_t len)
{
    if (!buf || len < CH32_UID_LEN) return 0;

    memcpy(buf, (const void *)CH32_UID_ADDR, CH32_UID_LEN);
    return (uint8_t)CH32_UID_LEN;
}

hal_err_t hal_flash_read_mac(uint8_t mac[6])
{
    (void)mac;
    return HAL_ERR_UNSUP;      /* CH32 无出厂 MAC */
}

/* ===========================================================================
 * 便捷：擦 + 写
 * =========================================================================== */
hal_err_t hal_flash_update(uint32_t addr, const void *buf, uint32_t len)
{
    hal_err_t e;
    uint32_t  erase_len;

    if (!buf || !len) return HAL_ERR_PARAM;

    /* 擦除长度向上取整到 256 */
    erase_len = (len + CH32_ERASE_SIZE - 1) / CH32_ERASE_SIZE * CH32_ERASE_SIZE;

    e = hal_flash_erase(addr, erase_len);
    if (e != HAL_OK) return e;

    return hal_flash_write(addr, buf, len);
}
