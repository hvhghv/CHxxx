/**
 * @file    usb_irq_mret.S
 * @brief   USB 中断入口：jalr 调用 USB_DevTransProcess，然后 mret
 *
 * ★ 为什么需要这个文件
 * ---------------------------------------------------------------------------
 * 官方 app_usb.c 里 USB_IRQHandler 的写法是：
 *
 *     __attribute__((interrupt("WCH-Interrupt-fast")))
 *     __attribute__((section(".highcode")))
 *     void USB_IRQHandler(void) { USB_DevTransProcess(); }
 *
 * 官方工具链（WCH GCC）生成：
 *     auipc ra, ...
 *     jalr  ra, ...
 *     mret                    <- 正确：从 MPIE 恢复 MIE
 *
 * 而 GCC 15.2 生成的是：
 *     addi sp, sp, -16
 *     sw   ra, 12(sp)
 *     auipc ra, ...
 *     jalr ra, ...
 *     lw   ra, 12(sp)
 *     addi sp, sp, 16
 *     ret                     <- 错误：ret 不恢复 mstatus
 *
 * `ret` 只是跳到 ra，不会把 MPIE 恢复到 MIE。
 * 结果：第一次 USB 中断返回后 MIE=0，全局中断被永久关闭，
 *       后续所有 USB 中断（SETUP/IN/OUT）全部丢失，
 *       主机看到的设备永远停在「已连接但描述符请求失败」。
 *
 * 这里用汇编直接实现官方的指令序列，保证结尾是 mret。
 */

    .section .highcode,"ax",@progbits
    .align 2
    .global USB_IRQHandler
    .type   USB_IRQHandler, @function

USB_IRQHandler:
    /* 调用普通 C 函数，需要保存 ra（jalr 会覆盖它） */
    addi    sp, sp, -16
    sw      ra, 12(sp)

    call    USB_DevTransProcess

    lw      ra, 12(sp)
    addi    sp, sp, 16

    /* ★ 关键：mret 而不是 ret。
     *   mret 会把 MPIE 恢复到 MIE，保证中断可以持续响应。 */
    mret

    .size   USB_IRQHandler, . - USB_IRQHandler
