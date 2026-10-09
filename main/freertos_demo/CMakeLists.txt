# CH592/CH591 FreeRTOS 示例
#
# 演示多任务调度（task1 / task2）+ 互斥量保护的 printf。
# 需要 CH592 或 CH591（26KB RAM）。

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" ABSOLUTE)
    include(${SDK_ROOT}/common/cmake/sdk.cmake)
endif()

ch572_add_executable(freertos_demo
    freertos_demo.c
    FREERTOS
)
