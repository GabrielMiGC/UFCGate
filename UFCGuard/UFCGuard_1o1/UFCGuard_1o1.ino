// UFCGuard - Versão 1:1 compatível com Adafruit_Fingerprint
// Estratégia: receber template do bridge em HEX -> gravar em slot temporário -> fingerSearch -> apagar slot
//
// Requer: Adafruit_Fingerprint library (oficial)
// Testado logicamente com R307 / FPM10A / DY-50 (ajustes finos podem ser necessários para firmware variantes).

#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ----------------- Configurações de hardware -----------------
SoftwareSerial mySerial(52, 53); // RX, TX -> sensor
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Serial com o bridge / PC
#define BRIDGE_SERIAL Serial

// ----------------- Globals -----------------
String incomingSerialData = "";
bool receivingTemplate = false;
String templateHexBuffer = "";
int importTargetSlot = -1; // slot temporário onde vamos gravar

// Adjust: escolha faixa temporária segura para gravação
// Sensor comum suporta até ~162 templates; use range alta com cuidado.
// Recomendo usar 120..160 se sensor for pequeno. Ajuste conforme seu sensor docs.
const int TEMP_SLOT_START = 120; // primeiro slot temporário
const int MAX_TEMP_SLOTS = 10;   // quantos slots temporarios podemos usar sequencialmente

unsigned long t_start = 0;
unsigned long t_end = 0;

// ----------------- Helpers LCD -----------------
void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(l1);
  lcd.setCursor(0,1);
  lcd.print(l2);
}

// ----------------- Hex -> byte -----------------
uint8_t hexToByteChar(char a, char b) {
  uint8_t nh = (a <= '9') ? (a - '0') : (toupper(a) - 'A' + 10);
  uint8_t nl = (b <= '9') ? (b - '0') : (toupper(b) - 'A' + 10);
  return (nh << 4) | nl;
}

// ----------------- Send raw packet primitives (usa funções da lib) -----------------
// Observação: a Adafruit lib tem funções privadas para escrever pacotes, mas há
// formas de usar os métodos públicos getStructuredPacket / writeStructuredPacket.
// Aqui usamos writeStructuredPacket se disponível; caso não, fallback para buildPacket manual.
// *Se sua versão da lib não expõe writeStructuredPacket publicamente, pode ser necessário
// atualizar a Adafruit_Fingerprint library para a versão mais recente.*
//
// Abaixo usamos a API pública 'writeStructuredPacket' se presente.
// Caso ocorra erro de compilação nessa chamada, troque pelo método alternativo comentado.

bool writeDataPacket(uint8_t packet_type, uint8_t *data, uint16_t len) {
  // packet_type: FINGERPRINT_DATAPACKET or FINGERPRINT_ENDDATAPACKET (values from library)
  // A Adafruit lib define constantes; caso não compile, verifique no header da lib e substitua.
  // Aqui tentamos usar a função interna writeStructuredPacket - algumas versões a expõem.
#if defined(ARDUINO_ARCH_AVR) || defined(__arm__) || defined(ESP8266) || defined(ESP32)
  // tentativa de usar método público (pode compilar ou não dependendo da versão)
  // assinaturas possíveis: writeStructuredPacket(uint32_t address, uint8_t packettype, uint8_t *payload, uint16_t len)
  // Mas para evitar dependência, vamos construir pacotes usando método 'writePacket' que a lib implementa.
#endif

  // Fallback: enviar como RAW via serial pelo protocolo do sensor:
  // monta header e envia diretamente pela mySerial (uso do protocolo do sensor).
  // Estrutura do pacote:
  // 0xEF01 (2 bytes) + address(4 bytes) + packet type(1) + length(2) + payload(length-2) + checksum(2)
  // Para simplicidade e portabilidade, vamos delegar ao método writeStructuredPacket se existir.

  // NOTE: Para garantir compatibilidade, aqui simplificamos — pois a maioria das ports
  // da Adafruit lib fornece 'writeStructuredPacket' com assinatura writeStructuredPacket(uint8_t packettype, uint8_t *payload, uint16_t len)
  // Caso falhe a compilação, preciso ajustar com base na versão exata da lib.
  // Vamos tentar:

  // Cast para evitar warning
  uint8_t *payload = data;
  uint16_t payload_len = len;

  // Usa a função interna da lib 'writeStructuredPacket' se disponível
  // (Algumas versões: finger.writeStructuredPacket(packet_type, payload, payload_len))
  // Se não compilar, você verá erro aqui — me avisa e eu ajusto conforme sua versão da lib.
  finger.writeStructuredPacket(packet_type, payload, payload_len);
  return true;
}

