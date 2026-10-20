# ===========================================================================
# SDK 选择配置
# ===========================================================================
# 选择目标芯片 SDK。
#
# 支持的值：
#
# 【CH59x 系列】CH592/CH591 数据手册 V1.9 选型表全部 7 个型号
#   CH592X    - CH592X (448KB Flash, 26KB RAM, QFN32, 24 GPIO, BLE 5.4)
#   CH592F    - CH592F (448KB Flash, 26KB RAM, QFN28, 20 GPIO)
#   CH592A    - CH592A (448KB Flash, 26KB RAM, QFN28, 20 GPIO，同 CH592F，仅最低电压 2.3V)
#   CH592D    - CH592D (448KB Flash, 26KB RAM, QFN20, 12 GPIO)
#   CH591F    - CH591F (192KB Flash, 26KB RAM, QFN28, 20 GPIO)
#   CH591D    - CH591D (192KB Flash, 26KB RAM, QFN20, 10 GPIO)
#   CH591R    - CH591R (192KB Flash, 26KB RAM, TSSOP16, 10 GPIO)
#   CH592/CH591 - 兼容别名（分别等同于 CH592X / CH591F）
#
# 【CH57x 系列】
#   CH572     - CH572 (RV32IMAC, 12KB RAM, 240KB Flash, BLE 5.0)
#
# 【CH58x 系列】CH585/CH584 数据手册 V1.6 选型表（BLE 5.4 + USB）
#   CH585M    - CH585M (448+24+32KB Flash, 128KB RAM, QFN48, 40 GPIO, USBHS)
#   CH585F    - CH585F (448+24+32KB Flash, 128KB RAM, QFN32, 24 GPIO)
#   CH585C    - CH585C (448+24+32KB Flash, 128KB RAM, QFN26C3, 17 GPIO)
#   CH585D    - CH585D (448+24+32KB Flash, 128KB RAM, QFN20, 12 GPIO)
#   CH584M    - CH584M (448+24+32KB Flash,  96KB RAM, QFN48, 40 GPIO)
#   CH584X    - CH584X (480+24+512KB Flash, 96KB RAM, QFN48, 40 GPIO)
#   CH584F    - CH584F (448+24+32KB Flash,  96KB RAM, QFN32, 24 GPIO)
#   CH585/CH584 - 兼容别名（分别等同于 CH585F / CH584F）
#
# 【CH32 系列】（rv32imac，无 BLE）
#   CH32V003  - CH32V003 (RV32EC, 2KB RAM, 16KB Flash, 无 BLE/USB)
#   CH32X035  - CH32X035 (62KB Flash, 20KB RAM, USB FS + PDUSB, 无 BLE)
#   CH32L103  - CH32L103 (64KB Flash, 20KB RAM, USB FS + PDUSB, 无 BLE)
#   CH32V203  - CH32V203 (32/64/128KB Flash, 10/20/64KB RAM, 双 USB，无 BLE)
#
# 优先级（从高到低）：
#   1. 命令行 -DCHIP_SDK=CH592        （覆盖，最高）
#   2. 环境变量 CHIP_SDK
#   3. 项目根 default_target.txt 里的 `chip = ...`
#   4. 默认 CH572
#
# 变量说明：
#   CHIP_SDK          —— 命令行/环境变量覆盖值（用户可见，一般不手动改）
#   CHIP_SDK_RESOLVED —— 最终生效的芯片（内部使用，供 sdk.cmake 等消费）
# ===========================================================================

