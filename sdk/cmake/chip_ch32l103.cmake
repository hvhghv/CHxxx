# ===========================================================================
# CH32L103 芯片描述
# ===========================================================================
# CH32L103：RV32IMAC，64KB Flash，20KB RAM，USB FS + PDUSB，无 BLE
# 外设驱动前缀 ch32l103_
# ===========================================================================

set(CHIP_NAME          "ch32l103")
set(CHIP_FAMILY        "CH32L")
set(CHIP_DISPLAY_NAME  "CH32L103")

set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei")
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei")

set(CHIP_FLASH_SIZE    64K)
set(CHIP_RAM_SIZE      20K)

set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch32l103")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch32")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/startup_ch32l103.S"
)
set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/Core/core_riscv.c"
    "${CHIP_VENDOR_DIR}/System/system_ch32l103.c"
)

set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/Peripheral")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_rcc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_misc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_pwr.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_flash.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_tim.c"
    # EXTI 是 GPIO 中断的底层依赖
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_exti.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_usart.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_tim.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_spi.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32l103_tim.c"
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
    CH32L103
    CH32L103C8T6
    FUNCONF_SYSTEM_CORE_CLOCK=48000000
)
