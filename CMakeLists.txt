# CH5xx 项目模板 - 根 CMakeLists
# ---------------------------------------------------------------------------
# 目录结构：
#   <项目根>/
#   ├── CMakeLists.txt        <- 本文件
#   ├── main/                 <- 应用代码（多芯片共用）
#   ├── sdk/                  <- SDK（所有非 main 源码）
#   │   ├── hal/              <- 通用 HAL 层（原 common/）
#   │   ├── port/             <- 芯片移植层
#   │   ├── vendor/           <- 官方 SDK 原样拷贝
#   │   ├── cmake/            <- 构建脚本
#   │   └── prebuilt/         <- RISC-V 工具链
#   ├── tools/                <- 烧录工具 / 测试脚本
#   └── tmp/                  <- 参考源码 + 旧架构备份（gitignore）
#
# 构建：
#   cmake -B build_hal -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=sdk/cmake/toolchain-riscv.cmake \
#         -DHAL_BUILD=ON -DCHIP=ch59x
#   cmake --build build_hal
#
# 切换芯片：-DCHIP=<ch59x|ch572|ch585|ch32x035|ch32l103|ch32v20x>
#
# 产物在 build_hal/main/<目标名>/ 下。
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)
project(ch5xx_template C ASM)

# ===========================================================================
# HAL 架构
# ===========================================================================
# 用 -DHAL_BUILD=ON 启用（sdk/hal + sdk/port + sdk/vendor）。
#
# 构建方式（独立构建目录）：
#   cmake -B build_hal -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=sdk/cmake/toolchain-riscv.cmake \
#         -DHAL_BUILD=ON -DCHIP=ch59x
#   cmake --build build_hal
# ===========================================================================
option(HAL_BUILD "启用 HAL 架构（sdk/hal + sdk/port + sdk/vendor）" OFF)
# ===========================================================================
option(HAL_BUILD "启用新 HAL 架构（sdk/hal + sdk/port + sdk/vendor）" OFF)

if(HAL_BUILD)
    if(NOT CHIP)
        set(CHIP "ch59x" CACHE STRING "目标芯片")
    endif()

    include(${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/hal.cmake)
    hal_select_chip(${CHIP})

    # -----------------------------------------------------------------------
    # 应用（按芯片能力筛选）
    # -----------------------------------------------------------------------
    # ---- 基础目标（所有芯片都支持）----
    # blink 验证工具链 + GPIO + 时基；uart_demo 验证 UART 驱动。
    add_subdirectory(main/blink)
    add_subdirectory(main/uart_demo)
    # flash_demo 验证 Flash / ROM 读写（UID/MAC/擦写读回环）
    add_subdirectory(main/flash_demo)

    # ---- 需要 BLE + USB（ble_usb 是 BLE+USB 共存示例） ----
    # ★ CH572 只有 12KB RAM，完整 BLE 栈装不下，暂不支持。
    if(CHIP_BLE_SUPPORTED AND CHIP_PERIPH_USB_SOURCES AND
       NOT CHIP_NAME STREQUAL "ch572")
        add_subdirectory(main/ble_usb)
    endif()

    # ---- 需要 USB ----
    if(CHIP_PERIPH_USB_SOURCES)
        add_subdirectory(main/usb_cdc)
    endif()

    # ---- gateway（BLE + USB + LWIP，仅 CH59x） ----
    # ★ 暂时排除：gateway 源码仍依赖旧 ch32fun 框架头文件
    #   （ch32fun.h / ch5xx_flash.h / fsusb.h / usb_defines.h / rndis.h），
    #   这些文件目前只在 tmp/ 里（已 gitignore）。
    #   迁移到 HAL API 需要重写 18 个源文件的底层驱动调用，
    #   属于架构决策，待确认后再启用。
    #   启用方式：-DGW_BUILD=ON
    option(GW_BUILD "构建 gateway（需先完成 ch32fun → HAL 迁移）" OFF)
    if(CHIP_NAME STREQUAL "ch59x" AND GW_BUILD)
        add_subdirectory(main/gateway)
    endif()

    return()
endif()

# ===========================================================================
# 未启用 HAL_BUILD
# ===========================================================================
# 旧架构（sdk/*_DEV + ch32fun）已迁移到 tmp/legacy_sdk/，不再参与构建。
message(FATAL_ERROR
    "请启用 HAL 构建：\n"
    "  cmake -B build_hal -G Ninja \\\n"
    "        -DCMAKE_TOOLCHAIN_FILE=sdk/cmake/toolchain-riscv.cmake \\\n"
    "        -DHAL_BUILD=ON -DCHIP=ch59x\n"
    "  cmake --build build_hal\n"
    "\n"
    "可选 CHIP：ch59x / ch572 / ch585 / ch32x035 / ch32l103 / ch32v20x")
