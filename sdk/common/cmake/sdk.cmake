# CH572 Lite SDK - CMake 模块
# ---------------------------------------------------------------------------
# 用法（工程 CMakeLists.txt）：
#
#   cmake_minimum_required(VERSION 3.20)
#   project(myapp C ASM)
#
#   set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
#   include(${SDK_ROOT}/cmake/sdk.cmake)
#
#   ch572_add_executable(myapp myapp.c)
#
# 配置：
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=${SDK_ROOT}/cmake/riscv.cmake
#   cmake --build build
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# 优化级别：全局强制 -Os（体积优先）
# ---------------------------------------------------------------------------
# 无论 CMAKE_BUILD_TYPE 如何设置，所有编译单元统一使用 -Os：
#   - 先移除 CMAKE_C_FLAGS / CMAKE_C_FLAGS_<CONFIG> 中已有的 -O* 选项
#   - 再追加 -Os，确保唯一且一致
# 注意：BLE 库 libCH59xBLE.a 为预编译静态库，其优化级别由 WCH 决定，无法修改。
foreach(_lang C CXX ASM)
    string(REGEX REPLACE "-O[0-9sgz]+" "" _sdk_cflags "${CMAKE_${_lang}_FLAGS}")
    set(CMAKE_${_lang}_FLAGS "${_sdk_cflags} -Os" CACHE STRING "SDK: 强制 -Os" FORCE)
    foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(REGEX REPLACE "-O[0-9sgz]+" "" _sdk_cfg "${CMAKE_${_lang}_FLAGS_${_cfg}}")
        set(CMAKE_${_lang}_FLAGS_${_cfg} "${_sdk_cfg} -Os" CACHE STRING "SDK: 强制 -Os" FORCE)
    endforeach()
endforeach()

# ---------------------------------------------------------------------------
# SDK 路径
# ---------------------------------------------------------------------------
# 注意：这些变量用 CACHE INTERNAL 存储，而不是普通 set()。
# 原因：sdk.cmake 可能被 sdk/CMakeLists.txt 通过 include() 引入，
# 当该子目录处理完毕，其作用域就结束了。父工程随后调用
# ch572_add_executable() 时，普通变量已不可见，会导致路径/宏为空。
# CACHE INTERNAL 变量全局可见且不会出现在 CMake GUI 中。
#
# 目录结构（多芯片）：
#   sdk/common/          <- 本文件所在（共享）
#   sdk/CH572_DEV/       <- 芯片专属
#   sdk/CH592_DEV/
#
# SDK_CHIP_DIR 由 sdk/CMakeLists.txt 设置（根据 CHIP_SDK_RESOLVED 选择）。
get_filename_component(_sdk_common_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT SDK_CHIP_DIR)
    # 兼容旧结构（CH572_DEV 直接作为 SDK 根）
    get_filename_component(_sdk_chip_root "${_sdk_common_root}/.." ABSOLUTE)
    set(SDK_CHIP_DIR "${_sdk_chip_root}" CACHE INTERNAL "芯片 SDK 目录")
endif()

set(CH572_SDK_ROOT     "${_sdk_common_root}"                CACHE INTERNAL "SDK 共享根目录")
set(CH572_CORE_DIR     "${_sdk_common_root}/core"           CACHE INTERNAL "共享核心目录")
set(CH572_EXTRALIB_DIR "${_sdk_common_root}/extralibs"      CACHE INTERNAL "共享外设库目录")
set(CH572_BLE_DIR      "${SDK_CHIP_DIR}/ble"                CACHE INTERNAL "芯片 BLE 目录")
set(CH572_FREERTOS_DIR "${SDK_CHIP_DIR}/freertos"           CACHE INTERNAL "芯片 FreeRTOS 目录")
set(CH572_LWIP_DIR     "${SDK_CHIP_DIR}/lwip"               CACHE INTERNAL "芯片 LWIP 目录")
set(CH572_CMAKE_DIR    "${CMAKE_CURRENT_LIST_DIR}"          CACHE INTERNAL "cmake 模块目录")
set(CH572_CHIP_CORE_DIR "${SDK_CHIP_DIR}/core"              CACHE INTERNAL "芯片核心目录")

message(STATUS "SDK 共享核心：${CH572_CORE_DIR}")
message(STATUS "SDK 芯片核心：${CH572_CHIP_CORE_DIR}")
message(STATUS "SDK 芯片 BLE ：${CH572_BLE_DIR}")

