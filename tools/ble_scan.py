"""
BLE 扫描工具 —— 验证 CH592 BLE 广播是否正常

用法：
    python tool/ble_scan.py            # 扫描 12 秒
    python tool/ble_scan.py 20         # 扫描 20 秒
    python tool/ble_scan.py 20 CH592   # 扫描 20 秒并过滤名字包含 CH592 的设备
"""
import asyncio
import sys

from bleak import BleakScanner


async def main(duration: float, name_filter: str | None):
    print(f"扫描 BLE 设备 {duration} 秒 ...")
    print("-" * 70)

    found: dict[str, dict] = {}

    def on_detect(device, adv):
        key = device.address
        if key in found:
            return
        name = device.name or adv.local_name or ""
        found[key] = {
            "name": name,
            "rssi": adv.rssi,
            "services": list(adv.service_uuids or []),
        }

    scanner = BleakScanner(detection_callback=on_detect)
    await scanner.start()
    await asyncio.sleep(duration)
    await scanner.stop()

    if not found:
        print("未发现任何 BLE 设备")
        return

    # 按 RSSI 排序
    items = sorted(found.items(), key=lambda kv: kv[1]["rssi"], reverse=True)

    hit = 0
    for addr, info in items:
        name = info["name"] or "(无名)"
        if name_filter and name_filter.lower() not in name.lower():
            continue
        hit += 1
        print(f"  {name:<28} RSSI={info['rssi']:>4} dBm   {addr}")
        if info["services"]:
            print(f"      服务: {', '.join(info['services'])}")

    print("-" * 70)
    print(f"共 {len(items)} 个设备", end="")
    if name_filter:
        print(f"，其中 {hit} 个匹配 '{name_filter}'")
    else:
        print()


if __name__ == "__main__":
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
    flt = sys.argv[2] if len(sys.argv) > 2 else None
    asyncio.run(main(dur, flt))