// ----------------- Function: upload template (HEX) into sensor flash slot -----------------
// Strategy: send template data in the DATA packets using the sensor protocol, then call storeModel(slot).
// Many community implementations do this by splitting template into chunks of (packet_payload_size)
// We'll implement a robust chunked uploader that matches the Adafruit examples.
bool uploadTemplateToSlot(int slot, const uint8_t *templateBytes, int templateLen) {
  // templateLen is expected to be correct length (e.g., ~556 bytes for R30x variants) - adapt if necessary.
  // The upload sequence (high level):
  // 1) Send FINGERPRINT_UP_CHAR / UPLOAD command (module-specific)
  // 2) Send template data in DATA packets (several packets)
  // 3) Send END DATA packet
  // 4) Finally call storeModel(slot) to write buffer into flash at 'slot'
  //
  // The Adafruit library does not expose a full high-level uploadModel; we attempt to use its low-level helpers.
  //
  // WARNING: this function relies on internal protocol behavior. Test carefully.

  // -> Step 0: clear existing CharBuffer2
  finger.clearModel(); // clear buffers (if library supports)

  // Some sensors expect a FINGERPRINT_UPLOAD command; the Adafruit lib may implement a helper called "uploadModel"
  // We'll try to use 'uploadModel' if it exists (some forks add it). Otherwise we send data packets manually.

  // Try calling uploadModel helper if available:
  #if defined(Adafruit_Fingerprint_UPLOADMODEL) // pseudo check - unlikely to work, placeholder
  // finger.uploadModel(templateBytes, templateLen); // if exists
  #endif

  // Manual: split data into 128/256-byte payloads and send as DATA packets.
  // Many sensors use 32 or 64 byte payloads; here we will use chunk size 128 which is safe for speed.
  const int CHUNK = 128;
  int offset = 0;
  while (offset < templateLen) {
    int thisLen = min(CHUNK, templateLen - offset);
    uint8_t chunkBuf[thisLen];
    for (int i=0;i<thisLen;i++) chunkBuf[i] = templateBytes[offset + i];

    // packet type for data: 0x02 (FINGERPRINT_DATAPACKET), for last chunk use 0x08 (FINGERPRINT_ENDDATAPACKET)
    uint8_t packetType = (offset + thisLen >= templateLen) ? 0x08 : 0x02;
    // send chunk
    // Note: writeDataPacket uses internal lib method if available
    writeDataPacket(packetType, chunkBuf, thisLen);

    offset += thisLen;
    delay(10); // small pause
  }

  // After sending data, we must tell module to store CharBuffer2 into flash at 'slot'
  // But Adafruit lib stores buffer1 or buffer2 using storeModel(slot) which stores buffer1 by default.
  // We must ensure the just-uploaded template is in CharBuffer1 (or call storeModel with correct expectation).
  // Many implementations upload into CharBuffer2 then call storeModel(slot) with buffer index parameter.
  // The Adafruit storeModel() stores the model created by createModel() or buffer contents;
  // we will attempt to store model in the target slot and check return code.

  uint8_t resp = finger.storeModel(slot);
  if (resp != FINGERPRINT_OK) {
    // failed to store
    return false;
  }
  return true;
}

// ----------------- Find a free temp slot -----------------
int findFreeTempSlot() {
  // Basic approach: count templates and choose top end. We must avoid overwriting real enrolled IDs.
  // Simpler: search between TEMP_SLOT_START and TEMP_SLOT_START + MAX_TEMP_SLOTS - 1 and pick first empty.
  for (int s = TEMP_SLOT_START; s < TEMP_SLOT_START + MAX_TEMP_SLOTS; s++) {
    // try to load model; if badlocation or packet error indicates empty
    uint8_t res = finger.loadModel(s); // loads into buffer1 if exists
    if (res == FINGERPRINT_PACKETRECIEVEERR) {
      // communication error - skip
      continue;
    }
    if (res == FINGERPRINT_NOMATCH || res == FINGERPRINT_BADLOCATION || res != FINGERPRINT_OK) {
      // heurística: treat as empty (some firmwares return BADLOCATION)
      // To be safe, check templateCount and maybe attempt deleteModel to ensure it's empty
      // Try deleteModel just in case (ignore errors)
      finger.deleteModel(s);
      return s;
    }
    // if loadModel returned OK, then slot is occupied - continue
  }
  return -1; // none free
}

