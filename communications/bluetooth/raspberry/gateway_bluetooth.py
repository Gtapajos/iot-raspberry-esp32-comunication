from pathlib import Path
import asyncio
import csv
import json
import math
import os
import tempfile
import time
from datetime import datetime, timedelta

from bleak import BleakClient, BleakScanner
from config import (
    BLUETOOTH_INTERFACE, SERVICE_UUID, MEASUREMENT_UUID, ACK_UUID,
    DEVICE_NAME_PREFIX, SCAN_TIMEOUT_S, MEASUREMENT_TIMEOUT_S,
    EXPECTED_INTERVAL_S, MISSED_GRACE_S
)

CSV_FILE = Path(__file__).resolve().parent / "gateway.csv"
CSV_FIELDS = ["node_id", "timestamp", "temperature", "ph", "turbidity", "status"]
MESSAGE_FIELDS = ["node_id", "timestamp", "temperature", "ph", "turbidity"]
NEXT_EXPECTED = {}

def ensure_csv():
    if not CSV_FILE.exists():
        with CSV_FILE.open("w", newline="", encoding="utf-8") as f:
            csv.DictWriter(f, fieldnames=CSV_FIELDS).writeheader()
        return

    with CSV_FILE.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames == CSV_FIELDS:
            return
        rows = list(reader)

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", newline="", encoding="utf-8",
            dir=CSV_FILE.parent, delete=False
        ) as f:
            temp_path = Path(f.name)
            writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            writer.writeheader()
            for old_row in rows:
                row = {field: old_row.get(field, "") for field in CSV_FIELDS}
                if not row["status"]:
                    row["status"] = "RECEIVED"
                writer.writerow(row)
        os.replace(temp_path, CSV_FILE)
    finally:
        if temp_path and temp_path.exists():
            temp_path.unlink()

def store_row(row):
    with CSV_FILE.open("a", newline="", encoding="utf-8") as f:
        csv.DictWriter(f, fieldnames=CSV_FIELDS).writerow(row)

def record_overdue(now):
    for node_id, expected in NEXT_EXPECTED.items():
        while now >= expected["deadline"]:
            store_row({
                "node_id": node_id,
                "timestamp": expected["timestamp"].isoformat(timespec="seconds"),
                "temperature": "",
                "ph": "",
                "turbidity": "",
                "status": "NOT_RECEIVED"
            })
            expected["deadline"] += EXPECTED_INTERVAL_S
            expected["timestamp"] += timedelta(seconds=EXPECTED_INTERVAL_S)

def store_received(data):
    now = time.monotonic()
    received_at = datetime.now().astimezone()
    record_overdue(now)
    store_row({
        **data,
        "ph": data["ph"] if data["ph"] is not None else "NA",
        "turbidity": data["turbidity"] if data["turbidity"] is not None else "NA",
        "status": "RECEIVED"
    })
    NEXT_EXPECTED[data["node_id"]] = {
        "deadline": now + EXPECTED_INTERVAL_S + MISSED_GRACE_S,
        "timestamp": received_at + timedelta(seconds=EXPECTED_INTERVAL_S)
    }

def valid_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)

def validate(data):
    for field in MESSAGE_FIELDS:
        if field not in data:
            return False
    if not isinstance(data["node_id"], str):
        return False
    if not isinstance(data["timestamp"], str):
        return False
    if not valid_number(data["temperature"]):
        return False
    for field in ("ph", "turbidity"):
        if data[field] is not None and not valid_number(data[field]):
            return False
    return True

async def handle_device(device):
    print(f"Conectando a {device.name} [{device.address}]")
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
            return None

        data = received["data"]

        if not validate(data):
            print("Medição rejeitada.")
            await client.write_gatt_char(ACK_UUID, b"ERROR", response=True)
            await client.stop_notify(MEASUREMENT_UUID)
            return None

        store_received(data)
        await client.write_gatt_char(ACK_UUID, b"OK", response=True)

        print(f"Recebido e armazenado: {data['node_id']} {data['timestamp']}")
        await client.stop_notify(MEASUREMENT_UUID)
        return data

async def main():
    ensure_csv()
    print("Procurando continuamente nós ICC316 via BLE...")

    while True:
        devices = await BleakScanner.discover(
            timeout=SCAN_TIMEOUT_S,
            bluez={"adapter": BLUETOOTH_INTERFACE}
        )

        candidates = [
            d for d in devices
            if d.name and d.name.startswith(DEVICE_NAME_PREFIX)
        ]

        for device in candidates:
            try:
                await handle_device(device)
            except Exception as exc:
                print(f"Erro ao processar {device.name}: {exc}")

        record_overdue(time.monotonic())

if __name__ == "__main__":
    asyncio.run(main())
