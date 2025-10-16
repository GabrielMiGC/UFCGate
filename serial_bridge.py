import json, sys, time, logging
import requests
import serial

# Configurações fixas (sem .env)
SERIAL_PORT = 'COM5'
SERIAL_BAUD = 9600

# URLs do backend (fixas; ajuste aqui se necessário)
VERIFY_URL = 'http://127.0.0.1:8000/api/verify/'
REGISTER_URL = 'http://127.0.0.1:8000/api/register/'
CAPTURE_URL = 'http://127.0.0.1:8000/api/capture/submit/'

# Timeouts e comportamento
READ_TIMEOUT = 1.0
HTTP_TIMEOUT = 5.0
RECONNECT_DELAY = 2.0
DUMP_JSON_LINES = False

# Logging simples
fmt = logging.Formatter('%(asctime)s [%(levelname)s] %(message)s')
sh = logging.StreamHandler(sys.stdout)
sh.setFormatter(fmt)
logging.basicConfig(level=logging.INFO, handlers=[sh])
log = logging.getLogger('bridge')


def detect_port():
    """Retorna sempre a porta fixa configurada."""
    return SERIAL_PORT


def open_serial(port):
    try:
        ser = serial.serial_for_url(port, baudrate=SERIAL_BAUD, timeout=READ_TIMEOUT)
        time.sleep(2)
        return ser
    except Exception as e:
        log.error(f"Erro ao abrir serial '{port}': {e}")
        return None


def handle_message(msg, ser):
    """Trata mensagens JSON vindas do Arduino."""
    if 'template_b64' in msg:
        if DUMP_JSON_LINES:
            log.debug(f"JSON from Arduino: {msg}")
        try:
            r = requests.post(CAPTURE_URL, json={'template_b64': msg['template_b64']}, timeout=HTTP_TIMEOUT)
            log.debug(f"capture_submit status={r.status_code}")
        except Exception:
            log.exception('capture_submit failed')
        try:
            resp = requests.post(VERIFY_URL, json={'template_b64': msg['template_b64']}, timeout=HTTP_TIMEOUT)
            data = resp.json() if resp.content else {}
            if resp.ok and data.get('match'):
                ser.write(b'OK\n')
                log.info("[+] Digital reconhecida pelo backend.")
            else:
                ser.write(b'FAIL\n')
                log.info("[-] Digital não reconhecida.")
        except Exception as e:
            log.exception('HTTP error (verify)')
            ser.write(b'FAIL\n')
        return

    if msg.get('action') == 'register':
        payload = {
            'nome': msg.get('nome'),
            'codigo': msg.get('codigo'),
            'tipo_usuario': msg.get('tipo_usuario', 'aluno'),
            'template_b64': msg.get('template_b64'),
            'dedo': msg.get('dedo', 1)
        }
        try:
            resp = requests.post(REGISTER_URL, json=payload, timeout=HTTP_TIMEOUT)
            ser.write((("OK" if resp.ok else 'FAIL') + '\n').encode())
        except Exception as e:
            log.exception('HTTP error (register)')
            ser.write(b'FAIL\n')


def main_loop():
    log.info(f"Startup: SERIAL_PORT={SERIAL_PORT}, SERIAL_BAUD={SERIAL_BAUD}, VERIFY_URL={VERIFY_URL}")
    port = detect_port()
    if not port:
        log.warning('Nenhuma porta serial encontrada. Reconecte o Arduino.')
        return None

    log.info(f"Conectando na porta {port} @ {SERIAL_BAUD} baud...")
    ser = open_serial(port)
    if not ser:
        return None

    log.info(f"Bridge ativa: port={port}, baud={SERIAL_BAUD}")
    log.info("Aguardando mensagens do Arduino...")

    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue

            # Tenta interpretar como JSON
            try:
                msg = json.loads(line)
                handle_message(msg, ser)
            except json.JSONDecodeError:
                log.debug(f"[SERIAL RAW] {line}")
                continue
        except serial.SerialException as e:
            log.exception('Erro serial')
            break
        except Exception as e:
            log.exception('Erro inesperado')
            continue

    try:
        ser.close()
    except Exception:
        pass
    return None


if __name__ == '__main__':
    try:
        while True:
            res = main_loop()
            time.sleep(RECONNECT_DELAY)
    except KeyboardInterrupt:
        sys.exit(0)
