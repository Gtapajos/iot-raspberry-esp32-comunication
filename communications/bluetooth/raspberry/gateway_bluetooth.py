from pathlib import Path
import asyncio
import csv
import json
import math

from bleak import BleakClient, BleakScanner
from config import (
    BLUETOOTH_INTERFACE, SERVICE_UUID, MEASUREMENT_UUID, ACK_UUID,
    DEVICE_NAME_PREFIX, SCAN_TIMEOUT_S, MEASUREMENT_TIMEOUT_S
)

CSV_FILE = Path(__file__).resolve().parent / "gateway.csv"
CSV_FIELDS = ["node_id", "timestamp", "temperature", "ph", "turbidity", "rssi"]

def ensure_csv():
    if not CSV_FILE.exists():
        with CSV_FILE.open("w", newline="", encoding="utf-8") as f:
            csv.DictWriter(f, fieldnames=CSV_FIELDS).writeheader()

def valid_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)

def validate(data):
    # Nota: removemos "rssi" daqui pois o sensor envia apenas as medições no payload JSON
    for field in ("node_id", "timestamp", "temperature"):
        if field not in data:
            return False
    if not isinstance(data["node_id"], str):
        return False
    if not isinstance(data["timestamp"], str):
        return False
    if not valid_number(data["temperature"]):
        return False
    for field in ("ph", "turbidity"):
        if data.get(field) is not None and not valid_number(data[field]):
            return False
    return True

def store(data, rssi):
    row = dict(data)
    row["rssi"] = rssi if rssi is not None else "NA"
    if row["ph"] is None: row["ph"] = "NA"
    if row["turbidity"] is None: row["turbidity"] = "NA"
    with CSV_FILE.open("a", newline="", encoding="utf-8") as f:
        csv.DictWriter(f, fieldnames=CSV_FIELDS).writerow(row)

async def handle_device(device, rssi):
    print(f"Conectando a {device.name} [{device.address}] | RSSI: {rssi} dBm")
    async with BleakClient(
        device,
        bluez={"adapter": BLUETOOTH_INTERFACE}
    ) as client:
        if not client.is_connected:
            return

        event = asyncio.Event()
        received = {}

        def notification_callback(_, data):
            try:
                received["data"] = json.loads(bytes(data).decode("utf-8"))
                event.set()
            except (UnicodeDecodeError, json.JSONDecodeError):
                pass

        await client.start_notify(MEASUREMENT_UUID, notification_callback)
        try:
            await asyncio.wait_for(event.wait(), timeout=MEASUREMENT_TIMEOUT_S)
        except asyncio.TimeoutError:
            print("Timeout aguardando medição.")
            await client.stop_notify(MEASUREMENT_UUID)
            return

        data = received["data"]

        if not validate(data):
            print("Medição rejeitada.")
            await client.write_gatt_char(ACK_UUID, b"ERROR", response=True)
            return

        store(data, rssi)
        await client.write_gatt_char(ACK_UUID, b"OK", response=True)

        print(f"Recebido e armazenado: {data['node_id']} {data['timestamp']} (RSSI: {rssi} dBm)")
        await client.stop_notify(MEASUREMENT_UUID)

async def main():
    ensure_csv()
    print("Procurando nós ICC316 via BLE...")

    # Usamos return_adv=True para obter os dados de anúncio junto com o RSSI
    devices_dict = await BleakScanner.discover(
        timeout=SCAN_TIMEOUT_S,
        return_adv=True,
        bluez={"adapter": BLUETOOTH_INTERFACE}
    )

    candidates = []
    for d, adv in devices_dict.values():
        if d.name and d.name.startswith(DEVICE_NAME_PREFIX):
            candidates.append((d, adv.rssi))

    if not candidates:
        print("Nenhum nó ICC316 encontrado.")
        return

    for device, rssi in candidates:
        try:
            await handle_device(device, rssi)
        except Exception as exc:
            print(f"Erro ao processar {device.name}: {exc}")

if __name__ == "__main__":
    asyncio.run(main())