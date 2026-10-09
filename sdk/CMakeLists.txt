# ===========================================================================
# SDK 顶层 CMakeLists（多芯片支持）
# ===========================================================================
# 目录结构：
#   sdk/
#   ├── CMakeLists.txt        <- 本文件
#   ├── common/               <- 共享：工具链、ch32fun、USB 栈、构建逻辑
#   │   ├── prebuilt/         <- RISC-V GCC 工具链
#   │   ├── cmake/
#   │   ├── core/             <- ch32fun.c/h
#   │   └── extralibs/        <- fsusb 等
#   ├── CH572_DEV/            <- CH572 专属：BLE 库 + 芯片参数
#   │   ├── core/             <- ch5xxhw.h, ch32fun.ld
#   │   └── ble/
#   └── CH592_DEV/            <- CH592/CH591 专属
#       ├── core/
#       └── ble/
#
# 芯片选择见项目根的 sdk_config.cmake（CHIP_SDK 变量）。
# ===========================================================================

cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# 芯片选择（若父工程未提供，则读取 sdk_config.cmake）
# ---------------------------------------------------------------------------
if(NOT CHIP_SDK_RESOLVED)
    set(_cfg "${CMAKE_CURRENT_SOURCE_DIR}/../sdk_config.cmake")
    if(EXISTS "${_cfg}")
        include("${_cfg}")
    else()
        set(CHIP_SDK_RESOLVED "CH572")
        message(STATUS "未找到 sdk_config.cmake，默认使用 CH572")
    endif()
endif()

# ---------------------------------------------------------------------------
# 路径定义
# ---------------------------------------------------------------------------
get_filename_component(_sdk_root "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(SDK_COMMON_DIR "${_sdk_root}/common" CACHE INTERNAL "SDK 共享目录")

if(CHIP_SDK_RESOLVED STREQUAL "CH572")
    set(SDK_CHIP_DIR "${_sdk_root}/CH572_DEV" CACHE INTERNAL "芯片 SDK 目录")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH592" OR CHIP_SDK_RESOLVED STREQUAL "CH591")
    set(SDK_CHIP_DIR "${_sdk_root}/CH592_DEV" CACHE INTERNAL "芯片 SDK 目录")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    set(SDK_CHIP_DIR "${_sdk_root}/CH32V003_DEV" CACHE INTERNAL "芯片 SDK 目录")
else()
    message(FATAL_ERROR "不支持的芯片：${CHIP_SDK_RESOLVED}（可选 CH572 / CH592 / CH591 / CH32V003）")
endif()

set(CHIP_SDK_RESOLVED "${CHIP_SDK_RESOLVED}" CACHE INTERNAL "目标芯片型号")

message(STATUS "SDK：common = ${SDK_COMMON_DIR}")
message(STATUS "SDK：chip   = ${SDK_CHIP_DIR}")

# ---------------------------------------------------------------------------
# 引入构建模块
# ---------------------------------------------------------------------------
include(${SDK_COMMON_DIR}/cmake/sdk.cmake)

# ---------------------------------------------------------------------------
# 独立构建时，把项目根的 main/ 应用也加进来
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ch5xx_sdk C ASM)
    get_filename_component(_project_root "${_sdk_root}/.." ABSOLUTE)
    set(_main_dir "${_project_root}/main")
    if(EXISTS "${_main_dir}/CMakeLists.txt")
        add_subdirectory("${_main_dir}" "${CMAKE_BINARY_DIR}/main")
    endif()
endif()
