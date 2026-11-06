"""
BLE 深度扫描 —— 显示所有设备的完整广播数据（含 scan response）

用法：
    python tool/ble_scan_full.py            # 扫描 15 秒
    python tool/ble_scan_full.py 20         # 扫描 20 秒
"""
import asyncio
import sys

from bleak import BleakScanner


async def main(duration: float):
    print(f"深度扫描 {duration} 秒（含 scan response）...")
    print("=" * 78)

    found: dict[str, dict] = {}

    def on_detect(device, adv):
        key = device.address
        entry = found.setdefault(key, {
            "name": "",
            "rssi": adv.rssi,
            "adv_data": b"",
            "scan_rsp": b"",
            "services": [],
        })
        # 名字可能来自 device.name 或 adv.local_name
        name = device.name or adv.local_name or ""
        if name:
            entry["name"] = name
        entry["rssi"] = adv.rssi
        # 广播原始数据
        if adv.platform_data:
            for pd in adv.platform_data:
                data = getattr(pd, "data", None)
                if data:
                    entry["adv_data"] = bytes(data)
        if adv.service_data:
            entry["services"] = list(adv.service_data.keys())

    scanner = BleakScanner(detection_callback=on_detect)
    await scanner.start()
    await asyncio.sleep(duration)
    await scanner.stop()

    if not found:
        print("未发现任何 BLE 设备")
        return

    items = sorted(found.items(), key=lambda kv: kv[1]["rssi"], reverse=True)

    named = 0
    for addr, info in items:
        name = info["name"]
        raw = info["adv_data"]
        # 从原始广播数据里提取名字（0x09 = Complete Local Name, 0x08 = Shortened）
        raw_name = ""
        i = 0
        while i < len(raw):
            ln = raw[i]
            if ln == 0 or i + ln >= len(raw):
                break
            typ = raw[i + 1]
            if typ in (0x08, 0x09):
                try:
                    raw_name = raw[i + 2:i + 1 + ln].decode("utf-8", "ignore")
                except Exception:
                    pass
                break
            i += ln + 1

        display = name or raw_name
        if display:
            named += 1
            print(f"  {display:<28} RSSI={info['rssi']:>4}  {addr}")
            if raw:
                print(f"      广播: {raw.hex(' ')}")

    print("=" * 78)
    print(f"共 {len(items)} 个设备，其中 {named} 个有名字")


if __name__ == "__main__":
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 15.0
    asyncio.run(main(dur))
