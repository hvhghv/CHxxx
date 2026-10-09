# CH591/CH592 多功能网关（裸机，无 FreeRTOS）
#
# 3×CDC + RNDIS + LWIP（NO_SYS=1，raw API）+ BLE + DHCP。

cmake_minimum_required(VERSION 3.20)

if(NOT COMMAND ch572_add_executable)
    get_filename_component(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" ABSOLUTE)
    include(${SDK_ROOT}/common/cmake/sdk.cmake)
endif()

ch572_add_executable(gateway
    gateway.c
    pinmux.c
    config.c
    settings.c
    crc.c
    frame.c
    frame_wire.c
    framelink.c
    framehdl.c
    netroute.c
    periph.c
    httpd.c
    swio.c
    bleapp.c
    blemgr.c
    dhcps.c
    util.c
    USB
    LWIP
    LWIP_NOSYS
    LWIP_NO_ETHIF
    BLE
)

# ---- Web UI：HTML → C 头文件（构建时生成）----------------------------------
# webui.html 是纯 HTML（可浏览器预览）；构建时用 tools/html2h.py 转成
# webui_html.h（C 字符串数组），由 webui.h #include。
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(WEBUI_HTML   "${CMAKE_CURRENT_SOURCE_DIR}/webui.html")
set(WEBUI_GEN_H  "${CMAKE_CURRENT_BINARY_DIR}/webui_html.h")

add_custom_command(
    OUTPUT  "${WEBUI_GEN_H}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/html2h.py"
            "${WEBUI_HTML}" "${WEBUI_GEN_H}" "WEBUI_HTML"
    DEPENDS "${WEBUI_HTML}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/html2h.py"
    COMMENT "生成 webui_html.h（来自 webui.html）"
    VERBATIM
)
add_custom_target(webui_gen DEPENDS "${WEBUI_GEN_H}")
add_dependencies(gateway webui_gen)

# 让编译器能找到生成的头文件
target_include_directories(gateway PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# BLE 内存堆：CH592 版 ble_hal.c 硬性要求 >= 4KB（否则死循环）。
# BufNumber 必须 > 连接数（1 从机 + 3 主机 = 4），故取 5。
target_compile_definitions(gateway PRIVATE
    BLE_MEMHEAP_SIZE=4096
    BLE_BUFF_NUM=5
    BLE_SNV=FALSE
)

# ---- BLE 角色模式 -----------------------------------------------------------
# BLE_ROLE 可选值：
#   perf  = 仅从机（Peripheral）—— 省 Flash（无 Central 栈 ~28KB），CH591 可用
#   cent  = 仅主机（Central）
#   dual  = 双模式（从机 + 主机，默认）
# 默认：CH591 → perf（Flash 仅 192K）；其它 → dual
set(BLE_ROLE "dual" CACHE STRING "BLE role mode: perf | cent | dual")

if(CHIP_SDK_RESOLVED STREQUAL "CH591" AND BLE_ROLE STREQUAL "dual")
    message(STATUS "CH591 检测到：自动将 BLE_ROLE 从 dual 降为 perf（Flash 限制）")
    set(BLE_ROLE "perf")
endif()

if(BLE_ROLE STREQUAL "perf")
    set(_ble_role_mode 1)
elseif(BLE_ROLE STREQUAL "cent")
    set(_ble_role_mode 2)
else()
    set(_ble_role_mode 3)
endif()

target_compile_definitions(gateway PRIVATE BLE_ROLE_MODE=${_ble_role_mode})
message(STATUS "gateway BLE 角色模式：${BLE_ROLE} (BLE_ROLE_MODE=${_ble_role_mode})")

# ---- BLE UUID128 支持 -------------------------------------------------------
# 关闭时仅支持 16bit UUID，省 RAM（帧接收缓冲更小）与 Flash。
option(BLE_UUID128 "支持 128bit UUID（含帧负载扩容）" OFF)

if(BLE_UUID128)
    target_compile_definitions(gateway PRIVATE BLE_UUID128_SUPPORT=1)
    message(STATUS "gateway BLE UUID128：开启（帧负载上限 128）")
else()
    target_compile_definitions(gateway PRIVATE BLE_UUID128_SUPPORT=0)
    message(STATUS "gateway BLE UUID128：关闭（仅 16bit UUID，帧负载上限 48）")
endif()

# ---- HTTP 固件上传（SWIO）--------------------------------------------------
# 强制启用（不再提供开关）。上传状态机与流式烧录始终编译。

# ---- 内嵌 Web UI（fallback）------------------------------------------------
# 开启：未配置前端 URL 时，首页使用内嵌 webui.html（占 ~2.5KB Flash）。
# 关闭：未配置前端 URL 时，首页显示提示（省 Flash）。
# 默认：CH592 开启；CH591 关闭（Flash 仅 192K）。
option(GW_EMBED_WEBUI "内嵌 Web UI fallback" ON)

if(CHIP_SDK_RESOLVED STREQUAL "CH591" AND GW_EMBED_WEBUI)
    message(STATUS "CH591 检测到：自动关闭 GW_EMBED_WEBUI（Flash 限制）")
    set(GW_EMBED_WEBUI OFF)
endif()

if(GW_EMBED_WEBUI)
    target_compile_definitions(gateway PRIVATE GW_EMBED_WEBUI=1)
    message(STATUS "gateway 内嵌 Web UI：开启")
else()
    target_compile_definitions(gateway PRIVATE GW_EMBED_WEBUI=0)
    message(STATUS "gateway 内嵌 Web UI：关闭（仅外部前端）")
endif()

# ---- CDC 数量 --------------------------------------------------------------
# 控制 USB 申请多少个 CDC-ACM 接口（含终端口 CDC-A）。
#   3 = 终端 + 2 个 UART 转发（CDC-A/B/C）—— CH591 默认（Flash/RAM 受限）
#   4 = 终端 + 3 个 UART 转发（CDC-A/B/C/D）—— CH592 默认
# 注意：CDC 数量受 USB 端点限制（CH59x 可用 EP1/2/3/5/6/7，EP4 不可用）。
set(GW_CDC_COUNT 4 CACHE STRING "CDC-ACM 数量（3 或 4）")
if(CHIP_SDK_RESOLVED STREQUAL "CH591" AND GW_CDC_COUNT GREATER 3)
    message(STATUS "CH591 检测到：自动将 GW_CDC_COUNT 从 ${GW_CDC_COUNT} 降为 3（资源限制）")
    set(GW_CDC_COUNT 3)
endif()
target_compile_definitions(gateway PRIVATE GW_CDC_COUNT=${GW_CDC_COUNT})
message(STATUS "gateway CDC 数量：${GW_CDC_COUNT}")

# ---- CDC 动态绑定 ----------------------------------------------------------
# 开启：UART 可绑定到任意 CDC 转发口（绑定号存 pin_cfg.flags，可在 Web UI 配置）。
# 关闭：固定映射（UART0→CDC-B, UART1→CDC-C, UART2→CDC-D），省 Flash。
# 默认：CH592 开启；CH591 强制关闭（Flash 受限）。
option(GW_CDC_DYNAMIC "CDC 动态绑定（UART 可绑定任意 CDC）" ON)
if(GW_CDC_COUNT LESS 4 AND GW_CDC_DYNAMIC)
    message(STATUS "CDC 数量 <4：自动关闭 GW_CDC_DYNAMIC（固定映射）")
    set(GW_CDC_DYNAMIC OFF)
endif()
if(GW_CDC_DYNAMIC)
    target_compile_definitions(gateway PRIVATE GW_CDC_DYNAMIC=1)
    message(STATUS "gateway CDC 动态绑定：开启")
else()
    target_compile_definitions(gateway PRIVATE GW_CDC_DYNAMIC=0)
    message(STATUS "gateway CDC 动态绑定：关闭（固定映射）")
endif()
