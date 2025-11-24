// UFCGuard - Batch Upload Manual (best-effort)
// Recebe templates em HEX via serial e tenta fazer upload raw para sensor,
// grava com storeModel(slot) e roda fingerSearch() para identificação.
// Use com cautela. Ajuste TEMP_SLOT_RANGE e CHUNK_MAX_BYTES conforme seu sensor.

#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

SoftwareSerial mySerial(52, 53); // RX, TX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 20, 4);

#define BRIDGE Serial

String incomingSerialData;
bool receivingTemplate = false;
String templateBufferHex = "";
int currentBatchSlot = -1;

// Buffer statico para evitar VLA (ajuste tamanho conforme template)
const int MAX_TEMPLATE_BYTES = 1024; // ajuste se seu template for maior
static uint8_t templateBytes[MAX_TEMPLATE_BYTES];
int templateByteLen = 0;

// Configurações de slot temporário válidas
const int TEMP_SLOT_MIN = 1;   // usar faixa segura, ajuste conforme sua DB
const int TEMP_SLOT_MAX = 160; // não ultrapassar a capacidade do sensor

unsigned long t_start = 0;
unsigned long t_end = 0;

void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

// converte 2 chars hex -> byte
uint8_t hexToByte(char hi, char lo) {
  uint8_t h = (hi <= '9') ? (hi - '0') : (toupper(hi) - 'A' + 10);
  uint8_t l = (lo <= '9') ? (lo - '0') : (toupper(lo) - 'A' + 10);
  return (h << 4) | l;
}

// ------- Monta e envia um pacote bruto para o sensor usando protocolo clássico -------
// header: 0xEF01, address: 0xFFFFFFFF, packetType, length (2 bytes), payload, checksum(2 bytes)
// packetType: 0x01 command, 0x02 data, 0x08 end data, 0x07 ack (as per many sensors)
// Use com cautela: se sua Adafruit lib intercepta os pacotes, pode ser necessário usar mySerial direto.
void sendPacketToSensor(uint8_t packetType, uint8_t* payload, uint16_t payloadLen) {
  uint16_t packetLen = payloadLen + 2; // length field = payload + checksum(2)
  uint16_t checksum = 0;
  // header
  mySerial.write((uint8_t)0xEF);
  mySerial.write((uint8_t)0x01);
  // address 4 bytes (default)
  mySerial.write((uint8_t)0xFF);
  mySerial.write((uint8_t)0xFF);
  mySerial.write((uint8_t)0xFF);
  mySerial.write((uint8_t)0xFF);
  // packet type
  mySerial.write(packetType);
  // length hi, lo
  mySerial.write((uint8_t)(packetLen >> 8));
  mySerial.write((uint8_t)(packetLen & 0xFF));
  // payload
  checksum = packetType + (packetLen >> 8) + (packetLen & 0xFF);
  for (uint16_t i=0;i<payloadLen;i++) {
    uint8_t b = payload[i];
    mySerial.write(b);
    checksum += b;
  }
  // checksum hi lo
  mySerial.write((uint8_t)(checksum >> 8));
  mySerial.write((uint8_t)(checksum & 0xFF));
  mySerial.flush();
}

// Le um pacote de resposta do sensor (simplificado; bloqueante com timeout)
bool readPacketFromSensor(uint8_t &packetTypeOut, uint8_t *respBuf, uint16_t &respLen, unsigned long timeout=1000) {
  unsigned long start = millis();
  // read header 0xEF01
  while (millis() - start < timeout) {
    if (mySerial.available() >= 2) {
      if (mySerial.read() == 0xEF && mySerial.read() == 0x01) {
        // read address
        while (mySerial.available() < 4) { if (millis() - start > timeout) return false; }
        mySerial.read(); mySerial.read(); mySerial.read(); mySerial.read();
        // packet type
        while (mySerial.available() < 1) { if (millis() - start > timeout) return false; }
        packetTypeOut = mySerial.read();
        // length
        while (mySerial.available() < 2) { if (millis() - start > timeout) return false; }
        uint16_t len = ((uint16_t)mySerial.read() << 8) | mySerial.read(); // includes checksum
        uint16_t payloadLen = len - 2;
        respLen = 0;
        while (mySerial.available() < payloadLen + 2) { if (millis() - start > timeout) return false; }
        for (uint16_t i=0;i<payloadLen;i++) {
          respBuf[i] = mySerial.read();
          respLen++;
        }
        // read checksum (2)
        uint8_t c1 = mySerial.read();
        uint8_t c2 = mySerial.read();
        return true;
      } else {
        // shift window
        // continue
      }
    }
  }
  return false;
}

