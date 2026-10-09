# CH32V003 LED 闪烁示例
#
# 目标芯片：CH32V003（RV32EC，2KB RAM，16KB Flash）
# 无 BLE / USB 依赖，仅使用 ch32fun 核心。

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" ABSOLUTE)
    include(${SDK_ROOT}/common/cmake/sdk.cmake)
endif()

ch572_add_executable(blink_v003
    blink_v003.c
)
