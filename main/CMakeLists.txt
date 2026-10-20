# main/ - 应用集合
# ---------------------------------------------------------------------------
# 每个子目录是一个独立固件。
# 按目标芯片选择可编译的固件（CHIP_SDK_RESOLVED 由 sdk_config.cmake 提供）。
# ---------------------------------------------------------------------------

if(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    # CH32V003：无 BLE / USB
    add_subdirectory(blink_v003)
elseif(CHIP_IS_CH32RV)
    # 其他 CH32 系列（CH32X035 / CH32L103 / CH32V203）：
    # 无 BLE、有 USB。目前只有 LED 闪烁示例可用。
    add_subdirectory(blink_ch32)
else()
    # CH5xx：BLE + USB
    add_subdirectory(usb_dual)
    # BLE 功能独立测试（验证广播/连接/GATT 透传）
    add_subdirectory(ble_test)
    # CH59x 系列（CH592X / CH591F / CH591D / CH591R）：FreeRTOS 示例 + 网关
    if(CHIP_IS_CH59X)
        add_subdirectory(freertos_demo)
        add_subdirectory(gateway)
    endif()
endif()