# ---------------------------------------------------------------------------
# 1) 读取 default_target.txt 里的 `chip = ...`
# ---------------------------------------------------------------------------
set(_cfg_chip "")
set(_cfg_dt "${CMAKE_CURRENT_SOURCE_DIR}/default_target.txt")
if(EXISTS "${_cfg_dt}")
    file(STRINGS "${_cfg_dt}" _cfg_lines
         REGEX "^[ \t]*[^# \t\r\n][^\r\n]*")
    foreach(_ln ${_cfg_lines})
        string(STRIP "${_ln}" _ln)
        if(_ln MATCHES "^[Cc][Hh][Ii][Pp][ \t]*=[ \t]*(.+)$")
            string(STRIP "${CMAKE_MATCH_1}" _cfg_chip)
            break()
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 2) 解析优先级：命令行 -DCHIP_SDK > 环境变量 CHIP_SDK > 文件 > 默认
# ---------------------------------------------------------------------------
# 说明：
#   - CHIP_SDK（缓存）用于命令行覆盖，一旦用 -D 指定会一直保留（CMake 行为）。
#   - 为了让「修改 default_target.txt 后重新配置即生效」，用 CHIP_SDK_FILE_LAST
#     记录上次读到的文件值：只要文件里的 chip 发生变化，就清除命令行覆盖，
#     改以文件值为准。
#   - 想临时切回文件值又不想改文件：删除 build 目录，或再传一次 -D。
if(DEFINED CHIP_SDK_FILE_LAST AND NOT _cfg_chip STREQUAL CHIP_SDK_FILE_LAST)
    # 文件值相对上次发生了变化 → 放弃旧的命令行覆盖
    if(DEFINED CHIP_SDK AND NOT CHIP_SDK STREQUAL "")
        unset(CHIP_SDK CACHE)
    endif()
endif()
# 记录本次文件值（供下次比较）
set(CHIP_SDK_FILE_LAST "${_cfg_chip}" CACHE INTERNAL "上次读到的 default_target.txt chip 值" FORCE)

if(DEFINED CHIP_SDK AND NOT CHIP_SDK STREQUAL "")
    set(_cfg_use "${CHIP_SDK}")
elseif(DEFINED ENV{CHIP_SDK} AND NOT "$ENV{CHIP_SDK}" STREQUAL "")
    set(_cfg_use "$ENV{CHIP_SDK}")
elseif(NOT _cfg_chip STREQUAL "")
    set(_cfg_use "${_cfg_chip}")
else()
    set(_cfg_use "CH572")
endif()

set(CHIP_SDK_RESOLVED "${_cfg_use}" CACHE STRING "最终生效的芯片：CH572 / CH592X / CH592F / CH592A / CH592D / CH591F / CH591D / CH591R / CH585M / CH585F / CH585C / CH585D / CH584M / CH584X / CH584F / CH32V003 / CH32X035 / CH32L103 / CH32V203" FORCE)
set_property(CACHE CHIP_SDK_RESOLVED PROPERTY STRINGS
    CH572
    CH592X CH592F CH592A CH592D CH591F CH591D CH591R CH592 CH591
    CH585M CH585F CH585C CH585D CH584M CH584X CH584F CH585 CH584
    CH32V003 CH32X035 CH32L103 CH32V203)

# ---------------------------------------------------------------------------
# 芯片系列判定（供各 CMakeLists 复用，避免重复罗列型号）
# ---------------------------------------------------------------------------
# CH59x 系列：CH592X/F/A/D + CH591F/D/R（+ 兼容别名 CH592/CH591）
set(_ch59x_list CH592X CH592F CH592A CH592D CH591F CH591D CH591R CH592 CH591)
if(CHIP_SDK_RESOLVED IN_LIST _ch59x_list)
    set(CHIP_IS_CH59X TRUE)
else()
    set(CHIP_IS_CH59X FALSE)
endif()

# CH591 系列（192KB Flash，需激进裁剪）：CH591F/D/R + 别名 CH591
set(_ch591_list CH591F CH591D CH591R CH591)
if(CHIP_SDK_RESOLVED IN_LIST _ch591_list)
    set(CHIP_IS_CH591 TRUE)
else()
    set(CHIP_IS_CH591 FALSE)
endif()

# CH592 系列（448KB Flash）：CH592X/F/A/D + 别名 CH592
set(_ch592_list CH592X CH592F CH592A CH592D CH592)
if(CHIP_SDK_RESOLVED IN_LIST _ch592_list)
    set(CHIP_IS_CH592 TRUE)
else()
    set(CHIP_IS_CH592 FALSE)
endif()

# CH58x 系列（BLE 5.4 + USB，CH585/CH584）：M/F/C/D/X + 别名 CH585/CH584
set(_ch58x_list CH585M CH585F CH585C CH585D CH584M CH584X CH584F CH585 CH584)
if(CHIP_SDK_RESOLVED IN_LIST _ch58x_list)
    set(CHIP_IS_CH58X TRUE)
else()
    set(CHIP_IS_CH58X FALSE)
endif()