# ---------------------------------------------------------------------------
# wchisp 烧录工具（USB ISP，无需编程器）
# ---------------------------------------------------------------------------
# CH572 内置 USB Bootloader，上电时 PA1 拉低即进入 ISP 模式。
# wchisp 通过 USB 直接烧录，适合只有 USB 口、没有调试口的板子。
#
# 自动探测顺序：
#   1. 项目根的 tool/wchisp/
#   2. SDK 同级的 tool/wchisp/
#   3. PATH 中的 wchisp
#
# 可用 -DCH572_WCHISP=<path> 覆盖。
if(NOT CH572_WCHISP)
    get_filename_component(_proj_root2 "${_sdk_common_root}/../.." ABSOLUTE)

    if(CMAKE_HOST_WIN32)
        set(_wi_name "wchisp.exe")
    else()
        set(_wi_name "wchisp")
    endif()

    foreach(_cand
            "${_proj_root2}/tool/wchisp/${_wi_name}"
            "${_sdk_common_root}/tool/wchisp/${_wi_name}")
        if(EXISTS "${_cand}")
            set(CH572_WCHISP "${_cand}" CACHE FILEPATH "wchisp 可执行文件路径")
            break()
        endif()
    endforeach()

    if(NOT CH572_WCHISP)
        find_program(CH572_WCHISP NAMES wchisp)
    endif()
endif()

if(CH572_WCHISP AND EXISTS "${CH572_WCHISP}")
    message(STATUS "CH572 wchisp    ：${CH572_WCHISP}")
else()
    message(STATUS "CH572 wchisp    ：未找到（USB 烧录目标不可用）")
    set(CH572_WCHISP "" CACHE FILEPATH "wchisp 可执行文件路径" FORCE)
endif()

# ---------------------------------------------------------------------------
# 默认烧录方式
# ---------------------------------------------------------------------------
# usb = wchisp（USB Bootloader，无需编程器）
#
# 可用 -DCH572_FLASH_METHOD=usb 显式指定。
if(NOT CH572_FLASH_METHOD)
    if(CH572_WCHISP)
        set(CH572_FLASH_METHOD "usb" CACHE STRING "默认烧录方式：usb")
    else()
        set(CH572_FLASH_METHOD "" CACHE STRING "默认烧录方式：usb")
    endif()
endif()
message(STATUS "CH572 烧录方式  ：${CH572_FLASH_METHOD}")

# ---------------------------------------------------------------------------
# 目标芯片配置（按 CHIP_SDK_RESOLVED 选择）
# ---------------------------------------------------------------------------
# TARGET_MCU_LD 对应 ch32fun.ld 中的分支：
#   0  = CH32V003    (RAM 2K,  Flash 16K)
#   9  = CH591/CH592 (RAM 26K)
#   10 = CH57x       (RAM 12K/18K)
if(CHIP_SDK_RESOLVED STREQUAL "CH572")
    set(CH572_MCU_PACKAGE 2  CACHE INTERNAL "MCU_PACKAGE")
    set(CH572_MCU_LD      10 CACHE INTERNAL "TARGET_MCU_LD")
    set(CH572_CHIP_DEFINE "CH57x" CACHE INTERNAL "芯片宏")
    set(CH572_FAMILY_DEFINE "CH5xx" CACHE INTERNAL "系列宏")
    set(CH572_MARCH "rv32imac" CACHE STRING "RISC-V 架构扩展")
    set(CH572_MABI  "ilp32"   CACHE STRING "RISC-V ABI")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH591")
    set(CH572_MCU_PACKAGE 1  CACHE INTERNAL "MCU_PACKAGE (CH591: Flash 192K)")
    set(CH572_MCU_LD      9  CACHE INTERNAL "TARGET_MCU_LD")
    set(CH572_CHIP_DEFINE "CH59x" CACHE INTERNAL "芯片宏")
    set(CH572_FAMILY_DEFINE "CH5xx" CACHE INTERNAL "系列宏")
    set(CH572_MARCH "rv32imac" CACHE STRING "RISC-V 架构扩展")
    set(CH572_MABI  "ilp32"   CACHE STRING "RISC-V ABI")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH592")
    set(CH572_MCU_PACKAGE 2  CACHE INTERNAL "MCU_PACKAGE (CH592: Flash 448K)")
    set(CH572_MCU_LD      9  CACHE INTERNAL "TARGET_MCU_LD")
    set(CH572_CHIP_DEFINE "CH59x" CACHE INTERNAL "芯片宏")
    set(CH572_FAMILY_DEFINE "CH5xx" CACHE INTERNAL "系列宏")
    set(CH572_MARCH "rv32imac" CACHE STRING "RISC-V 架构扩展")
    set(CH572_MABI  "ilp32"   CACHE STRING "RISC-V ABI")
