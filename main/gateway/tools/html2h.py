#!/usr/bin/env python3
"""把 HTML 文件转成 C 字符串数组头文件。

用法: python html2h.py <input.html> <output.h> <symbol_name>

生成内容示例:
    static const char SYMBOL[] =
    "line1\n"
    "line2\n"
    ;
"""
import sys


def c_escape(line):
    """转义为 C 字符串字面量内容（不含首尾引号）。"""
    out = []
    for ch in line:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\t':
            out.append('\\t')
        elif ch == '\r':
            out.append('\\r')
        elif ch == '\n':
            out.append('\\n')
        else:
            out.append(ch)
    return ''.join(out)


def main():
    if len(sys.argv) != 4:
        print("usage: html2h.py <input.html> <output.h> <symbol>", file=sys.stderr)
        return 1

    inp, outp, symbol = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(inp, 'r', encoding='utf-8') as f:
        content = f.read()

    # Minify：去掉行首尾空白，把所有行合并为一行（省 Flash）。
    # 前提：源文件不含 JS 的 // 行注释（否则会吞掉后续代码）；
    #       如需注释请用 /* */ 块注释。
    lines = [l.strip() for l in content.split('\n')]
    merged = ''.join(l for l in lines if l)

    with open(outp, 'w', encoding='utf-8', newline='\n') as f:
        f.write("/* 自动生成，请勿编辑。源文件: %s */\n" % inp.replace('\\', '/'))
        f.write("static const char %s[] =\n" % symbol)
        f.write('"%s"\n' % c_escape(merged))
        f.write(";\n")

    return 0


if __name__ == '__main__':
    sys.exit(main())
