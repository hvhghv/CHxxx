/**
 * @file    hal_irq_ch585.c
 * @brief   CH585 中断管理实现（影子副本方案）
 *
 * ★ 核心问题
 * ---------------------------------------------------------------------------
 * CH585 的 PFIC->IENR[] / IRER[] / IPSR[] / IPRR[] 全部是【只写】寄存器，
 * 读回恒为 0。因此无法用「读-改-写」。
 *
 * 而官方 core_riscv.h 的 NVIC_EnableIRQ 定义为：
 *     PFIC->IENR[(n) >> 5] = 1 << ((n) & 0x1F);     ← 赋值！
 * 任何一次调用都会把同一 32 位字里的其它中断使能位清零。
 *
 * 实例：fsusb.c 调用 NVIC_EnableIRQ(USB_IRQn=22) 后，
 *       IENR[0] 变成 1<<22，BB(20)/LLE(21) 的使能位被清掉，
 *       BLE 协议栈收不到链路层中断，射频永不发包 —— 手机扫不到设备。
 *
 * ★ 解决方案
 * ---------------------------------------------------------------------------
 * 在 RAM 中维护影子副本 s_ienr_shadow[8]。所有使能/禁用操作只改影子副本，
 * 然后整体回写 PFIC->IENR[0..7]。因为 IENR 是只写，整体回写是安全的。
 *
 * 这样无论多少个模块调用 hal_irq_enable()，彼此都不会干扰。
 */
#include "hal_core.h"
#include "CH58x_common.h"      /* PFIC 结构体定义与 IRQn 枚举 */
#include <string.h>

/* ===========================================================================
 * 影子副本
 * =========================================================================== */
#define PFIC_WORDS      8
static uint32_t s_ienr_shadow[PFIC_WORDS];

/* 已 attach 的 C 处理函数表
 *
 * ★ RAM 优化：CH585 的 IRQn 最大到 31（见 CH58x_common.h 的 IRQn_Type），
 *   原来固定 64 项浪费一半 RAM（64×8=512B → 32×8=256B）。
 *   可用 HAL_IRQ_MAX_ENTRIES 覆盖（最小 32 即可覆盖全部 IRQn）。 */
#ifndef HAL_IRQ_MAX_ENTRIES
#define HAL_IRQ_MAX_ENTRIES 32
#endif
typedef struct {
    hal_isr_t isr;
    void     *user;
} isr_entry_t;
static isr_entry_t s_isr_table[HAL_IRQ_MAX_ENTRIES];

/* ===========================================================================
 * 内部：整体回写 IENR
 * ---------------------------------------------------------------------------
 * IENR 是只写寄存器，整体写 8 个字是安全的（不会误清其它位，
 * 因为影子副本里保存了完整状态）。
 * =========================================================================== */
static void pfic_sync_ienr(void)
{
    int i;
    for (i = 0; i < PFIC_WORDS; i++) {
        PFIC->IENR[i] = s_ienr_shadow[i];
    }
    __asm__ volatile(".word 0x0000100F" ::: "memory");   /* fence.i */
}

/* ===========================================================================
 * 使能 / 禁用
 * =========================================================================== */
void hal_irq_enable(int irqn)
{
    uint32_t n = (uint32_t)irqn;
    if (n >= PFIC_WORDS * 32u) return;
    s_ienr_shadow[n >> 5] |= (1u << (n & 0x1Fu));
    pfic_sync_ienr();
}

void hal_irq_disable(int irqn)
{
    uint32_t n = (uint32_t)irqn;
    if (n >= PFIC_WORDS * 32u) return;
    s_ienr_shadow[n >> 5] &= ~(1u << (n & 0x1Fu));
    pfic_sync_ienr();
}

uint8_t hal_irq_is_enabled(int irqn)
{
    uint32_t n = (uint32_t)irqn;
    if (n >= PFIC_WORDS * 32u) return 0;
    return (s_ienr_shadow[n >> 5] >> (n & 0x1Fu)) & 1u;
}

/* ===========================================================================
 * 优先级 / pending
 * =========================================================================== */
void hal_irq_set_priority(int irqn, uint8_t prio)
{
    /* CH585 只支持 2 级优先级：0 = 低，0x80 = 高（bit7） */
    PFIC->IPRIOR[(uint32_t)irqn] = (prio <= HAL_IRQ_PRIO_HIGH) ? 0x80 : 0x00;
    __asm__ volatile(".word 0x0000100F" ::: "memory");
}

