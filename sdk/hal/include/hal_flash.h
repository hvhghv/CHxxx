/**
 * @file    hal_flash.h
 * @brief   Flash / ROM / Data-Flash 读写 —— 芯片无关
 *
 * 本层把三种芯片的存储访问统一成一套 API：
 *
 *   ┌──────────┬────────────────────────┬─────────────────────────────┐
 *   │ 芯片族   │ 底层接口               │ 说明                        │
 *   ├──────────┼────────────────────────┼─────────────────────────────┤
 *   │ CH59x    │ FLASH_EEPROM_CMD       │ Code Flash + Data Flash     │
 *   │ CH572    │ FLASH_EEPROM_CMD       │ 同上                        │
 *   │ CH585    │ FLASH_EEPROM_CMD       │ 同上                        │
 *   │ CH32x035 │ FLASH_ROM_ERASE/WRITE  │ Code Flash（无独立 Data）   │
 *   │ CH32L103 │ FLASH_ROM_ERASE/WRITE  │ 同上                        │
 *   │ CH32V20x │ FLASH_ROM_ERASE/WRITE  │ 同上                        │
 *   └──────────┴────────────────────────┴─────────────────────────────┘
 *
 * 概念区分：
 *   - Code Flash：程序存储区，擦除粒度大（CH5xx 4KB block，CH32 256B page）
 *   - Data Flash：CH5xx 独有，独立于 Code Flash，32KB，擦除粒度 256B/4KB
 *   - ROM 只读区：芯片出厂信息（MAC、UID、启动信息），只能读
 *
 * 典型用法（保存配置到 Data Flash）：
 *   hal_flash_cfg_t cfg = { .region = HAL_FLASH_REGION_DATA };
 *   hal_flash_init(&cfg);
 *
 *   uint8_t buf[64];
 *   hal_flash_read(0, buf, sizeof(buf));      // 读
 *   hal_flash_erase(0, 256);                  // 擦（256 对齐）
 *   hal_flash_write(0, buf, sizeof(buf));     // 写
 *
 * 典型用法（读芯片唯一 ID）：
 *   uint8_t uid[8];
 *   hal_flash_read_uid(uid);
 *
 * ★ 地址约定
 *   所有 API 的 addr 都是 **区域内偏移**（从 0 开始），不是绝对地址。
 *   绝对地址由 port 层按芯片转换为：
 *     CH5xx Data Flash  → 0x00000000 + offset（ROM 库内部映射）
 *     CH5xx Code Flash  → 0x00000000 + offset（ROM 库内部映射）
 *     CH32  Code Flash  → 0x08000000 + offset
 *   这样应用代码不需要关心各芯片的地址布局差异。
 */

#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 区域
 * =========================================================================== */
typedef enum {
    HAL_FLASH_REGION_CODE = 0,  /**< 程序存储区（慎用：会覆盖代码） */
    HAL_FLASH_REGION_DATA = 1,  /**< 数据区（CH5xx 独有；CH32 回落到 CODE） */
} hal_flash_region_t;

/* ===========================================================================
 * 配置
 * =========================================================================== */
typedef struct {
    /** 操作区域。默认 HAL_FLASH_REGION_DATA。 */
    hal_flash_region_t region;

    /**
     * 区域起始偏移（默认 0）。
     * 用于把数据区划出一段给应用，避免覆盖 bootloader / 校准数据。
     */
    uint32_t base;

    /**
     * 区域可用长度（字节）。0 = 用芯片默认上限。
     * CH5xx Data Flash 默认 0x8000（32KB）；
     * CH32 默认 CHIP_FLASH_SIZE。
     */
    uint32_t size;

    /**
     * 是否允许写 Code Flash（默认 0）。
     * ★ 置 1 会允许覆盖程序区，只在 bootloader / 自升级场景使用。
     */
    uint8_t  allow_code_write;
} hal_flash_cfg_t;

/* ===========================================================================
 * 几何信息（擦除/写入粒度）
 * =========================================================================== */
