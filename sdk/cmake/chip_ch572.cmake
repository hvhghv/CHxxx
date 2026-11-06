# ===========================================================================
# CH572 芯片描述
# ===========================================================================
# 本文件只描述「芯片是什么」，不含构建逻辑。
# 由 sdk/cmake/hal.cmake 读取。
#
# CH572：RV32IMAC，240KB Flash，12KB RAM，BLE 5.0
# 与 CH59x 的主要差异：
#   - 外设驱动前缀是 CH57x_ 而非 CH59x_
#   - 定时器只有一个 CH57x_timer.c（TMR0~TMR3 都在里面）
#   - UART 只有一个 CH57x_uart.c（UART0~UART3 都在里面）
#   - BLE 库是 libCH572BLE_PERI.a（Peripheral 专用）
#   - RAM 只有 12KB，BLE 堆必须更小
# ===========================================================================

set(CHIP_NAME          "ch572")
set(CHIP_FAMILY        "CH57x")
set(CHIP_DISPLAY_NAME  "CH572")

# ---------------------------------------------------------------------------
# 架构
# ---------------------------------------------------------------------------
set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei_zmmul")
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs")

# ---------------------------------------------------------------------------
# 内存
# ---------------------------------------------------------------------------
set(CHIP_FLASH_SIZE    240K)
set(CHIP_RAM_SIZE      12K)

# ---------------------------------------------------------------------------
# 目录
# ---------------------------------------------------------------------------
set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch572")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch572")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/startup_CH572.S"
)

set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/RVMSIS/core_riscv.c"
)

set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/StdPeriphDriver")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_sys.c"
    "${CHIP_STDPERIPH_DIR}/CH57x_clk.c"
    "${CHIP_STDPERIPH_DIR}/CH57x_pwr.c"
    "${CHIP_STDPERIPH_DIR}/CH57x_flash.c"
    # ★ TMR0 是 HAL 时基（hal_time）的底层依赖，必须始终链接
    "${CHIP_STDPERIPH_DIR}/CH57x_timer.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_uart.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_timer.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_spi.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    # CH572 的 ADC 在 StdPeriphDriver 里（若不存在则留空，由 hal.cmake 跳过）
    "${CHIP_STDPERIPH_DIR}/CH57x_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_pwm.c"
)
set(CHIP_PERIPH_USB_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH57x_usbdev.c"
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
set(CHIP_BLE_LIB        "${CHIP_BLE_DIR}/LIB/libCH572BLE_PERI.a")
set(CHIP_BLE_ISP_LIB    "${CHIP_STDPERIPH_DIR}/libISP572.a")
# ★ CH572 的 BLE 库是 Peripheral 专用（libCH572BLE_PERI.a），
#   调度器已内置于库中，没有独立的 ble_task_scheduler.S
set(CHIP_BLE_ASM        "")

# ROM 库（FLASH_EEPROM_CMD 等）
set(CHIP_ROM_LIB        "${CHIP_STDPERIPH_DIR}/libISP572.a")

set(CHIP_BLE_HAL_SOURCES
    "${CHIP_BLE_DIR}/HAL/MCU.c"
    "${CHIP_BLE_DIR}/HAL/RTC.c"
    "${CHIP_BLE_DIR}/HAL/SLEEP.c"
)

# CH572 的 BLE HAL 依赖 ADC（HAL_GetInterTempValue）
# 若 CH57x_adc.c 不存在，说明 CH572 的温度采样走内部 ROM，无需额外源文件
if(EXISTS "${CHIP_STDPERIPH_DIR}/CH57x_adc.c")
    set(CHIP_BLE_EXTRA_SOURCES
        "${CHIP_STDPERIPH_DIR}/CH57x_adc.c"
    )
else()
    set(CHIP_BLE_EXTRA_SOURCES "")
endif()

set(CHIP_BLE_INCLUDES
    "${CHIP_BLE_DIR}/HAL/include"
    "${CHIP_BLE_DIR}/LIB"
)

# ---------------------------------------------------------------------------
# 宏定义
# ---------------------------------------------------------------------------
# ★ CH572 只有 12KB RAM，BLE 堆必须最小化。
#   协议栈硬下限 3KB（MCU.c: if(cfg.MEMLen < 3*1024)）。
set(CHIP_BLE_DEFINES
    BLE_MEMHEAP_SIZE=3072
    BLE_BUFF_MAX_LEN=27
    BLE_BUFF_NUM=2
    BLE_TX_NUM_EVENT=1
)

# ★ RAM 紧张：关闭日志 RAM 后端，缩小 UART 缓冲
add_compile_definitions(
    LOG_RAM_BUF_SIZE=0
    HAL_UART_RX_BUF_SIZE=64
    HAL_UART_TX_BUF_SIZE=64
)
