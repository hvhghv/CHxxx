# ===========================================================================
# HAL 构建规则
# ===========================================================================
# 用法（应用 CMakeLists.txt）：
#
#   hal_add_executable(blink
#       SOURCES blink.c
#   )
#
#   hal_add_executable(ble_echo
#       SOURCES ble_echo.c
#       BLE
#   )
#
# 选项：
#   BLE         启用 BLE 协议栈（加入 BLE 库 + HAL + 汇编）
#   USB         启用 USB（加入 CH59x_usbdev.c）
#   UART        启用 UART 驱动
#   LWIP        启用 lwIP 协议栈（NO_SYS=1 raw API）
#   I2C / SPI / PWM / ADC / TIMER   按需启用
# ===========================================================================

# ---------------------------------------------------------------------------
# 载入芯片描述
# ---------------------------------------------------------------------------
# 注意：不能用 function()，否则 include() 里的 set() 只在该函数作用域有效，
# 后续 hal_add_executable() 看不到 CHIP_* 变量。这里用 macro（宏展开到调用者作用域）。
macro(hal_select_chip CHIP)
    set(_chip_file "${CMAKE_SOURCE_DIR}/sdk/cmake/chip_${CHIP}.cmake")
    if(NOT EXISTS "${_chip_file}")
        message(FATAL_ERROR "找不到芯片描述文件：${_chip_file}")
    endif()
    include("${_chip_file}")
    message(STATUS "HAL 芯片：${CHIP_DISPLAY_NAME} (${CHIP_NAME})")
endmacro()

