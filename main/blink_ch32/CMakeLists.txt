# CH32 系列 LED 闪烁示例
#
# 目标芯片：CH32X035 / CH32L103 / CH32V203
# 无 BLE 依赖，仅使用 ch32fun 核心。

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" ABSOLUTE)
    include(${SDK_ROOT}/common/cmake/sdk.cmake)
endif()

ch572_add_executable(blink_ch32
    blink.c
)
