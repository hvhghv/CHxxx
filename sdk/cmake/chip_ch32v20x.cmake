# ===========================================================================
# CH32V20x 芯片描述
# ===========================================================================
# CH32V203：RV32IMAC，32/64/128KB Flash，10/20/64KB RAM，双 USB，无 BLE
# 外设驱动前缀 ch32v20x_
#
# ★ 启动文件按型号区分：
#   startup_ch32v20x_D6.S  → CH32V203（64/128KB Flash）
#   startup_ch32v20x_D8.S  → CH32V208（BLE 型号）
#   startup_ch32v20x_D8W.S → CH32V208W（BLE + 更大 RAM）
# 这里默认用 D6（CH32V203，无 BLE）。
# ===========================================================================

set(CHIP_NAME          "ch32v20x")
set(CHIP_FAMILY        "CH32V2")
set(CHIP_DISPLAY_NAME  "CH32V203")

set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei")
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei")

set(CHIP_FLASH_SIZE    128K)
set(CHIP_RAM_SIZE      64K)

set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch32v20x")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch32")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

# 启动文件：默认 D6（CH32V203）
if(NOT DEFINED CH32V20X_STARTUP)
    set(CH32V20X_STARTUP "startup_ch32v20x_D6.S")
endif()
set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/${CH32V20X_STARTUP}"
)
set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/Core/core_riscv.c"
    "${CHIP_VENDOR_DIR}/System/system_ch32v20x.c"
)

set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/Peripheral")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_rcc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_misc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_pwr.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_flash.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_tim.c"
    # EXTI 是 GPIO 中断的底层依赖
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_exti.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_usart.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_tim.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_spi.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32v20x_tim.c"
)
# CH32 系列 USB 驱动按例程提供，见 chip_ch32x035.cmake 的说明
set(CHIP_PERIPH_USB_SOURCES "")

set(CHIP_VENDOR_INCLUDES
    "${CHIP_VENDOR_DIR}/Core"
    "${CHIP_VENDOR_DIR}/System"
    "${CHIP_STDPERIPH_DIR}/inc"
)

set(CHIP_BLE_SUPPORTED  FALSE)

set(CHIP_DEFINES
    CH32V20x
    CH32V203C8T6
    FUNCONF_SYSTEM_CORE_CLOCK=144000000
)
