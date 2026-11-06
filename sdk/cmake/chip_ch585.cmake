# ===========================================================================
# CH585 / CH584 芯片描述
# ===========================================================================
# CH585：RV32IMAC，448+24+32KB Flash，128KB RAM，BLE 5.4 + USBHS
# 外设驱动前缀 CH58x_（与 CH59x 风格一致）
# ===========================================================================

set(CHIP_NAME          "ch585")
set(CHIP_FAMILY        "CH58x")
set(CHIP_DISPLAY_NAME  "CH585/CH584")

set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei_zmmul")
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs")

set(CHIP_FLASH_SIZE    448K)
set(CHIP_RAM_SIZE      128K)

set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch585")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch585")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/startup_CH585.S"
)
set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/RVMSIS/core_riscv.c"
)

set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/StdPeriphDriver")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_sys.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_clk.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_pwr.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_flash.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_timer0.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_uart0.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_uart1.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_uart2.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_uart3.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_timer0.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_timer1.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_timer2.c"
    "${CHIP_STDPERIPH_DIR}/CH58x_timer3.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_spi0.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_pwm.c"
)
set(CHIP_PERIPH_USB_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_usbdev.c"
)

set(CHIP_VENDOR_INCLUDES
    "${CHIP_VENDOR_DIR}/RVMSIS"
    "${CHIP_STDPERIPH_DIR}/inc"
)

# ---------------------------------------------------------------------------
# BLE
# ---------------------------------------------------------------------------
set(CHIP_BLE_SUPPORTED  TRUE)
set(CHIP_BLE_DIR        "${CHIP_VENDOR_DIR}/BLE")
set(CHIP_BLE_LIB        "${CHIP_BLE_DIR}/LIB/libCH58xBLE.a")
set(CHIP_BLE_ISP_LIB    "${CHIP_STDPERIPH_DIR}/libISP585.a")
set(CHIP_BLE_ASM        "${CHIP_BLE_DIR}/LIB/ble_task_scheduler.S")

set(CHIP_ROM_LIB        "${CHIP_STDPERIPH_DIR}/libISP585.a")

set(CHIP_BLE_HAL_SOURCES
    "${CHIP_BLE_DIR}/HAL/MCU.c"
    "${CHIP_BLE_DIR}/HAL/RTC.c"
    "${CHIP_BLE_DIR}/HAL/SLEEP.c"
)
set(CHIP_BLE_EXTRA_SOURCES
    "${CHIP_STDPERIPH_DIR}/CH58x_adc.c"
)
set(CHIP_BLE_INCLUDES
    "${CHIP_BLE_DIR}/HAL/include"
    "${CHIP_BLE_DIR}/LIB"
)

# ---------------------------------------------------------------------------
# 宏定义
# ---------------------------------------------------------------------------
# CH585 有 128KB RAM，BLE 堆可用官方默认 6KB
set(CHIP_BLE_DEFINES
    BLE_MEMHEAP_SIZE=6144
    BLE_BUFF_MAX_LEN=251
    BLE_BUFF_NUM=5
    BLE_TX_NUM_EVENT=1
)

set(CHIP_DEFINES
    CH58x
    CH585
    FUNCONF_SYSTEM_CORE_CLOCK=80000000
)
