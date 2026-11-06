# ===========================================================================
# RISC-V 工具链（xPack riscv-none-elf-gcc）
# ===========================================================================
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# 工具链路径：优先用环境变量 RISCV_TOOLCHAIN_PATH，否则用 SDK 内置
if(DEFINED ENV{RISCV_TOOLCHAIN_PATH})
    set(_toolchain_dir "$ENV{RISCV_TOOLCHAIN_PATH}")
else()
    # 本文件位于 <root>/sdk/cmake/，工具链在 <root>/sdk/prebuilt/
    set(_toolchain_dir
        "${CMAKE_CURRENT_LIST_DIR}/../prebuilt/xpack-riscv-none-elf-gcc-15.2.0-1")
endif()

set(_toolchain_bin "${_toolchain_dir}/bin")

# 注意：toolchain 文件在 project() 之前执行，此时 CMAKE_EXECUTABLE_SUFFIX 可能为空，
# 因此显式判断宿主平台加后缀。
if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()

set(CMAKE_C_COMPILER   "${_toolchain_bin}/riscv-none-elf-gcc${_exe}")
set(CMAKE_ASM_COMPILER "${_toolchain_bin}/riscv-none-elf-gcc${_exe}")
set(CMAKE_CXX_COMPILER "${_toolchain_bin}/riscv-none-elf-g++${_exe}")
set(CMAKE_OBJCOPY      "${_toolchain_bin}/riscv-none-elf-objcopy${_exe}")
set(CMAKE_OBJDUMP      "${_toolchain_bin}/riscv-none-elf-objdump${_exe}")
set(CMAKE_SIZE         "${_toolchain_bin}/riscv-none-elf-size${_exe}")
set(CMAKE_AR           "${_toolchain_bin}/riscv-none-elf-ar${_exe}")
set(CMAKE_RANLIB       "${_toolchain_bin}/riscv-none-elf-ranlib${_exe}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