# CH585 系列（128KB RAM）：M/F/C/D + 别名 CH585
set(_ch585_list CH585M CH585F CH585C CH585D CH585)
if(CHIP_SDK_RESOLVED IN_LIST _ch585_list)
    set(CHIP_IS_CH585 TRUE)
else()
    set(CHIP_IS_CH585 FALSE)
endif()

# CH584 系列（96KB RAM，去掉了 USBHS + SPI1）：M/X/F + 别名 CH584
set(_ch584_list CH584M CH584X CH584F CH584)
if(CHIP_SDK_RESOLVED IN_LIST _ch584_list)
    set(CHIP_IS_CH584 TRUE)
else()
    set(CHIP_IS_CH584 FALSE)
endif()

# CH32RISC-V 系列（ch32fun 的 V4 内核芯片，rv32imac）
set(_ch32rv_list CH32V003 CH32X035 CH32L103 CH32V203)
if(CHIP_SDK_RESOLVED IN_LIST _ch32rv_list)
    set(CHIP_IS_CH32RV TRUE)
else()
    set(CHIP_IS_CH32RV FALSE)
endif()

# CH32V20x 系列（V203）
set(_ch32v20x_list CH32V203)
if(CHIP_SDK_RESOLVED IN_LIST _ch32v20x_list)
    set(CHIP_IS_CH32V20X TRUE)
else()
    set(CHIP_IS_CH32V20X FALSE)
endif()

# 具备 BLE 能力的芯片（CH57x / CH58x / CH59x）
set(_ble_list
    CH572
    CH592X CH592F CH592A CH592D CH591F CH591D CH591R CH592 CH591
    CH585M CH585F CH585C CH585D CH584M CH584X CH584F CH585 CH584)
if(CHIP_SDK_RESOLVED IN_LIST _ble_list)
    set(CHIP_HAS_BLE TRUE)
else()
    set(CHIP_HAS_BLE FALSE)
endif()

# 具备 USB 能力的芯片（CH57x / CH58x / CH59x / CH32X035 / CH32L103 / CH32V203）
set(_usb_list
    CH572
    CH592X CH592F CH592A CH592D CH591F CH591D CH591R CH592 CH591
    CH585M CH585F CH585C CH585D CH584M CH584X CH584F CH585 CH584
    CH32X035 CH32L103 CH32V203)
if(CHIP_SDK_RESOLVED IN_LIST _usb_list)
    set(CHIP_HAS_USB TRUE)
else()
    set(CHIP_HAS_USB FALSE)
endif()

# ---------------------------------------------------------------------------
# SDK 目录映射
# ---------------------------------------------------------------------------
set(SDK_ROOT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(SDK_COMMON_DIR "${SDK_ROOT_DIR}/common")

if(CHIP_SDK_RESOLVED STREQUAL "CH572")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH572_DEV")
    set(SDK_CHIP_NAME "CH572")
elseif(CHIP_IS_CH59X)
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH592_DEV")
    set(SDK_CHIP_NAME "${CHIP_SDK_RESOLVED}")
elseif(CHIP_IS_CH58X)
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH585_DEV")
    set(SDK_CHIP_NAME "${CHIP_SDK_RESOLVED}")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH32V003_DEV")
    set(SDK_CHIP_NAME "CH32V003")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32X035")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH32X035_DEV")
    set(SDK_CHIP_NAME "CH32X035")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32L103")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH32L103_DEV")
    set(SDK_CHIP_NAME "CH32L103")
elseif(CHIP_IS_CH32V20X)
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH32V20x_DEV")
    set(SDK_CHIP_NAME "${CHIP_SDK_RESOLVED}")
else()
    message(FATAL_ERROR "不支持的芯片：${CHIP_SDK_RESOLVED}\n"
        "可选：CH572 / CH592X|F|A|D / CH591F|D|R / CH585M|F|C|D / CH584M|X|F / "
        "CH32V003 / CH32X035 / CH32L103 / CH32V203")
endif()

if(NOT EXISTS "${SDK_CHIP_DIR}")
    message(FATAL_ERROR "SDK 目录不存在：${SDK_CHIP_DIR}")
endif()

message(STATUS "目标芯片 SDK：${SDK_CHIP_NAME}")
message(STATUS "  SDK 根目录：${SDK_ROOT_DIR}")
message(STATUS "  芯片目录  ：${SDK_CHIP_DIR}")
