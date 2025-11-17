/* 
  UFCGuard_Protocol.ino
  Implementação direta do protocolo do sensor DY50/AS608 (DownChar/UpChar/GenImg/Img2Tz/Match/Store/Load)
  Comunicação:
    - Serial  : USB -> PC (debug + transferência de templates)
    - Serial1 : UART -> Sensor (DY50) -> RX/TX do módulo

  Uso:
    1) Conecte o módulo ao Serial1 do Arduino (p.ex. MEGA: TX1/RX1).
    2) Abra o monitor serial a 115200 (USB).
    3) Quando o sketch iniciar ele vai:
        A) Capturar 5 templates (pedirá para por o dedo), enviar cada template ao PC.
        B) Salvar no módulo apenas o template #5 (posição 1).
        C) Depois entra em loop esperando o PC enviar templates de volta:
            - PC envia 4 bytes (uint32 LE) do tamanho N e depois N bytes (o template).
            - Arduino baixa pro módulo no buffer 2, chama Match e responde pro PC:
                * um JSON simples com { matched: true/false, id: X, confidence: Y, time_ms: Z }
    4) O PC fica responsável por salvar no Postgres e re-enviar os templates (na mesma ordem que tu quiser).
*/

#include <Arduino.h>

#define SENSOR Serial1
#define USB Serial

// Serial speeds
#define USB_BAUD 115200
#define SENSOR_BAUD 57600

// Packet constants
const uint8_t HEADER[2] = {0xEF, 0x01};
const uint8_t ADDRESS[4] = {0xFF, 0xFF, 0xFF, 0xFF};

enum PacketType {
  PACKET_COMMAND = 0x01,
  PACKET_DATA = 0x02,
  PACKET_ACK = 0x07
};

const unsigned long SENSOR_READ_TIMEOUT = 2000; // ms

// --- low-level packet helpers ---
void writeUInt16BE(uint16_t v) {
  SENSOR.write((v >> 8) & 0xFF);
  SENSOR.write(v & 0xFF);
}

uint16_t readUInt16BEFromBuffer(const uint8_t* buf, int offset) {
  return (uint16_t(buf[offset]) << 8) | uint16_t(buf[offset+1]);
}

uint32_t readUint32LEFromSerial(Stream &s) {
  while (s.available() < 4) {
    // wait
    delay(1);
  }
  uint8_t b0 = s.read();
  uint8_t b1 = s.read();
  uint8_t b2 = s.read();
  uint8_t b3 = s.read();
  return uint32_t(b0) | (uint32_t(b1) << 8) | (uint32_t(b2) << 16) | (uint32_t(b3) << 24);
}

void writeUint32LEToSerial(Stream &s, uint32_t v) {
  s.write(uint8_t(v & 0xFF));
  s.write(uint8_t((v >> 8) & 0xFF));
  s.write(uint8_t((v >> 16) & 0xFF));
  s.write(uint8_t((v >> 24) & 0xFF));
}

// Build and send a command packet (payload = pointer to bytes, len payload length)
void sendCommandPacket(const uint8_t* payload, uint16_t payloadLen) {
  // HEADER
  SENSOR.write(HEADER, 2);
  // ADDRESS
  SENSOR.write(ADDRESS, 4);
  // PID
  SENSOR.write(PACKET_COMMAND);
  // LENGTH = payloadLen + 2 (checksum bytes included)
  uint16_t length = payloadLen + 2;
  writeUInt16BE(length);
  // PAYLOAD
  uint16_t checksum = PACKET_COMMAND + (length >> 8) + (length & 0xFF);
  for (uint16_t i = 0; i < payloadLen; ++i) {
    SENSOR.write(payload[i]);
    checksum += payload[i];
  }
  // CHECKSUM (2 bytes BE)
  writeUInt16BE(checksum);
}