elseif(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
    # CH32V003 是 RV32EC（压缩指令 + 16 寄存器），ABI 用 ilp32e
    set(CH572_MCU_PACKAGE 1  CACHE INTERNAL "MCU_PACKAGE (CH32V003 固定 1)")
    set(CH572_MCU_LD      0  CACHE INTERNAL "TARGET_MCU_LD")
    set(CH572_CHIP_DEFINE "CH32V003" CACHE INTERNAL "芯片宏")
    set(CH572_FAMILY_DEFINE "" CACHE INTERNAL "系列宏（V003 无）")
    set(CH572_MARCH "rv32ec" CACHE STRING "RISC-V 架构扩展")
    set(CH572_MABI  "ilp32e" CACHE STRING "RISC-V ABI")
else()
    message(FATAL_ERROR "未知芯片：${CHIP_SDK_RESOLVED}")
endif()

# ---------------------------------------------------------------------------
# 架构扩展（已在上面的芯片分支中按芯片设置）
# ---------------------------------------------------------------------------
# 纯外设：rv32imac（CH5xx）/ rv32ec（CH32V003）
# 链接官方 BLE 库：使用 ch572_add_executable(... BLE) 会自动切换为
#   rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs

# ---------------------------------------------------------------------------
# newlib 头文件：xPack 自带，位于 <toolchain>/riscv-none-elf/include
# ---------------------------------------------------------------------------
get_filename_component(_tc_root "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(_tc_root "${_tc_root}" DIRECTORY)
set(_newlib "${_tc_root}/riscv-none-elf/include")
if(NOT EXISTS "${_newlib}")
    set(_newlib "${_tc_root}/include")
endif()
set(CH572_NEWLIB_INC "${_newlib}" CACHE INTERNAL "CH572 newlib 头文件路径")

# ---------------------------------------------------------------------------
# 生成链接脚本（预处理 ch32fun.ld）
# ---------------------------------------------------------------------------
set(CH572_LD_GENERATED "${CMAKE_BINARY_DIR}/generated_ch572.ld"
    CACHE INTERNAL "CH572 生成的链接脚本路径")

# 用自定义目标而非单纯的 add_custom_command(OUTPUT)，
# 否则 CMake 不会把它加入构建图（没有目标依赖它）。
add_custom_command(
    OUTPUT "${CH572_LD_GENERATED}"
    COMMAND ${CMAKE_COMMAND}
            -DLD_COMPILER=${CMAKE_C_COMPILER}
            -DLD_TEMPLATE=${CH572_CHIP_CORE_DIR}/ch32fun.ld
            -DLD_OUTPUT=${CH572_LD_GENERATED}
            -DLD_MCU_PACKAGE=${CH572_MCU_PACKAGE}
            -DLD_MCU_LD=${CH572_MCU_LD}
            -DLD_TARGET_MCU=${CH572_CHIP_DEFINE}
            -P "${CH572_CMAKE_DIR}/gen_linker_script.cmake"
    DEPENDS "${CH572_CHIP_CORE_DIR}/ch32fun.ld"
    COMMENT "生成链接脚本 generated_ch572.ld"
    VERBATIM
)

# 目标名加 SDK 前缀，避免与父工程目标冲突
if(NOT TARGET ch572_linker_script)
    add_custom_target(ch572_linker_script DEPENDS "${CH572_LD_GENERATED}")
endif()

# ---------------------------------------------------------------------------
# 生成 FreeRTOS 链接脚本（预处理 freertos.ld）
# ---------------------------------------------------------------------------
# freertos.ld 同样含 #if TARGET_MCU_LD 分支，需预处理后再交给链接器。
set(CH572_LD_GENERATED_FREERTOS "${CMAKE_BINARY_DIR}/generated_freertos.ld"
    CACHE INTERNAL "FreeRTOS 生成的链接脚本路径")

add_custom_command(
    OUTPUT "${CH572_LD_GENERATED_FREERTOS}"
    COMMAND ${CMAKE_COMMAND}
            -DLD_COMPILER=${CMAKE_C_COMPILER}
            -DLD_TEMPLATE=${CH572_FREERTOS_DIR}/freertos.ld
            -DLD_OUTPUT=${CH572_LD_GENERATED_FREERTOS}
            -DLD_MCU_PACKAGE=${CH572_MCU_PACKAGE}
            -DLD_MCU_LD=${CH572_MCU_LD}
            -DLD_TARGET_MCU=${CH572_CHIP_DEFINE}
            -P "${CH572_CMAKE_DIR}/gen_linker_script.cmake"
    DEPENDS "${CH572_FREERTOS_DIR}/freertos.ld"
    COMMENT "生成链接脚本 generated_freertos.ld"
    VERBATIM
)

if(NOT TARGET ch572_linker_script_freertos)
    add_custom_target(ch572_linker_script_freertos DEPENDS "${CH572_LD_GENERATED_FREERTOS}")
endif()

# ---------------------------------------------------------------------------
# 核心源文件
# ---------------------------------------------------------------------------
set(CH572_CORE_SOURCES "${CH572_CORE_DIR}/ch32fun.c"
    CACHE INTERNAL "CH572 核心源文件")

# ---------------------------------------------------------------------------
# 函数：添加 CH572 可执行目标
# ---------------------------------------------------------------------------
# ch572_add_executable(<target> <sources...>
#     [EXTRA_SOURCES ...]     额外源文件（如 BLE HAL）
#     [EXTRA_INCLUDES ...]    额外头文件路径
#     [EXTRA_LIBS ...]        额外链接库
#     [MARCH <string>]        覆盖架构扩展（默认用 CH572_MARCH）
#     [LINKER_SCRIPT <path>]  使用自定义链接脚本（默认用 SDK 生成的）
#     [BLE]                   启用官方 BLE 库支持
#     [USB]                   启用 USB 设备支持（fsusb）
#     [FREERTOS]              启用 FreeRTOS（CH59x 专用；自动用 freertos.ld 启动文件）
# )
#
# 注意：使用 BLE 时，本函数会自动把 MARCH 提升为 BLE 库要求的扩展，
# 无需调用方手动设置，也不会污染父工程的缓存变量。
function(ch572_add_executable TARGET)
    cmake_parse_arguments(ARG "BLE;USB;FREERTOS;LWIP;LWIP_NOSYS;LWIP_NO_ETHIF" "MARCH;LINKER_SCRIPT"
        "EXTRA_SOURCES;EXTRA_INCLUDES;EXTRA_LIBS" ${ARGN})

    # 架构选择：命令行 > 函数参数 > 默认
    set(_march "${CH572_MARCH}")
    if(ARG_MARCH)
        set(_march "${ARG_MARCH}")
    endif()

    # FreeRTOS 的 portASM.S / 启动文件使用 CSR 指令，需要 zicsr/zifencei
    if(ARG_FREERTOS)
        set(_march "rv32imac_zicsr_zifencei")
    endif()

    # 链接脚本：自定义 > FreeRTOS 专用 > SDK 生成的
    if(ARG_LINKER_SCRIPT)
        set(_ld "${ARG_LINKER_SCRIPT}")
        set(_ld_generated FALSE)
        set(_ld_freertos FALSE)
    elseif(ARG_FREERTOS)
        # FreeRTOS 需要 .vector 段放入 RAM 及 __freertos_irq_stack_top 符号
        set(_ld "${CH572_LD_GENERATED_FREERTOS}")
        set(_ld_generated FALSE)
        set(_ld_freertos TRUE)
    else()
        set(_ld "${CH572_LD_GENERATED}")
        set(_ld_generated TRUE)
        set(_ld_freertos FALSE)
    endif()

    # 收集源文件
    set(_sources ${ARG_UNPARSED_ARGUMENTS} ${CH572_CORE_SOURCES})

    # 头文件路径
    set(_includes
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CH572_CORE_DIR}"
        "${CH572_CHIP_CORE_DIR}"
        "${CH572_EXTRALIB_DIR}"
    )
    if(CH572_NEWLIB_INC AND EXISTS "${CH572_NEWLIB_INC}")
        list(APPEND _includes "${CH572_NEWLIB_INC}")
    endif()

    # 自动把每个源文件所在目录加入包含路径。
    # 这样用户把 funconfig.h 和 .c 放在同一目录（常见做法）时无需额外配置。
    foreach(_src ${ARG_UNPARSED_ARGUMENTS})
        if(IS_ABSOLUTE "${_src}")
            get_filename_component(_dir "${_src}" DIRECTORY)
        else()
            get_filename_component(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_src}" DIRECTORY)
        endif()
        list(APPEND _includes "${_dir}")
    endforeach()
    list(REMOVE_DUPLICATES _includes)

    # 额外源文件和头文件
    if(ARG_EXTRA_SOURCES)
        list(APPEND _sources ${ARG_EXTRA_SOURCES})
    endif()
    if(ARG_EXTRA_INCLUDES)
        list(APPEND _includes ${ARG_EXTRA_INCLUDES})
    endif()

    # BLE 支持
    set(_libs ${ARG_EXTRA_LIBS})
    if(ARG_BLE)
        # 官方 BLE 库用 GCC 13+ 编译，需要这些扩展：
        #   zicsr/zifencei       - CSR 访问和 fence.i
        #   zmmul/zba/zbb/zbc/zbs - 库使用的扩展
        set(_march "rv32imac_zicsr_zifencei_zmmul_zba_zbb_zbc_zbs")

        list(APPEND _sources
            "${CH572_BLE_DIR}/hal/ble_shim.c"
            "${CH572_BLE_DIR}/hal/ble_hal.c"
        )
        list(APPEND _includes
            "${CH572_BLE_DIR}/hal"
            "${CH572_BLE_DIR}/lib"
            "${CH572_BLE_DIR}/vendor/inc"
        )
        # BLE 库文件名按芯片选择
        if(CHIP_SDK_RESOLVED STREQUAL "CH572")
            list(APPEND _libs "${CH572_BLE_DIR}/lib/libCH572BLE_PERI.a")
        else()
            list(APPEND _libs "${CH572_BLE_DIR}/lib/libCH59xBLE.a")
            # CH59x 的 SNV 需要 EEPROM(Data-Flash) 访问，由官方 ROM 库提供
            # FLASH_EEPROM_CMD 等函数（libISP592.a）。
            list(APPEND _libs "${CH572_BLE_DIR}/lib/libISP592.a")
        endif()
    endif()

    # USB 支持（fsusb 全速 USB 设备驱动）
    if(ARG_USB)
        list(APPEND _sources "${CH572_EXTRALIB_DIR}/fsusb.c")
        # fsusb.c 需要 usb_config.h / usb_defines.h，已在 extralibs 路径中
    endif()

    # FreeRTOS 支持（CH59x 专用）
    if(ARG_FREERTOS)
        if(NOT CHIP_SDK_RESOLVED STREQUAL "CH592" AND
           NOT CHIP_SDK_RESOLVED STREQUAL "CH591")
            message(FATAL_ERROR "FREERTOS 目前仅支持 CH592 / CH591")
        endif()

        # 内核源文件（tasks/queue/list/timers/event_groups/stream_buffer/croutine）
        list(APPEND _sources
            "${CH572_FREERTOS_DIR}/kernel/tasks.c"
            "${CH572_FREERTOS_DIR}/kernel/queue.c"
            "${CH572_FREERTOS_DIR}/kernel/list.c"
            "${CH572_FREERTOS_DIR}/kernel/timers.c"
            "${CH572_FREERTOS_DIR}/kernel/event_groups.c"
            "${CH572_FREERTOS_DIR}/kernel/stream_buffer.c"
            "${CH572_FREERTOS_DIR}/kernel/croutine.c"
            "${CH572_FREERTOS_DIR}/kernel/heap_4.c"
        )
        # WCH 移植层 + 启动文件
        list(APPEND _sources
            "${CH572_FREERTOS_DIR}/port/port.c"
            "${CH572_FREERTOS_DIR}/port/portASM.S"
            "${CH572_FREERTOS_DIR}/Startup_CH592_FreeRTOS.S"
        )
        list(APPEND _includes
            "${CH572_FREERTOS_DIR}/kernel/include"
            "${CH572_FREERTOS_DIR}/config"
            "${CH572_FREERTOS_DIR}/port"
        )
    endif()

    # LWIP 支持（CH59x 专用，依赖 FreeRTOS）
    if(ARG_LWIP)
        if(NOT CHIP_SDK_RESOLVED STREQUAL "CH592" AND
           NOT CHIP_SDK_RESOLVED STREQUAL "CH591")
            message(FATAL_ERROR "LWIP 目前仅支持 CH592 / CH591")
        endif()

        # 核心源文件（RAW API + TCP/UDP/ICMP/ARP，无 socket/DNS/DHCP）
        list(APPEND _sources
            "${CH572_LWIP_DIR}/core/init.c"
            "${CH572_LWIP_DIR}/core/def.c"
            "${CH572_LWIP_DIR}/core/mem.c"
            "${CH572_LWIP_DIR}/core/memp.c"
            "${CH572_LWIP_DIR}/core/netif.c"
            "${CH572_LWIP_DIR}/core/pbuf.c"
            "${CH572_LWIP_DIR}/core/raw.c"
            "${CH572_LWIP_DIR}/core/stats.c"
            "${CH572_LWIP_DIR}/core/sys.c"
            "${CH572_LWIP_DIR}/core/tcp.c"
            "${CH572_LWIP_DIR}/core/tcp_in.c"
            "${CH572_LWIP_DIR}/core/tcp_out.c"
            "${CH572_LWIP_DIR}/core/udp.c"
            "${CH572_LWIP_DIR}/core/timeouts.c"
            "${CH572_LWIP_DIR}/core/ip.c"
            "${CH572_LWIP_DIR}/core/inet_chksum.c"
            "${CH572_LWIP_DIR}/core/etharp.c"
            "${CH572_LWIP_DIR}/core/ip4.c"
            "${CH572_LWIP_DIR}/core/ip4_addr.c"
            "${CH572_LWIP_DIR}/core/icmp.c"
            "${CH572_LWIP_DIR}/core/altcp.c"
            "${CH572_LWIP_DIR}/core/altcp_alloc.c"
            "${CH572_LWIP_DIR}/core/altcp_tcp.c"
            "${CH572_LWIP_DIR}/netif/ethernet.c"
        )
        # 移植层（sys_arch + RNDIS + ethernetif）
        # LWIP_NO_ETHIF：应用自实现 netif 初始化时，跳过 ethernetif.c（省 RAM）
        set(_lwip_port_common
            "${CH572_LWIP_DIR}/port/rndis.c"
        )
        if(NOT ARG_LWIP_NO_ETHIF)
            list(APPEND _lwip_port_common "${CH572_LWIP_DIR}/port/ethernetif.c")
        endif()
        if(ARG_LWIP_NOSYS)
            # 裸机模式：用 sys_nosys.c（仅 sys_now + rand），不用 sys_arch.c
            list(APPEND _sources
                "${CH572_LWIP_DIR}/port/sys_nosys.c"
                ${_lwip_port_common}
            )
        else()
            list(APPEND _sources
                "${CH572_LWIP_DIR}/port/sys_arch.c"
                ${_lwip_port_common}
            )
        endif()
        list(APPEND _includes
            "${CH572_LWIP_DIR}/include"
            "${CH572_LWIP_DIR}/port"
            "${CH572_LWIP_DIR}/port/arch"
        )
    endif()

    add_executable(${TARGET}
        ${_sources}
        ${_ld}
    )

    # 确保链接脚本先生成（仅当使用 SDK 生成的脚本时）
    if(_ld_generated)
        add_dependencies(${TARGET} ch572_linker_script)
    elseif(_ld_freertos)
        add_dependencies(${TARGET} ch572_linker_script_freertos)
    endif()

    target_include_directories(${TARGET} PRIVATE ${_includes})

    # 系列宏：CH5xx 用 -DCH5xx；CH32V003 无系列宏（仅 -DCH32V003）
    set(_family_defs "")
    if(NOT CH572_FAMILY_DEFINE STREQUAL "")
        list(APPEND _family_defs "-D${CH572_FAMILY_DEFINE}")
    endif()

    # FreeRTOS：禁用 ch32fun 自带启动代码（由 Startup_CH592_FreeRTOS.S 提供）
    set(_freertos_defs "")
    if(ARG_FREERTOS)
        list(APPEND _freertos_defs "-DFUNCONF_OVERRIDE_STARTUP=1")
    endif()

    # LWIP 裸机模式：定义 LWIP_NO_SYS=1（lwipopts.h 据此设 NO_SYS=1）
    set(_lwip_defs "")
    if(ARG_LWIP_NOSYS)
        list(APPEND _lwip_defs "-DLWIP_NO_SYS=1")
    endif()

    target_compile_options(${TARGET} PRIVATE
        -march=${_march}
        -mabi=${CH572_MABI}
        -D${CH572_CHIP_DEFINE}=1
        ${_family_defs}
        ${_freertos_defs}
        ${_lwip_defs}
        -DMCU_PACKAGE=${CH572_MCU_PACKAGE}
        -ffunction-sections
        -fdata-sections
        -fmessage-length=0
        -msmall-data-limit=8
        -fno-tree-loop-distribute-patterns
        -fno-common
        -Wall
        -Os
        -g
    )

    target_link_options(${TARGET} PRIVATE
        -march=${_march}
        -mabi=${CH572_MABI}
        -T "${_ld}"
        -nostdlib
        -static-libgcc
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map=${TARGET}.map
    )

    # 链接库（BLE 库需要 __udivdi3 等 64 位运算，来自 libgcc）
    if(_libs)
        target_link_libraries(${TARGET} PRIVATE ${_libs})
    endif()

    # libgcc：CH32V003 用 rv32ec/ilp32e，标准工具链 libgcc 不适用，
    # 需使用 SDK 自带的专用 libgcc.a（sdk/CH32V003_DEV/lib/）。
    if(CHIP_SDK_RESOLVED STREQUAL "CH32V003")
        target_link_options(${TARGET} PRIVATE
            -L"${SDK_CHIP_DIR}/lib"
        )
        target_link_libraries(${TARGET} PRIVATE gcc)
    else()
        # libgcc 必须放在所有库之后，否则 64 位除法符号无法解析
        target_link_libraries(${TARGET} PRIVATE gcc)
    endif()

    # 生成 bin / hex / lst
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                "$<TARGET_FILE:${TARGET}>" "${TARGET}.bin"
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                "$<TARGET_FILE:${TARGET}>" "${TARGET}.hex"
        COMMAND ${CMAKE_OBJDUMP} -S "$<TARGET_FILE:${TARGET}>" > "${TARGET}.lst"
        COMMENT "生成 ${TARGET}.bin / .hex / .lst"
        VERBATIM
    )

    # 便捷目标：显示固件大小
    # 用 CH572_ 前缀避免与父工程的目标名冲突
    add_custom_target(${TARGET}-size
        COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET}>"
        DEPENDS ${TARGET}
        COMMENT "固件大小：${TARGET}"
        VERBATIM
    )

    # -----------------------------------------------------------------------
    # 烧录目标
    # -----------------------------------------------------------------------
    # 用法：cmake --build build --target <目标名>-flash
    #
    # wchisp（USB Bootloader）—— 需上电时 PA1 拉低进入 ISP

    # --- wchisp（USB ISP）---
    if(CH572_WCHISP AND EXISTS "${CH572_WCHISP}")
        add_custom_target(${TARGET}-flash-usb
            COMMAND "${CH572_WCHISP}" flash "${TARGET}.bin"
            DEPENDS ${TARGET}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "通过 USB (wchisp) 烧录 ${TARGET}.bin"
            USES_TERMINAL
            VERBATIM
        )
    endif()

    # --- 通用 erase / verify ---
    if(CH572_WCHISP AND EXISTS "${CH572_WCHISP}")
        add_custom_target(${TARGET}-erase
            COMMAND "${CH572_WCHISP}" erase
            COMMENT "擦除 CH572 Flash（USB）"
            USES_TERMINAL
            VERBATIM
        )
        add_custom_target(${TARGET}-verify
            COMMAND "${CH572_WCHISP}" verify "${TARGET}.bin"
            DEPENDS ${TARGET}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "校验 ${TARGET}.bin（USB）"
            USES_TERMINAL
            VERBATIM
        )
    endif()

    # --- 固件名-flash：按默认方式 ---
    if(CH572_FLASH_METHOD STREQUAL "usb" AND TARGET ${TARGET}-flash-usb)
        add_custom_target(${TARGET}-flash DEPENDS ${TARGET}-flash-usb)
    elseif(TARGET ${TARGET}-flash-usb)
        add_custom_target(${TARGET}-flash DEPENDS ${TARGET}-flash-usb)
    endif()

    # -----------------------------------------------------------------------
    # 默认烧录目标
    # -----------------------------------------------------------------------
    # 决定 `flash` / `size` / `verify` 等裸目标作用于哪个固件。
    #
    # 优先级（从高到低）：
    #   1. -DCH572_TARGET=<名字>                  （命令行，最高，推荐用这个）
    #   2. 环境变量 CH572_DEFAULT_TARGET=<名字>
    #   3. 项目根 default_target.txt 里的第一行   （推荐，改文件即可）
    #   4. 第一个调用 ch572_add_executable 的固件
    #
    # 说明：CMake 的 -D 无法覆盖「已存在的缓存变量」，
    # 因此命令行覆盖请用 -DCH572_TARGET=...（每次都会重新解析）。
    # default_target.txt 改动后会自动生效（无需清理 build）。
    get_filename_component(_ch572_proj_root "${CH572_SDK_ROOT}/.." ABSOLUTE)
    set(_dt_file "${_ch572_proj_root}/default_target.txt")

    # 读取文件中的目标名（若有）
    # 支持两种格式：
    #   target = usb_dual      （键值格式，推荐）
    #   usb_dual               （旧格式：裸行 = 目标名）
    # 同时会跳过 chip = ... 之类的其它键。
    set(_dt_target "")
    if(EXISTS "${_dt_file}")
        file(STRINGS "${_dt_file}" _dt_lines
             REGEX "^[ \t]*[^# \t\r\n][^\r\n]*")
        foreach(_ln ${_dt_lines})
            string(STRIP "${_ln}" _ln)
            if(_ln STREQUAL "")
                continue()
            endif()
            if(_ln MATCHES "^[Tt][Aa][Rr][Gg][Ee][Tt][ \t]*=[ \t]*(.+)$")
                string(STRIP "${CMAKE_MATCH_1}" _dt_target)
                break()
            elseif(_ln MATCHES "^[A-Za-z_][A-Za-z0-9_]*[ \t]*=")
                # 其它 key = value 行（如 chip = ...），跳过
                continue()
            else()
                # 旧格式：裸行视为目标名
                set(_dt_target "${_ln}")
                break()
            endif()
        endforeach()
    endif()

    # 1) 命令行 -DCH572_TARGET（最高优先级，每次配置都生效）
    if(CH572_TARGET)
        set(_dt_use "${CH572_TARGET}")
    # 2) 环境变量
    elseif(DEFINED ENV{CH572_DEFAULT_TARGET} AND NOT "$ENV{CH572_DEFAULT_TARGET}" STREQUAL "")
        set(_dt_use "$ENV{CH572_DEFAULT_TARGET}")
    # 3) default_target.txt
    elseif(NOT _dt_target STREQUAL "")
        set(_dt_use "${_dt_target}")
    # 4) 兜底：第一个固件
    else()
        set(_dt_use "${TARGET}")
    endif()

    # 只在「本轮配置尚未解析过」时写入，避免后续固件覆盖已定的默认目标。
    if(NOT CH572_DEFAULT_TARGET_RESOLVED)
        if(NOT _dt_use STREQUAL CH572_DEFAULT_TARGET)
            set(CH572_DEFAULT_TARGET "${_dt_use}"
                CACHE STRING "默认烧录目标（固件名）" FORCE)
        endif()
        set(CH572_DEFAULT_TARGET_RESOLVED 1 CACHE INTERNAL "默认目标本轮已解析")
    endif()

    # 只有当本固件就是被选中的默认目标时，才创建裸目标
    if(CH572_DEFAULT_TARGET STREQUAL "${TARGET}")

        # 默认 size（不依赖烧录工具）
        if(NOT TARGET size)
            add_custom_target(size
                COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${TARGET}>"
                DEPENDS ${TARGET}
                COMMENT "固件大小：${TARGET}（默认目标）"
                VERBATIM
            )
        endif()

        # --- 默认 flash（按 CH572_FLASH_METHOD）---
        if(NOT TARGET flash)
            if(CH572_FLASH_METHOD STREQUAL "usb" AND CH572_WCHISP)
                add_custom_target(flash
                    COMMAND "${CH572_WCHISP}" flash "${TARGET}.bin"
                    DEPENDS ${TARGET}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                    COMMENT "通过 USB (wchisp) 烧录 ${TARGET}.bin"
                    USES_TERMINAL
                    VERBATIM
                )
            endif()
        endif()

        # --- 默认 flash-usb（显式指定方式）---
        if(CH572_WCHISP AND NOT TARGET flash-usb)
            add_custom_target(flash-usb
                COMMAND "${CH572_WCHISP}" flash "${TARGET}.bin"
                DEPENDS ${TARGET}
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                COMMENT "通过 USB (wchisp) 烧录 ${TARGET}.bin"
                USES_TERMINAL
                VERBATIM
            )
        endif()

        # --- 默认 erase ---
        if(NOT TARGET erase)
            if(CH572_WCHISP)
                add_custom_target(erase
                    COMMAND "${CH572_WCHISP}" erase
                    COMMENT "擦除 CH572 Flash（USB）"
                    USES_TERMINAL
                    VERBATIM
                )
            endif()
        endif()

        # --- 默认 verify（仅 wchisp）---
        if(CH572_WCHISP AND NOT TARGET verify)
            add_custom_target(verify
                COMMAND "${CH572_WCHISP}" verify "${TARGET}.bin"
                DEPENDS ${TARGET}
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                COMMENT "校验 ${TARGET}.bin（USB）"
                USES_TERMINAL
                VERBATIM
            )
        endif()

        # --- 默认 probe（仅 wchisp）---
        if(CH572_WCHISP AND NOT TARGET probe)
            add_custom_target(probe
                COMMAND "${CH572_WCHISP}" probe
                COMMENT "探测 USB ISP 设备"
                USES_TERMINAL
                VERBATIM
            )
        endif()
    endif()

    # 记录所有已注册的固件名，便于最后校验默认目标是否存在
    set_property(GLOBAL APPEND PROPERTY CH572_ALL_TARGETS "${TARGET}")

    message(STATUS "CH572 目标：${TARGET}  (MARCH=${_march})")
