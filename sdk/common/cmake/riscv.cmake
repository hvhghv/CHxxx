# CH572 Lite SDK - CMake 工具链文件
# ---------------------------------------------------------------------------
# 用法：
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<SDK>/cmake/riscv.cmake
#   cmake --build build
#
# 工具链位置可通过 -DTOOLCHAIN_ROOT=... 覆盖，默认使用 SDK 内置的 prebuilt。
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# ---------------------------------------------------------------------------
# 工具链根目录
# ---------------------------------------------------------------------------
# 默认：SDK/prebuilt/xpack-riscv-none-elf-gcc-15.2.0-1
# 可用 -DTOOLCHAIN_ROOT=<path> 覆盖
if(NOT TOOLCHAIN_ROOT)
    get_filename_component(_sdk_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    file(GLOB _prebuilt_dirs "${_sdk_root}/prebuilt/xpack-riscv-none-elf-gcc-*")
    list(SORT _prebuilt_dirs)
    list(REVERSE _prebuilt_dirs)
    list(GET _prebuilt_dirs 0 TOOLCHAIN_ROOT)
endif()

if(NOT EXISTS "${TOOLCHAIN_ROOT}")
    message(FATAL_ERROR "找不到工具链：${TOOLCHAIN_ROOT}\n"
                        "请用 -DTOOLCHAIN_ROOT=<path> 指定，或把工具链放到 SDK/prebuilt/ 下")
endif()

set(TOOLCHAIN_BIN "${TOOLCHAIN_ROOT}/bin")

# Windows 下可执行文件带 .exe 后缀
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()

set(CMAKE_C_COMPILER   "${TOOLCHAIN_BIN}/riscv-none-elf-gcc${_exe}")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN}/riscv-none-elf-gcc${_exe}")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/riscv-none-elf-g++${_exe}")
set(CMAKE_AR           "${TOOLCHAIN_BIN}/riscv-none-elf-ar${_exe}")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_BIN}/riscv-none-elf-objcopy${_exe}")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_BIN}/riscv-none-elf-objdump${_exe}")
set(CMAKE_SIZE         "${TOOLCHAIN_BIN}/riscv-none-elf-size${_exe}")

# 校验
foreach(_tool CMAKE_C_COMPILER CMAKE_OBJCOPY CMAKE_OBJDUMP)
    if(NOT EXISTS "${${_tool}}")
        message(FATAL_ERROR "工具不存在：${${_tool}}")
    endif()
endforeach()

message(STATUS "CH572 SDK 工具链：${TOOLCHAIN_ROOT}")

# ---------------------------------------------------------------------------
# 交叉编译搜索策略
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
