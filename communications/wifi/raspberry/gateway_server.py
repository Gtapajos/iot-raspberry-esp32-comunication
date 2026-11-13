from flask import Flask, request, jsonify
from pathlib import Path
import csv
from datetime import datetime, timedelta
import math
import os
import tempfile
import threading
import time


app = Flask(__name__)


# -------------------------------------------------------------
# Arquivo CSV
# -------------------------------------------------------------

CSV_FILE = Path(__file__).resolve().parent / "gateway.csv"

CSV_FIELDS = [
    "node_id",
    "timestamp",
    "rssi",
    "temperature",
    "ph",
    "turbidity",
    "status"
]

EXPECTED_INTERVAL_S = 15
MISSED_GRACE_S = 5
MONITOR_INTERVAL_S = 0.25
CSV_LOCK = threading.Lock()
NEXT_EXPECTED = {}


# -------------------------------------------------------------
# Cria o CSV caso ainda não exista
# -------------------------------------------------------------

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
            delete=False
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


def append_row(row):
    with CSV_FILE.open("a", newline="", encoding="utf-8") as file:
        csv.DictWriter(file, fieldnames=CSV_FIELDS).writerow(row)


def append_missing(node_id, expected_at):
    append_row({
        "node_id": node_id,
        "timestamp": expected_at.isoformat(timespec="seconds"),
        "rssi": "",
        "temperature": "",
        "ph": "",
        "turbidity": "",
        "status": "NOT_RECEIVED"
    })


def record_overdue(now):
    for node_id, expected in NEXT_EXPECTED.items():
        while now >= expected["deadline"]:
            append_missing(node_id, expected["timestamp"])
            expected["deadline"] += EXPECTED_INTERVAL_S
            expected["timestamp"] += timedelta(seconds=EXPECTED_INTERVAL_S)


def monitor_missing_messages():
    while True:
        time.sleep(MONITOR_INTERVAL_S)
        with CSV_LOCK:
            record_overdue(time.monotonic())


def record_received(data):
    now = time.monotonic()
    received_at = datetime.now().astimezone()
    with CSV_LOCK:
        record_overdue(now)
        append_row({
            "node_id": data["node_id"],
            "timestamp": data["timestamp"],
            "rssi": data.get("rssi", ""),
            "temperature": data["temperature"],
            "ph": data["ph"] if data["ph"] is not None else "NA",
            "turbidity": (
                data["turbidity"] if data["turbidity"] is not None else "NA"
            ),
            "status": "RECEIVED"
        })
        NEXT_EXPECTED[data["node_id"]] = {
            "deadline": now + EXPECTED_INTERVAL_S + MISSED_GRACE_S,
            "timestamp": received_at + timedelta(seconds=EXPECTED_INTERVAL_S)
        }


# -------------------------------------------------------------
# Verifica se um valor é numérico finito
# -------------------------------------------------------------

def is_valid_number(value):

    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


# -------------------------------------------------------------
# Endpoint para receber medições
# -------------------------------------------------------------

@app.route("/dados", methods=["POST"])
def receive_data():

    data = request.get_json(silent=True)


    # ---------------------------------------------------------
    # JSON válido?
    # ---------------------------------------------------------

    if data is None:

        return jsonify({
            "status": "error",
            "message": "JSON invalido"
        }), 400


    # ---------------------------------------------------------
    # Campos obrigatórios
    # ---------------------------------------------------------

    required_fields = [
        "node_id",
        "timestamp",
        "temperature",
        "ph",
        "turbidity"
    ]


    for field in required_fields:

        if field not in data:

            return jsonify({
                "status": "error",
                "message": f"Campo ausente: {field}"
            }), 400


    # ---------------------------------------------------------
    # Validação dos tipos
    # ---------------------------------------------------------

    if not isinstance(data["node_id"], str):

        return jsonify({
            "status": "error",
            "message": "node_id invalido"
        }), 400


    if not isinstance(data["timestamp"], str):

        return jsonify({
            "status": "error",
            "message": "timestamp invalido"
        }), 400


    if not is_valid_number(data["temperature"]):

        return jsonify({
            "status": "error",
            "message": "temperature invalida"
        }), 400

    if "rssi" in data and not is_valid_number(data["rssi"]):

        return jsonify({
            "status": "error",
            "message": "rssi invalido"
        }), 400


    if (
        data["ph"] is not None
        and not is_valid_number(data["ph"])
    ):

        return jsonify({
            "status": "error",
            "message": "ph invalido"
        }), 400


    if (
        data["turbidity"] is not None
        and not is_valid_number(data["turbidity"])
    ):

        return jsonify({
            "status": "error",
            "message": "turbidity invalida"
        }), 400


    # ---------------------------------------------------------
    # Garantir que o CSV exista
    # ---------------------------------------------------------

    ensure_csv()
    record_received(data)


    # ---------------------------------------------------------
    # Confirmar recebimento
    # ---------------------------------------------------------

    return jsonify({
        "status": "ok"
    }), 200


# -------------------------------------------------------------
# Inicialização
# -------------------------------------------------------------

if __name__ == "__main__":

    ensure_csv()
    threading.Thread(
        target=monitor_missing_messages,
        daemon=True
    ).start()

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False
    )
