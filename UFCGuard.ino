/*
  Arduino Mega 2560 sketch
  - LCD I2C 16x2 (address 0x27)
  - Sensor biométrico (DY50 / compatível) via SoftwareSerial
  - Extrai template (binário), converte pra Base64 e envia JSON via Serial USB
  - Mostra mensagens no LCD durante o processo

  Observação: a função getFingerprintTemplate() usa internals do sensor.
  Dependendo da versão da biblioteca Adafruit_Fingerprint você pode precisar
  adaptar essa função para usar os comandos de download do módulo.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// ========== Configs hardware ==========
LiquidCrystal_I2C lcd(0x27, 20, 4); // Ajuste endereço se necessário

// Pinos para comunicação com o sensor (SoftwareSerial)
#define FINGER_RX_PIN 52 
#define FINGER_TX_PIN 53 
SoftwareSerial fingerSerial(FINGER_RX_PIN, FINGER_TX_PIN);

// Instância da lib Adafruit
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// Buffer máximo esperado para o template
#define TEMPLATE_MAX_SIZE 1024
uint8_t templateBuf[TEMPLATE_MAX_SIZE];
uint16_t templateLen = 0;

// Timeout em ms para aguardar resposta do servidor
#define SERVER_RESPONSE_TIMEOUT 5000UL

// ========== UTIL: Base64 ==========
static const char B64_TBL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t *data, size_t len) {
  String out;
  if (!data || len == 0) return out;
  out.reserve(((len + 2) / 3) * 4);

  size_t i = 0;
  // full 3-byte chunks
  for (; i + 3 <= len; i += 3) {
    uint32_t triple = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out += B64_TBL[(triple >> 18) & 0x3F];
    out += B64_TBL[(triple >> 12) & 0x3F];
    out += B64_TBL[(triple >> 6) & 0x3F];
    out += B64_TBL[triple & 0x3F];
  }

  // remainder
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t triple = ((uint32_t)data[i] << 16);
    out += B64_TBL[(triple >> 18) & 0x3F];
    out += B64_TBL[(triple >> 12) & 0x3F];
    out += '=';
    out += '=';
  } else if (rem == 2) {
    uint32_t triple = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out += B64_TBL[(triple >> 18) & 0x3F];
    out += B64_TBL[(triple >> 12) & 0x3F];
    out += B64_TBL[(triple >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

// ========== Protocolo do sensor (pacotes) via serial direta ==========
// Implementação mínima do comando UpChar (upload de CharBuffer para host) usando fingerSerial.
// Isso evita depender de APIs internas/privadas da Adafruit_Fingerprint.

// Constantes do protocolo ZFM/R3xx (DY-50 compatível)
#define FP_STARTCODE 0xEF01
#define FP_ADDR      0xFFFFFFFFUL
#define FP_PACKET_COMMAND  0x01
#define FP_PACKET_DATA     0x02
#define FP_PACKET_ACK      0x07
#define FP_PACKET_DATA_END 0x08

#define FP_CMD_UPCHAR 0x08

// Helpers de escrita (big-endian)
static inline void fp_write16(uint16_t v) {
  fingerSerial.write((uint8_t)(v >> 8));
  fingerSerial.write((uint8_t)(v & 0xFF));
}

static inline void fp_write32(uint32_t v) {
  fingerSerial.write((uint8_t)(v >> 24));
  fingerSerial.write((uint8_t)((v >> 16) & 0xFF));
  fingerSerial.write((uint8_t)((v >> 8) & 0xFF));
  fingerSerial.write((uint8_t)(v & 0xFF));
}

// Envia pacote de comando com payload bytes[plen]
void fp_send_command(const uint8_t *payload, uint16_t plen) {
  uint16_t length = plen + 2; // inclui checksum
  uint16_t checksum = FP_PACKET_COMMAND + (length >> 8) + (length & 0xFF);
  for (uint16_t i = 0; i < plen; i++) checksum += payload[i];

  // Header
  fp_write16(FP_STARTCODE);
  fp_write32(FP_ADDR);
  fingerSerial.write((uint8_t)FP_PACKET_COMMAND);
  fp_write16(length);
  // Payload
  fingerSerial.write(payload, plen);
  // Checksum
  fp_write16(checksum);
}

// Le um pacote completo; retorna tipo do pacote (FP_PACKET_ACK/DATA/DATA_END) ou 0 em erro/timeout
uint8_t fp_read_packet(uint8_t *buf, uint16_t bufsize, uint16_t &outLen, unsigned long timeoutMs = 500) {
  unsigned long start = millis();

  auto timedRead = [&](int &outByte) -> bool {
    while (millis() - start < timeoutMs) {
      if (fingerSerial.available()) { outByte = fingerSerial.read(); return true; }
    }
    return false;
  };

  int b;
  // Startcode 0xEF01
  if (!timedRead(b)) return 0; uint16_t sc = (uint16_t)b << 8;
  if (!timedRead(b)) return 0; sc |= (uint16_t)b;
  if (sc != FP_STARTCODE) return 0;
  // Address (4 bytes) - ignoramos o valor
  for (int i = 0; i < 4; i++) { if (!timedRead(b)) return 0; }
  // Tipo de pacote
  if (!timedRead(b)) return 0; uint8_t ptype = (uint8_t)b;
  // Length (2 bytes)
  if (!timedRead(b)) return 0; uint16_t length = (uint16_t)b << 8;
  if (!timedRead(b)) return 0; length |= (uint16_t)b;

  // Dados: length inclui checksum (2 bytes)
  if (length < 2) return 0;
  uint16_t dataLen = length - 2;
  outLen = (dataLen < bufsize) ? dataLen : bufsize;
  for (uint16_t i = 0; i < dataLen; i++) {
    if (!timedRead(b)) return 0;
    if (i < bufsize) buf[i] = (uint8_t)b;
  }
  // checksum (2 bytes) - podemos ler e ignorar
  if (!timedRead(b)) return 0; if (!timedRead(b)) return 0;

  return ptype;
}

// Executa UpChar do buffer selecionado e lê todos os pacotes DATA para o outBuf
bool fp_upload_char(uint8_t bufferNum, uint8_t *outBuf, uint16_t &outLen) {
  // Comando payload: [CMD, bufferNum]
  uint8_t payload[2] = { FP_CMD_UPCHAR, bufferNum };
  fp_send_command(payload, 2);

  // Espera ACK
  uint8_t rbuf[64]; uint16_t rlen = 0;
  uint8_t ptype = fp_read_packet(rbuf, sizeof(rbuf), rlen, 1000);
  if (ptype != FP_PACKET_ACK) return false;
  if (rlen < 1 || rbuf[0] != 0x00) return false; // 0x00 = FINGERPRINT_OK

  // Lê DATA até ENDDATA
  outLen = 0;
  while (true) {
    ptype = fp_read_packet(outBuf + outLen, TEMPLATE_MAX_SIZE - outLen, rlen, 1000);
    if (ptype == 0) return false; // erro/timeout
    if (ptype == FP_PACKET_DATA || ptype == FP_PACKET_DATA_END) {
      outLen += rlen; // já limitado pelo bufsize passado
      if (ptype == FP_PACKET_DATA_END) break;
    } else if (ptype == FP_PACKET_ACK) {
      // erro inesperado
      return false;
    } else {
      return false;
    }
    if (outLen >= TEMPLATE_MAX_SIZE) break;
  }
  return (outLen > 0);
}

// ========== Funções de UI no LCD ==========
void showWelcome() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Ola, Insira sua");
  lcd.setCursor(0,1);
  lcd.print("digital :)");
}

void showReading() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Lendo digital...");
  lcd.setCursor(0,1);
  lcd.print("Aguarde");
}

void showCheckingDB() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Aguarde enquanto");
  lcd.setCursor(0,1);
  lcd.print("checamos na base");
}

void showSuccess() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Acesso liberado!");
}

void showFail() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Acesso negado!");
}

void showReadError() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Falha leitura!");
  lcd.setCursor(0,1);
  lcd.print("Tente novamente");
}

// ========== Função: pegar ID simples (para detecção inicial) ==========
int getFingerprintIDez() {
  // versão simples para detectar se há uma impressão cadastrada no sensor
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -2;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -2;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -2;
  // em caso de sucesso, finger.fingerID e finger.confidence estão preenchidos
  return finger.fingerID;
}

/*
  ========== Função crítica: extrair template do sensor ==========
*/
bool getFingerprintTemplate(uint8_t bufferNum, uint8_t *outBuf, uint16_t &outLen) {
  // Captura imagem e converte em características no CharBuffer (1 ou 2)
  if (finger.getImage() != FINGERPRINT_OK) {
    return false;
  }
  if (finger.image2Tz(bufferNum) != FINGERPRINT_OK) {
    return false;
  }

  // Faz upload (UpChar) do CharBuffer 'bufferNum' utilizando fingerSerial diretamente
  return fp_upload_char(bufferNum, outBuf, outLen);
}

