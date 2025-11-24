import os
import serial
import requests
import json
import threading
import time
from dotenv import load_dotenv
from flask import Flask, request, jsonify

# -------------------------------------------------------------------
# Configuração Global
# -------------------------------------------------------------------
load_dotenv()
SERIAL_PORT = os.getenv("SERIAL_PORT", "COM5")
SERIAL_BAUD = int(os.getenv("SERIAL_BAUD", 9600))
LOG_ACCESS_URL = os.getenv("LOG_ACCESS_URL")
BRIDGE_PORT = int(os.getenv("BRIDGE_PORT", 8082))
HTTP_TIMEOUT = 5

ser = None
app = Flask(__name__)


# -------------------------------------------------------------------
# Abrir Serial
# -------------------------------------------------------------------
def open_serial():
    global ser
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
            print(f"[Serial] Conectado em {SERIAL_PORT}")
            return
        except serial.SerialException:
            print(f"[Serial] Porta {SERIAL_PORT} indisponível, tentando...")
            time.sleep(2)


# -------------------------------------------------------------------
# Envia comando e espera resposta JSON (ou texto cru)
# -------------------------------------------------------------------
def send_cmd(cmd: str, expect_json=True):
    if not ser or not ser.is_open:
        open_serial()

    ser.write((cmd + "\n").encode())
    print(f"[-> Arduino] {cmd}")

    try:
        line = ser.readline().decode().strip()
        print(f"[<- Arduino] {line}")

        if expect_json:
            return json.loads(line)
        return line

    except Exception as e:
        print("[Erro] Falha lendo JSON da serial:", e)
        return None


# -------------------------------------------------------------------
# =================== FUNÇÕES DA VERSÃO B ===========================
# -------------------------------------------------------------------

# ---------------------- BEGIN_BATCH ------------------------
def begin_batch():
    return send_cmd("BEGIN_BATCH", expect_json=False)


# ---------------------- IMPORTAR TEMPLATE -------------------
def batch_send_template(slot: int, hex_blob: str):
    """
    Envia um template completo para um slot do batch.
    """
    ser.write(f"TEMPLATE_SLOT:{slot}\n".encode())
    print(f"[-> Arduino] TEMPLATE_SLOT:{slot}")
    time.sleep(0.1)

    # manda HEX em pedaços
    for i in range(0, len(hex_blob), 512):
        chunk = hex_blob[i:i+512]
        ser.write(f"TEMPLATE_DATA:{chunk}\n".encode())
        time.sleep(0.02)

    ser.write(b"TEMPLATE_END\n")
    print("[-> Arduino] TEMPLATE_END")


# ---------------------- RUN_BATCH_MATCH ---------------------
def run_batch_match():
    """
    Solicita ao Arduino para capturar o dedo, comparar com o LOTE
    e retornar match/notmatch + tempo.
    """
    resp = send_cmd("RUN_BATCH_MATCH", expect_json=True)
    return resp


# ---------------------- CLEAR_BATCH -------------------------
def clear_batch():
    return send_cmd("CLEAR_BATCH", expect_json=False)


# -------------------------------------------------------------------
# =================== ENDPOINTS HTTP ===============================
# -------------------------------------------------------------------

@app.route("/batch/start", methods=["POST"])
def http_batch_start():
    begin_batch()
    return jsonify({"status": "batch_started"})


@app.route("/batch/upload", methods=["POST"])
def http_batch_upload():
    """
    Envia um template para o DY50 dentro do batch.
    Payload:
    {
        "slot": 1,
        "hex": "AABBCCDDEEFF..."
    }
    """
    data = request.json
    slot = data.get("slot")
    hex_blob = data.get("hex")

    batch_send_template(slot, hex_blob)
    return jsonify({"status": "template_received", "slot": slot})


@app.route("/batch/run", methods=["POST"])
def http_batch_run():
    result = run_batch_match()
    return jsonify(result)


@app.route("/batch/clear", methods=["POST"])
def http_batch_clear():
    clear_batch()
    return jsonify({"status": "batch_cleared"})


# -------------------------------------------------------------------
# Thread para capturar eventos padrões do Arduino
# -------------------------------------------------------------------
def serial_listener():
    global ser

    while True:
        if ser and ser.in_waiting:
            try:
                line = ser.readline().decode().strip()

                if not line:
                    continue

                print("[Arduino Event]", line)

                if "match_found" in line:
                    try:
                        data = json.loads(line)
                        requests.post(LOG_ACCESS_URL, json=data, timeout=HTTP_TIMEOUT)
                    except:
                        pass

            except Exception as e:
                print("[Listener] Erro:", e)

        time.sleep(0.1)


# -------------------------------------------------------------------
# MAIN
# -------------------------------------------------------------------
def main():
    print("Iniciando Bridge — Versão B (Batch Mode)")
    open_serial()

    threading.Thread(target=serial_listener, daemon=True).start()

    print(">>> Bridge rodando na porta", BRIDGE_PORT)
    app.run(host="0.0.0.0", port=BRIDGE_PORT, debug=False)


if __name__ == "__main__":
    main()