endfunction()

# ---------------------------------------------------------------------------
# 校验默认目标是否存在（在所有固件注册完成后调用）
# ---------------------------------------------------------------------------
# 用法（放在所有 add_subdirectory 之后）：
#   ch572_finalize()
function(ch572_finalize)
    get_property(_all GLOBAL PROPERTY CH572_ALL_TARGETS)
    if(NOT CH572_DEFAULT_TARGET)
        return()
    endif()

    list(FIND _all "${CH572_DEFAULT_TARGET}" _idx)
    if(_idx EQUAL -1)
        message(WARNING
            "CH572 默认目标 '${CH572_DEFAULT_TARGET}' 不存在！\n"
            "  可用固件：${_all}\n"
            "  请修改 default_target.txt 或 -DCH572_TARGET=<名字>")
    else()
        message(STATUS "CH572 默认目标  ：${CH572_DEFAULT_TARGET}（flash/size/verify 等裸目标作用于它）")
    endif()

    # 本轮配置结束：清除「已解析」标记，使下次配置能重新读取
    # default_target.txt / -DCH572_TARGET / 环境变量。
    unset(CH572_DEFAULT_TARGET_RESOLVED CACHE)
    # -DCH572_TARGET 是一次性覆盖，用完即清，
    # 下次不带 -D 重新配置时会回到 default_target.txt 的值。
    if(CH572_TARGET)
        unset(CH572_TARGET CACHE)
    endif()
endfunction()
