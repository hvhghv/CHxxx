/********************************** (C) COPYRIGHT *******************************
 * File Name          : swio.h
 * Description        : CH32V003 SWIO 单线烧录协议
 *
 * 通过一个 GPIO 引脚（软件模拟）按 WCH SWIO 单线协议烧录 CH32V003。
 *
 * 时序敏感：所有位操作在关中断下忙等，保证 μs 级精度。
 *******************************************************************************/

#ifndef _SWIO_H
#define _SWIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 烧录状态 */
typedef enum {
	SWIO_OK = 0,
	SWIO_ERR_NO_PIN,      /* 未配置 SWIO 引脚 */
	SWIO_ERR_HANDSHAKE,   /* 握手失败（无目标） */
	SWIO_ERR_UNLOCK,      /* Flash 解锁失败 */
	SWIO_ERR_ERASE,       /* 擦除失败 */
	SWIO_ERR_WRITE,       /* 写入失败 */
	SWIO_ERR_VERIFY,      /* 校验失败 */
} swio_status_t;

/* 设置 SWIO 引脚（pinmux 编号，如 PIN_PA(1)） */
void swio_set_pin(int pin);

/* 复位目标芯片（拉低 SWIO 一段时间） */
void swio_reset_target(void);

/* 握手：读 CH32V003 芯片 ID，返回 0 成功 */
int swio_handshake(uint32_t *chip_id);

/* 读目标芯片 ID（通过注入 RISC-V 指令），返回 0 成功 */
int swio_read_chip_id_pub(uint32_t *chip_id);

/* 读目标内存一个字 */
int swio_read_word(uint32_t addr, uint32_t *data);

/* 写目标内存一个字 */
int swio_write_word(uint32_t addr, uint32_t data);

/* 解锁 Flash */
int swio_flash_unlock(void);

/* 擦除 Flash（整片或指定范围） */
int swio_flash_erase(uint32_t addr, uint32_t len);

/* 写 Flash 数据（addr 需 64B 对齐，len 为 64 的倍数） */
int swio_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/* 校验 Flash（读回比对） */
int swio_flash_verify(uint32_t addr, const uint8_t *data, uint32_t len);

/* 完整烧录流程：擦除 + 写入 + 校验
 * 返回 swio_status_t */
int swio_flash_program(uint32_t addr, const uint8_t *data, uint32_t len);

/* 进度（0~100），供 HTTP 查询 */
int swio_progress(void);

/* ---------------------------------------------------------------------------
 * 流式烧录（HTTP 上传用，无需缓存整个固件）
 * ---------------------------------------------------------------------------
 * 用法：
 *   swio_stream_begin(addr, total_len)   // 握手 + 解锁 + 擦除
 *   swio_stream_data(buf, n)             // 反复调用，内部按 64B 页写入
 *   swio_stream_end()                    // 收尾（写最后不足页）
 * 返回 swio_status_t
 * ------------------------------------------------------------------------- */
int swio_stream_begin(uint32_t addr, uint32_t total_len);
int swio_stream_data(const uint8_t *data, uint32_t len);
int swio_stream_end(void);
int swio_stream_active(void);

#ifdef __cplusplus
}
#endif

#endif /* _SWIO_H */