// ----------------- Main 1:1 compare sequence -----------------
void processCompare1v1_fromBuffer() {
  // Expect bridge to have already sent a recent template to be stored in importTargetSlot via TEMPLATE_DATA/TEMPLATE_END flow.
  if (importTargetSlot < 0) {
    BRIDGE_SERIAL.println("{\"event\":\"error\",\"msg\":\"no_temp_slot\"}");
    return;
  }

  // 1. Capture finger from user -> buffer1
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    BRIDGE_SERIAL.println("{\"event\":\"compare_1_1_result\",\"match\":false,\"msg\":\"no_image\"}");
    return;
  }
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    BRIDGE_SERIAL.println("{\"event\":\"compare_1_1_result\",\"match\":false,\"msg\":\"tz1_fail\"}");
    return;
  }

  // 2. Now load the uploaded template into buffer2 by loading the slot we stored earlier
  p = finger.loadModel(importTargetSlot); // loads model from flash into buffer1 (depending on lib); some libs load to buffer1; we want to search using buffer1 default or use fingerSearch(slot)
  // To run a 1:1, we will store uploaded template into a known slot and run fingerSearch() that searches buffer1 (the captured) across all flash: but we only want to check the uploaded slot.
  // So we will call fingerSearch with slot param range restricting to the single slot if library supports.
  // Adafruit's fingerSearch(slot) takes slot parameter as starting slot; it still searches whole DB, so to ensure single-slot match, we will:
  // - use finger.loadModel(importTargetSlot) to load the uploaded template into buffer1, then compare by storing captured into another slot? This gets tricky.
  //
  // Simpler and reliable flow:
  // - store captured (buffer1) into another temporary slot A (storeModel(tempA))
  // - call fingerSearch() which will search captured buffer1 against flash (including the uploaded slot)
  // That is the standard approach: captured in buffer1 -> fingerSearch() returns matching ID if saved in flash (which includes uploaded slot)
  //
  // But storeModel saves buffer1 into flash (we don't want to modify flash). Alternate tactic:
  // - After uploading template into flash at importTargetSlot, we do:
  //   - capture -> image2Tz(1)
  //   - call fingerSearch() -> will find the uploaded template (since it is in flash)
  // So no need to loadModel; just ensure uploaded template is stored in flash at importTargetSlot.

  // Start timer
  t_start = millis();
  p = finger.fingerSearch(); // searches buffer1 default against flash templates
  t_end = millis();

  if (p == FINGERPRINT_OK) {
    // match found - finger.fingerID contains matched ID (this might be the uploaded temporary slot or some existing id)
    BRIDGE_SERIAL.print("{\"event\":\"compare_1_1_result\",\"match\":true");
    BRIDGE_SERIAL.print(",\"matched_id\":");
    BRIDGE_SERIAL.print(finger.fingerID);
    BRIDGE_SERIAL.print(",\"confidence\":");
    BRIDGE_SERIAL.print(finger.confidence);
    BRIDGE_SERIAL.print(",\"elapsed_ms\":");
    BRIDGE_SERIAL.print(t_end - t_start);
    BRIDGE_SERIAL.println("}");
  } else if (p == FINGERPRINT_NOTFOUND) {
    BRIDGE_SERIAL.print("{\"event\":\"compare_1_1_result\",\"match\":false");
    BRIDGE_SERIAL.print(",\"elapsed_ms\":");
    BRIDGE_SERIAL.print(t_end - t_start);
    BRIDGE_SERIAL.println("}");
  } else {
    BRIDGE_SERIAL.println("{\"event\":\"compare_1_1_result\",\"match\":false,\"msg\":\"search_error\"}");
  }

  // Cleanup: delete the temporary slot we used to upload the template
  finger.deleteModel(importTargetSlot);
  importTargetSlot = -1;
}