// Envia o JSON com template base64 via Serial USB 
void sendTemplateJson(const String &b64, uint16_t rawLen) {
  // Saída JSON compacta (uma linha)
  // {"template_b64":"...","len":123}
  Serial.print("{\"template_b64\":\"");
  Serial.print(b64);
  Serial.print("\",\"len\":");
  Serial.print(rawLen);
  Serial.println("}");
}

// Aguarda resposta do servidor via Serial USB (simples)
String waitForServerResponse(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (Serial.available()) {
      String resp = Serial.readStringUntil('\n');
      resp.trim();
      if (resp.length() > 0) return resp;
    }
  }
  return String("");
}

// ========== setup & loop ==========
void setup() {
  // Serial com a CPU (USB)
  Serial.begin(115200);
  delay(100);

  // Inicializa LCD
  lcd.init();
  lcd.backlight();

  // Inicializa sensor biométrico
  fingerSerial.begin(57600);
  delay(100);
  finger.begin(57600);

  // Verifica conexão com sensor
  if (finger.verifyPassword()) {
    Serial.println("Sensor biometrico conectado.");
  } else {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Falha sensor!");
    lcd.setCursor(0,1);
    lcd.print("Verifique conexao");
    Serial.println("ERRO: sensor nao respondeu (verifyPassword).");
    while (1) delay(1000);
  }

  showWelcome();
}