// uploadTemplate: envia DATA packets, depois ENDDATA e espera ACK, por fim chama storeModel(slot)
bool uploadTemplateToSlot(int slot, uint8_t *data, int dataLen) {
  // chunk size (payload) por packet
  const int CHUNK = 128;
  int offset = 0;
  uint8_t respType;
  uint8_t respBuf[256];
  uint16_t respLen;

  // enviar comando "UpChar" (comando para iniciar upload) - comando code varia com o fabricante.
  // Muitos módulos usam o comando 0x05 (UPLOAD_CHAR) — mas o firmware do seu sensor pode diferir.
  // Vamos enviar um "command packet" com código 0x0A (placeholder) — **MUITO IMPORTANTE**: esse valor provavelmente precisa ser ajustado.
  // Se o sensor responder com ACK, continue. Caso contrário, ajuste o código de comando com base no datasheet do seu módulo.
  uint8_t cmdPayload[2];
  cmdPayload[0] = 0x0A; // *** ATENÇÃO: placeholder do comando UPLOAD. Ajuste conforme seu sensor! ***
  cmdPayload[1] = 0x00;
  sendPacketToSensor(0x01, cmdPayload, 2);

  // lê ACK do sensor
  if (!readPacketFromSensor(respType, respBuf, respLen, 500)) {
    BRIDGE.println("{\"event\":\"upload_error\",\"msg\":\"no_ack_start\"}");
    return false;
  }
  // Agora envia os DATA packets
  while (offset < dataLen) {
    int thisLen = min(CHUNK, dataLen - offset);
    sendPacketToSensor(0x02, data + offset, thisLen); // 0x02 = data packet
    // opcional: ler ACK para cada data
    if (!readPacketFromSensor(respType, respBuf, respLen, 500)) {
      BRIDGE.println("{\"event\":\"upload_error\",\"msg\":\"no_ack_data\"}");
      return false;
    }
    offset += thisLen;
    delay(5);
  }
  // enviar ENDDATA packet (0x08)
  sendPacketToSensor(0x08, NULL, 0);
  if (!readPacketFromSensor(respType, respBuf, respLen, 500)) {
    BRIDGE.println("{\"event\":\"upload_error\",\"msg\":\"no_ack_end\"}");
    return false;
  }

  // Por fim, armazenar o char buffer (dependendo do sensor, talvez precise chamar storeModel)
  uint8_t r = finger.storeModel(slot);
  if (r != FINGERPRINT_OK) {
    BRIDGE.print("{\"event\":\"upload_failed_store\",\"slot\":");
    BRIDGE.print(slot);
    BRIDGE.println("}");
    return false;
  }
  return true;
}