// Read one packet from sensor (returns payload bytes in vector-like malloc b, sets outLen, returns packet type, also sets ack code in outAck if available)
// Caller must free(*outBuf) when outLen>0
int readPacket(uint8_t** outBuf, uint16_t* outLen, uint8_t* outAck, unsigned long timeoutMs = SENSOR_READ_TIMEOUT) {
  *outBuf = nullptr;
  *outLen = 0;
  *outAck = 0xFF;
  unsigned long start = millis();
  // find header 0xEF01
  while (millis() - start < timeoutMs) {
    if (SENSOR.available() >= 2) {
      int h1 = SENSOR.read();
      if (h1 == 0xEF) {
        int h2 = SENSOR.read();
        if (h2 == 0x01) {
          // read address(4), pid(1), len(2)
          while (SENSOR.available() < 7 && (millis() - start < timeoutMs)) delay(1);
          if (SENSOR.available() < 7) return -1;
          uint8_t addr[4];
          for (int i=0;i<4;i++) addr[i] = SENSOR.read();
          uint8_t pid = SENSOR.read();
          uint8_t lenh = SENSOR.read();
          uint8_t lenl = SENSOR.read();
          uint16_t len = (uint16_t(lenh) << 8) | uint16_t(lenl);
          // len includes checksum bytes
          uint16_t payloadLen = len - 2;
          uint8_t* buf = (uint8_t*) malloc(payloadLen);
          unsigned long t0 = millis();
          uint16_t idx = 0;
          while (idx < payloadLen && (millis() - t0 < timeoutMs)) {
            if (SENSOR.available()) {
              buf[idx++] = SENSOR.read();
            } else {
              delay(1);
            }
          }
          if (idx < payloadLen) {
            free(buf);
            return -2;
          }
          // read checksum (2 bytes)
          while (SENSOR.available() < 2 && (millis() - t0 < timeoutMs)) delay(1);
          if (SENSOR.available() < 2) {
            free(buf);
            return -3;
          }
          uint8_t c1 = SENSOR.read();
          uint8_t c2 = SENSOR.read();
          uint16_t checksum = (uint16_t(c1) << 8) | uint16_t(c2);
          // we won't verify checksum strictly here for speed, but we could.
          *outBuf = buf;
          *outLen = payloadLen;
          *outAck = pid; // pid tells packet type (ACK/DATA...)
          return pid;
        }
      }
    } else {
      delay(1);
    }
  }
  return -4; // timeout
}

// --- High level commands (command codes are standard AS608) ---
// Each returns the ack code (the first byte of payload is the confirmation code).
// Confirmation codes: 0x00 = OK, 0x01 = error receiving packet, 0x02 = no finger, etc (see datasheet)
uint8_t genImg() {
  uint8_t cmd[1] = {0x01};
  sendCommandPacket(cmd, 1);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) {
    if (buf) free(buf);
    return 0xFF;
  }
  uint8_t confirmation = buf[0];
  free(buf);
  return confirmation;
}

uint8_t img2Tz(uint8_t slot) {
  // slot: 1 or 2
  uint8_t cmd[2] = {0x02, slot};
  sendCommandPacket(cmd, 2);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  uint8_t confirmation = buf[0];
  free(buf);
  return confirmation;
}

uint8_t regModel() {
  uint8_t cmd[1] = {0x05};
  sendCommandPacket(cmd, 1);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  uint8_t confirmation = buf[0];
  free(buf);
  return confirmation;
}

uint8_t storeModel(uint16_t id) {
  // id: 0..(n) -> two bytes high low
  uint8_t cmd[3];
  cmd[0] = 0x06;
  cmd[1] = (id >> 8) & 0xFF;
  cmd[2] = id & 0xFF;
  sendCommandPacket(cmd, 3);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  uint8_t confirmation = buf[0];
  free(buf);
  return confirmation;
}

uint8_t loadModel(uint16_t id) {
  uint8_t cmd[3];
  cmd[0] = 0x07;
  cmd[1] = (id >> 8) & 0xFF;
  cmd[2] = id & 0xFF;
  sendCommandPacket(cmd, 3);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  uint8_t confirmation = buf[0];
  free(buf);
  return confirmation;
}