void loop() {
  // Espera por uma impressão
  int id = getFingerprintIDez();
  if (id == -2) {
    // nenhuma leitura no momento - continuar
    delay(200);
    return;
  }

  // Se chegou aqui, detectou impressão (pode ter id se sensor encontrou match local)
  showReading();
  delay(200);

  // TENTAR extrair o template do caract buffer 
  templateLen = 0;
  bool ok = getFingerprintTemplate(1, templateBuf, templateLen);

  if (!ok || templateLen == 0) {
    // Não conseguiu extrair o template
    showReadError();
    Serial.println("{\"error\":\"read_template_failed\"}");
    delay(2000);
    showWelcome();
    return;
  }

  // Convertendo para base64
  String b64 = base64_encode(templateBuf, templateLen);

  // Mostrar que estamos checando no LCD
  showCheckingDB();

  // Envia JSON via Serial USB
  sendTemplateJson(b64, templateLen);

  // Opcional: aguarda resposta do servidor (OK/FAIL) para exibir no LCD
  String resp = waitForServerResponse(SERVER_RESPONSE_TIMEOUT);
  if (resp.length() == 0) {
    // sem resposta do servidor
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Sem resposta do");
    lcd.setCursor(0,1);
    lcd.print("servidor!");
    Serial.println("{\"info\":\"no_server_response\"}");
    delay(2000);
    showWelcome();
    return;
  } else {
    // Interpretar respostas simples: "OK" ou "FAIL"
    if (resp == "OK") {
      showSuccess();
    } else if (resp == "FAIL") {
      showFail();
    } else {
      // Pode ser JSON; apenas mostrar mensagem curta
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Resposta:");
      lcd.setCursor(0,1);
      if (resp.length() > 16) {
        lcd.print(resp.substring(0,16));
      } else {
        lcd.print(resp);
      }
    }
    // log da resposta
    Serial.print("{\"server_response\":\"");
    Serial.print(resp);
    Serial.println("\"}");
    delay(2500);
    showWelcome();
  }

  // Pequeno delay para evitar dupla leitura
  delay(500);
}
