/**
 * @file    hal_flash_ch59x.c
 * @brief   CH59x Flash 实现（CH5xx 族通用，另见 ch572/ch585 版本）
 *
 * 底层：ROM 库的 FLASH_EEPROM_CMD()，通过 cmd 值区分操作：
 *   CMD_FLASH_ROM_ERASE  0x01  擦 Code Flash（4KB block）
 *   CMD_FLASH_ROM_WRITE  0x02  写 Code Flash（dword 对齐）
 *   CMD_EEPROM_ERASE     0x09  擦 Data Flash（256B page / 4KB block）
 *   CMD_EEPROM_WRITE     0x0A  写 Data Flash（任意字节）
 *   CMD_EEPROM_READ      0x0B  读 Data Flash
 *
 * 另外 FLASH_ROM_READ() 可直接读 Code Flash（走 ROM 库，比指针解引用安全）。
 *
 * ★ 地址约定
 *   ROM 库的 StartAddr 是「区域内的偏移」，不是绝对地址：
 *     Code Flash 偏移 0 = 程序起始
 *     Data Flash 偏移 0 = Data Flash 起始
 *   所以本层直接透传 addr，不需要加基址。
 *
 * ★ 擦除粒度
 *   Data Flash：EEPROM_MIN_ER_SIZE = EEPROM_PAGE_SIZE = 256
 *              也可用 4KB block（EEPROM_BLOCK_SIZE）一次擦完
 *   Code Flash：4KB block（EEPROM_BLOCK_SIZE）
 */

#include "hal_core.h"
#include "hal_flash.h"
#include "CH59x_common.h"
#include "ISP592.h"
#include <string.h>

/* ===========================================================================
 * 兜底宏（ISP592.h 已定义，这里防止版本差异）
 * =========================================================================== */
#ifndef CMD_FLASH_ROM_ERASE
#define CMD_FLASH_ROM_ERASE     0x01
#endif
#ifndef CMD_FLASH_ROM_WRITE
#define CMD_FLASH_ROM_WRITE     0x02
#endif
#ifndef CMD_EEPROM_ERASE
#define CMD_EEPROM_ERASE        0x09
#endif
#ifndef CMD_EEPROM_WRITE
#define CMD_EEPROM_WRITE        0x0A
#endif
#ifndef CMD_EEPROM_READ
#define CMD_EEPROM_READ         0x0B
#endif
#ifndef EEPROM_PAGE_SIZE
#define EEPROM_PAGE_SIZE        256
#endif
#ifndef EEPROM_BLOCK_SIZE
#define EEPROM_BLOCK_SIZE       4096
#endif
#ifndef EEPROM_MAX_SIZE
#define EEPROM_MAX_SIZE         0x8000u     /* 32KB Data Flash */
#endif
#ifndef EEPROM_MIN_ER_SIZE
#define EEPROM_MIN_ER_SIZE      EEPROM_PAGE_SIZE
#endif
#ifndef EEPROM_MIN_WR_SIZE
#define EEPROM_MIN_WR_SIZE      1
#endif
#ifndef FLASH_ROM_MAX_SIZE
#define FLASH_ROM_MAX_SIZE      0x070000u   /* 448KB */
#endif

/* ===========================================================================
 * 内部状态
 * =========================================================================== */
typedef struct {
    uint8_t  inited;
    uint8_t  region;
    uint32_t base;
    uint32_t size;
    uint8_t  allow_code_write;
} flash_ctx_t;

static flash_ctx_t s_flash;

/* ===========================================================================
 * 生命周期
 * =========================================================================== */
hal_err_t hal_flash_init(const hal_flash_cfg_t *cfg)
{
    memset(&s_flash, 0, sizeof(s_flash));

    if (cfg) {
        s_flash.region = (uint8_t)cfg->region;
        s_flash.base   = cfg->base;
        s_flash.size   = cfg->size;
        s_flash.allow_code_write = cfg->allow_code_write;
    } else {
        s_flash.region = (uint8_t)HAL_FLASH_REGION_DATA;
    }

    /* 默认长度 */
    if (!s_flash.size) {
        s_flash.size = (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA)
                       ? (uint32_t)EEPROM_MAX_SIZE
                       : (uint32_t)FLASH_ROM_MAX_SIZE;
    }

    /* 越界检查 */
    {
        uint32_t limit = (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA)
                         ? (uint32_t)EEPROM_MAX_SIZE
                         : (uint32_t)FLASH_ROM_MAX_SIZE;
        if ((uint64_t)s_flash.base + s_flash.size > (uint64_t)limit) {
            return HAL_ERR_PARAM;
        }
    }

    s_flash.inited = 1;
    return HAL_OK;
}

