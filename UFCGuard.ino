// ========== Inclusões ==========
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
// Biblioteca LCD I2C (ajuste se usar outra)
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pinos RX/TX conectados ao módulo DY-50
SoftwareSerial mySerial(52, 53); // RX, TX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// LCD: endereço comum 0x27 ou 0x3F. Ajuste se necessário.
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Buffer para template
#define TEMPLATE_MAX_SIZE 1024
uint8_t templateBuf[TEMPLATE_MAX_SIZE];
uint16_t templateLen = 0;

unsigned long lastStatusTime = 0;
const unsigned long statusInterval = 5000; // 5 segundos

// ========== Base64 ==========
static const char B64_TBL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t *data, size_t len) {
  String out;
  if (!data || len == 0) return out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 3 <= len; i += 3) {
    uint32_t t = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out += B64_TBL[(t >> 18) & 0x3F];
    out += B64_TBL[(t >> 12) & 0x3F];
    out += B64_TBL[(t >> 6) & 0x3F];
    out += B64_TBL[t & 0x3F];
  }
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t t = ((uint32_t)data[i] << 16);
    out += B64_TBL[(t >> 18) & 0x3F];
    out += B64_TBL[(t >> 12) & 0x3F];
    out += '=';
    out += '=';
  } else if (rem == 2) {
    uint32_t t = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out += B64_TBL[(t >> 18) & 0x3F];
    out += B64_TBL[(t >> 12) & 0x3F];
    out += B64_TBL[(t >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

// ========== Protocolo DY-50: UpChar ==========
#define FP_STARTCODE 0xEF01
#define FP_ADDR      0xFFFFFFFFUL
#define FP_PACKET_COMMAND  0x01
#define FP_PACKET_DATA     0x02
#define FP_PACKET_ACK      0x07
#define FP_PACKET_DATA_END 0x08
#define FP_CMD_UPCHAR 0x08

static inline void fp_write16(uint16_t v) {
  mySerial.write((uint8_t)(v >> 8));
  mySerial.write((uint8_t)(v & 0xFF));
}

static inline void fp_write32(uint32_t v) {
  mySerial.write((uint8_t)(v >> 24));
  mySerial.write((uint8_t)((v >> 16) & 0xFF));
  mySerial.write((uint8_t)((v >> 8) & 0xFF));
  mySerial.write((uint8_t)(v & 0xFF));
}

void fp_send_command(const uint8_t *payload, uint16_t plen) {
  uint16_t length = plen + 2; // + checksum
  uint16_t checksum = FP_PACKET_COMMAND + (length >> 8) + (length & 0xFF);
  for (uint16_t i = 0; i < plen; i++) checksum += payload[i];
  fp_write16(FP_STARTCODE);
  fp_write32(FP_ADDR);
  mySerial.write((uint8_t)FP_PACKET_COMMAND);
  fp_write16(length);
  mySerial.write(payload, plen);
  fp_write16(checksum);
}

uint8_t fp_read_packet(uint8_t *buf, uint16_t bufsize, uint16_t &outLen, unsigned long timeoutMs = 1000) {
  unsigned long start = millis();
  auto timedRead = [&](int &outByte) -> bool {
    while (millis() - start < timeoutMs) {
      if (mySerial.available()) { outByte = mySerial.read(); return true; }
    }
    return false;
  };
  int b;
  if (!timedRead(b)) return 0; uint16_t sc = (uint16_t)b << 8;
  if (!timedRead(b)) return 0; sc |= (uint16_t)b; if (sc != FP_STARTCODE) return 0;
  for (int i = 0; i < 4; i++) { if (!timedRead(b)) return 0; }
  if (!timedRead(b)) return 0; uint8_t ptype = (uint8_t)b;
  if (!timedRead(b)) return 0; uint16_t length = (uint16_t)b << 8;
  if (!timedRead(b)) return 0; length |= (uint16_t)b;
  if (length < 2) return 0;
  uint16_t dataLen = length - 2;
  outLen = (dataLen < bufsize) ? dataLen : bufsize;
  for (uint16_t i = 0; i < dataLen; i++) {
    if (!timedRead(b)) return 0; if (i < bufsize) buf[i] = (uint8_t)b;
  }
  if (!timedRead(b)) return 0; if (!timedRead(b)) return 0; // checksum
  return ptype;
}

bool fp_upload_char(uint8_t bufferNum, uint8_t *outBuf, uint16_t &outLen) {
  uint8_t payload[2] = { FP_CMD_UPCHAR, bufferNum };
  fp_send_command(payload, 2);
  uint8_t rbuf[64]; uint16_t rlen = 0;
  uint8_t ptype = fp_read_packet(rbuf, sizeof(rbuf), rlen, 1000);
  if (ptype != FP_PACKET_ACK) return false;
  if (rlen < 1 || rbuf[0] != 0x00) return false; // OK
  outLen = 0;
  while (true) {
    ptype = fp_read_packet(outBuf + outLen, TEMPLATE_MAX_SIZE - outLen, rlen, 1000);
    if (ptype == 0) return false;
    if (ptype == FP_PACKET_DATA || ptype == FP_PACKET_DATA_END) {
      outLen += rlen;
      if (ptype == FP_PACKET_DATA_END) break;
    } else return false;
    if (outLen >= TEMPLATE_MAX_SIZE) break;
  }
  return (outLen > 0);
}

bool getFingerprintTemplate(uint8_t bufferNum, uint8_t *outBuf, uint16_t &outLen) {
  if (finger.getImage() != FINGERPRINT_OK) return false;
  if (finger.image2Tz(bufferNum) != FINGERPRINT_OK) return false;
  return fp_upload_char(bufferNum, outBuf, outLen);
}

void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  // LCD init
  lcd.init();
  lcd.backlight();
  lcdMsg("Iniciando...", "DY-50");
  Serial.println("[BOOT] UFCGuard DY50 inicializando...");

  mySerial.begin(57600);
  delay(200);
  finger.begin(57600);
  delay(200);
  if (finger.verifyPassword()) {
    Serial.println("[OK] Sensor biométrico pronto.");
    lcdMsg("Sensor pronto", "Aguardando");
  } else {
    Serial.println("[ERRO] DY-50 falhou");
    lcdMsg("ERRO sensor", "Checar conex.");
    while (true) { delay(1); }
  }
  Serial.println("Aguardando leitura de digital...");
}

void loop() {
  // Envia batimento de status
  if (millis() - lastStatusTime > statusInterval) {
    Serial.println("[STATUS] Ativo...");
    lcdMsg("Aguardando", "Coloque dedo");
    lastStatusTime = millis();
  }

  // Tenta capturar e enviar template
  templateLen = 0;
  if (!getFingerprintTemplate(1, templateBuf, templateLen)) {
    // sem dedo ou erro leve
    delay(150);
    return;
  }

  Serial.println("[INFO] Template obtido, convertendo...");
  lcdMsg("Digital lida", "Processando...");
  String b64 = base64_encode(templateBuf, templateLen);
  // JSON de uma linha para o bridge
  Serial.print("{\"template_b64\":\"");
  Serial.print(b64);
  Serial.print("\",\"len\":");
  Serial.print(templateLen);
  Serial.println("}");
  lcdMsg("Enviado", "Aguardando...");

  // pequeno atraso para evitar leituras duplicadas
  delay(800);
}
