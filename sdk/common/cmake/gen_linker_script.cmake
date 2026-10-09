# CH572 Lite SDK - 链接脚本生成脚本
# ---------------------------------------------------------------------------
# 由 sdk.cmake 通过 cmake -P 调用。
#
# 作用：用 C 预处理器展开 ch32fun.ld 模板，生成具体的链接脚本。
# 用脚本而非 shell 重定向，是为了跨平台（Windows cmd 的 > 重定向不可靠）。
#
# 参数（-D 传入）：
#   LD_COMPILER    - C 编译器路径
#   LD_TEMPLATE    - ch32fun.ld 模板路径
#   LD_OUTPUT      - 输出路径
#   LD_MCU_PACKAGE - MCU_PACKAGE 值
#   LD_MCU_LD      - TARGET_MCU_LD 值
#   LD_TARGET_MCU  - 芯片名（可选，仅用于模板中的条件判断）
# ---------------------------------------------------------------------------

if(NOT LD_COMPILER OR NOT LD_TEMPLATE OR NOT LD_OUTPUT)
    message(FATAL_ERROR "缺少必需参数：LD_COMPILER / LD_TEMPLATE / LD_OUTPUT")
endif()

if(NOT LD_TARGET_MCU)
    set(LD_TARGET_MCU "CH572")
endif()

execute_process(
    COMMAND "${LD_COMPILER}" -E -P -x c
            -DTARGET_MCU=${LD_TARGET_MCU}
            -DMCU_PACKAGE=${LD_MCU_PACKAGE}
            -DTARGET_MCU_LD=${LD_MCU_LD}
            -DTARGET_MCU_MEMORY_SPLIT=
            "${LD_TEMPLATE}"
    OUTPUT_FILE "${LD_OUTPUT}"
    RESULT_VARIABLE _result
    ERROR_VARIABLE  _err
)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "生成链接脚本失败（${_result}）：\n${_err}")
endif()

message(STATUS "已生成链接脚本：${LD_OUTPUT}")
