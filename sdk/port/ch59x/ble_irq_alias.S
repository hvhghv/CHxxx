/**
 * @file    ble_irq_alias.S
 * @brief   让 BB_IRQHandler 复用 LLE_IRQHandler
 *
 * ★ 背景
 * ---------------------------------------------------------------------------
 * 官方 BLE_USB 例程的 hex 里，向量表 IRQ20(BB) 和 IRQ21(LLE) 指向同一个地址
 * （.highcode 偏移 0x90），说明官方把 BB 中断和 LLE 中断交给同一个处理函数。
 *
 * CH59x 的 BLE 射频中断分 BB(基带) 和 LLE(链路层) 两个 IRQn，
 * 但 libCH59xBLE.a 只提供 LLE_IRQHandler（内部会一并处理 BB 事件）。
 * 如果 BB_IRQHandler 落到 startup 的 weak 默认处理（j 1b 死循环），
 * 一旦射频中断触发就会卡死整个系统，表现为 BLE 不广播、USB 不枚举。
 *
 * 这里用汇编别名把 BB_IRQHandler 直接指向 LLE_IRQHandler，
 * 与官方 hex 的行为完全一致。
 */

    .section .highcode,"ax",@progbits
    .align 2
    .global BB_IRQHandler
    .type   BB_IRQHandler, @function
BB_IRQHandler:
    j       LLE_IRQHandler
    .size   BB_IRQHandler, . - BB_IRQHandler
