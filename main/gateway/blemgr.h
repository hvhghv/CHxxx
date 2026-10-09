/********************************** (C) COPYRIGHT *******************************
 * File Name          : blemgr.h
 * Description        : BLE 帧命令管理器（应用芯片 <-> CH592 BLE 物理层）
 *
 * 职责：
 *   - 解析 BLE 帧命令（注册/注销/发送/ACK）
 *   - 维护 owner 注册表（UUID 独占）
 *   - 维护逻辑连接（logical_id 单调递增，永不复用）
 *   - 处理连接事件（LINK_UP/DOWN + ACK）与订阅事件（NOTIFY_EN/DIS）
 *   - 数据透传：帧 <-> BLE
 *
 * 事件回推：回到该 owner 注册时所用的通道。
 *******************************************************************************/

#ifndef _BLEMGR_H
#define _BLEMGR_H

#include <stdint.h>
#include "framelink.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：注册帧命令处理器 + 绑定 bleapp 回调 */
void blemgr_init(void);

/* 主循环调用：处理 ACK 超时等 */
void blemgr_poll(void);

/* 测试辅助：模拟应用芯片注册 owner=0 的演示服务（RX 写 + TX 通知） */
int blemgr_test_reg(void);

#ifdef __cplusplus
}
#endif

#endif /* _BLEMGR_H */
