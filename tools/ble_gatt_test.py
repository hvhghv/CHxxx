"""
BLE GATT 收发测试（Windows / bleak）

用法：
    python tool/ble_gatt_test.py                # 自动找 CH592-BLE
    python tool/ble_gatt_test.py <MAC地址>       # 指定设备

流程：
    1. 扫描找到目标设备
    2. 连接
    3. 枚举所有服务 / 特征值 / 描述符
    4. 订阅 TX (0xFFF2) 的 notify
    5. 向 RX (0xFFF1) 写入 "hello"，等待回显
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

TARGET_NAME = "CH592-BLE"
SVC_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
RX_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"   # write
TX_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"   # notify

received = []


def on_notify(_handle, data: bytearray):
    received.append(bytes(data))
    print(f"  <<< NOTIFY: {data!r}  (hex: {bytes(data).hex(' ')})")


async def find_device(name: str, timeout: float = 12.0):
    print(f"扫描 {timeout}s 查找 '{name}' ...")
    found = {}

    def cb(dev, adv):
        n = dev.name or adv.local_name or ""
        if n and name.lower() in n.lower():
            found[dev.address] = n

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(timeout)
    await scanner.stop()

    if not found:
        print(f"未找到 '{name}'")
        return None
    addr = list(found.keys())[0]
    print(f"找到: {found[addr]}  {addr}")
    return addr


async def dump_services(client: BleakClient):
    print("\n=== GATT 服务枚举 ===")
    for svc in client.services:
        print(f"[Service] {svc.uuid}  ({svc.description})")
        for ch in svc.characteristics:
            print(f"   [Char] {ch.uuid}  props={','.join(ch.properties)}")
            for d in ch.descriptors:
                print(f"      [Desc] {d.uuid}  ({d.description})")


async def main():
    addr = sys.argv[1] if len(sys.argv) > 1 else await find_device(TARGET_NAME)
    if not addr:
        return

    print(f"\n连接 {addr} ...")
    async with BleakClient(addr, timeout=20.0) as client:
        print(f"已连接: {client.is_connected}")

        await dump_services(client)

        # 订阅 TX 通知
        print(f"\n订阅 TX ({TX_UUID}) ...")
        try:
            await client.start_notify(TX_UUID, on_notify)
            print("  订阅成功")
        except Exception as e:
            print(f"  订阅失败: {e}")

        # 向 RX 写入
        for msg in [b"hello", b"CH592", b"echo-123"]:
            print(f"\n>>> WRITE RX: {msg!r}")
            try:
                await client.write_gatt_char(RX_UUID, msg, response=True)
                print("  写入成功")
            except Exception as e:
                print(f"  写入失败: {e}")
            await asyncio.sleep(0.8)

        # 读 TX 当前值
        print("\n读取 TX 当前值 ...")
        try:
            val = await client.read_gatt_char(TX_UUID)
            print(f"  TX = {bytes(val)!r}")
        except Exception as e:
            print(f"  读取失败: {e}")

        await asyncio.sleep(1.0)
        try:
            await client.stop_notify(TX_UUID)
        except Exception:
            pass

    print("\n=== 结果 ===")
    if received:
        print(f"收到 {len(received)} 条通知：")
        for i, r in enumerate(received):
            print(f"  [{i}] {r!r}")
    else:
        print("未收到任何通知（回显失败）")


if __name__ == "__main__":
    asyncio.run(main())
