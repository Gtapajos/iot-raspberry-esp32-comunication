# Tutorial: Gateway Bluetooth BLE no Raspberry Pi

## 1. Instalar dependências

No terminal do Raspberry Pi, execute:

```bash
sudo apt update
sudo apt install bluez python3-bleak
```

## 2. Criar a pasta do projeto

```bash
mkdir -p ~/gateway_bluetooth
cd ~/gateway_bluetooth
```

## 3. Criar o arquivo `config.py`

Crie `config.py` nesta pasta e insira:

```python
# config.py
# Adaptador Bluetooth do Raspberry Pi.
# Normalmente: hci0
# Verifique com: bluetoothctl list
BLUETOOTH_INTERFACE = "hci0"

SERVICE_UUID = "7d8f0001-3160-4b12-9a10-000000000001"
MEASUREMENT_UUID = "7d8f0002-3160-4b12-9a10-000000000002"
ACK_UUID = "7d8f0003-3160-4b12-9a10-000000000003"

DEVICE_NAME_PREFIX = "ICC316-G"
SCAN_TIMEOUT_S = 10
MEASUREMENT_TIMEOUT_S = 30
```

## 4. Criar o arquivo `gateway_bluetooth.py`

Na mesma pasta, crie `gateway_bluetooth.py` e insira o código:

```python
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
CSV_FIELDS = ["node_id", "timestamp", "temperature", "ph", "turbidity"]

def ensure_csv():
    if not CSV_FILE.exists():
        with CSV_FILE.open("w", newline="", encoding="utf-8") as f:
            csv.DictWriter(f, fieldnames=CSV_FIELDS).writeheader()

def valid_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)

def validate(data):
    for field in CSV_FIELDS:
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

def store(data):
    row = dict(data)
    if row["ph"] is None: row["ph"] = "NA"
    if row["turbidity"] is None: row["turbidity"] = "NA"
    with CSV_FILE.open("a", newline="", encoding="utf-8") as f:
        csv.DictWriter(f, fieldnames=CSV_FIELDS).writerow(row)

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
            return

        data = received["data"]

        if not validate(data):
            print("Medição rejeitada.")
            await client.write_gatt_char(ACK_UUID, b"ERROR", response=True)
            return

        store(data)
        await client.write_gatt_char(ACK_UUID, b"OK", response=True)

        print(f"Recebido e armazenado: {data['node_id']} {data['timestamp']}")
        await client.stop_notify(MEASUREMENT_UUID)

async def main():
    ensure_csv()
    print("Procurando nós ICC316 via BLE...")

    devices = await BleakScanner.discover(
        timeout=SCAN_TIMEOUT_S,
        bluez={"adapter": BLUETOOTH_INTERFACE}
    )

    candidates = [
        d for d in devices
        if d.name and d.name.startswith(DEVICE_NAME_PREFIX)
    ]

    if not candidates:
        print("Nenhum nó ICC316 encontrado.")
        return

    for device in candidates:
        try:
            await handle_device(device)
        except Exception as exc:
            print(f"Erro ao processar {device.name}: {exc}")

if __name__ == "__main__":
    asyncio.run(main())
```

## 5. Executar o gateway

Com o ESP32 ligado, inicializado e aguardando a conexão:

```bash
cd ~/gateway_bluetooth
python3 gateway_bluetooth.py
```

O programa procura dispositivos BLE cujo nome comece com `ICC316-G`, conecta-se a cada dispositivo encontrado, aguarda uma notificação com uma medição JSON, valida os campos, acrescenta os dados a `gateway.csv` e envia `OK` ao ESP32 quando a medição é válida. Se a medição não for válida, envia `ERROR`.

## 6. Arquivo CSV gerado

O arquivo `gateway.csv` é criado automaticamente na pasta do projeto. Ele contém as colunas:

```text
node_id,timestamp,temperature,ph,turbidity
```

Se `ph` ou `turbidity` vierem como `null` no JSON, serão registrados como `NA`.

## Observações

- Confirme o adaptador Bluetooth com `bluetoothctl list`; se necessário, ajuste `BLUETOOTH_INTERFACE` em `config.py`.
- O nome anunciado pelo ESP32 precisa começar com `ICC316-G`.
- O firmware do ESP32 deve usar os mesmos UUIDs de serviço, medição e confirmação.
- O gateway só grava a medição depois de validar os dados; em seguida, envia a confirmação `OK`.