# ---------------------------------------------------------------------------
# 添加可执行文件
# ---------------------------------------------------------------------------
function(hal_add_executable TARGET)
    cmake_parse_arguments(ARG
        "BLE;USB;UART;LWIP;I2C;SPI;PWM;ADC;TIMER;HAL_SKIP_BLE_PORT;HAL_NO_TRAMPOLINE;HAL_SKIP_USB_PORT"
        "LINKER_SCRIPT;LWIP_DIR"
        "SOURCES;INCLUDES;LIBS;DEFINES;LWIP_SOURCES"
        ${ARGN})

    if(NOT CHIP_NAME)
        message(FATAL_ERROR "请先调用 hal_select_chip(<name>)")
    endif()

    # ---- 收集源文件 ------------------------------------------------------
    # startup_CH592.S 必须在前面（定义 .init 入口跳转和 .handle_reset）。
    # trampoline 在后面：它的强符号会覆盖 startup 的弱符号中断处理器。
    set(_sources
        ${ARG_SOURCES}
        ${CHIP_STARTUP_SOURCES}
        ${CHIP_CORE_SOURCES}
        ${CHIP_PERIPH_SYS_SOURCES}
        ${CHIP_PERIPH_GPIO_SOURCES}
    )

    # 中断入口汇编 trampoline（必须完整保存寄存器）
    # ★ HAL_NO_TRAMPOLINE：直接编译官方例程时不能加 trampoline，
    #   否则它的强符号会覆盖官方 startup 的弱符号中断处理器，
    #   导致中断走到空的 hal_irq_dispatch，BLE/USB 全部失效。
    if(NOT ARG_HAL_NO_TRAMPOLINE)
        if(EXISTS "${CHIP_PORT_DIR}/hal_irq_trampoline_${CHIP_NAME}.S")
            enable_language(ASM)
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_irq_trampoline_${CHIP_NAME}.S")
        endif()
    endif()

    # 通用层（排除 USB 类驱动，它们只在启用 USB 时加入）
    file(GLOB _common_src "${CMAKE_SOURCE_DIR}/sdk/hal/src/*.c")
    foreach(_src ${_common_src})
        get_filename_component(_name "${_src}" NAME)
        if(_name MATCHES "^hal_usb")
            continue()      # USB 类驱动在下面按需加入
        endif()
        list(APPEND _sources "${_src}")
    endforeach()

    # port 层：自动扫描 ${CHIP_PORT_DIR} 下的所有 .c
    # ★ 这样新增芯片只需放文件，不用改构建脚本。
    #   例外：
    #     hal_usb_*      → 仅在 ARG_USB 时加入
    #     hal_uart_*     → 仅在 ARG_UART 时加入
    #     hal_i2c/spi/pwm/adc_* → 对应开关
    file(GLOB _port_src "${CHIP_PORT_DIR}/*.c")
    foreach(_src ${_port_src})
        get_filename_component(_name "${_src}" NAME)
        if(_name MATCHES "^hal_usb")
            continue()
        endif()
        # ★ BLE 相关文件仅在 ARG_BLE 时加入
        if(_name MATCHES "^hal_ble" AND NOT ARG_BLE)
            continue()
        endif()
        if(_name MATCHES "^ble_" AND NOT ARG_BLE)
            continue()
        endif()
        if(_name MATCHES "^hal_uart" AND NOT ARG_UART)
            continue()
        endif()
        if(_name MATCHES "^hal_i2c" AND NOT ARG_I2C)
            continue()
        endif()
        if(_name MATCHES "^hal_spi" AND NOT ARG_SPI)
            continue()
        endif()
        if(_name MATCHES "^hal_pwm" AND NOT ARG_PWM)
            continue()
        endif()
        if(_name MATCHES "^hal_adc" AND NOT ARG_ADC)
            continue()
        endif()
        list(APPEND _sources "${_src}")
    endforeach()
    # 中断入口汇编 trampoline 已在上面（startup 之前）加入

    # ---- 按需加入外设 ----------------------------------------------------
    if(ARG_UART)
        list(APPEND _sources ${CHIP_PERIPH_UART_SOURCES})
        if(EXISTS "${CHIP_PORT_DIR}/hal_uart_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_uart_${CHIP_NAME}.c")
        endif()
    endif()
    if(ARG_I2C)
        list(APPEND _sources ${CHIP_PERIPH_I2C_SOURCES})
        if(EXISTS "${CHIP_PORT_DIR}/hal_i2c_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_i2c_${CHIP_NAME}.c")
        endif()
    endif()
    if(ARG_SPI)
        list(APPEND _sources ${CHIP_PERIPH_SPI_SOURCES})
        if(EXISTS "${CHIP_PORT_DIR}/hal_spi_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_spi_${CHIP_NAME}.c")
        endif()
    endif()
    if(ARG_PWM)
        list(APPEND _sources ${CHIP_PERIPH_PWM_SOURCES})
        if(EXISTS "${CHIP_PORT_DIR}/hal_pwm_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_pwm_${CHIP_NAME}.c")
        endif()
    endif()
    if(ARG_ADC)
        list(APPEND _sources ${CHIP_PERIPH_ADC_SOURCES})
        if(EXISTS "${CHIP_PORT_DIR}/hal_adc_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/hal_adc_${CHIP_NAME}.c")
        endif()
    endif()
    if(ARG_TIMER)
        list(APPEND _sources ${CHIP_PERIPH_TIMER_SOURCES})
    endif()
    if(ARG_USB)
        if(NOT CHIP_PERIPH_USB_SOURCES)
            message(FATAL_ERROR "芯片 ${CHIP_NAME} 不支持 USB")
        endif()
        list(APPEND _sources ${CHIP_PERIPH_USB_SOURCES})
        # HAL_SKIP_USB_PORT=1 时不加 HAL 自研的 USB 状态机，
        # 用于「用官方 app_usb.c + 薄包装」的对比实验。
        if(EXISTS "${CHIP_PORT_DIR}/hal_usb_${CHIP_NAME}.c")
            if(NOT ARG_HAL_SKIP_USB_PORT)
                list(APPEND _sources "${CHIP_PORT_DIR}/hal_usb_${CHIP_NAME}.c")
            endif()
        endif()
        # USB 类驱动（CDC/HID/MSC）只在启用 USB 时加入
        # HAL_SKIP_USB_PORT=1 时也跳过，因为包装层会自己提供这些接口。
        if(NOT ARG_HAL_SKIP_USB_PORT)
            file(GLOB _usb_common "${CMAKE_SOURCE_DIR}/sdk/hal/src/hal_usb_*.c")
            list(APPEND _sources ${_usb_common})
        endif()
    endif()

    # ---- BLE -------------------------------------------------------------
    set(_libs ${ARG_LIBS})
    set(_ble_sources "")
    set(_march_compile "${CHIP_MARCH_COMPILE}")
    set(_march_link    "${CHIP_MARCH_LINK}")

    # ROM 库（FLASH_EEPROM_CMD 等）：StdPeriphDriver 的 sys/pwr 依赖它，
    # 即使不用 BLE 也必须链接。
    if(CHIP_ROM_LIB AND EXISTS "${CHIP_ROM_LIB}")
        list(APPEND _libs "${CHIP_ROM_LIB}")
    endif()

    if(ARG_BLE)
        if(NOT CHIP_BLE_SUPPORTED)
            message(FATAL_ERROR "芯片 ${CHIP_NAME} 不支持 BLE")
        endif()
        list(APPEND _sources ${CHIP_BLE_HAL_SOURCES})
        # BLE HAL 的额外依赖（如 ADC 驱动，MCU.c 的 HAL_GetInterTempValue 需要）
        if(CHIP_BLE_EXTRA_SOURCES)
            list(APPEND _sources ${CHIP_BLE_EXTRA_SOURCES})
        endif()
        if(EXISTS "${CHIP_BLE_ASM}")
            enable_language(ASM)
            list(APPEND _sources "${CHIP_BLE_ASM}")
        endif()
        if(EXISTS "${CHIP_PORT_DIR}/hal_ble_${CHIP_NAME}.c")
            # HAL_SKIP_BLE_PORT=1 时不加 HAL 的 BLE 适配层，
            # 用于直接编译官方 BLE 例程（官方自己定义了 MEM_BUF 等）。
            if(NOT ARG_HAL_SKIP_BLE_PORT)
                list(APPEND _sources "${CHIP_PORT_DIR}/hal_ble_${CHIP_NAME}.c")
            endif()
        endif()
        if(EXISTS "${CHIP_PORT_DIR}/ble_profile_${CHIP_NAME}.c")
            list(APPEND _sources "${CHIP_PORT_DIR}/ble_profile_${CHIP_NAME}.c")
        endif()
        list(APPEND _libs "${CHIP_BLE_LIB}")
        if(EXISTS "${CHIP_BLE_ISP_LIB}")
            list(APPEND _libs "${CHIP_BLE_ISP_LIB}")
        endif()
        # BLE HAL 源文件需要 zicsr/zifencei/zmmul，但不要 zba/zbb/zbc/zbs
        set(_ble_sources ${CHIP_BLE_HAL_SOURCES})
        if(EXISTS "${CHIP_BLE_ASM}")
            list(APPEND _ble_sources "${CHIP_BLE_ASM}")
        endif()
    endif()

    # ---- LWIP ------------------------------------------------------------
    # lwIP 源码在 sdk/common/libs/lwip/src。
    # 默认编译 core/ipv4/netif 三个目录（NO_SYS=1 raw API 足够）。
    # 项目可通过 LWIP_SOURCES 追加（如 netif/ethernet.c 之外的驱动）。
    # 项目自定义 lwipopts.h 放在项目目录，用 INCLUDES 传入并需在最前面。
    set(_lwip_inc "")
    if(ARG_LWIP)
        set(_lwip_root "${CMAKE_SOURCE_DIR}/sdk/common/libs/lwip")
        if(ARG_LWIP_DIR)
            set(_lwip_root "${ARG_LWIP_DIR}")
        endif()
        if(NOT EXISTS "${_lwip_root}/src")
            message(FATAL_ERROR "找不到 lwIP 源码：${_lwip_root}/src")
        endif()

        file(GLOB_RECURSE _lwip_src
            "${_lwip_root}/src/core/*.c"
            "${_lwip_root}/src/ipv4/*.c"
            "${_lwip_root}/src/netif/*.c"
        )
        # 排除不需要的（省 Flash）
        list(REMOVE_ITEM _lwip_src
            "${_lwip_root}/src/netif/ethernetif.c"   # 由项目自己实现
        )
        list(APPEND _sources ${_lwip_src} ${ARG_LWIP_SOURCES})

        set(_lwip_inc
            "${_lwip_root}/src/include"
            "${_lwip_root}/src"
        )

        # lwIP 需要 -fno-strict-aliasing（大量指针类型双关）
        set_source_files_properties(${_lwip_src} PROPERTIES
            COMPILE_OPTIONS "-fno-strict-aliasing"
        )
    endif()

    # ---- 创建目标 --------------------------------------------------------
    add_executable(${TARGET} ${_sources})

    # 头文件路径
    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/hal/include"
        ${CHIP_VENDOR_INCLUDES}
        ${_lwip_inc}
        ${ARG_INCLUDES}
    )
    if(ARG_BLE)
        target_include_directories(${TARGET} PRIVATE ${CHIP_BLE_INCLUDES})
        if(CHIP_BLE_DEFINES)
            target_compile_definitions(${TARGET} PRIVATE ${CHIP_BLE_DEFINES})
        endif()
    endif()

    # 宏
    target_compile_definitions(${TARGET} PRIVATE
        ${CHIP_DEFINES}
        ${ARG_DEFINES}
    )

    # 编译选项
    # ★ 严格对齐 WCH 官方 MounRiver 工程（.cproject）：
    #   -fsigned-char    RISC-V GCC 默认 char 是【无符号】，而官方代码
    #                    大量用 char 做数值运算，符号性不同会让 BLE 协议栈
    #                    行为异常（这是官方 hex 能跑、我们编译跑不了的关键）。
    #   -fno-common      禁止把重复的未初始化全局变量合并到 common 段。
    #   -std=gnu99       官方用 gnu99，默认 gnu17 对老代码有兼容风险。
    #   -fmessage-length=0  官方同样开启。
    #   -fno-optimize-sibling-calls
    #                    ★ 禁止尾调用优化。GCC 15.2 会把
    #                    __attribute__((interrupt)) 函数里的单句调用
    #                    优化成 jr（不保存 ra），而被调用函数用 ret 返回，
    #                    导致跳到垃圾地址。官方工具链生成 jalr + mret。
    target_compile_options(${TARGET} PRIVATE
        -march=${_march_compile}
        -mabi=ilp32
        -Os
        -std=gnu99
        -fsigned-char
        -fno-common
        -ffunction-sections
        -fdata-sections
        -fmessage-length=0
        -fno-optimize-sibling-calls
        -Wall
    )

    # BLE 源文件单独加更宽的编译架构。
    # 注意：官方 HAL 的 RTC.c/SLEEP.c/MCU.c 只 include "HAL.h"，
    # 由 CONFIG.h 间接引入 CH59x_common.h / CH59xBLE_LIB.h，这是官方设计，
    # 不需要前置包含。
    #
    # ★ 但必须保证 common/include 不会遮蔽官方的 HAL.h：
    #   我们的总入口头文件叫 hal_core.h（不是 hal.h），
    #   否则 Windows 大小写不敏感的文件系统上会错误命中。
    if(_ble_sources)
        set_source_files_properties(${_ble_sources} PROPERTIES
            COMPILE_OPTIONS "-march=rv32imac_zicsr_zifencei_zmmul"
        )
    endif()

    # 链接选项
    set(_ld "${ARG_LINKER_SCRIPT}")
    if(NOT _ld)
        set(_ld "${CHIP_LINKER_SCRIPT}")
    endif()

    target_link_options(${TARGET} PRIVATE
        -march=${_march_link}
        -mabi=ilp32
        -T${_ld}
        -nostartfiles
        # ★ 必须用 newlib-nano：完整 newlib 的 printf 会拉入约 36KB，
        #   CH591 只有 192KB Flash，会直接超容量。
        --specs=nano.specs
        # ★ 必须加 nosys.specs（官方 .cproject 同样勾选）：
        #   缺失时 newlib 的 printf 会拉入默认 syscall 桩，
        #   进而引入 _sbrk / errno 相关代码，堆初始化异常，
        #   表现为「程序启动正常、USB 寄存器配置正确，但设备永不枚举」。
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map=${TARGET}.map
    )

    target_link_libraries(${TARGET} PRIVATE ${_libs})

    # 生成 bin / hex
    # ★ 重要：必须用 .hex 烧录，不能用 .bin！
    #   .highcode 段的 VMA 是 0x20000000（RAM），LMA 是 0x04（Flash）。
    #   objcopy -O binary 按 VMA 排列，会跳过 .highcode，导致
    #   Flash 0x04 处没有向量表和 BLE 库代码，程序复位后立即跑飞。
    #   .hex 格式保留 LMA，是正确的烧录格式。
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${TARGET}> ${TARGET}.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${TARGET}> ${TARGET}.hex
        COMMENT "生成 ${TARGET}.bin / .hex（烧录用 .hex）"
    )
endfunction()