uint8_t upChar(uint8_t slot, uint8_t** outTemplate, uint32_t* outLen) {
  // slot: 1 or 2
  uint8_t cmd[2] = {0x08, slot};
  sendCommandPacket(cmd, 2);
  // Now module will reply with ACK (payload starts with confirmation)
  uint8_t* ackBuf; uint16_t ackLen; uint8_t pid;
  int r = readPacket(&ackBuf, &ackLen, &pid);
  if (r <= 0) { if (ackBuf) free(ackBuf); return 0xFF; }
  uint8_t confirmation = ackBuf[0];
  free(ackBuf);
  if (confirmation != 0x00) return confirmation;

  // Now module will send data packets (PID=0x02) possibly in multiple data packets.
  // According to protocol, first data packet includes header of data section: data[0..1] = package length? We'll just collect consecutive DATA packets until we get an ACK packet with 0x07 again.
  // Simpler: read packets, append payloads while pid == PACKET_DATA
  uint8_t* accum = nullptr;
  uint32_t accumLen = 0;
  while (true) {
    uint8_t* dbuf; uint16_t dlen; uint8_t dpid;
    int rr = readPacket(&dbuf, &dlen, &dpid);
    if (rr <= 0) { if (dbuf) free(dbuf); if (accum) free(accum); return 0xFE; }
    if (dpid == PACKET_DATA) {
      // append dbuf (dlen bytes)
      uint8_t* newbuf = (uint8_t*) malloc(accumLen + dlen);
      if (accum) {
        memcpy(newbuf, accum, accumLen);
        free(accum);
      }
      memcpy(newbuf + accumLen, dbuf, dlen);
      accum = newbuf;
      accumLen += dlen;
      free(dbuf);
      // keep reading until next packet is ACK
      // loop
    } else if (dpid == PACKET_ACK) {
      // ack payload contains final confirmation code - but this ack likely indicates end of data transfer
      // dbuf[0] is confirmation code
      uint8_t finalConf = dbuf[0];
      free(dbuf);
      if (finalConf == 0x00) {
        // success -> return accumulated template
        *outTemplate = accum;
        *outLen = accumLen;
        return 0x00;
      } else {
        if (accum) free(accum);
        return finalConf;
      }
    } else {
      // unexpected
      free(dbuf);
      if (accum) free(accum);
      return 0xFD;
    }
  }
}

uint8_t downChar(uint8_t slot, const uint8_t* templateData, uint32_t templateLen) {
  // DownChar: we send command 0x09 slot, then send the template as DATA packets (PID=0x02), then wait for ACK
  uint8_t cmd[2] = {0x09, slot};
  sendCommandPacket(cmd, 2);

  // Wait for ack confirming module ready to receive data
  uint8_t* ackBuf; uint16_t ackLen; uint8_t pid;
  int r = readPacket(&ackBuf, &ackLen, &pid);
  if (r <= 0) { if (ackBuf) free(ackBuf); return 0xFF; }
  uint8_t confirmation = ackBuf[0];
  free(ackBuf);
  if (confirmation != 0x00) return confirmation;

  // Now we should send data packets with up to (typically) 128 bytes per DATA packet payload (datasheet uses 128)
  const uint16_t CHUNK = 128;
  uint32_t sent = 0;
  while (sent < templateLen) {
    uint16_t chunkSize = (templateLen - sent > CHUNK) ? CHUNK : (templateLen - sent);
    // Build data packet:
    // header, address, PID=0x02, length = chunkSize + 2, payload (chunk), checksum
    SENSOR.write(HEADER, 2);
    SENSOR.write(ADDRESS, 4);
    SENSOR.write(PACKET_DATA);
    uint16_t plen = chunkSize + 2;
    writeUInt16BE(plen);
    uint16_t checksum = PACKET_DATA + (plen >> 8) + (plen & 0xFF);
    for (uint16_t i=0;i<chunkSize;i++) {
      uint8_t b = templateData[sent + i];
      SENSOR.write(b);
      checksum += b;
    }
    writeUInt16BE(checksum);
    sent += chunkSize;
    delay(5); // tiny gap between chunks
  }

  // After all data packets, sensor should respond with an ACK packet with confirmation byte
  uint8_t* finalAck; uint16_t finalAckLen; uint8_t finalPid;
  int rr = readPacket(&finalAck, &finalAckLen, &finalPid, SENSOR_READ_TIMEOUT);
  if (rr <= 0) { if (finalAck) free(finalAck); return 0xFE; }
  uint8_t finalConf = finalAck[0];
  free(finalAck);
  return finalConf;
}

