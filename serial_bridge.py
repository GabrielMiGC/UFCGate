import json, os, sys, time
import requests
import serial
from serial.tools import list_ports

def _env_str(name, default=""):
    val = os.getenv(name, default)
    if isinstance(val, str):
        # strip surrounding quotes if present (e.g., 'COM5')
        val = val.strip().strip("'\"")
    return val

# Environment-driven configuration with sensible defaults
SERIAL_PORT = _env_str('SERIAL_PORT', 'auto')  # 'auto', 'COM5', '/dev/ttyUSB0', or rfc2217://host:port
# Support both SERIAL_BAUD and SERIAL_BAUDRATE env names
_baud = _env_str('SERIAL_BAUD') or _env_str('SERIAL_BAUDRATE') or '115200'
try:
    SERIAL_BAUD = int(_baud)
except Exception:
    SERIAL_BAUD = 115200
VERIFY_URL = os.getenv('VERIFY_URL', 'http://127.0.0.1:8000/api/verify/')
REGISTER_URL = os.getenv('REGISTER_URL', 'http://127.0.0.1:8000/api/register/')
READ_TIMEOUT = float(os.getenv('READ_TIMEOUT', '1'))  # seconds
HTTP_TIMEOUT = float(os.getenv('HTTP_TIMEOUT', '5'))  # seconds
RECONNECT_DELAY = float(os.getenv('RECONNECT_DELAY', '2'))  # seconds
CAPTURE_URL = os.getenv('CAPTURE_URL', 'http://127.0.0.1:8000/api/capture/submit/')


def detect_port():
    """Try to auto-detect an Arduino-like serial port.
    Returns a serial URL/port string or None.
    """
    # If user provided an explicit value, use it
    if SERIAL_PORT and SERIAL_PORT != 'auto':
        return SERIAL_PORT

    # Try common patterns
    candidates = []
    try:
        ports = list(list_ports.comports())
        for p in ports:
            candidates.append(p.device)
            # Prefer devices with Arduino/Fingerprint hints
            if any(hint in (p.description or '').lower() for hint in ['arduino', 'usb serial', 'ch340', 'cp210', 'ftdi']):
                return p.device
        # Fallback to first enumerated
        if candidates:
            return candidates[0]
    except Exception:
        pass

    # Last-resort platform guesses
    if os.name == 'nt':
        # common COM range
        return 'COM5'  # user can override
    else:
        for guess in ['/dev/ttyUSB0', '/dev/ttyACM0']:
            if os.path.exists(guess):
                return guess
    return None


def open_serial(port):
    """Open serial using pyserial. Supports RFC2217 URLs (serial over TCP)."""
    # pyserial can open URL forms like rfc2217://host:port
    try:
        ser = serial.serial_for_url(port, baudrate=SERIAL_BAUD, timeout=READ_TIMEOUT)
        # give MCU reset time on DTR
        time.sleep(2)
        return ser
    except Exception as e:
        print(f"Failed to open serial '{port}': {e}")
        return None


def handle_message(msg, ser):
    # Verify flow: expects {"template_b64": "..."}
    if 'template_b64' in msg:
        try:
            # Mirror to capture endpoint for UI autofill
            try:
                requests.post(CAPTURE_URL, json={'template_b64': msg['template_b64']}, timeout=HTTP_TIMEOUT)
            except Exception:
                pass
            resp = requests.post(VERIFY_URL, json={'template_b64': msg['template_b64']}, timeout=HTTP_TIMEOUT)
            data = resp.json() if resp.content else {}
            if resp.ok and data.get('match'):
                ser.write(b'OK\n')
            else:
                ser.write(b'FAIL\n')
        except Exception as e:
            print('HTTP error (verify):', e)
            ser.write(b'FAIL\n')
        return

    # Registration flow over serial (optional)
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
            print('HTTP error (register):', e)
            ser.write(b'FAIL\n')


def main_loop():
    port = detect_port()
    if not port:
        print('No serial port found. Set SERIAL_PORT env var or connect the device. Retrying...')
        return None

    print(f"Opening serial on: {port} @ {SERIAL_BAUD}")
    ser = open_serial(port)
    if not ser:
        return None

    print(f"Bridge running: port={port}, baud={SERIAL_BAUD}, verify={VERIFY_URL}")
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                print('Non-JSON from Arduino:', line)
                continue
            handle_message(msg, ser)
        except serial.SerialException as e:
            print('Serial error:', e)
            break
        except Exception as e:
            print('Unexpected error:', e)
            # continue the loop, do not break
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