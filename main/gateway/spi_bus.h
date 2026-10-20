/********************************** (C) COPYRIGHT *******************************
 * File Name          : spi_bus.h
 * Description        : SPI 多设备总线管理（主机/从机 + 多 CS/INT 引脚）
 *
 * 引脚配置（PIN_FUNC_SPI）：
 *   SCK/MOSI/MISO  : param2 = PIN_SPI_SCK/MOSI/MISO，param1 忽略
 *   CS（主机）      : param2 = PIN_SPI_CS，param1 = 设备号（软件 GPIO 输出）
 *   CS（从机）      : param2 = PIN_SPI_CS，param1 忽略（硬件 SCS 输入）
 *   INT            : param2 = PIN_SPI_INT，param1 = 设备号
 *                    flags bit0 = 有效电平（0=低有效，1=高有效）
 *                    arg1 = 触发方式（pin_irq_t）
 *   总线角色（主机/从机）由 SCK 引脚的 flags bit1 决定（0=主机，1=从机）
 *
 * 主机模式：INT 为输入（对端驱动），检测到有效电平 → 拉低 CS → 传输 → 释放 CS
 * 从机模式：INT 为输出（本机驱动），有数据时拉高通知主机；CS 由硬件 SCS 自动处理
 *
 * 长度前缀协议（双向对称，仅存在于 SPI 传输层，不进入 framelink）：
 *   [LEN_H][LEN_L][PAYLOAD ...]   LEN = 负载字节数（大端，最大 65535）
 *   从机发送：spibus_slave_send() 自动加前缀，主机 spibus_master_poll() 自动解析
 *   主机发送：spibus_master_send() 自动加前缀，从机 spibus_slave_poll() 自动解析
 *   传输时先发 2 字节长度，再连续传 LEN 字节；从机 FIFO 仅 8 字节，
 *   由 spibus_slave_poll() 在 FIFO 空时自动续传，INT 保持有效直到全部发完
 *******************************************************************************/

#ifndef _SPI_BUS_H
#define _SPI_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：扫描引脚配置，建立设备表（在 pinmux_apply_all 之后调用） */
void spibus_init(void);

/* 重新加载设备表（配置变更后调用） */
void spibus_reload(void);

/* 当前总线角色：0=主机，1=从机 */
int spibus_is_slave(void);

/* 设备数量 */
int spibus_dev_count(void);

/* 查询设备信息（返回 0 成功，-1 无此设备）
 *   cs_pin  : CS 引脚紧凑编号（-1 表示无）
 *   int_pin : INT 引脚紧凑编号（-1 表示无） */
int spibus_dev_info(int dev, int *cs_pin, int *int_pin);

/* 主机模式：轮询各设备 INT 线，有中断则自动 CS + 读取长度前缀 + 读满整帧
 *   协议：[LEN_H][LEN_L][PAYLOAD]，自动解析后回调 on_data(dev, payload, len)
 *   回调收到的只有 PAYLOAD，不含长度前缀
 * 返回处理的设备数 */
typedef void (*spibus_rx_cb_t)(int dev, const uint8_t *data, uint32_t len);
int spibus_master_poll(spibus_rx_cb_t on_data);

/* 主机模式：向指定设备传输（自动控制 CS）
 *   注意：本函数是裸传输，不处理长度前缀。一般请用 spibus_master_send()。
 * 返回传输字节数，<0 错误 */
int spibus_master_xfer(int dev, const uint8_t *tx, uint8_t *rx, uint32_t len);

/* 主机模式：向指定设备发送数据（自动加长度前缀 + 自动 CS）
 *   协议：[LEN_H][LEN_L][PAYLOAD]
 * 返回写入的负载字节数，<0 错误 */
int spibus_master_send(int dev, const uint8_t *data, uint32_t len);

/* 从机模式：设置 INT 线电平（通知主机有数据） */
void spibus_slave_set_int(int dev, int level);

/* 从机模式：发送数据到指定设备（自动加长度前缀 + 拉高 INT 通知主机）
 *   协议：[LEN_H][LEN_L][PAYLOAD]
 * 主机取走数据（发送 FIFO 空）后，spibus_slave_poll() 会自动拉低 INT
 * 返回写入的负载字节数，<0 错误 */
int spibus_slave_send(int dev, const uint8_t *data, uint32_t len);

/* 从机模式：轮询（自动解析主机发来的长度前缀，回调只给 PAYLOAD） */
int spibus_slave_poll(spibus_rx_cb_t on_data);

/* 主循环统一轮询：主机模式检测 INT 并收发；从机模式读 FIFO
 * 数据自动喂给 framelink（FRAMELINK_SPI 通道） */
void spi_bus_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* _SPI_BUS_H */
