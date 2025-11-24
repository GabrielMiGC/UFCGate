// =========================================================
// ============ VERSÃO B — BATCH (fingerSearch) ==============
// ================== UFCGuard + DY50 ========================
// ========== Busca otimizada carregando lotes ===============
// =========================================================

// ========== Inclusões ==========
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Comunicação com DY-50
SoftwareSerial mySerial(52, 53);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ========== Variáveis Globais ==========
unsigned long lastStatusTime = 0;
const unsigned long statusInterval = 5000;

String incomingSerialData;
bool commandInProgress = false;

// ---- Controle de batch ----
bool receivingTemplate = false;
String templateBufferHex = "";
int currentBatchSlot = 0;      // Slot interno (1..N)

// Temporização
unsigned long t_start = 0;
unsigned long t_end = 0;


// =========================================================
// ====================== LCD ==============================
// =========================================================

void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(l1);
  lcd.setCursor(0,1);
  lcd.print(l2);
}



// =========================================================
// ===== Utilidades para converter HEX → buffer de bytes ====
// =========================================================

uint8_t hexToByte(const char* hex) {
  uint8_t high = (hex[0] <= '9') ? hex[0]-'0' : toupper(hex[0])-'A'+10;
  uint8_t low  = (hex[1] <= '9') ? hex[1]-'0' : toupper(hex[1])-'A'+10;
  return (high << 4) | low;
}



// =========================================================
// ========== Função para FINALIZAR template import =========
// ====== Grava o template no ID atual (1..N do batch) ======
// =========================================================

void finalizeBatchTemplateImport() {

  int len = templateBufferHex.length() / 2;
  uint8_t tmpl[len];

  for (int i = 0; i < len; i++) {
    tmpl[i] = hexToByte(templateBufferHex.substring(2*i, 2*i+2).c_str());
  }

  // Carrega no sensor (buffer interno)
  finger.clearModel();
  finger.setModel(tmpl);

  // Armazena no ID correspondente
  finger.storeModel(currentBatchSlot);

  Serial.print("{\"event\":\"batch_template_loaded\",\"slot\":");
  Serial.print(currentBatchSlot);
  Serial.println("}");

  receivingTemplate = false;
  templateBufferHex = "";
}



// =========================================================
// ================= Cadastro / Exclusão ====================
// =========================================================

uint8_t getFingerprintImage() {
  uint8_t p = finger.getImage();

  if (p == FINGERPRINT_NOFINGER) return p;
  if (p == FINGERPRINT_OK) return p;

  if (p == FINGERPRINT_PACKETRECIEVEERR)
    Serial.println("{\"status\":\"error\",\"msg\":\"Erro comunicacao\"}");
  else if (p == FINGERPRINT_IMAGEFAIL)
    Serial.println("{\"status\":\"error\",\"msg\":\"Erro imagem\"}");
  else
    Serial.println("{\"status\":\"error\",\"msg\":\"Erro desconhecido\"}");

  return p;
}


void enrollFingerprint(int id) {
  commandInProgress = true;
  
  lcdMsg("Modo Cadastro", ("ID: "+String(id)).c_str());
  Serial.println("{\"status\":\"enroll_starting\",\"id\":"+String(id)+"}");
  delay(1000);

  // 1ª leitura
  lcdMsg("Coloque o dedo","Aguardando...");
  Serial.println("{\"status\":\"enroll_prompt_1\"}");
  while (getFingerprintImage() != FINGERPRINT_OK);

  uint8_t p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Img1\"}");
    commandInProgress = false;
    return;
  }

  lcdMsg("Digital 1 OK","Remova o dedo");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  // 2ª leitura
  lcdMsg("Coloque de novo","Mesmo dedo...");
  Serial.println("{\"status\":\"enroll_prompt_2\"}");
  while (getFingerprintImage() != FINGERPRINT_OK);

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Img2\"}");
    commandInProgress = false;
    return;
  }

  // Combina
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Combinar\"}");
    commandInProgress = false;
    return;
  }

  // Salva
  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Salvar\"}");
    commandInProgress = false;
    return;
  }

  lcdMsg("Cadastro OK!", ("ID: "+String(id)).c_str());
  Serial.println("{\"status\":\"enroll_success\",\"id\":"+String(id)+"}");
  delay(2000);
  commandInProgress = false;
}


void deleteFingerprint(int id) {
  commandInProgress = true;

  uint8_t p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK)
    Serial.println("{\"status\":\"delete_success\",\"id\":"+String(id)+"}");
  else
    Serial.println("{\"status\":\"delete_failed\",\"id\":"+String(id)+"}");

  delay(1000);
  commandInProgress = false;
}


void deleteAllFingerprints() {
  commandInProgress = true;

  uint8_t p = finger.emptyDatabase();
  if (p == FINGERPRINT_OK)
    Serial.println("{\"status\":\"delete_all_success\"}");
  else
    Serial.println("{\"status\":\"delete_all_failed\"}");

  delay(1000);
  commandInProgress = false;
}



// =========================================================
// ============== VERSÃO B — RUN BATCH MATCH ===============
// =========================================================
//
// Carrega digital → buffer 1
// Executa fingerSearch() contra LOTES gravados em ID 1..N
// Retorna tempo + ID encontrado
//
// =========================================================

