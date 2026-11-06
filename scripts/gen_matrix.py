#!/usr/bin/env python3
"""
生成 GitHub Actions 构建矩阵（JSON）。

按 CMakeLists.txt 的筛选逻辑，推导每个芯片实际会构建哪些目标：

  blink / uart_demo                     → 所有芯片
  ble_usb                               → CHIP_BLE_SUPPORTED 且 有 USB 且 非 ch572
  usb_cdc                               → 有 USB
  gateway                               → 仅 ch59x 且 GW_BUILD=ON（默认关闭）

输出格式（GitHub Actions matrix.include）：
  [{"chip":"ch59x","target":"blink"}, ...]
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE_DIR = ROOT / "sdk" / "cmake"

# 芯片顺序（固定，便于阅读日志）
CHIP_ORDER = ["ch59x", "ch585", "ch572", "ch32x035", "ch32l103", "ch32v20x"]


def read_chip(name: str) -> dict:
    """从 chip_<name>.cmake 提取关键能力。"""
    f = CMAKE_DIR / f"chip_{name}.cmake"
    if not f.exists():
        return {}

    text = f.read_text(encoding="utf-8", errors="replace")
    info = {"name": name}

    # CHIP_BLE_SUPPORTED TRUE/FALSE
    m = re.search(r'set\(CHIP_BLE_SUPPORTED\s+(\w+)\s*\)', text)
    info["ble"] = bool(m and m.group(1).upper() == "TRUE")

    # CHIP_PERIPH_USB_SOURCES 是否为空（逐行解析，避免跨行误匹配）
    info["usb"] = False
    lines = text.splitlines()
    for i, ln in enumerate(lines):
        if "set(CHIP_PERIPH_USB_SOURCES" not in ln:
            continue
        # 收集到配对的右括号
        buf = ln
        j = i
        while buf.count("(") > buf.count(")") and j + 1 < len(lines):
            j += 1
            buf += "\n" + lines[j]
        body = buf.split("CHIP_PERIPH_USB_SOURCES", 1)[1]
        body = body.rsplit(")", 1)[0]
        body = re.sub(r'(?m)#.*$', '', body)
        # 去掉空字符串字面量 "" 与空白
        body = body.replace('""', '').strip()
        info["usb"] = bool(body)
        break

    return info


def targets_for(chip: dict) -> list:
    """按 CMakeLists.txt 的条件推导目标列表。"""
    t = ["blink", "uart_demo"]

    if chip.get("ble") and chip.get("usb") and chip["name"] != "ch572":
        t.append("ble_usb")

    if chip.get("usb"):
        t.append("usb_cdc")

    # gateway 默认不构建（依赖旧 ch32fun 框架，需 -DGW_BUILD=ON）
    return t


def main() -> int:
    include = []

    for name in CHIP_ORDER:
        info = read_chip(name)
        if not info:
            print(f"警告：找不到 chip_{name}.cmake", file=sys.stderr)
            continue
        for t in targets_for(info):
            include.append({"chip": name, "target": t})

    # 直接输出数组，供 matrix: { include: fromJSON(...) } 使用
    print(json.dumps(include))
    return 0


if __name__ == "__main__":
    sys.exit(main())