hal_err_t hal_flash_get_geo(hal_flash_geo_t *geo)
{
    if (!geo) return HAL_ERR_PARAM;

    geo->total_size = s_flash.size;

    if (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA) {
        geo->erase_size = EEPROM_MIN_ER_SIZE;   /* 256 */
        geo->write_size = EEPROM_MIN_WR_SIZE;   /* 1 */
        geo->block_size = EEPROM_BLOCK_SIZE;    /* 4096 */
    } else {
        geo->erase_size = EEPROM_BLOCK_SIZE;    /* Code Flash 只能 4KB 擦 */
        geo->write_size = 4;                    /* dword */
        geo->block_size = EEPROM_BLOCK_SIZE;
    }
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
    if (s_flash.region == (uint8_t)HAL_FLASH_REGION_CODE &&
        !s_flash.allow_code_write) {
        return HAL_ERR_UNSUP;       /* 未授权写代码区 */
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

    if (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA) {
        /* Data Flash 走 ROM 库 */
        if (FLASH_EEPROM_CMD(CMD_EEPROM_READ, addr, buf, len) != 0) {
            return HAL_ERR_IO;
        }
    } else {
        /* Code Flash 走 ROM 库的 FLASH_ROM_READ */
        FLASH_ROM_READ(addr, buf, len);
    }
    return HAL_OK;
}

/* ===========================================================================
 * 擦除
 * =========================================================================== */
hal_err_t hal_flash_erase(uint32_t addr, uint32_t len)
{
    hal_err_t e;
    uint32_t  erase_unit;

    if (!s_flash.inited) return HAL_ERR_NOINIT;

    /* len == 0 表示擦到区域末尾 */
    if (len == 0) {
        len = (s_flash.base + s_flash.size) - addr;
    }

    e = check_range(addr, len);
    if (e != HAL_OK) return e;

    /* 对齐检查 */
    erase_unit = (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA)
                 ? (uint32_t)EEPROM_MIN_ER_SIZE : (uint32_t)EEPROM_BLOCK_SIZE;

    if ((addr % erase_unit) || (len % erase_unit)) {
        return HAL_ERR_PARAM;
    }

    if (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA) {
        /* Data Flash：4KB 对齐时用 block 擦（快），否则逐 256B page 擦 */
        if (((len % EEPROM_BLOCK_SIZE) == 0) &&
            ((addr % EEPROM_BLOCK_SIZE) == 0)) {
            if (FLASH_EEPROM_CMD(CMD_EEPROM_ERASE, addr, NULL, len) != 0) {
                return HAL_ERR_IO;
            }
        } else {
            uint32_t off;
            for (off = 0; off < len; off += EEPROM_PAGE_SIZE) {
                if (FLASH_EEPROM_CMD(CMD_EEPROM_ERASE,
                                     addr + off, NULL,
                                     (uint32_t)EEPROM_PAGE_SIZE) != 0) {
                    return HAL_ERR_IO;
                }
            }
        }
    } else {
        if (FLASH_EEPROM_CMD(CMD_FLASH_ROM_ERASE, addr, NULL, len) != 0) {
            return HAL_ERR_IO;
        }
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

    if (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA) {
        /* Data Flash：任意字节，但 256 整页最快 */
        if (FLASH_EEPROM_CMD(CMD_EEPROM_WRITE, addr,
                             (void *)buf, len) != 0) {
            return HAL_ERR_IO;
        }
    } else {
        /* Code Flash：要求 dword 对齐 */
        if ((addr % 4) || (len % 4)) return HAL_ERR_PARAM;
        if (FLASH_EEPROM_CMD(CMD_FLASH_ROM_WRITE, addr,
                             (void *)buf, len) != 0) {
            return HAL_ERR_IO;
        }
    }
    return HAL_OK;
}

/* ===========================================================================
 * ROM 只读信息
 * =========================================================================== */
uint8_t hal_flash_read_uid(uint8_t *buf, uint8_t len)
{
    uint8_t uid[8];

    if (!buf || len < sizeof(uid)) return 0;

    GET_UNIQUE_ID(uid);
    memcpy(buf, uid, sizeof(uid));
    return (uint8_t)sizeof(uid);
}

hal_err_t hal_flash_read_mac(uint8_t mac[6])
{
    if (!mac) return HAL_ERR_PARAM;

    if (FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_MAC_ADDR, mac, 0) != 0) {
        return HAL_ERR_IO;
    }
    return HAL_OK;
}

/* ===========================================================================
 * 便捷：擦 + 写
 * =========================================================================== */
hal_err_t hal_flash_update(uint32_t addr, const void *buf, uint32_t len)
{
    hal_err_t e;
    uint32_t  erase_unit;
    uint32_t  erase_len;

    if (!buf || !len) return HAL_ERR_PARAM;

    erase_unit = (s_flash.region == (uint8_t)HAL_FLASH_REGION_DATA)
                 ? (uint32_t)EEPROM_MIN_ER_SIZE : (uint32_t)EEPROM_BLOCK_SIZE;

    /* 擦除长度向上取整到擦除单位 */
    erase_len = (len + erase_unit - 1) / erase_unit * erase_unit;

    e = hal_flash_erase(addr, erase_len);
    if (e != HAL_OK) return e;

    return hal_flash_write(addr, buf, len);
}
