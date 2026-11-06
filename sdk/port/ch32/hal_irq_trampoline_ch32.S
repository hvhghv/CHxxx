/**
 * @file    hal_irq_trampoline_ch32.S
 * @brief   CH32 系列中断入口汇编 trampoline（X035 / L103 / V20x 共用）
 *
 * 背景：
 *   WCH 的 RISC-V 内核在进入中断时【不会】自动保存全部寄存器，
 *   只保存 mepc / mstatus 等 CSR。若用 C 函数加 __attribute__((interrupt))
 *   直接处理，GCC 生成的保存序列不完整，复杂协议栈会因此崩溃。
 *
 * 因此所有中断入口都用汇编：完整保存 x1/x4-x31（28 个寄存器，128 字节栈），
 * 调用 C 分发函数，再完整恢复并 mret。
 *
 * ★ 三个芯片的中断向量名/编号不同，用 CHIP_DEFINES 区分：
 *   CH32X035 : TIM2_UP_IRQHandler(38) / EXTI7_0(20) / USART1(32)
 *   CH32L103 : TIM2_IRQHandler(44)    / EXTI0(22)   / USART1(53)
 *   CH32V20x : TIM2_IRQHandler(44)    / EXTI0(22)   / USART1(53)
 */
	.section	.text.hal_irq_trampoline,"ax",@progbits
	.align	2

/* ---------------------------------------------------------------------------
 * 默认中断处理（未注册的中断走这里）
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
 * ★ 必须是【强符号】。官方 startup 为所有中断定义了弱符号指向死循环，
 *   如果我们也用 .weak，链接器会优先采用官方的死循环版本。
 * ------------------------------------------------------------------------- */
	.macro	HAL_IRQ_ENTRY name, irqn
	.global	\name
	.func
\name:
	addi	sp, sp, -32*4
	sw	x1,  0*4(sp)
	sw	x4,  1*4(sp)
	sw	x5,  2*4(sp)
	sw	x6,  3*4(sp)
	sw	x7,  4*4(sp)
	sw	x8,  5*4(sp)
	sw	x9,  6*4(sp)
	sw	x10, 7*4(sp)
	sw	x11, 8*4(sp)
	sw	x12, 9*4(sp)
	sw	x13, 10*4(sp)
	sw	x14, 11*4(sp)
	sw	x15, 12*4(sp)
	sw	x16, 13*4(sp)
	sw	x17, 14*4(sp)
	sw	x18, 15*4(sp)
	sw	x19, 16*4(sp)
	sw	x20, 17*4(sp)
	sw	x21, 18*4(sp)
	sw	x22, 19*4(sp)
	sw	x23, 20*4(sp)
	sw	x24, 21*4(sp)
	sw	x25, 22*4(sp)
	sw	x26, 23*4(sp)
	sw	x27, 24*4(sp)
	sw	x28, 25*4(sp)
	sw	x29, 26*4(sp)
	sw	x30, 27*4(sp)
	sw	x31, 28*4(sp)

	li	a0, \irqn
	call	hal_irq_dispatch

	lw	x1,  0*4(sp)
	lw	x4,  1*4(sp)
	lw	x5,  2*4(sp)
	lw	x6,  3*4(sp)
	lw	x7,  4*4(sp)
	lw	x8,  5*4(sp)
	lw	x9,  6*4(sp)
	lw	x10, 7*4(sp)
	lw	x11, 8*4(sp)
	lw	x12, 9*4(sp)
	lw	x13, 10*4(sp)
	lw	x14, 11*4(sp)
	lw	x15, 12*4(sp)
	lw	x16, 13*4(sp)
	lw	x17, 14*4(sp)
	lw	x18, 15*4(sp)
	lw	x19, 16*4(sp)
	lw	x20, 17*4(sp)
	lw	x21, 18*4(sp)
	lw	x22, 19*4(sp)
	lw	x23, 20*4(sp)
	lw	x24, 21*4(sp)
	lw	x25, 22*4(sp)
	lw	x26, 23*4(sp)
	lw	x27, 24*4(sp)
	lw	x28, 25*4(sp)
	lw	x29, 26*4(sp)
	lw	x30, 27*4(sp)
	lw	x31, 28*4(sp)

	addi	sp, sp, 32*4
	mret
	.endfunc
	.endm

/* ===========================================================================
 * CH32X035 中断向量表
 * =========================================================================== */
#if defined(CH32X035)
	HAL_IRQ_ENTRY      WWDG_IRQHandler,        16
	HAL_IRQ_ENTRY      EXTI7_0_IRQHandler,     20
	HAL_IRQ_ENTRY      ADC1_IRQHandler,        29
	HAL_IRQ_ENTRY      USART1_IRQHandler,      32
	HAL_IRQ_ENTRY      SPI1_IRQHandler,        33
	HAL_IRQ_ENTRY      TIM1_UP_IRQHandler,     35
	HAL_IRQ_ENTRY      TIM2_UP_IRQHandler,     38
	HAL_IRQ_ENTRY      USART2_IRQHandler,      39
	HAL_IRQ_ENTRY      EXTI15_8_IRQHandler,    40
	HAL_IRQ_ENTRY      EXTI25_16_IRQHandler,   41
	HAL_IRQ_ENTRY      USART3_IRQHandler,      42
	HAL_IRQ_ENTRY      USART4_IRQHandler,      43
	HAL_IRQ_ENTRY      USBFS_IRQHandler,       45

/* ===========================================================================
 * CH32L103 / CH32V20x 中断向量表（两者中断号一致）
 * =========================================================================== */
#elif defined(CH32L103) || defined(CH32V20x)
	HAL_IRQ_ENTRY      EXTI0_IRQHandler,       22
	HAL_IRQ_ENTRY      EXTI1_IRQHandler,       23
	HAL_IRQ_ENTRY      EXTI2_IRQHandler,       24
	HAL_IRQ_ENTRY      EXTI3_IRQHandler,       25
	HAL_IRQ_ENTRY      EXTI4_IRQHandler,       26
	HAL_IRQ_ENTRY      ADC_IRQHandler,         34
	HAL_IRQ_ENTRY      EXTI9_5_IRQHandler,     39
	HAL_IRQ_ENTRY      TIM1_UP_IRQHandler,     41
	HAL_IRQ_ENTRY      TIM2_IRQHandler,        44
	HAL_IRQ_ENTRY      TIM3_IRQHandler,        45
	HAL_IRQ_ENTRY      TIM4_IRQHandler,        46
	HAL_IRQ_ENTRY      SPI1_IRQHandler,        51
	HAL_IRQ_ENTRY      USART1_IRQHandler,      53
	HAL_IRQ_ENTRY      USART2_IRQHandler,      54
	HAL_IRQ_ENTRY      USART3_IRQHandler,      55
	HAL_IRQ_ENTRY      EXTI15_10_IRQHandler,   56
	HAL_IRQ_ENTRY      USBFS_IRQHandler,       59
	HAL_IRQ_ENTRY      UART4_IRQHandler,       61
#endif

/* SysTick 是内核中断（IRQ12），同样需要完整保存寄存器 */
	HAL_IRQ_ENTRY SysTick_Handler,   12
