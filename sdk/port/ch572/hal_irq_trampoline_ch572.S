/**
 * @file    hal_irq_trampoline_ch572.S
 * @brief   CH572 中断入口汇编 trampoline
 *
 * 背景：
 *   WCH 的 RISC-V 内核在进入中断时【不会】自动保存全部寄存器，
 *   只保存 mepc / mstatus 等 CSR。若用 C 函数加 __attribute__((interrupt))
 *   直接处理，GCC 生成的保存序列不完整，BLE 协议栈会因此崩溃
 *   （实测：BLE_LibInit 内部卡死）。
 *
 * 因此所有中断入口都用汇编：完整保存 x1/x4-x31（28 个寄存器，128 字节栈），
 * 调用 C 分发函数，再完整恢复并 mret。
 *
 * 这些函数放在 .highcode 段，运行时从 Flash 复制到 RAM 执行。
 */
	.section	.highcode.hal_irq_trampoline,"ax",@progbits
	.align	2

/* ---------------------------------------------------------------------------
 * 默认中断处理（未注册的中断走这里）
 * 只做 mret 返回，不保存寄存器（因为没有 C 代码要跑）。
 * 官方 startup_CH572.S 的弱符号全部跳到这里，避免卡死在 j 1b。
 * ------------------------------------------------------------------------- */
	.global	hal_irq_default_handler
	.func
hal_irq_default_handler:
	mret
	.endfunc

/* ---------------------------------------------------------------------------
 * 通用中断入口宏
 *   参数：\name 函数名，\irqn 中断号
 *
 * ★ 关键：这里必须是【强符号】（.global，不加 .weak）。
 *   官方 startup_CH572.S 里为所有中断定义了【弱符号】指向死循环
 *   （j 1b）。如果我们也用 .weak，链接器会优先采用先出现的那个
 *   （即官方的死循环版本），导致所有中断进来就卡死。
 * ------------------------------------------------------------------------- */
	.macro	HAL_IRQ_ENTRY name, irqn
	.global	\name
	.func
\name:
	addi	sp, sp, -32*4

	sw	x1,  1*4(sp)
	sw	x4,  2*4(sp)
	sw	x5,  3*4(sp)
	sw	x6,  4*4(sp)
	sw	x7,  5*4(sp)
	sw	x8,  6*4(sp)
	sw	x9,  7*4(sp)
	sw	x10, 8*4(sp)
	sw	x11, 9*4(sp)
	sw	x12, 10*4(sp)
	sw	x13, 11*4(sp)
	sw	x14, 12*4(sp)
	sw	x15, 13*4(sp)
	sw	x16, 14*4(sp)
	sw	x17, 15*4(sp)
	sw	x18, 16*4(sp)
	sw	x19, 17*4(sp)
	sw	x20, 18*4(sp)
	sw	x21, 19*4(sp)
	sw	x22, 20*4(sp)
	sw	x23, 21*4(sp)
	sw	x24, 22*4(sp)
	sw	x25, 23*4(sp)
	sw	x26, 24*4(sp)
	sw	x27, 25*4(sp)
	sw	x28, 26*4(sp)
	sw	x29, 27*4(sp)
	sw	x30, 28*4(sp)
	sw	x31, 29*4(sp)

	li	a0, \irqn
	call	hal_irq_dispatch

	lw	x1,  1*4(sp)
	lw	x4,  2*4(sp)
	lw	x5,  3*4(sp)
	lw	x6,  4*4(sp)
	lw	x7,  5*4(sp)
	lw	x8,  6*4(sp)
	lw	x9,  7*4(sp)
	lw	x10, 8*4(sp)
	lw	x11, 9*4(sp)
	lw	x12, 10*4(sp)
	lw	x13, 11*4(sp)
	lw	x14, 12*4(sp)
	lw	x15, 13*4(sp)
	lw	x16, 14*4(sp)
	lw	x17, 15*4(sp)
	lw	x18, 16*4(sp)
	lw	x19, 17*4(sp)
	lw	x20, 18*4(sp)
	lw	x21, 19*4(sp)
	lw	x22, 20*4(sp)
	lw	x23, 21*4(sp)
	lw	x24, 22*4(sp)
	lw	x25, 23*4(sp)
	lw	x26, 24*4(sp)
	lw	x27, 25*4(sp)
	lw	x28, 26*4(sp)
	lw	x29, 27*4(sp)
	lw	x30, 28*4(sp)
	lw	x31, 29*4(sp)

	addi	sp, sp, 32*4
	mret
	.endfunc
	.endm

/* ---------------------------------------------------------------------------
 * 常用中断入口
 * ---------------------------------------------------------------------------
 * ★ 例外：以下三个中断在启用 BLE 时由官方实现提供，必须用 .weak，
 *   否则会与官方定义冲突（multiple definition）：
 *     RTC_IRQHandler  ← 官方 BLE/HAL/RTC.c
 *     LLE_IRQHandler  ← 官方 BLE/LIB/ble_task_scheduler.S
 *     BB_IRQHandler   ← 官方 BLE/LIB/ble_task_scheduler.S（复用 LLE）
 *   用 .weak 后：链接 BLE 时官方版本生效，不链接 BLE 时用本实现。
 * ------------------------------------------------------------------------- */
	.macro	HAL_IRQ_ENTRY_WEAK name, irqn
	.weak	\name
	.global	\name
	.func
