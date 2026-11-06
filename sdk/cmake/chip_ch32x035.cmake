# ===========================================================================
# CH32X035 芯片描述
# ===========================================================================
# 本文件只描述「芯片是什么」，不含构建逻辑。
# 由 sdk/cmake/hal.cmake 读取。
#
# CH32X035：RV32IMAC，62KB Flash，20KB RAM，USB FS + PDUSB，无 BLE
# 外设驱动前缀 ch32x035_（小写）
# ===========================================================================

set(CHIP_NAME          "ch32x035")
set(CHIP_FAMILY        "CH32X")
set(CHIP_DISPLAY_NAME  "CH32X035")

# ---------------------------------------------------------------------------
# 架构
# ---------------------------------------------------------------------------
set(CHIP_MARCH_COMPILE "rv32imac_zicsr_zifencei")
set(CHIP_MARCH_LINK    "rv32imac_zicsr_zifencei")

# ---------------------------------------------------------------------------
# 内存
# ---------------------------------------------------------------------------
set(CHIP_FLASH_SIZE    62K)
set(CHIP_RAM_SIZE      20K)

# ---------------------------------------------------------------------------
# 目录
# ---------------------------------------------------------------------------
set(CHIP_VENDOR_DIR    "${PROJECT_SOURCE_DIR}/sdk/vendor/ch32x035")
set(CHIP_PORT_DIR      "${PROJECT_SOURCE_DIR}/sdk/port/ch32")
set(CHIP_LINKER_SCRIPT "${CHIP_VENDOR_DIR}/Ld/Link.ld")

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
set(CHIP_STARTUP_SOURCES
    "${CHIP_VENDOR_DIR}/Startup/startup_ch32x035.S"
)

set(CHIP_CORE_SOURCES
    "${CHIP_VENDOR_DIR}/Core/core_riscv.c"
    "${CHIP_VENDOR_DIR}/System/system_ch32x035.c"
)

set(CHIP_STDPERIPH_DIR "${CHIP_VENDOR_DIR}/Peripheral")

set(CHIP_PERIPH_SYS_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_rcc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_misc.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_pwr.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_flash.c"
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_tim.c"
    # ★ EXTI 是 GPIO 中断的底层依赖，必须始终链接
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_exti.c"
)
set(CHIP_PERIPH_GPIO_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_gpio.c"
)
set(CHIP_PERIPH_UART_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_usart.c"
)
set(CHIP_PERIPH_TIMER_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_tim.c"
)
set(CHIP_PERIPH_SPI_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_spi.c"
)
set(CHIP_PERIPH_I2C_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_i2c.c"
)
set(CHIP_PERIPH_ADC_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_adc.c"
)
set(CHIP_PERIPH_PWM_SOURCES
    "${CHIP_STDPERIPH_DIR}/src/ch32x035_tim.c"
)
# ★ CH32 系列的 USB 驱动是「每个例程一份 User 文件」，
#   不像 CH5xx 有统一的 CH59x_usbdev.c。
#   需要 USB 的应用请在自身 CMakeLists 里显式加入：
#     ${CHIP_VENDOR_DIR}/USB/DEVICE/<例程>/User/ch32x035_usbfs_device.c
set(CHIP_PERIPH_USB_SOURCES "")

# ---------------------------------------------------------------------------
# 头文件路径
# ---------------------------------------------------------------------------
set(CHIP_VENDOR_INCLUDES
    "${CHIP_VENDOR_DIR}/Core"
    "${CHIP_VENDOR_DIR}/System"
    "${CHIP_STDPERIPH_DIR}/inc"
)

# ---------------------------------------------------------------------------
# BLE（CH32X035 无 BLE）
# ---------------------------------------------------------------------------
set(CHIP_BLE_SUPPORTED  FALSE)

# ---------------------------------------------------------------------------
# 宏定义
# ---------------------------------------------------------------------------
set(CHIP_DEFINES
    CH32X035
    CH32X035D8
    FUNCONF_SYSTEM_CORE_CLOCK=48000000
)
