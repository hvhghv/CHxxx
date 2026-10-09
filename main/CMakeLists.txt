# main/ - 应用集合
# ---------------------------------------------------------------------------
# 每个子目录是一个独立固件。
# 按目标芯片选择可编译的固件（CHIP_SDK_RESOLVED 由 sdk_config.cmake 提供）。
# ---------------------------------------------------------------------------

if(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    # CH32V003：无 BLE / USB
    add_subdirectory(blink_v003)
else()
    # CH5xx：BLE + USB
    add_subdirectory(usb_dual)
    # CH592 / CH591：FreeRTOS 示例 + 网关
    if(CHIP_SDK_RESOLVED STREQUAL "CH592" OR CHIP_SDK_RESOLVED STREQUAL "CH591")
        add_subdirectory(freertos_demo)
        add_subdirectory(gateway)
    endif()
endif()
