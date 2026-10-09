# CH592 / CH591 FreeRTOS 支持

WCH 官方 FreeRTOS 移植（FreeRTOS-Kernel V11.1.0，Qingke V4C RISC-V 内核）。

## 目录结构

```
freertos/
├── kernel/                     # FreeRTOS 内核
│   ├── tasks.c / queue.c / list.c / timers.c
│   ├── event_groups.c / stream_buffer.c / croutine.c
│   ├── heap_4.c                # 内存管理（heap_4）
│   └── include/                # 内核头文件
├── port/                       # WCH Qingke V4C 移植层
│   ├── port.c                  # 调度器 / SysTick / 临界区
│   ├── portASM.S               # 上下文切换（SW_Handler）
│   ├── portmacro.h             # 移植宏
│   └── freertos_shim.h         # 适配层（补齐 ch32fun 缺失的官方接口）
├── config/
│   └── FreeRTOSConfig.h        # FreeRTOS 配置
├── freertos.ld                 # 链接脚本（.vector 入 RAM + 中断栈符号）
└── Startup_CH592_FreeRTOS.S    # 启动文件（统一中断入口）
```

## 使用方式

在 `main/<app>/CMakeLists.txt` 中：

```cmake
ch572_add_executable(myapp
    myapp.c
    FREERTOS        # 启用 FreeRTOS
)
```

`FREERTOS` 选项会自动：
- 加入内核 + 移植层 + 启动文件源文件
- 使用 `freertos.ld`（含 `.vector` 段与 `__freertos_irq_stack_top`）
- 定义 `FUNCONF_OVERRIDE_STARTUP=1`（禁用 ch32fun 自带启动代码）
- 将 MARCH 提升为 `rv32imac_zicsr_zifencei`（汇编用 CSR 指令）

## 与 ch32fun 的适配

官方移植依赖 `core_riscv.h` / `CH59x_common.h`，本 SDK 用 ch32fun 替代。
`port/freertos_shim.h` 补齐了 ch32fun 缺失的接口：

| 接口 | 说明 |
|---|---|
| `PFIC_SetPriority` / `PFIC_EnableIRQ` / `PFIC_DisableIRQ` | 中断优先级/使能（映射到 ch32fun 的 NVIC 接口） |
| `PFIC_SetPendingIRQ` / `PFIC_ClearPendingIRQ` | 中断挂起 |
| `SysTick_Config` | CH59x 为 64 位 SysTick |
| `SWI_IRQn` | 软件中断编号（别名 `Software_IRQn`） |
| `__nop()` | 小写别名（ch32fun 用 `__NOP()`） |

ch32fun 已提供：`SetVTFIRQ`、`FunctionalState`、`__enable_irq`/`__disable_irq`、
`NVIC_EnableIRQ`/`NVIC_DisableIRQ`/`NVIC_SetPendingIRQ` 等。

## 注意事项（摘自官方 readme）

1. 本移植默认使用**硬件压栈**（不可关闭），中断嵌套可选。
2. 用户外部中断函数建议用 `__attribute__((section(".highcode")))` 修饰。
3. 统一入口中断（`unified_interrupt_entry`）无需 `__attribute__((interrupt(...)))` 修饰。
4. 中断使用 `mscratch` 作为临时 sp 寄存器，**用户不可再使用 mscratch**。
5. 任务中打印请使用 `App_Printf`（互斥量保护），否则可能 HardFault。
6. 不可使用 `core_riscv.h` 的 `__enable_irq`/`__disable_irq`，应使用
   `portENTER_CRITICAL` / `portEXIT_CRITICAL`。
7. CH59x 无 `RCC` 外设，funconfig.h 必须设置 `FUNCONF_USE_CLK_SEC 0`。

## 示例

`main/freertos_demo/` — 多任务调度（task1/task2）+ 互斥量保护的 printf。
