/**
 * @file    hal_irq.h
 * @brief   中断管理（安全封装，解决 IENR 只写 + 赋值语义问题）
 *
 * 背景：CH59x 的 PFIC->IENR / IRER 是【只写】寄存器，读回恒为 0。
 * 而官方 NVIC_EnableIRQ(n) 展开为 IENR[n>>5] = 1<<(n&31)，是【赋值】，
 * 会清掉同一 32 位字里的其它中断使能位。
 *
 * 本接口在 port 层用 RAM 影子副本维护使能状态，所有操作改影子副本后
 * 整体回写 IENR，绝不破坏其它中断。
 */
#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 中断优先级（数值越小优先级越高，具体范围由芯片决定） */
#define HAL_IRQ_PRIO_HIGHEST   0
#define HAL_IRQ_PRIO_HIGH      1
#define HAL_IRQ_PRIO_NORMAL    2
#define HAL_IRQ_PRIO_LOW       3

/**
 * 安全使能中断：不会影响同字其它中断。
 * @param irqn  芯片中断号（如 CH59x 的 BB_IRQn=20 / LLE_IRQn=21）
 */
void hal_irq_enable(int irqn);

/** 安全禁用中断：不会影响同字其它中断 */
void hal_irq_disable(int irqn);

/** 查询某中断是否已使能（读影子副本） */
uint8_t hal_irq_is_enabled(int irqn);

/** 设置优先级 */
void hal_irq_set_priority(int irqn, uint8_t prio);

/** 手动置位 / 清除 pending */
void hal_irq_set_pending(int irqn);
void hal_irq_clear_pending(int irqn);

/** 读取 pending 状态（ISR 可读）。word_idx = irqn >> 5 */
uint32_t hal_irq_get_pending_mask(uint8_t word_idx);

/** 全局中断开关 */
void hal_irq_global_enable(void);
void hal_irq_global_disable(void);
uint8_t hal_irq_global_is_enabled(void);

/**
 * 保存并关闭全局中断，返回之前的状态。
 * 用于保护 SYS_SAFE_ACCESS 序列（如 GPIOA_ModeCfg），
 * 避免被中断打断导致寄存器配置失败。
 * 用法：uint32_t st = hal_irq_save(); ...关键代码...; hal_irq_restore(st);
 */
uint32_t hal_irq_save(void);
void     hal_irq_restore(uint32_t state);

/**
 * 注册 C 语言中断处理函数。
 * port 层会用汇编 trampoline 包装，保证寄存器完整保存（x1/x4-x31）。
 * 同一个 irqn 重复 attach 会覆盖。
 *
 * @param irqn  中断号
 * @param isr   处理函数（在中断上下文执行）
 * @param user  用户数据
 */
typedef void (*hal_isr_t)(void *user);
hal_err_t hal_irq_attach(int irqn, hal_isr_t isr, void *user);

/** 解除注册 */
hal_err_t hal_irq_detach(int irqn);

/** port 层内部使用：由汇编 trampoline 调用 */
void hal_irq_dispatch(int irqn);

/**
 * 诊断：打印/返回影子副本内容（调试用）
 * @param out  长度 >= 8 的数组，返回影子 IENR
 */
void hal_irq_get_shadow(uint32_t out[8]);

#ifdef __cplusplus
}
#endif

#endif /* HAL_IRQ_H */
