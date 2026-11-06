# ===========================================================================
# CH59x 芯片描述
# ===========================================================================
# 本文件只描述「芯片是什么」，不含构建逻辑。
# 由 cmake/hal.cmake 读取。

set(CHIP_NAME          "ch59x")
set(CHIP_FAMILY        "CH59x")
set(CHIP_DISPLAY_NAME  "CH591/CH592")

# ---------------------------------------------------------------------------
# 架构
# ---------------------------------------------------------------------------
# 基础编译架构：CH59x 是 RV32IMAC
set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei_zmmul")

# 链接架构：官方 BLE 库用 GCC13+ 编译，含 zba/zbb/zbc/zbs 位操作指令。
# 仅在链接时放宽，避免用户代码生成这些指令（CH59x 内核不完整支持）。
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs")

# ---------------------------------------------------------------------------
# 内存
# ---------------------------------------------------------------------------
# 注意：CH591 是 192K Flash，CH592 是 448K。
# 这里按 CH591 保守声明；实际链接大小由 Link.ld 决定。
set(CHIP_FLASH_SIZE    192K)
set(CHIP_RAM_SIZE      26K)

# ---------------------------------------------------------------------------
# 目录
# ---------------------------------------------------------------------------
set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch59x")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch59x")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
# 启动文件
set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/startup_CH592.S"
)

# 内核（RVMSIS）
set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/RVMSIS/core_riscv.c"
)

# 标准外设驱动（按需加入，hal.cmake 用 CHIP_PERIPH_* 控制）
set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/StdPeriphDriver")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_sys.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_clk.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_pwr.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_flash.c"
    # ★ TMR0 是 HAL 时基（hal_time）的底层依赖，必须始终链接。
    #   官方 CH59x 例程全部用 TMR0~TMR3 做定时，不用 SysTick。
    "${CHIP_STDPERIPH_DIR}/CH59x_timer0.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_uart0.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_uart1.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_uart2.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_uart3.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_timer0.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_timer1.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_timer2.c"
    "${CHIP_STDPERIPH_DIR}/CH59x_timer3.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_spi0.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_pwm.c"
)
set(CHIP_PERIPH_USB_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_usbdev.c"
)

# ---------------------------------------------------------------------------
# 头文件路径
# ---------------------------------------------------------------------------
set(CHIP_VENDOR_INCLUDES
    "${CHIP_VENDOR_DIR}/RVMSIS"
    "${CHIP_STDPERIPH_DIR}/inc"
)

# ---------------------------------------------------------------------------
# BLE
# ---------------------------------------------------------------------------
set(CHIP_BLE_SUPPORTED  TRUE)
set(CHIP_BLE_DIR        "${CHIP_VENDOR_DIR}/BLE")
set(CHIP_BLE_LIB        "${CHIP_BLE_DIR}/LIB/libCH59xBLE.a")
set(CHIP_BLE_ISP_LIB    "${CHIP_BLE_DIR}/LIB/libISP592.a")
set(CHIP_BLE_ASM        "${CHIP_BLE_DIR}/LIB/ble_task_scheduler.S")

# ROM 库（FLASH_EEPROM_CMD 等）：StdPeriphDriver 的 sys/pwr 依赖它，
# 即使不用 BLE 也必须链接。
set(CHIP_ROM_LIB        "${CHIP_STDPERIPH_DIR}/libISP592.a")
set(CHIP_BLE_HAL_SOURCES
    "${CHIP_BLE_DIR}/HAL/MCU.c"
    "${CHIP_BLE_DIR}/HAL/RTC.c"
    "${CHIP_BLE_DIR}/HAL/SLEEP.c"
)
# BLE HAL 依赖 ADC 驱动（HAL_GetInterTempValue → ADC_InterTSSampInit），
# 即使不用 ADC 也必须链接。
set(CHIP_BLE_EXTRA_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH59x_adc.c"
)
set(CHIP_BLE_INCLUDES
    "${CHIP_BLE_DIR}/HAL/include"
    "${CHIP_BLE_DIR}/LIB"
)

# ---------------------------------------------------------------------------
# 宏定义
# ---------------------------------------------------------------------------
set(CHIP_DEFINES
    CH59x
    CH591_CH592
    FUNCONF_SYSTEM_CORE_CLOCK=60000000
)

# ---------------------------------------------------------------------------
# BLE 资源占用（实测，CH592 26KB RAM / 448KB Flash）
# ---------------------------------------------------------------------------
#   配置                           .bss(RAM)   .text(ROM)
#   官方默认 6K堆+5缓冲            8904 B      147020 B
#   最小可用 4K堆+3缓冲            7448 B      146944 B   ← 推荐默认
#   极限     4K堆+3缓冲+无RAM日志  7184 B      146832 B   ← BLE_MIN_EXPERIMENT=ON
#
# ★ 协议栈硬下限：BLE_MEMHEAP_SIZE >= 4096（MCU.c 里 while(1) 检查）
# ★ BLE_BUFF_NUM=2 会丢首包，3 是安全最小值
# ★ BLE_BUFF_MAX_LEN 最小 27（ATT_MTU=23 + 4 字节头）
# ---------------------------------------------------------------------------
if(BLE_MIN_EXPERIMENT)
    # 极限省 RAM：关闭日志 RAM 后端（LOG_RAM_BUF_SIZE=0）
    set(CHIP_BLE_DEFINES
        BLE_MEMHEAP_SIZE=4096
        BLE_BUFF_MAX_LEN=27
        BLE_BUFF_NUM=3
        BLE_TX_NUM_EVENT=1
    )
    add_compile_definitions(LOG_RAM_BUF_SIZE=0)
else()
    # 推荐默认：4KB 堆 + 3 缓冲（实测 GATT 收发 5/5 无丢包）
    set(CHIP_BLE_DEFINES
        BLE_MEMHEAP_SIZE=4096
        BLE_BUFF_MAX_LEN=27
        BLE_BUFF_NUM=3
        BLE_TX_NUM_EVENT=1
    )
endif()