void runBatchMatch() {
  lcdMsg("Ler digital...", "");

  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"event\":\"batch_match_result\",\"match\":false,\"msg\":\"no_image\"}");
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"event\":\"batch_match_result\",\"match\":false,\"msg\":\"tz1_fail\"}");
    return;
  }

  // Tempo
  t_start = millis();
  p = finger.fingerSearch();
  t_end = millis();

  if (p == FINGERPRINT_OK) {
    Serial.print("{\"event\":\"batch_match_result\",\"match\":true");
    Serial.print(",\"id\":");
    Serial.print(finger.fingerID);
    Serial.print(",\"confidence\":");
    Serial.print(finger.confidence);
    Serial.print(",\"elapsed_ms\":");
    Serial.print(t_end - t_start);
    Serial.println("}");
  }
  else {
    Serial.print("{\"event\":\"batch_match_result\",\"match\":false");
    Serial.print(",\"elapsed_ms\":");
    Serial.print(t_end - t_start);
    Serial.println("}");
  }
}



// =========================================================
// ============= LIMPAR MODELOS TEMPORÁRIOS ================
// =========================================================
// (IDs 1..300 ou conforme lote enviado pelo backend)

void clearBatch() {
  commandInProgress = true;
  lcdMsg("Limpando batch", "");

  for (int i = 1; i <= 300; i++) {
    finger.deleteModel(i);
  }

  Serial.println("{\"event\":\"batch_cleared\"}");
  delay(500);
  commandInProgress = false;
}



// =========================================================
// ================== Loop de Verificação ==================
// =========================================================

void runVerificationScan() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerSearch();
  
  if (p == FINGERPRINT_OK) {
    lcdMsg("Acesso Liberado", ("ID:" + String(finger.fingerID)).c_str());
    Serial.print("{\"event\":\"match_found\",\"sensor_id\":");
    Serial.print(finger.fingerID);
    Serial.print(",\"confidence\":");
    Serial.print(finger.confidence);
    Serial.println("}");
    delay(1500);
  }
  else if (p == FINGERPRINT_NOTFOUND) {
    lcdMsg("Acesso Negado","Tente de novo");
    Serial.println("{\"event\":\"match_failed\"}");
    delay(800);
  }
}



// =========================================================
// ===================== COMANDOS ==========================
// =========================================================

void checkSerialCommands() {

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {
      incomingSerialData.trim();

      // =================== Comandos padrão =====================
      if (incomingSerialData.startsWith("ENROLL:"))
        enrollFingerprint(incomingSerialData.substring(7).toInt());

      else if (incomingSerialData.startsWith("DELETE:"))
        deleteFingerprint(incomingSerialData.substring(7).toInt());

      else if (incomingSerialData.startsWith("DELETE_ALL"))
        deleteAllFingerprints();


      // =================== VERSÃO B: Begin batch ================
      else if (incomingSerialData.startsWith("BEGIN_BATCH")) {
        currentBatchSlot = 0;
        Serial.println("{\"event\":\"batch_start\"}");
      }

      // ========== Novo modelo para gravar em um slot ============
      else if (incomingSerialData.startsWith("TEMPLATE_SLOT:")) {
        currentBatchSlot = incomingSerialData.substring(14).toInt();
        receivingTemplate = true;
        templateBufferHex = "";
        Serial.print("{\"event\":\"batch_slot_recv\",\"slot\":");
        Serial.print(currentBatchSlot);
        Serial.println("}");
      }

      else if (incomingSerialData.startsWith("TEMPLATE_DATA:")) {
        templateBufferHex += incomingSerialData.substring(14);
      }

      else if (incomingSerialData.startsWith("TEMPLATE_END")) {
        finalizeBatchTemplateImport();
      }

      // ================= RUN BATCH MATCH ========================
      else if (incomingSerialData.startsWith("RUN_BATCH_MATCH")) {
        runBatchMatch();
      }

      // ================= CLEAR BATCH TEMP =======================
      else if (incomingSerialData.startsWith("CLEAR_BATCH")) {
        clearBatch();
      }


      // Limpa buffer
      incomingSerialData = "";
    }
    else {
      incomingSerialData += c;
    }
  }
}



// =========================================================
// ======================= SETUP ============================
// =========================================================

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  lcd.init();
  lcd.backlight();
  lcdMsg("Iniciando...", "DY-50");

  Serial.println("[BOOT] UFCGuard Batch Mode");

  mySerial.begin(57600);
  delay(200);

  finger.begin(57600);
  delay(200);

  if (finger.verifyPassword()) {
    lcdMsg("Sensor pronto", "");
    Serial.println("[OK] Finger ready.");
  } else {
    lcdMsg("ERRO sensor", "");
    Serial.println("[ERRO] Finger fail.");
    while(true);
  }

  finger.getTemplateCount();
  Serial.print("[INFO] Templates na flash: ");
  Serial.println(finger.templateCount);
}



// =========================================================
// ======================== LOOP ============================
// =========================================================

void loop() {

  checkSerialCommands();

  if (commandInProgress) return;

  runVerificationScan();

  if (millis() - lastStatusTime > statusInterval) {
    Serial.println("[STATUS] Ativo...");
    lcdMsg("Aguardando", "Coloque dedo");
    lastStatusTime = millis();
  }
}

