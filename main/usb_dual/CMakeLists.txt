# CH572 三 USB 串口设备
#
# 枚举为三个 CDC-ACM 虚拟串口：
#   串口 A：终端（系统信息、蓝牙控制）
#   串口 B：蓝牙桥（USB <-> BLE 透传，配合 web_ble.html）
#   串口 C：调试口（printf / scanf）
#
# 需要 BLE 和 USB 支持。

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../CH572_DEV" ABSOLUTE)
    include(${SDK_ROOT}/cmake/sdk.cmake)
endif()

ch572_add_executable(usb_dual
    usb_dual.c
    usb_ble_profile.c
    BLE
    USB
)