void hal_irq_set_pending(int irqn)
{
    uint32_t n = (uint32_t)irqn;
    if (n >= PFIC_WORDS * 32u) return;
    PFIC->IPSR[n >> 5] = (1u << (n & 0x1Fu));
}

void hal_irq_clear_pending(int irqn)
{
    uint32_t n = (uint32_t)irqn;
    if (n >= PFIC_WORDS * 32u) return;
    PFIC->IPRR[n >> 5] = (1u << (n & 0x1Fu));
}

uint32_t hal_irq_get_pending_mask(uint8_t word_idx)
{
    if (word_idx >= PFIC_WORDS) return 0;
    return PFIC->ISR[word_idx];      /* ISR 可读 */
}

/* ===========================================================================
 * 全局中断开关
 * ---------------------------------------------------------------------------
 * 注意：disable/enable 不是简单配对，enable 会无条件打开中断。
 * 如果需要「保存-恢复」语义，用下面的 hal_irq_save / hal_irq_restore。
 * =========================================================================== */
void hal_irq_global_enable(void)
{
    /* 用官方 __enable_irq 语义：csrrs mstatus, 0x88（同时置 MIE 和 MPIE）
     * 注意：不能用 csrsi mstatus, 0x8 —— 在 CH585 上实测会导致死锁。 */
    __asm__ volatile("csrrs zero, 0x800, %0" :: "r"(0x88u) : "memory");
}

void hal_irq_global_disable(void)
{
    /* 用官方 __disable_irq 语义：csrrc mstatus, 0x88 */
    __asm__ volatile("csrrc zero, 0x800, %0" :: "r"(0x88u) : "memory");
}

uint8_t hal_irq_global_is_enabled(void)
{
    uint32_t ms;
    __asm__ volatile("csrr %0, 0x800" : "=r"(ms));
    return (ms >> 3) & 1u;
}

/* 保存并关闭全局中断，返回之前的状态 */
uint32_t hal_irq_save(void)
{
    uint32_t ms;
    /* 只操作 MIE(bit3)，不碰 MPIE(bit7)/MPP：
     * 用 csrrc 一条指令完成「读旧值 + 清 MIE」，避免中间被中断。 */
    __asm__ volatile("csrrc %0, 0x800, %1" : "=r"(ms) : "r"(0x8u) : "memory");
    return ms;
}

/* 恢复到 hal_irq_save 返回的状态 */
void hal_irq_restore(uint32_t state)
{
    if (state & 0x8u) {
        __asm__ volatile("csrrs zero, 0x800, %0" :: "r"(0x8u) : "memory");
    } else {
        __asm__ volatile("csrrc zero, 0x800, %0" :: "r"(0x8u) : "memory");
    }
}

/* ===========================================================================
 * C 处理函数注册
 * =========================================================================== */
hal_err_t hal_irq_attach(int irqn, hal_isr_t isr, void *user)
{
    if (irqn < 0 || irqn >= HAL_IRQ_MAX_ENTRIES) return HAL_ERR_PARAM;
    if (!isr) return HAL_ERR_PARAM;
    s_isr_table[irqn].isr  = isr;
    s_isr_table[irqn].user = user;
    return HAL_OK;
}

hal_err_t hal_irq_detach(int irqn)
{
    if (irqn < 0 || irqn >= HAL_IRQ_MAX_ENTRIES) return HAL_ERR_PARAM;
    s_isr_table[irqn].isr  = NULL;
    s_isr_table[irqn].user = NULL;
    return HAL_OK;
}

/* 由汇编 trampoline 调用
 * ★ 关键：必须放在 .highcode（RAM）段！
 *   中断入口在 RAM，如果处理函数在 Flash，RAM→Flash 的调用在中断
 *   上下文下会导致死机（Flash 访问时序与中断上下文冲突）。
 *   官方 BLE 的 LLE_IRQLibHandler 也全部在 .highcode。 */
__attribute__((section(".highcode")))
void hal_irq_dispatch(int irqn)
{
    if (irqn < 0 || irqn >= HAL_IRQ_MAX_ENTRIES) return;
    /* 防御：中断可能在 attach 之前就触发（如 SysTick），
     * 此时 isr 为 NULL，直接返回，不能跳 NULL 死机。 */
    if (s_isr_table[irqn].isr) {
        s_isr_table[irqn].isr(s_isr_table[irqn].user);
    }
}

/* ===========================================================================
 * 诊断
 * =========================================================================== */
void hal_irq_get_shadow(uint32_t out[8])
{
    if (!out) return;
    memcpy(out, s_ienr_shadow, sizeof(s_ienr_shadow));
}
