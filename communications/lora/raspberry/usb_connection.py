from datetime import datetime, timedelta
from pathlib import Path
import csv
import json
import math
import os
import serial
import tempfile
import time

# =============================================================
# USB configuration
# =============================================================

SERIAL_PORT = "/dev/ttyUSB0"
BAUDRATE = 115200
SERIAL_TIMEOUT = 1

# The LoRa transmitter sends every 15 seconds. The grace period allows
# for radio reception, USB forwarding, and serial scheduling delays.
EXPECTED_INTERVAL_S = 15
MISSED_GRACE_S = 5

# =============================================================
# CSV file
# =============================================================

CSV_FILE = Path(__file__).resolve().parent / "gateway.csv"
CSV_FIELDS = [
    "node_id",
    "timestamp",
    "temperature",
    "ph",
    "turbidity",
    "status",
]
MESSAGE_FIELDS = CSV_FIELDS[:-1]

# Each node is monitored independently, starting with its first valid packet.
NEXT_EXPECTED = {}


def ensure_csv():
    if not CSV_FILE.exists():
        with CSV_FILE.open("w", newline="", encoding="utf-8") as file:
            csv.DictWriter(file, fieldnames=CSV_FIELDS).writeheader()
        return

    with CSV_FILE.open("r", newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        if reader.fieldnames == CSV_FIELDS:
            return
        rows = list(reader)

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            newline="",
            encoding="utf-8",
            dir=CSV_FILE.parent,
            delete=False,
        ) as file:
            temp_path = Path(file.name)
            writer = csv.DictWriter(file, fieldnames=CSV_FIELDS)
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


def is_valid_number(value):
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


def validate_data(data):
    if not isinstance(data, dict):
        return False, "JSON invalido"

    for field in MESSAGE_FIELDS:
        if field not in data:
            return False, f"Campo ausente: {field}"

    if not isinstance(data["node_id"], str) or not data["node_id"].strip():
        return False, "node_id invalido"

    if not isinstance(data["timestamp"], str) or not data["timestamp"].strip():
        return False, "timestamp invalido"

    if not is_valid_number(data["temperature"]):
        return False, "temperature invalida"

    for field in ("ph", "turbidity"):
        if data[field] is not None and not is_valid_number(data[field]):
            return False, f"{field} invalido"

    return True, None


def append_row(row):
    with CSV_FILE.open("a", newline="", encoding="utf-8") as file:
        csv.DictWriter(file, fieldnames=CSV_FIELDS).writerow(row)
        file.flush()


def record_missing(node_id, expected_at):
    append_row({
        "node_id": node_id,
        "timestamp": expected_at.isoformat(timespec="seconds"),
        "temperature": "",
        "ph": "",
        "turbidity": "",
        "status": "NOT_RECEIVED",
    })
    print(f"[CSV] Pacote ausente registrado: {node_id} ({expected_at.isoformat(timespec='seconds')})")


def record_overdue(now=None):
    if now is None:
        now = time.monotonic()

    for node_id, expected in NEXT_EXPECTED.items():
        while now >= expected["deadline"]:
            record_missing(node_id, expected["timestamp"])
            expected["deadline"] += EXPECTED_INTERVAL_S
            expected["timestamp"] += timedelta(seconds=EXPECTED_INTERVAL_S)


def save_measurement(data):
    ensure_csv()
    now = time.monotonic()
    received_at = datetime.now().astimezone()

    # Record any missed slots before the packet that arrived late.
    record_overdue(now)

    append_row({
        "node_id": data["node_id"],
        "timestamp": data["timestamp"],
        "temperature": data["temperature"],
        "ph": data["ph"] if data["ph"] is not None else "NA",
        "turbidity": data["turbidity"] if data["turbidity"] is not None else "NA",
        "status": "RECEIVED",
    })

    NEXT_EXPECTED[data["node_id"]] = {
        "deadline": now + EXPECTED_INTERVAL_S + MISSED_GRACE_S,
        "timestamp": received_at + timedelta(seconds=EXPECTED_INTERVAL_S),
    }


def process_message(ser, mensagem):
    try:
        data = json.loads(mensagem)
        valid, error = validate_data(data)
        if not valid:
            print("[USB] Erro:", error)
            return

        print("\n================================")
        print("[USB] Medicao recebida:")
        print("Node ID:", data["node_id"])
        print("Timestamp:", data["timestamp"])
        print("Temperatura:", data["temperature"])
        print("pH:", data["ph"])
        print("Turbidez:", data["turbidity"])

        save_measurement(data)
        print("[CSV] Medicao armazenada com status RECEIVED.")

        # Acknowledge only after the received measurement has been saved.
        ser.write(b"ACK\n")
        ser.flush()
        print("[USB] ACK enviado ao ESP32.")
        print("================================")

    except json.JSONDecodeError:
        print("[USB] Erro: JSON invalido.")
    except (OSError, csv.Error) as error:
        print("[Raspberry] Erro ao armazenar medicao:", error)
    except Exception as error:
        print("[Raspberry] Erro ao processar medicao:", error)


def main():
    ensure_csv()
    print("================================")
    print("RASPBERRY PI - GATEWAY USB")
    print("================================")
    print("[CSV] Arquivo:", CSV_FILE)
    print(
        f"[Gateway] Monitorando pacotes LoRa a cada {EXPECTED_INTERVAL_S} "
        f"segundos, com {MISSED_GRACE_S} segundos de tolerancia."
    )
    print("[USB] Conectando ao ESP32...")

    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            timeout=SERIAL_TIMEOUT,
            write_timeout=2,
        )
    except serial.SerialException as error:
        print("[USB] Erro na comunicacao serial:", error)
        return

    try:
        time.sleep(2)
        print("[USB] Conexao estabelecida!")
        print("[Gateway] Aguardando dados via USB...")

        while True:
            record_overdue()

            linha = ser.readline()
            if not linha:
                continue

            try:
                mensagem = linha.decode("utf-8").strip()
            except UnicodeDecodeError:
                print("[USB] Erro de codificacao.")
                continue

            if mensagem:
                process_message(ser, mensagem)

    except serial.SerialException as error:
        print("[USB] Erro na comunicacao serial:", error)
    except KeyboardInterrupt:
        print("\n[Gateway] Encerrando programa...")
    finally:
        if ser.is_open:
            ser.close()
            print("[USB] Conexao encerrada.")


if __name__ == "__main__":
    main()