\name:
	addi	sp, sp, -32*4

	sw	x1,  1*4(sp)
	sw	x4,  2*4(sp)
	sw	x5,  3*4(sp)
	sw	x6,  4*4(sp)
	sw	x7,  5*4(sp)
	sw	x8,  6*4(sp)
	sw	x9,  7*4(sp)
	sw	x10, 8*4(sp)
	sw	x11, 9*4(sp)
	sw	x12, 10*4(sp)
	sw	x13, 11*4(sp)
	sw	x14, 12*4(sp)
	sw	x15, 13*4(sp)
	sw	x16, 14*4(sp)
	sw	x17, 15*4(sp)
	sw	x18, 16*4(sp)
	sw	x19, 17*4(sp)
	sw	x20, 18*4(sp)
	sw	x21, 19*4(sp)
	sw	x22, 20*4(sp)
	sw	x23, 21*4(sp)
	sw	x24, 22*4(sp)
	sw	x25, 23*4(sp)
	sw	x26, 24*4(sp)
	sw	x27, 25*4(sp)
	sw	x28, 26*4(sp)
	sw	x29, 27*4(sp)
	sw	x30, 28*4(sp)
	sw	x31, 29*4(sp)

	li	a0, \irqn
	call	hal_irq_dispatch

	lw	x1,  1*4(sp)
	lw	x4,  2*4(sp)
	lw	x5,  3*4(sp)
	lw	x6,  4*4(sp)
	lw	x7,  5*4(sp)
	lw	x8,  6*4(sp)
	lw	x9,  7*4(sp)
	lw	x10, 8*4(sp)
	lw	x11, 9*4(sp)
	lw	x12, 10*4(sp)
	lw	x13, 11*4(sp)
	lw	x14, 12*4(sp)
	lw	x15, 13*4(sp)
	lw	x16, 14*4(sp)
	lw	x17, 15*4(sp)
	lw	x18, 16*4(sp)
	lw	x19, 17*4(sp)
	lw	x20, 18*4(sp)
	lw	x21, 19*4(sp)
	lw	x22, 20*4(sp)
	lw	x23, 21*4(sp)
	lw	x24, 22*4(sp)
	lw	x25, 23*4(sp)
	lw	x26, 24*4(sp)
	lw	x27, 25*4(sp)
	lw	x28, 26*4(sp)
	lw	x29, 27*4(sp)
	lw	x30, 28*4(sp)
	lw	x31, 29*4(sp)

	addi	sp, sp, 32*4
	mret
	.endfunc
	.endm

	/* =====================================================================
	 * CH572 中断向量表（与 CH59x 的差异）：
	 *   - TMR0~TMR3 共用 TMR_IRQn(24)，向量名 TMR_IRQHandler
	 *   - UART0~UART3 共用 UART_IRQn(27)，向量名 UART_IRQHandler
	 *   - GPIO A/B 共用 GPIO_A_IRQn(17)，向量名 GPIOA_IRQHandler
	 *   - SPI 只有一个 SPI_IRQn(19)，向量名 SPI_IRQHandler
	 *   - 多出 KEYSCAN / ENCODER 中断
	 * ===================================================================== */
	HAL_IRQ_ENTRY      GPIOA_IRQHandler,  17
	HAL_IRQ_ENTRY      SPI_IRQHandler,    19
	HAL_IRQ_ENTRY_WEAK BB_IRQHandler,     20
	HAL_IRQ_ENTRY_WEAK LLE_IRQHandler,    21
	/* ★ USB 必须是【强符号】。
	 *   HAL 版没有其他地方定义 USB_IRQHandler，
	 *   若这里是 weak，链接器会绑定到 startup 的 .vector_handler 默认处理
	 *   （j hal_irq_default_handler 自循环），USB 中断永远进不来。
	 *   直接编译官方例程时用 HAL_NO_TRAMPOLINE 排除本文件即可，不冲突。 */
	HAL_IRQ_ENTRY      USB_IRQHandler,    22
	HAL_IRQ_ENTRY      TMR_IRQHandler,    24
	HAL_IRQ_ENTRY      UART_IRQHandler,   27
	HAL_IRQ_ENTRY_WEAK RTC_IRQHandler,    28
	HAL_IRQ_ENTRY      CMP_IRQHandler,    29
	HAL_IRQ_ENTRY      I2C_IRQHandler,    30
	HAL_IRQ_ENTRY      PWMX_IRQHandler,   31
	HAL_IRQ_ENTRY      KEYSCAN_IRQHandler, 33
	HAL_IRQ_ENTRY      ENCODER_IRQHandler, 34
	HAL_IRQ_ENTRY      WDOG_BAT_IRQHandler, 35

/* ---------------------------------------------------------------------------
 * SysTick 是内核中断（IRQ12），但同样需要完整保存寄存器，
 * 因此也走同一套 trampoline。
 * ------------------------------------------------------------------------- */
	HAL_IRQ_ENTRY SysTick_Handler,   12
