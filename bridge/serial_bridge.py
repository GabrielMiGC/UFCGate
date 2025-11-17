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

# Objeto Serial global
ser = None
# Objeto Flask global
app = Flask(__name__)

# --- Lógica de Leitura do Arduino ---

def handle_arduino_message(line):
    """Processa mensagens JSON ou de STATUS recebidas DO Arduino."""
    
    if "[STATUS]" in line or "[BOOT]" in line or "[INFO]" in line or "[DEBUG]" in line:
        print(f"[Arduino] {line}")
        return

    try:
        msg = json.loads(line)
        
        if msg.get('event') == 'match_found':
            sensor_id = msg.get('sensor_id')
            confidence = msg.get('confidence')
            print(f"============================================================")
            print(f"[Bridge] SENSOR ENCONTROU MATCH! ID: {sensor_id}, Confiança: {confidence}")
            
            if not LOG_ACCESS_URL:
                print("[Bridge] ERRO: LOG_ACCESS_URL não definida no .env")
                return

            try:
                payload = {'sensor_id': sensor_id, 'confidence': confidence}
                r = requests.post(LOG_ACCESS_URL, json=payload, timeout=HTTP_TIMEOUT)
                r.raise_for_status()
                print(f"[Bridge] Servidor respondeu: {r.json()}")
                print(f"============================================================")
            except Exception as e:
                print(f"[Bridge] ERRO ao registrar acesso no Django: {e}")

        elif msg.get('event') == 'match_failed':
            print("[Bridge] Leitura falhou (Acesso Negado).")
        
        elif "status" in msg:
            print(f"[Arduino] {line}")
        
        else:
            print(f"[Arduino JSON] {line}")

    except json.JSONDecodeError:
        if line.strip():
            print(f"[Arduino TXT] {line}") 
    except Exception as e:
        print(f"[Bridge] Erro ao processar linha: {e}")

def read_from_port(ser_conn):
    """
    Thread 1: Ouve continuamente o que o Arduino envia.
    """
    print(f"[Bridge] Thread de LEITURA (Arduino -> PC) iniciada.")
    while True:
        try:
            if ser_conn.in_waiting > 0:
                line = ser_conn.readline().decode('utf-8').strip()
                if line:
                    handle_arduino_message(line)
        except Exception as e:
            print(f"[Bridge] Porta serial desconectada. Encerrando thread de leitura. {e}")
            break
        time.sleep(0.05)

# --- Servidor Web (Flask) para Receber Comandos do Django ---

@app.route("/command", methods=["POST"])
def handle_django_command():
    """
    Endpoint: Ouve por comandos vindos do Django (ex: do painel Admin).
    """
    global ser
    data = request.get_json()
    command = data.get('command')
    
    if not command:
        return jsonify({"error": "Comando ausente"}), 400
        
    if not ser or not ser.is_open:
        return jsonify({"error": "Porta serial não está pronta"}), 500

    try:
        # Envia o comando para o Arduino (ex: "ENROLL:5\n")
        ser.write(f"{command.upper().strip()}\n".encode('utf-8'))
        
        print(f"---------------------------------------------------------------------")
        print(f">>> [Bridge] Comando recebido do Django: {command.upper()}")
        print(f"---------------------------------------------------------------------")
        
        return jsonify({"status": "command_sent", "command": command}), 200
    except Exception as e:
        print(f"[Bridge] Erro ao escrever na serial: {e}")
        return jsonify({"error": f"Erro serial: {e}"}), 500

@app.route("/health", methods=["GET"])
def health_check():
    """Endpoint para o Django verificar se o bridge está vivo."""
    return jsonify({
        "status": "ok",
        "serial_port": SERIAL_PORT,
        "serial_open": ser.is_open if ser else False
    }), 200

# --- Função Principal ---

def main():
    """Inicia a conexão serial e o servidor Flask."""
    global ser
    if not LOG_ACCESS_URL:
        print("ERRO FATAL: LOG_ACCESS_URL não definida no .env")
        return

    try:
        ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=1)
        print(f"[Bridge] Conectado na porta {SERIAL_PORT} @ {SERIAL_BAUD} baud.")
    except serial.SerialException as e:
        print(f"ERRO FATAL: Não foi possível abrir a porta {SERIAL_PORT}. {e}")
        return

    print("[Bridge] Iniciando thread de leitura do Arduino...")
    read_thread = threading.Thread(target=read_from_port, args=(ser,), daemon=True)
    read_thread.start()

    print(f"[Bridge] Iniciando servidor Flask (para o Django) em http://0.0.0.0:{BRIDGE_PORT}")
    print(">>> O Bridge está pronto e operando. <<<")
    
    # Inicia o servidor Flask na thread principal
    # '0.0.0.0' permite que o contêiner Docker (Django) acesse o bridge
    try:
        app.run(host='0.0.0.0', port=BRIDGE_PORT, debug=False)
    except KeyboardInterrupt:
        print("\n[Bridge] Desligando (Ctrl+C pressionado)...")
    finally:
        if ser and ser.is_open:
            ser.close()
        print("[Bridge] Conexão serial fechada.")

if __name__ == "__main__":
    main()