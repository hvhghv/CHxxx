# ===========================================================================
# SDK 选择配置
# ===========================================================================
# 选择目标芯片 SDK。
#
# 支持的值：
#   CH572     - CH572 (RV32IMAC, 12KB RAM, 240KB Flash, BLE 5.0)
#   CH592     - CH592 (RV32IMAC, 26KB RAM, 448KB Flash, BLE 5.4)
#   CH591     - CH591 (RV32IMAC, 26KB RAM, 192KB Flash, BLE 5.4)
#   CH32V003  - CH32V003 (RV32EC, 2KB RAM, 16KB Flash, 无 BLE/USB)
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

set(CHIP_SDK_RESOLVED "${_cfg_use}" CACHE STRING "最终生效的芯片：CH572 / CH592 / CH591 / CH32V003" FORCE)
set_property(CACHE CHIP_SDK_RESOLVED PROPERTY STRINGS CH572 CH592 CH591 CH32V003)

# ---------------------------------------------------------------------------
# SDK 目录映射
# ---------------------------------------------------------------------------
set(SDK_ROOT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(SDK_COMMON_DIR "${SDK_ROOT_DIR}/common")

if(CHIP_SDK_RESOLVED STREQUAL "CH572")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH572_DEV")
    set(SDK_CHIP_NAME "CH572")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH592" OR CHIP_SDK_RESOLVED STREQUAL "CH591")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH592_DEV")
    set(SDK_CHIP_NAME "${CHIP_SDK_RESOLVED}")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    set(SDK_CHIP_DIR "${SDK_ROOT_DIR}/CH32V003_DEV")
    set(SDK_CHIP_NAME "CH32V003")
else()
    message(FATAL_ERROR "不支持的芯片：${CHIP_SDK_RESOLVED}（可选 CH572 / CH592 / CH591 / CH32V003）")
endif()

if(NOT EXISTS "${SDK_CHIP_DIR}")
    message(FATAL_ERROR "SDK 目录不存在：${SDK_CHIP_DIR}")
endif()

message(STATUS "目标芯片 SDK：${SDK_CHIP_NAME}")
message(STATUS "  SDK 根目录：${SDK_ROOT_DIR}")
message(STATUS "  芯片目录  ：${SDK_CHIP_DIR}")
