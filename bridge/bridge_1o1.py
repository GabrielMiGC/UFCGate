import os
import serial
import requests
import json
import threading
import time
from dotenv import load_dotenv
from flask import Flask, request, jsonify

# --- Configuração Global ---
load_dotenv()
SERIAL_PORT = os.getenv('SERIAL_PORT', 'COM5')
SERIAL_BAUD = int(os.getenv('SERIAL_BAUD', 9600))
LOG_ACCESS_URL = os.getenv('LOG_ACCESS_URL')
BRIDGE_PORT = int(os.getenv('BRIDGE_PORT', 8081))
HTTP_TIMEOUT = 5

ser = None
app = Flask(__name__)

# =====================================================================
#  Conectar ao sensor (Arduino)
# =====================================================================

def open_serial():
    global ser
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
            print(f"[Serial] Conectado em {SERIAL_PORT}")
            return
        except serial.SerialException:
            print(f"[Serial] Porta {SERIAL_PORT} indisponível, tentando de novo...")
            time.sleep(2)

# =====================================================================
#  Função auxiliar para mandar comando ao Arduino e receber resposta
# =====================================================================

def send_cmd(cmd, expect_json=True):
    """Envia comando tipo STRING e retorna linha vinda da Serial."""
    if not ser or not ser.is_open:
        open_serial()

    ser.write((cmd + "\n").encode())
    print(f"[-> Arduino] {cmd}")

    # Espera a resposta
    try:
        line = ser.readline().decode().strip()
        print(f"[<- Arduino] {line}")
        if expect_json:
            return json.loads(line)
        return line
    except Exception as e:
        print("[Erro] Falha lendo Serial:", e)
        return None

# =====================================================================
#  ==================== NOVAS FUNÇÕES PARA VERSÃO A ====================
# =====================================================================

# ------------------------ EXTRAI MODELO -----------------------

def extract_template(sensor_id):
    """
    Envia comando GET_MODEL:<id> e aguarda HEX do Arduino.
    """
    cmd = f"GET_MODEL:{sensor_id}"
    ser.write((cmd + "\n").encode())

    print(f"[-> Arduino] {cmd}")

    # Arduino vai responder várias linhas:
    # {status:"start_export"}
    # TEMPLATE_HEX:<muitooo_hex>
    # {status:"export_done"}

    hex_data = ""

    while True:
        line = ser.readline().decode().strip()
        if not line:
            continue

        print(f"[<- Arduino] {line}")

        if line.startswith("TEMPLATE_HEX:"):
            hex_data += line.replace("TEMPLATE_HEX:", "")

        if '"export_done"' in line:
            break

    return hex_data


# ------------------------ CARREGA MODELO -----------------------

def load_template(sensor_id, hex_payload):
    """
    Envia SET_MODEL:<id> + modelo em HEX para gravar no buffer do sensor.
    """
    cmd = f"SET_MODEL:{sensor_id}"
    ser.write((cmd + "\n").encode())
    time.sleep(0.2)

    # envia em pedaços de 512 caracteres
    for i in range(0, len(hex_payload), 512):
        chunk = hex_payload[i:i+512]
        ser.write((f"HEX:{chunk}\n").encode())
        time.sleep(0.05)

    ser.write(b"HEX_END\n")
    print("[Bridge] Modelo enviado.")

    return True


# ------------------------ SCAN & COMPARE -----------------------

def scan_and_compare():
    """
    Manda comando que faz o Arduino capturar a digital e comparar
    APENAS com o modelo carregado no buffer.
    """
    resp = send_cmd("SCAN_AND_COMPARE", expect_json=True)
    return resp


# ------------------------ LIMPA TEMPORÁRIOS --------------------

def clear_temp_models():
    return send_cmd("CLEAR_TEMP_MODELS")


# =====================================================================
#  ======================= ENDPOINTS HTTP =============================
# =====================================================================

@app.route("/extract/<int:model_id>", methods=["POST"])
def http_extract(model_id):
    hex_data = extract_template(model_id)
    return jsonify({"id": model_id, "template_hex": hex_data})


@app.route("/upload_template", methods=["POST"])
def http_upload_template():
    data = request.json
    model_id = data.get("id")
    hex_blob = data.get("hex")

    load_template(model_id, hex_blob)

    return jsonify({"status": "ok"})


@app.route("/scan", methods=["POST"])
def http_scan():
    """
    Chama SCAN_AND_COMPARE no Arduino.
    """
    result = scan_and_compare()
    return jsonify(result)


@app.route("/clear_temp", methods=["POST"])
def http_clear_temp():
    clear_temp_models()
    return jsonify({"status": "cleared"})


# =====================================================================
#  Thread para ler mensagens normais do Arduino (match_found etc)
# =====================================================================

def serial_listener():
    global ser
    while True:
        if ser and ser.in_waiting:
            try:
                line = ser.readline().decode().strip()
                if not line:
                    continue
                print("[Arduino Event]", line)

                # Eventos normais de acesso
                if "match_found" in line:
                    try:
                        data = json.loads(line)
                        requests.post(LOG_ACCESS_URL, json=data, timeout=HTTP_TIMEOUT)
                    except:
                        pass

            except Exception as e:
                print("[Listener] Erro:", e)
        time.sleep(0.1)


# =====================================================================
#  MAIN
# =====================================================================

def main():
    print("Iniciando bridge verso A...")
    open_serial()

    # Thread para escutar Arduino
    threading.Thread(target=serial_listener, daemon=True).start()

    print(">>> Bridge rodando na porta", BRIDGE_PORT)

    app.run(host="0.0.0.0", port=BRIDGE_PORT, debug=False)


if __name__ == "__main__":
    main()