// Uso dos comandos recebidos via Serial (bridge)
void processSerialCmd(String line) {
  line.trim();
  if (line.startsWith("BEGIN_BATCH")) {
    currentBatchSlot = TEMP_SLOT_MIN;
    BRIDGE.println("{\"event\":\"batch_begin\"}");
  }
  else if (line.startsWith("TEMPLATE_SLOT:")) {
    currentBatchSlot = line.substring(14).toInt();
    templateByteLen = 0;
    receivingTemplate = true;
    templateBufferHex = "";
    BRIDGE.print("{\"event\":\"slot_set\",\"slot\":");
    BRIDGE.print(currentBatchSlot);
    BRIDGE.println("}");
  }
  else if (line.startsWith("TEMPLATE_DATA:")) {
    String chunk = line.substring(14);
    // append chunk to templateBufferHex but be careful with memory
    templateBufferHex += chunk;
    BRIDGE.print("{\"event\":\"chunk_len\",\"len\":");
    BRIDGE.print(chunk.length());
    BRIDGE.println("}");
  }
  else if (line.startsWith("TEMPLATE_END")) {
    // convert HEX to bytes into templateBytes
    int hexLen = templateBufferHex.length();
    templateByteLen = hexLen / 2;
    if (templateByteLen > MAX_TEMPLATE_BYTES) {
      BRIDGE.println("{\"event\":\"error\",\"msg\":\"template_too_big\"}");
      templateBufferHex = "";
      receivingTemplate = false;
      return;
    }
    for (int i=0;i<templateByteLen;i++) {
      templateBytes[i] = hexToByte(templateBufferHex.charAt(2*i), templateBufferHex.charAt(2*i+1));
    }
    // upload to sensor
    BRIDGE.print("{\"event\":\"upload_start\",\"slot\":");
    BRIDGE.print(currentBatchSlot);
    BRIDGE.println("}");
    bool ok = uploadTemplateToSlot(currentBatchSlot, templateBytes, templateByteLen);
    if (ok) {
      BRIDGE.print("{\"event\":\"upload_ok\",\"slot\":");
      BRIDGE.print(currentBatchSlot);
      BRIDGE.println("}");
    } else {
      BRIDGE.print("{\"event\":\"upload_failed\",\"slot\":");
      BRIDGE.print(currentBatchSlot);
      BRIDGE.println("}");
    }
    templateBufferHex = "";
    receivingTemplate = false;
  }
  else if (line.startsWith("RUN_BATCH_MATCH")) {
    // captura e busca
    uint8_t p = finger.getImage();
    if (p != FINGERPRINT_OK) {
      BRIDGE.println("{\"event\":\"batch_match_result\",\"match\":false,\"msg\":\"no_image\"}");
      return;
    }
    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
      BRIDGE.println("{\"event\":\"batch_match_result\",\"match\":false,\"msg\":\"tz1_fail\"}");
      return;
    }
    t_start = millis();
    p = finger.fingerSearch();
    t_end = millis();
    if (p == FINGERPRINT_OK) {
      BRIDGE.print("{\"event\":\"batch_match_result\",\"match\":true,\"id\":");
      BRIDGE.print(finger.fingerID);
      BRIDGE.print(",\"confidence\":");
      BRIDGE.print(finger.confidence);
      BRIDGE.print(",\"elapsed_ms\":");
      BRIDGE.print(t_end - t_start);
      BRIDGE.println("}");
    } else {
      BRIDGE.print("{\"event\":\"batch_match_result\",\"match\":false,\"elapsed_ms\":");
      BRIDGE.print(t_end - t_start);
      BRIDGE.println("}");
    }
  }
  else if (line.startsWith("CLEAR_BATCH")) {
    for (int i=TEMP_SLOT_MIN;i<=TEMP_SLOT_MAX;i++) {
      finger.deleteModel(i);
    }
    BRIDGE.println("{\"event\":\"batch_cleared\"}");
  }
  else if (line.startsWith("DELETE_ALL")) {
    uint8_t r = finger.emptyDatabase();
    if (r == FINGERPRINT_OK) BRIDGE.println("{\"event\":\"delete_all_ok\"}");
    else BRIDGE.println("{\"event\":\"delete_all_failed\"}");
  }
}

// Serial read loop
void checkBridgeSerial() {
  while (BRIDGE.available()) {
    String line = BRIDGE.readStringUntil('\n');
    processSerialCmd(line);
  }
}

void setup() {
  BRIDGE.begin(9600);
  while (!BRIDGE) ;
  lcd.init(); lcd.backlight();
  lcdMsg("Iniciando...", "Batch-Manual");

  mySerial.begin(57600);
  finger.begin(57600);
  delay(200);

  if (!finger.verifyPassword()) {
    BRIDGE.println("{\"event\":\"sensor_fail\"}");
    lcdMsg("ERRO sensor","checar conex.");
    while (1) delay(10);
  }
  finger.getTemplateCount();
  BRIDGE.print("[INFO] templates: "); BRIDGE.println(finger.templateCount);
}

void loop() {
  checkBridgeSerial();
}
