#!/usr/bin/env python3
"""
删除 sdk/port/ch32/*.c 中被替换脚本破坏的自引用宏块。

问题：之前用 sed 式替换时，把 `#define CH32_IRQ_USART1  USART1_IRQn`
的右值也替换成了宏名本身，产生：
    #define CH32_IRQ_USART1  CH32_IRQ_USART1
这些自引用宏覆盖了前面正确的定义，导致编译时宏未定义。

策略：删除所有形如 `#define X X` 的行；若某个 `#if/#else` 分支
在删除后只剩空行/注释，则把整块也删掉。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "sdk" / "port" / "ch32"

SELF_REF = re.compile(r'^\s*#define\s+(\w+)\s+\1\s*$')

def clean_file(path: Path) -> bool:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines(True)
    out = []
    removed = 0
    for ln in lines:
        if SELF_REF.match(ln):
            removed += 1
            continue
        out.append(ln)

    if not removed:
        return False

    # 清理因删除而变空的 #if/#else 分支
    text = "".join(out)
    # 匹配：可选注释块 + #if 条件 + 全是空行 + #else/#endif
    text = re.sub(
        r'(?m)^[ \t]*#if[^\n]*\n(?:[ \t]*\n)+[ \t]*#(?:else|endif)',
        lambda m: m.group(0).split('\n')[0] + '\n' + m.group(0).split('\n')[-1],
        text)

    path.write_text(text, encoding="utf-8")
    print(f"  {path.name}: 删除 {removed} 个自引用宏")
    return True

def main() -> int:
    if not PORT.is_dir():
        print(f"目录不存在：{PORT}")
        return 1

    total = 0
    for f in sorted(PORT.glob("*.c")) + sorted(PORT.glob("*.h")):
        if clean_file(f):
            total += 1

    print(f"共处理 {total} 个文件")
    return 0

if __name__ == "__main__":
    sys.exit(main())
