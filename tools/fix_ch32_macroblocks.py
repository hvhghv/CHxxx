#!/usr/bin/env python3
"""
修复 sdk/port/ch32/*.c 的宏定义块。

背景：这些文件原本有 3 段宏定义，其中第 2、3 段是替换脚本产生的
自引用垃圾（`#define X X`）。第 1 段（`#if defined(CH32_FAMILY_X035)`）
是正确的完整定义。

本脚本：
  1. 找到第一个 "中断号统一" 注释块，删除它到 "RCC 前缀统一" 块结束
     之间的所有内容（即第 2、3 段宏块）。
  2. 保留第 1 段完整宏定义。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "sdk" / "port" / "ch32"

# 第 2 段起始标记
MARK_IRQ = "中断号统一"
# 第 3 段起始标记
MARK_RCC = "RCC 前缀统一"
# 第 3 段之后的第一个正常代码标记
MARK_AFTER = "GPIO_PINS_PER_PORT"

def fix_file(path: Path) -> bool:
    text = path.read_text(encoding="utf-8", errors="replace")

    i_irq = text.find(MARK_IRQ)
    if i_irq < 0:
        return False

    # 回退到该注释块的开头 "/***"
    i_start = text.rfind("/*", 0, i_irq)
    if i_start < 0:
        return False

    # 找到第 3 段结束：从 MARK_RCC 之后找 "#endif" 后的空行
    i_rcc = text.find(MARK_RCC, i_irq)
    if i_rcc < 0:
        return False

    # 从 MARK_RCC 开始找连续的 #if/#define/#else/#endif 块结束
    i_scan = i_rcc
    depth = 0
    end_pos = -1
    for m in re.finditer(r'(?m)^[ \t]*#(if|ifdef|ifndef|else|elif|endif)\b.*$',
                         text[i_scan:]):
        kw = m.group(1)
        if kw in ("if", "ifdef", "ifndef"):
            depth += 1
        elif kw == "endif":
            depth -= 1
            if depth == 0:
                end_pos = i_scan + m.end()
                break
    if end_pos < 0:
        return False

    # 把 end_pos 之后的空行也吃掉
    while end_pos < len(text) and text[end_pos] == '\n':
        end_pos += 1

    new_text = text[:i_start] + text[end_pos:]
    path.write_text(new_text, encoding="utf-8")
    print(f"  {path.name}: 删除重复宏块 {end_pos - i_start} 字节")
    return True

def main() -> int:
    if not PORT.is_dir():
        print(f"目录不存在：{PORT}")
        return 1

    total = 0
    for f in sorted(PORT.glob("*.c")):
        if fix_file(f):
            total += 1

    print(f"共处理 {total} 个文件")
    return 0

if __name__ == "__main__":
    sys.exit(main())
