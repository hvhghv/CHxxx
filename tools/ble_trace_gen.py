#!/usr/bin/env python3
"""
自动分析 BLE 闭源库的调用链，生成 --wrap 插桩代码。

用法：
    python tools/ble_trace_gen.py <lib.a> <output.c> [root_func1 root_func2 ...]

原理：
  1. 解包 .a，对每个 .o 用 objdump -dr 提取函数边界与重定位（R_RISCV_CALL）
  2. 从 root 函数出发做 BFS，得到可达的库内函数集合
  3. 为集合中每个函数生成 __wrap_xxx，打印 ">name" / "<name ret=N"

注意：只插桩库内定义的函数（外部依赖如 memcpy 不插桩）。
"""

import os
import re
import subprocess
import sys
import tempfile
import shutil

# 工具链路径（相对仓库根，运行时转为绝对路径）
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_TOOLCHAIN = os.path.join(_REPO_ROOT, "sdk", "common", "prebuilt",
                          "xpack-riscv-none-elf-gcc-15.2.0-1", "bin")
OBJDUMP = os.path.join(_TOOLCHAIN, "riscv-none-elf-objdump.exe")
AR = os.path.join(_TOOLCHAIN, "riscv-none-elf-ar.exe")
NM = os.path.join(_TOOLCHAIN, "riscv-none-elf-nm.exe")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return r.stdout


def extract_archive(lib_path, workdir):
    """解包 .a 到 workdir，返回 .o 文件列表"""
    lib_abs = os.path.abspath(lib_path)
    if not os.path.exists(lib_abs):
        raise FileNotFoundError("库文件不存在: %s" % lib_abs)

    # 在 workdir 内执行 ar x（ar 会解包到当前目录）
    r = subprocess.run([AR, "x", lib_abs], cwd=workdir,
                       capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        raise RuntimeError("ar x 失败: %s\n%s" % (r.returncode, r.stderr))

    return sorted(f for f in os.listdir(workdir) if f.endswith(".o"))


def parse_object(obj_path):
    """解析单个 .o，返回 {func_name: [callee_names]}"""
    out = run([OBJDUMP, "-dr", "--no-show-raw-insn", obj_path])

    funcs = {}          # name -> set(callees)
    cur = None
    pending_reloc = False

    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if m:
            cur = m.group(1)
            funcs.setdefault(cur, set())
            pending_reloc = False
            continue

        if cur is None:
            continue

        # 重定位行：形如 "  4:\tR_RISCV_CALL\tLL_SlaveEnalbe"
        m = re.match(r"^\s+[0-9a-f]+:\s+(R_RISCV_CALL|R_RISCV_CALL_PLT)\s+(\S+)", line)
        if m:
            funcs[cur].add(m.group(2))
            continue

        # 新函数起点（无标签但有地址）在 objdump 中一般带 <name>，已处理
    return funcs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1

    lib_path = sys.argv[1]
    out_c = sys.argv[2]
    roots = sys.argv[3:] or [
        "BLE_LibInit", "GAPRole_PeripheralInit", "GAP_SetParamValue",
        "GGS_AddService", "GGS_SetParameter", "GATTServApp_AddService",
        "GAPRole_SetParameter", "TMOS_ProcessEventRegister",
    ]

    workdir = tempfile.mkdtemp(prefix="bletrace_")
    try:
        objs = extract_archive(lib_path, workdir)
        print("解包 %d 个目标文件" % len(objs))

        all_funcs = {}       # name -> set(callees)
        for o in objs:
            funcs = parse_object(os.path.join(workdir, o))
            for name, callees in funcs.items():
                all_funcs.setdefault(name, set()).update(callees)

        print("库内函数总数: %d" % len(all_funcs))

        # BFS：从 roots 出发，收集可达的库内函数
        reachable = set()
        queue = [r for r in roots if r in all_funcs]
        missing = [r for r in roots if r not in all_funcs]
        if missing:
            print("警告：以下 root 不在库中: %s" % ", ".join(missing))

        while queue:
            f = queue.pop()
            if f in reachable:
                continue
            reachable.add(f)
            for callee in all_funcs.get(f, ()):
                if callee in all_funcs and callee not in reachable:
                    queue.append(callee)

        print("可达函数数: %d" % len(reachable))

        # 生成插桩代码
        lines = []
        lines.append("/* 自动生成：BLE 闭源库插桩（--wrap）")
        lines.append(" * 由 tools/ble_trace_gen.py 生成，请勿手工修改。")
        lines.append(" * 覆盖 %d 个可达函数。" % len(reachable))
        lines.append(" */")
        lines.append("")
        lines.append('#include "ch32fun.h"')
        lines.append('#include "usb_log.h"')
        lines.append("")
        lines.append("/* 通用：打印函数进入/返回（非 static，供内联汇编 call 引用） */")
        lines.append("void tr_in(const char *n) { ulog_str(\">\"); ulog_str(n); ulog_str(\"\\n\"); }")
        lines.append("void tr_out(const char *n, int32_t r) {")
        lines.append("    ulog_str(\"<\"); ulog_str(n); ulog_str(\" ret=\"); ulog_i32(r); ulog_str(\"\\n\");")
        lines.append("}")
        lines.append("")

        for f in sorted(reachable):
            # 跳过含 '.' 的编译器内部符号（如 GATTServApp_InitCharCfg.part.12），
            # 它们不是合法的 C 标识符，也无法被 --wrap 正常引用。
            if "." in f:
                continue
            # 统一用 void 原型 + 内联汇编转发，避免逐个推断签名。
            # 由于 RISC-V ilp32 ABI 参数都在 a0-a7 / 返回值在 a0，直接跳转是安全的。
            lines.append("/* %s */" % f)
            lines.append("extern void __real_%s(void);" % f)
            lines.append("void __wrap_%s(void);" % f)
            lines.append("__asm__(")
            lines.append('    ".pushsection .text.__wrap_%s,\\"ax\\",@progbits\\n"' % f)
            lines.append('    ".globl __wrap_%s\\n"' % f)
            lines.append('    ".type __wrap_%s,@function\\n"' % f)
            lines.append('    "__wrap_%s:\\n"' % f)
            lines.append('    "addi sp,sp,-16\\n"')
            lines.append('    "sw ra,12(sp)\\n"')
            lines.append('    "sw a0,8(sp)\\n"')
            lines.append('    "sw a1,4(sp)\\n"')
            lines.append('    "sw a2,0(sp)\\n"')
            lines.append('    "la a0,%s\\n"' % ("str_" + f))
            lines.append('    "call tr_in\\n"')
            lines.append('    "lw a0,8(sp)\\n"')
            lines.append('    "lw a1,4(sp)\\n"')
            lines.append('    "lw a2,0(sp)\\n"')
            lines.append('    "call __real_%s\\n"' % f)
            lines.append('    "sw a0,8(sp)\\n"')
            lines.append('    "mv a1,a0\\n"')
            lines.append('    "la a0,%s\\n"' % ("str_" + f))
            lines.append('    "call tr_out\\n"')
            lines.append('    "lw a0,8(sp)\\n"')
            lines.append('    "lw ra,12(sp)\\n"')
            lines.append('    "addi sp,sp,16\\n"')
            lines.append('    "ret\\n"')
            lines.append('    ".popsection\\n"')
            lines.append(");")
            lines.append('__attribute__((used)) static const char str_%s[] = "%s";' % (f, f))
            lines.append("")

        with open(out_c, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines))

        print("已生成: %s" % out_c)
        print("链接时需要为以上每个函数加 -Wl,--wrap=<name>")
        return 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