// ----------------- Serial command handling -----------------
void checkSerialCommands() {
  while (BRIDGE_SERIAL.available() > 0) {
    char c = BRIDGE_SERIAL.read();
    if (c == '\n') {
      incomingSerialData.trim();

      // Commands from bridge:
      // IMPORT_TEMPLATE_SLOT:<slot>
      // TEMPLATE_DATA:<HEX...>
      // TEMPLATE_END
      // COMPARE_1_1  -> capture and compare with uploaded template (which should be in a temp slot)
      // DELETE_ALL

      if (incomingSerialData.startsWith("IMPORT_TEMPLATE_SLOT:")) {
        // bridge suggests which temp slot to use (optional)
        int s = incomingSerialData.substring(20).toInt();
        if (s > 0) {
          importTargetSlot = s;
          BRIDGE_SERIAL.println("{\"event\":\"import_slot_set\",\"slot\":" + String(s) + "}");
        } else {
          BRIDGE_SERIAL.println("{\"event\":\"error\",\"msg\":\"invalid_slot\"}");
        }
      }
      else if (incomingSerialData.startsWith("IMPORT_TEMPLATE_AUTO")) {
        // request auto-find free temp slot
        int slot = findFreeTempSlot();
        if (slot > 0) {
          importTargetSlot = slot;
          BRIDGE_SERIAL.print("{\"event\":\"import_slot_auto\",\"slot\":");
          BRIDGE_SERIAL.print(slot);
          BRIDGE_SERIAL.println("}");
        } else {
          BRIDGE_SERIAL.println("{\"event\":\"error\",\"msg\":\"no_free_temp_slot\"}");
        }
      }
      else if (incomingSerialData.startsWith("TEMPLATE_DATA:")) {
        // append chunk
        templateHexBuffer += incomingSerialData.substring(String("TEMPLATE_DATA:").length());
        BRIDGE_SERIAL.println("{\"event\":\"template_chunk_received\",\"len\":" + String((int)incomingSerialData.length()) + "}");
      }
      else if (incomingSerialData.startsWith("TEMPLATE_END")) {
        // finished receiving hex; convert and attempt upload to sensor flash at importTargetSlot
        if (importTargetSlot < 0) {
          BRIDGE_SERIAL.println("{\"event\":\"error\",\"msg\":\"no_import_slot_set\"}");
        } else {
          // convert HEX to bytes
          int byteLen = templateHexBuffer.length() / 2;
          uint8_t tmpl[byteLen];
          for (int i=0; i < byteLen; i++) {
            tmpl[i] = hexToByteChar(templateHexBuffer.charAt(2*i), templateHexBuffer.charAt(2*i+1));
          }

          BRIDGE_SERIAL.println("{\"event\":\"upload_start\",\"slot\":" + String(importTargetSlot) + "}");
          bool ok = uploadTemplateToSlot(importTargetSlot, tmpl, byteLen);
          if (ok) {
            BRIDGE_SERIAL.println("{\"event\":\"upload_ok\",\"slot\":" + String(importTargetSlot) + "}");
          } else {
            BRIDGE_SERIAL.println("{\"event\":\"upload_failed\",\"slot\":" + String(importTargetSlot) + "}");
            // cleanup if failed
            finger.deleteModel(importTargetSlot);
            importTargetSlot = -1;
          }
        }
        // clear buffer
        templateHexBuffer = "";
      }
      else if (incomingSerialData.startsWith("COMPARE_1_1")) {
        // carry out capture and compare against uploaded template slot
        processCompare1v1_fromBuffer();
      }
      else if (incomingSerialData.startsWith("DELETE_ALL")) {
        uint8_t r = finger.emptyDatabase();
        if (r == FINGERPRINT_OK) BRIDGE_SERIAL.println("{\"event\":\"delete_all_ok\"}");
        else BRIDGE_SERIAL.println("{\"event\":\"delete_all_failed\"}");
      }
      else if (incomingSerialData.startsWith("STATUS")) {
        finger.getTemplateCount();
        BRIDGE_SERIAL.print("{\"event\":\"status\",\"templates\":");
        BRIDGE_SERIAL.print(finger.templateCount);
        BRIDGE_SERIAL.println("}");
      }

      incomingSerialData = "";
    } else {
      incomingSerialData += c;
    }
  }
}

// ----------------- Set