uint8_t match() {
  uint8_t cmd[1] = {0x03};
  sendCommandPacket(cmd, 1);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  // If success, buf[0] == 0x00 and next two bytes may contain matched ID (two bytes) and then confidence (two bytes)
  uint8_t confirmation = buf[0];
  if (confirmation == 0x00 && blen >= 5) {
    // typically: buf[1]=high id, buf[2]=low id, buf[3]=high conf, buf[4]=low conf
    // Keep these bytes available to caller by storing in global temporary if needed
  }
  // We'll copy buf to a small local store and return confirmation; caller should call readLastAckData() if needed
  // For simplicity, we copy values to globals:
  static uint8_t lastMatchData[6];
  uint8_t copyLen = (blen > 6) ? 6 : blen;
  for (uint8_t i=0;i<copyLen;i++) lastMatchData[i] = buf[i];
  // expose via USB when returning
  free(buf);
  return confirmation;
}

// Helper to parse match ack: read last ack by querying the module? For simplicity, we re-run readPacket earlier to get details.
// Instead, we'll implement a special matchWithResult() that returns confirmation and fills id and confidence.
uint8_t matchWithResult(uint16_t* outId, uint16_t* outConfidence) {
  uint8_t cmd[1] = {0x03};
  sendCommandPacket(cmd, 1);
  uint8_t* buf; uint16_t blen; uint8_t pid;
  int r = readPacket(&buf, &blen, &pid);
  if (r <= 0) { if (buf) free(buf); return 0xFF; }
  uint8_t confirmation = buf[0];
  if (confirmation == 0x00 && blen >= 5) {
    *outId = (uint16_t(buf[1]) << 8) | uint16_t(buf[2]);
    *outConfidence = (uint16_t(buf[3]) << 8) | uint16_t(buf[4]);
  } else {
    *outId = 0xFFFF;
    *outConfidence = 0;
  }
  free(buf);
  return confirmation;
}

// --- Utility: send template to USB (4 bytes len LE + bytes) ---
void sendTemplateToUSB(const uint8_t* data, uint32_t len) {
  writeUint32LEToSerial(USB, len);
  for (uint32_t i=0;i<len;i++) USB.write(data[i]);
  USB.flush();
}

// Receive template from USB (4 bytes len LE + len bytes) into malloc buffer. returns pointer (must free) and len.
uint8_t* receiveTemplateFromUSB(uint32_t* outLen, unsigned long timeoutMs = 60000) {
  unsigned long start = millis();
  while (USB.available() < 4 && (millis() - start < timeoutMs)) delay(1);
  if (USB.available() < 4) {
    *outLen = 0;
    return nullptr;
  }
  uint32_t len = readUint32LEFromSerial(USB);
  *outLen = len;
  uint8_t* buf = (uint8_t*) malloc(len);
  uint32_t got = 0;
  unsigned long t0 = millis();
  while (got < len && (millis() - t0 < timeoutMs)) {
    if (USB.available()) {
      buf[got++] = USB.read();
    } else delay(1);
  }
  if (got < len) {
    free(buf);
    *outLen = 0;
    return nullptr;
  }
  return buf;
}

// Helper to convert confirmation codes to human
String confToStr(uint8_t c) {
  switch (c) {
    case 0x00: return "OK";
    case 0x01: return "ERR_PACKET";
    case 0x02: return "NO_FINGER";
    case 0x03: return "IMAGE_FAIL";
    case 0x06: return "FEATURE_FAIL";
    case 0x07: return "NO_MATCH";
    default: {
      char tmp[10];
      sprintf(tmp, "0x%02X", c);
      return String(tmp);
    }
  }
}

