/**
 * @file    hal_delay_ch32x035.c
 * @brief   CH32X035 官方驱动依赖的 Delay_xxx 桩实现
 *
 * 背景：官方 ch32x035_pwr.c 的 PWR_PVDCmd() 里调用了 application 层的
 *       Delay_Init() / Delay_Us()（来自官方 debug.c）。
 *       HAL 不依赖 PVD，这里提供最小实现，避免链接错误。
 */
/* ===========================================================================
 * 芯片头文件（由 CHIP_DEFINES 选择）
 * ---------------------------------------------------------------------------
 * 本 port 层同时支持 CH32X035 / CH32L103 / CH32V20x。
 * =========================================================================== */
#if defined(CH32X035)
  #include "ch32x035.h"
  #define CH32_FAMILY_X035   1
#elif defined(CH32L103)
  #include "ch32l103.h"
  #define CH32_FAMILY_L103   1
#elif defined(CH32V20x)
  #include "ch32v20x.h"
  #define CH32_FAMILY_V20X   1
#else
  #error "未定义 CH32 芯片宏（CH32X035 / CH32L103 / CH32V20x）"
#endif

#if defined(CH32_FAMILY_X035)
  #define CH32_IRQ_TIM2_UP       TIM2_UP_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI7_0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI15_8_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI25_16_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        USART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_APB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_APB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_APB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_APB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_APB2Periph_USART1
  #define CH32_USART2_CLK        RCC_APB1Periph_USART2
  #define CH32_USART3_CLK        RCC_APB1Periph_USART3
  #define CH32_USART4_CLK        RCC_APB1Periph_USART4
  #define CH32_APB1_CLK_CMD      RCC_APB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_APB2PeriphClockCmd
  #define CH32_USART4_PERIPH     USART4
#elif defined(CH32_FAMILY_L103)
  #define CH32_IRQ_TIM2_UP       TIM2_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI9_5_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI15_10_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        USART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_PB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_PB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_PB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_PB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_PB2Periph_USART1
  #define CH32_USART2_CLK        RCC_PB1Periph_USART2
  #define CH32_USART3_CLK        RCC_PB1Periph_USART3
  #define CH32_USART4_CLK        RCC_PB1Periph_USART4
  #define CH32_APB1_CLK_CMD      RCC_PB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_PB2PeriphClockCmd
  #define CH32_USART4_PERIPH     USART4
#elif defined(CH32_FAMILY_V20X)
  #define CH32_IRQ_TIM2_UP       TIM2_IRQn
  #define CH32_IRQ_EXTI_LOW      EXTI0_IRQn
  #define CH32_IRQ_EXTI_MID      EXTI9_5_IRQn
  #define CH32_IRQ_EXTI_HIGH     EXTI15_10_IRQn
  #define CH32_IRQ_USART1        USART1_IRQn
  #define CH32_IRQ_USART2        USART2_IRQn
  #define CH32_IRQ_USART3        USART3_IRQn
  #define CH32_IRQ_USART4        UART4_IRQn
  #define CH32_IRQ_USBFS         USBFS_IRQn
  #define CH32_GPIOA_CLK         RCC_APB2Periph_GPIOA
  #define CH32_GPIOB_CLK         RCC_APB2Periph_GPIOB
  #define CH32_GPIOC_CLK         RCC_APB2Periph_GPIOC
  #define CH32_TIM2_CLK          RCC_APB1Periph_TIM2
  #define CH32_USART1_CLK        RCC_APB2Periph_USART1
  #define CH32_USART2_CLK        RCC_APB1Periph_USART2
  #define CH32_USART3_CLK        RCC_APB1Periph_USART3
  #define CH32_USART4_CLK        RCC_APB1Periph_UART4
  #define CH32_APB1_CLK_CMD      RCC_APB1PeriphClockCmd
  #define CH32_APB2_CLK_CMD      RCC_APB2PeriphClockCmd
  #define CH32_USART4_PERIPH     UART4
#endif

/* 官方 ch32x035_pwr.c 引用这些函数，但没有头文件声明，
 * 这里在编译期提供声明，避免 implicit declaration 错误。 */
void Delay_Init(void);
void Delay_Us(uint32_t n);
void Delay_Ms(uint32_t n);

void Delay_Init(void)
{
    /* 无需初始化（HAL 用 hal_time 的忙等延时） */
}

void Delay_Us(uint32_t n)
{
    volatile uint32_t loops = (SystemCoreClock / 1000000u / 4u) * n;
    if (loops == 0) loops = 1;
    while (loops--) {
        __asm__ volatile("nop");
    }
}

void Delay_Ms(uint32_t n)
{
    while (n--) {
        Delay_Us(1000);
    }
}
