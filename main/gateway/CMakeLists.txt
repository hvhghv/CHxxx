# CH591/CH592 多功能网关（裸机，无 FreeRTOS）
#
# 3×CDC + RNDIS + LWIP（NO_SYS=1，raw API）+ BLE + DHCP。

cmake_minimum_required(VERSION 3.20)

# ---- 子型号（决定 Flash 裁剪策略）------------------------------------------
# 新架构按芯片族构建（ch59x = CH591 + CH592），子型号通过缓存变量指定：
#   CH591R / CH591D / CH591F  → 192K Flash，裁剪
#   CH592D / CH592F / CH592A / CH592X → 448K Flash，全功能
set(CHIP_SUB "CH592F" CACHE STRING "CH59x 子型号（CH591R/D/F 或 CH592D/F/A/X）")
if(CHIP_SUB MATCHES "^CH591")
    set(CHIP_IS_CH591 TRUE)
    set(CHIP_SDK_RESOLVED "${CHIP_SUB}")
else()
    set(CHIP_IS_CH591 FALSE)
    set(CHIP_SDK_RESOLVED "${CHIP_SUB}")
endif()
message(STATUS "gateway 子型号：${CHIP_SUB}（CH591 系列=${CHIP_IS_CH591}）")

hal_add_executable(gateway
    SOURCES
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
        spi_bus.c
        httpd.c
        swio.c
        bleapp.c
        blemgr.c
        dhcps.c
        util.c
    LWIP
    BLE
    USB
    UART
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

# ---- LWIP 项目级配置 --------------------------------------------------------
# main/gateway/lwipopts.h 是项目独立配置（不共享 SDK 的 lwipopts.h）。
# 必须放在 include 路径最前面，优先于 SDK 的 lwip/port 目录。
target_include_directories(gateway BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- LWIP 协议裁剪 ----------------------------------------------------------
# 控制 LWIP 编译哪些协议（省 Flash）。
#   UDP  : DHCP 服务器必需（关掉则 PC 需手动配 IP）
#   RAW  : 自定义原始 IP 协议（本项目未用）
#   ICMP : ping / ICMP 错误响应（关掉则无法 ping）
# 默认：CH592 全开；CH591 系列开启 UDP/ICMP，仅关闭 RAW（本项目未用）。
#
# 芯片系列判定（CH591F / CH591D / CH591R 均为 192K Flash，裁剪策略相同）：
# CHIP_IS_CH591 由 sdk_config.cmake 统一提供。

option(GW_LWIP_UDP  "LWIP UDP 支持（DHCP 服务器需要）" ON)
option(GW_LWIP_RAW  "LWIP RAW API 支持" ON)
option(GW_LWIP_ICMP "LWIP ICMP 支持（ping）" ON)
if(CHIP_IS_CH591)
    set(GW_LWIP_RAW  OFF)   # RAW 本项目未用，CH591 系列继续关闭
endif()
if(GW_LWIP_UDP)
    target_compile_definitions(gateway PRIVATE GW_LWIP_UDP=1)
else()
    target_compile_definitions(gateway PRIVATE GW_LWIP_UDP=0)
endif()
if(GW_LWIP_RAW)
    target_compile_definitions(gateway PRIVATE GW_LWIP_RAW=1)
else()
    target_compile_definitions(gateway PRIVATE GW_LWIP_RAW=0)
endif()
if(GW_LWIP_ICMP)
    target_compile_definitions(gateway PRIVATE GW_LWIP_ICMP=1)
else()
    target_compile_definitions(gateway PRIVATE GW_LWIP_ICMP=0)
endif()
message(STATUS "gateway LWIP 协议：UDP=${GW_LWIP_UDP} RAW=${GW_LWIP_RAW} ICMP=${GW_LWIP_ICMP}")

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
# 默认：CH591 系列 → perf（Flash 仅 192K）；其它 → dual
set(BLE_ROLE "dual" CACHE STRING "BLE role mode: perf | cent | dual")

if(CHIP_IS_CH591 AND BLE_ROLE STREQUAL "dual")
    message(STATUS "${CHIP_SDK_RESOLVED} 检测到：自动将 BLE_ROLE 从 dual 降为 perf（Flash 限制）")
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
# 默认：CH592 开启；CH591 系列关闭（Flash 仅 192K）。
option(GW_EMBED_WEBUI "内嵌 Web UI fallback" ON)

if(CHIP_IS_CH591 AND GW_EMBED_WEBUI)
    message(STATUS "${CHIP_SDK_RESOLVED} 检测到：自动关闭 GW_EMBED_WEBUI（Flash 限制）")
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
if(CHIP_IS_CH591 AND GW_CDC_COUNT GREATER 3)
    message(STATUS "${CHIP_SDK_RESOLVED} 检测到：自动将 GW_CDC_COUNT 从 ${GW_CDC_COUNT} 降为 3（资源限制）")
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

# ---- 引脚数量 --------------------------------------------------------------
# 紧凑引脚编号空间（0..GW_PIN_COUNT-1），映射到物理引脚（见 pinmux.c g_pin_map）。
#   10 = CH591R TSSOP16（PA8/9/12/13/14/15 + PB10/11/22/23）
#   10 = CH591D QFN20（PA8/9/12/13/14/15 + PB10/11/22/23）
#   12 = CH592D QFN20（PA8/9/10/11/12/13/14/15 + PB4/7/10/11）
#   20 = CH591F QFN28（PA4/5/8/9/10-15 + PB4/7/10-15/22-23）
#   20 = CH592F/A QFN28（PA4/5/8-15 + PB4/7/10-15/22-23）
#   24 = CH592X QFN32（PA4-15 + PB0/4/6/7/10-15/22-23）
# 减小该值可省 RAM（g_cfg.pins[] 数组）与 Flash。
# 注意：芯片相关值必须用 CACHE ... FORCE 覆盖，否则用户在缓存里留下的旧值
#       （如切换芯片前设的 24）会覆盖这里的 set()，导致 g_pin_map[] 越界。
set(GW_PIN_COUNT 24 CACHE STRING "引脚数量")
if(CHIP_SDK_RESOLVED STREQUAL "CH591R" OR CHIP_SDK_RESOLVED STREQUAL "CH591D")
    set(GW_PIN_COUNT 10 CACHE STRING "引脚数量" FORCE)
elseif(CHIP_SDK_RESOLVED STREQUAL "CH592D")
    set(GW_PIN_COUNT 12 CACHE STRING "引脚数量" FORCE)
elseif(CHIP_SDK_RESOLVED STREQUAL "CH592F" OR CHIP_SDK_RESOLVED STREQUAL "CH592A")
    set(GW_PIN_COUNT 20 CACHE STRING "引脚数量" FORCE)
elseif(CHIP_IS_CH591)
    set(GW_PIN_COUNT 20 CACHE STRING "引脚数量" FORCE)
endif()
target_compile_definitions(gateway PRIVATE GW_PIN_COUNT=${GW_PIN_COUNT})
message(STATUS "gateway 引脚数量：${GW_PIN_COUNT}")

# ---- SPI 多设备 ------------------------------------------------------------
# 开启：支持多 CS/INT 引脚、主从切换、软件 CS（占 ~1.3KB Flash）。
# 关闭：仅单设备基本 SPI（framelink 的 spi master/slave 命令仍可用）。
# 默认：CH592 / CH591 均开启（CH591 关闭 LWIP UDP/RAW/ICMP 后空间已足够）。
option(GW_SPI_MULTIDEV "SPI 多设备（多 CS/INT + 主从切换）" ON)
if(GW_SPI_MULTIDEV)
    target_compile_definitions(gateway PRIVATE GW_SPI_MULTIDEV=1)
    message(STATUS "gateway SPI 多设备：开启")
else()
    target_compile_definitions(gateway PRIVATE GW_SPI_MULTIDEV=0)
    message(STATUS "gateway SPI 多设备：关闭（仅单设备）")
endif()

# ---- SPI 角色 --------------------------------------------------------------
# 编译期裁剪 SPI 角色支持（省 Flash）：
#   master = 仅主机（无从机代码）
#   slave  = 仅从机（无主机代码）
#   both   = 主机 + 从机
# 默认：CH591 系列 → slave（Flash 仅 192K）；其它 → both
set(GW_SPI_ROLE "both" CACHE STRING "SPI 角色：master | slave | both")
if(CHIP_IS_CH591 AND GW_SPI_ROLE STREQUAL "both")
    message(STATUS "${CHIP_SDK_RESOLVED} 检测到：自动将 GW_SPI_ROLE 从 both 降为 slave（Flash 限制）")
    set(GW_SPI_ROLE "slave")
endif()
if(GW_SPI_ROLE STREQUAL "master")
    target_compile_definitions(gateway PRIVATE GW_SPI_ROLE_MASTER=1 GW_SPI_ROLE_SLAVE=0)
elseif(GW_SPI_ROLE STREQUAL "slave")
    target_compile_definitions(gateway PRIVATE GW_SPI_ROLE_MASTER=0 GW_SPI_ROLE_SLAVE=1)
elseif(GW_SPI_ROLE STREQUAL "both")
    target_compile_definitions(gateway PRIVATE GW_SPI_ROLE_MASTER=1 GW_SPI_ROLE_SLAVE=1)
else()
    message(FATAL_ERROR "GW_SPI_ROLE 必须是 master / slave / both，当前：${GW_SPI_ROLE}")
endif()
message(STATUS "gateway SPI 角色：${GW_SPI_ROLE}")

# ---- 终端命令 --------------------------------------------------------------
# 开启：CDC-A 提供交互式命令行（help/info/pins/spi/swio/ble/...）。
# 关闭：CDC-A 仅输出日志（启动信息、错误提示），不解析任何输入命令。
#       省 Flash：命令表 + 全部 cmd_* 函数 + 行缓冲 + 输入回显。
# 默认：CH592 开启；CH591 系列关闭（Flash 仅 192K）。
option(GW_TERM "终端命令支持（关闭则仅输出日志）" ON)
if(CHIP_IS_CH591 AND GW_TERM)
    message(STATUS "${CHIP_SDK_RESOLVED} 检测到：自动关闭 GW_TERM（仅输出日志，Flash 限制）")
    set(GW_TERM OFF)
endif()
if(GW_TERM)
    target_compile_definitions(gateway PRIVATE GW_TERM=1)
    message(STATUS "gateway 终端命令：开启")
else()
    target_compile_definitions(gateway PRIVATE GW_TERM=0)
    message(STATUS "gateway 终端命令：关闭（CDC-A 仅输出日志）")
endif()