// --- Main test flow requested pelo orientador ---
void captureFiveAndSendToPC() {
  USB.println(F("=== CAPTURING 5 TEMPLATES AND SENDING TO PC ==="));
  for (int i=1;i<=5;i++) {
    USB.print("Coloca o dedo para captura #"); USB.println(i);
    // wait for finger and success
    uint8_t res;
    // loop getImage until we get image or timeouts
    unsigned long tstart = millis();
    while (true) {
      res = genImg();
      if (res == 0x00) break; // OK image captured
      if (res == 0x02) {
        USB.println("Nenhum dedo detectado. Coloca o dedo...");
      } else {
        USB.print("genImg returned "); USB.println(confToStr(res));
        // keep trying
      }
      delay(500);
      if (millis() - tstart > 30000) {
        USB.println("Tempo esgotado na captura.");
        return;
      }
    }
    // convert
    uint8_t r2 = img2Tz(1);
    USB.print("image2Tz -> "); USB.println(confToStr(r2));
    if (r2 != 0x00) {
      USB.println("Falha ao gerar template. Recomeçar.");
      i--;
      continue;
    }
    // upload template (upChar slot 1)
    uint8_t* tpl; uint32_t tlen;
    uint8_t up = upChar(1, &tpl, &tlen);
    USB.print("upChar -> "); USB.println(confToStr(up));
    if (up == 0x00 && tpl != nullptr && tlen > 0) {
      USB.print("Enviando template #"); USB.print(i); USB.print(" ao PC, len=");
      USB.println(tlen);
      sendTemplateToUSB(tpl, tlen);
      free(tpl);
    } else {
      USB.println("Falha no upChar.");
      if (tpl) free(tpl);
      i--;
      continue;
    }
    // If last (5), store in module at ID = 1
    if (i == 5) {
      // We already have the template in buffer 1 (img2Tz(1) was feito) — store that
      uint8_t st = storeModel(1);
      USB.print("storeModel(1) -> "); USB.println(confToStr(st));
      if (st != 0x00) {
        USB.println("Falha ao salvar no módulo.");
      }
    }
    delay(500);
  }
  USB.println(F("=== CAPTURAS CONCLUÍDAS ==="));
}

// After PC saved those templates in DB, it should send them back one by one.
// Arduino will receive each template (4-byte len + bytes), downChar to slot 2, call match() and return result+time
void receiveTemplatesAndCompareLoop() {
  USB.println(F("Aguardando templates do PC. Envie templates (4 bytes length LE + bytes)."));
  while (true) {
    USB.println("Pronto pra receber próximo template...");
    uint32_t len;
    uint8_t* tpl = receiveTemplateFromUSB(&len, 120000); // espera até 2min por template
    if (!tpl) {
      USB.println("Nenhum template recebido (timeout). Saindo do loop.");
      break;
    }
    USB.print("Recebi template len="); USB.println(len);
    unsigned long t0 = millis();
    // downChar to slot 2
    uint8_t downRes = downChar(2, tpl, len);
    unsigned long tDownEnd = millis();
    USB.print("downChar -> "); USB.println(confToStr(downRes));
    if (downRes != 0x00) {
      USB.println("Falha no downChar. Enviando resposta de falha ao PC.");
      // send result JSON-like
      USB.print("{\"matched\":false,\"error\":\"downChar:");
      USB.print(downRes, HEX);
      USB.println("\"}");
      free(tpl);
      continue;
    }
    // Now call match between buffer1 and buffer2
    uint16_t matchedId; uint16_t confidence;
    unsigned long tMatchStart = millis();
    uint8_t matchRes = matchWithResult(&matchedId, &confidence);
    unsigned long tMatchEnd = millis();
    unsigned long totalTime = tMatchEnd - t0; // from start of downChar to match response
    bool ok = (matchRes == 0x00);
    // send result to PC
    USB.print("{\"matched\":");
    USB.print(ok ? "true" : "false");
    USB.print(",\"id\":");
    if (ok) USB.print(matchedId);
    else USB.print("null");
    USB.print(",\"confidence\":");
    if (ok) USB.print(confidence);
    else USB.print(0);
    USB.print(",\"time_ms\":");
    USB.print(totalTime);
    USB.println("}");
    free(tpl);
    if (ok) {
      USB.println("Match encontrado, parando o loop conforme pedido.");
      break;
    }
    // else continua com próximo template
  }
}

void setup() {
  USB.begin(USB_BAUD);
  SENSOR.begin(SENSOR_BAUD);
  delay(100);
  USB.println(F("UFCGuard Protocol - inicializando"));
  // small test: verify module presence by asking a LoadModel(1) or getTemplateCount
  // We'll just proceed with the flow requested.
  delay(2000);

  captureFiveAndSendToPC();
  receiveTemplatesAndCompareLoop();
  USB.println(F("Processo terminado. Reinicie se quiser rodar de novo."));
}

void loop() {
  // nada no loop - a lógica foi feita em setup() para seguir o fluxo do teste
  delay(1000);
}
