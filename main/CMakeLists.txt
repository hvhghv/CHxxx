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
    # 最小 USB 枚举测试（排除 BLE 干扰，诊断 USB 问题）
    add_subdirectory(usb_probe)
    # 最小 USB CDC 串口（不含 BLE，验证 USB 通路）
    add_subdirectory(usb_cdc_min)
    # 最小复现：USB CDC + 单次 LL_SlaveEnalbe()（定位 BLE 破坏 USB）
    add_subdirectory(usb_ble_min)
    # 最小 UART 探针（验证 UART1/PA9 硬件，不依赖 BLE/USB）
    add_subdirectory(uart_probe)
    # 最简 GPIO 心跳（验证程序能否跑起来，不依赖 UART/USB/BLE）
    add_subdirectory(gpio_probe)
    # 用 USB CDC 输出日志验证 UART（不链接 BLE）
    add_subdirectory(uart_via_usb)
    # Data Flash 写入验证（用 wchisp eeprom dump 读回）
    add_subdirectory(ee_probe)
    # 官方 Peripheral 示例原样移植（验证基线）
    add_subdirectory(ble_official)
    # BLE 功能独立测试（验证广播/连接/GATT 透传）
    add_subdirectory(ble_test)
    # CH59x 系列（CH592X / CH591F / CH591D / CH591R）：FreeRTOS 示例 + 网关
    if(CHIP_IS_CH59X)
        add_subdirectory(freertos_demo)
        add_subdirectory(gateway)
    endif()
endif()