typedef struct {
    uint32_t total_size;    /**< 区域总字节数 */
    uint32_t erase_size;    /**< 最小擦除单位（CH5xx Data 256，CH32 256） */
    uint32_t write_size;    /**< 最小写入单位（CH5xx 1，CH32 4） */
    uint32_t block_size;    /**< 大块擦除单位（CH5xx 4096，CH32 0=不支持） */
} hal_flash_geo_t;

/* ===========================================================================
 * 生命周期
 * =========================================================================== */

/**
 * 初始化 Flash 层。
 * @param cfg  配置，NULL 用默认（DATA 区，偏移 0，全长度）
 * @return HAL_OK / HAL_ERR_PARAM / HAL_ERR_UNSUP
 */
hal_err_t hal_flash_init(const hal_flash_cfg_t *cfg);

/** 取几何信息（擦写粒度） */
hal_err_t hal_flash_get_geo(hal_flash_geo_t *geo);

/* ===========================================================================
 * 读
 * =========================================================================== */

/**
 * 读数据。
 * @param addr  区域内偏移
 * @param buf   输出缓冲
 * @param len   字节数
 * @return HAL_OK / HAL_ERR_PARAM / HAL_ERR_NOINIT
 *
 * 注意：读不需要擦除，也不要求对齐。
 */
hal_err_t hal_flash_read(uint32_t addr, void *buf, uint32_t len);

/* ===========================================================================
 * 擦除
 * =========================================================================== */

/**
 * 擦除一段区域。
 * @param addr  区域内偏移，必须按 erase_size 对齐
 * @param len   字节数，必须按 erase_size 对齐（0 = 擦到区域末尾）
 * @return HAL_OK / HAL_ERR_PARAM / HAL_ERR_NOINIT / HAL_ERR_IO
 *
 * ★ 擦除后该区域全部变为 0xFF。
 * ★ CH32 的 FLASH_ROM_ERASE 要求 addr 和 len 都是 256 的倍数。
 */
hal_err_t hal_flash_erase(uint32_t addr, uint32_t len);

/** 擦除整片（慎用，会清掉所有数据） */
hal_err_t hal_flash_erase_all(void);

/* ===========================================================================
 * 写
 * =========================================================================== */

/**
 * 写数据（必须先擦除，只能把 1 写成 0）。
 * @param addr  区域内偏移，必须按 write_size 对齐
 * @param buf   源数据
 * @param len   字节数，必须按 write_size 对齐
 * @return HAL_OK / HAL_ERR_PARAM / HAL_ERR_NOINIT / HAL_ERR_IO
 *
 * ★ CH5xx Data Flash 的 EEPROM_WRITE 支持任意字节数，但按 256 字节整页写最快。
 * ★ CH32 的 FLASH_ROM_WRITE 要求 addr 和 len 都是 4 的倍数。
 */
hal_err_t hal_flash_write(uint32_t addr, const void *buf, uint32_t len);

/* ===========================================================================
 * ROM 只读信息
 * =========================================================================== */

/**
 * 读芯片唯一 ID。
 * @param buf  输出缓冲
 * @param len  缓冲大小（CH5xx 需 8 字节，CH32 需 12 字节）
 * @return 实际写入字节数，0 表示不支持
 */
uint8_t hal_flash_read_uid(uint8_t *buf, uint8_t len);

/**
 * 读芯片出厂 MAC 地址（6 字节）。
 * @return HAL_OK / HAL_ERR_UNSUP（CH32 无出厂 MAC）
 */
hal_err_t hal_flash_read_mac(uint8_t mac[6]);

/* ===========================================================================
 * 便捷：整块读写（带自动对齐补齐）
 * =========================================================================== */

/**
 * 擦除 + 写入（erase_size 对齐的整块操作）。
 * @param addr  区域内偏移，必须按 erase_size 对齐
 * @param buf   源数据
 * @param len   字节数（不足 erase_size 的部分自动补 0xFF）
 * @return HAL_OK / 错误码
 *
 * 适合保存配置结构体这类小数据。
 */
hal_err_t hal_flash_update(uint32_t addr, const void *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
