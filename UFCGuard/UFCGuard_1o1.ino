// =========================================================
// =============== VERSÃO A — 1:1 (BUFFER × BUFFER) =========
// ===== UFCGuard + DY50 (Adafruit Fingerprint Library) =====
// =========================================================
// Integração com Bridge Flask (serial_bridge.py)
// Mantém completamente compatível com sua infra atual
// =========================================================

// ========== Inclusões ==========
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pinos RX/TX conectados ao módulo DY-50
SoftwareSerial mySerial(52, 53); // RX, TX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ========== Globais ==========
unsigned long lastStatusTime = 0;
const unsigned long statusInterval = 5000;

String incomingSerialData;
bool commandInProgress = false;

// === Para IMPORTAÇÃO DE TEMPLATE (Versão A) ===
String templateBufferHex = "";
bool receivingTemplate = false;
int importTargetID = -1;

// Timer para benchmarks
unsigned long t_start = 0;
unsigned long t_end = 0;


// =========================================================
// ===================== Funções LCD ========================
// =========================================================

void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}


// =========================================================
// ============= Utilidades para TEMPLATE HEX ==============
// =========================================================

uint8_t hexToByte(const char* hex) {
  uint8_t high = (hex[0] <= '9') ? hex[0]-'0' : toupper(hex[0])-'A'+10;
  uint8_t low  = (hex[1] <= '9') ? hex[1]-'0' : toupper(hex[1])-'A'+10;
  return (high << 4) | low;
}

void finalizeTemplateImport() {
  int len = templateBufferHex.length() / 2;
  uint8_t tmpl[len];

  for (int i = 0; i < len; i++) {
    tmpl[i] = hexToByte(templateBufferHex.substring(2*i, 2*i+2).c_str());
  }

  // === Carrega no buffer 2 do sensor ===
  finger.loadModel(importTargetID);    // só para limpar
  finger.setModel(tmpl);               // envia o modelo para o sensor (buffer2)

  Serial.println("{\"event\":\"template_loaded_buffer2\"}");
  receivingTemplate = false;
  templateBufferHex = "";
}



// =========================================================
// ================= Cadastro / Exclusão ====================
// =========================================================

uint8_t getFingerprintImage() {
  uint8_t p = finger.getImage();
  while (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) {
      delay(50);
      return p;
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("{\"status\":\"error\",\"msg\":\"Erro comunicacao\"}");
      return p;
    } else if (p == FINGERPRINT_IMAGEFAIL) {
      Serial.println("{\"status\":\"error\",\"msg\":\"Erro imagem\"}");
      return p;
    } else {
      Serial.println("{\"status\":\"error\",\"msg\":\"Erro desconhecido\"}");
      return p;
    }
  }
  return FINGERPRINT_OK;
}

void enrollFingerprint(int id) {
  commandInProgress = true;
  lcdMsg("Modo Cadastro", ("ID: "+String(id)).c_str());
  Serial.println("{\"status\":\"enroll_starting\",\"id\":"+String(id)+"}");
  delay(1000);

  lcdMsg("Coloque o dedo","Aguardando...");
  Serial.println("{\"status\":\"enroll_prompt_1\"}");
  while (getFingerprintImage() != FINGERPRINT_OK);

  uint8_t p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Leitura 1","");
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Img1\"}");
    commandInProgress = false;
    return;
  }

  lcdMsg("Digital 1 OK","Remova o dedo");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  lcdMsg("Coloque de novo","Mesmo dedo...");
  Serial.println("{\"status\":\"enroll_prompt_2\"}");
  while (getFingerprintImage() != FINGERPRINT_OK);

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Leitura 2","");
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Img2\"}");
    commandInProgress = false;
    return;
  }

  lcdMsg("Digital 2 OK","Combinando...");
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Combinar","");
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Combinar\"}");
    commandInProgress = false;
    return;
  }

  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Salvar","");
    Serial.println("{\"status\":\"enroll_failed\",\"msg\":\"Erro Salvar\"}");
    commandInProgress = false;
    return;
  }

  lcdMsg("Cadastro OK!",("ID: "+String(id)).c_str());
  Serial.println("{\"status\":\"enroll_success\",\"id\":"+String(id)+"}");
  delay(2000);
  commandInProgress = false;
}

void deleteFingerprint(int id) {
  commandInProgress = true;
  lcdMsg("Deletando...",("ID: "+String(id)).c_str());
  
  uint8_t p = finger.deleteModel(id);
  
  if (p == FINGERPRINT_OK) {
    lcdMsg("Deletado OK!",("ID: "+String(id)).c_str());
    Serial.println("{\"status\":\"delete_success\",\"id\":"+String(id)+"}");
  } else {
    lcdMsg("Erro ao Deletar","ID nao existe?");
    Serial.println("{\"status\":\"delete_failed\",\"id\":"+String(id)+"}");
  }
  delay(2000);
  commandInProgress = false;
}

void deleteAllFingerprints() {
  commandInProgress = true;
  lcdMsg("!! CUIDADO !!","Apagando TUDO?");
  delay(2000);

  uint8_t p = finger.emptyDatabase();
  if (p == FINGERPRINT_OK) {
    lcdMsg("Memoria Limpa!","");
    Serial.println("{\"status\":\"delete_all_success\"}");
  } else {
    lcdMsg("Erro ao Limpar","");
    Serial.println("{\"status\":\"delete_all_failed\"}");
  }
  delay(2000);
  commandInProgress = false;
}



// =========================================================
// ========== Função VERSÃO A — COMPARAÇÃO 1:1 ==============
// =========================================================

void compare1v1() {
  lcdMsg("Comparando...", "");

  // === Lê digital do usuário ===
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"event\":\"compare_1_1_result\",\"match\":false,\"msg\":\"no_image\"}");
    return;
  }

  p = finger.image2Tz(1);  // buffer 1
  if (p != FINGERPRINT_OK) {
    Serial.println("{\"event\":\"compare_1_1_result\",\"match\":false,\"msg\":\"tz1_fail\"}");
    return;
  }

  // === Tempo de início ===
  t_start = millis();
  p = finger.compareTemplates();   // Compara buffer1 x buffer2
  t_end = millis();

  // === Resultado ===
  if (p == FINGERPRINT_OK) {
    Serial.print("{\"event\":\"compare_1_1_result\",\"match\":true");
    Serial.print(",\"confidence\":");
    Serial.print(finger.confidence);
    Serial.print(",\"elapsed_ms\":");
    Serial.print(t_end - t_start);
    Serial.println("}");
  } else {
    Serial.print("{\"event\":\"compare_1_1_result\",\"match\":false");
    Serial.print(",\"elapsed_ms\":");
    Serial.print(t_end - t_start);
    Serial.println("}");
  }
}



// =========================================================
// =================== Loop de Verificação =================
// =========================================================

void runVerificationScan() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerSearch();
  
  if (p == FINGERPRINT_OK) {
    lcdMsg("Acesso Liberado",("ID: "+String(finger.fingerID)).c_str());
    Serial.print("{\"event\":\"match_found\",\"sensor_id\":");
    Serial.print(finger.fingerID);
    Serial.print(",\"confidence\":");
    Serial.print(finger.confidence);
    Serial.println("}");
    delay(2000);
  } 
  else if (p == FINGERPRINT_NOTFOUND) {
    lcdMsg("Acesso Negado","Tente de novo");
    Serial.println("{\"event\":\"match_failed\"}");
    delay(1000);
  } 
  else {
    lcdMsg("Erro Sensor","Comunicao?");
    delay(1000);
  }
}



// =========================================================
// ================== Comandos do Bridge ===================
// =========================================================

void checkSerialCommands() {

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n') {

      incomingSerialData.trim();

      // ========== COMANDOS NORMAIS ==========
      if (incomingSerialData.startsWith("ENROLL:")) {
        enrollFingerprint(incomingSerialData.substring(7).toInt());
      }
      else if (incomingSerialData.startsWith("DELETE:")) {
        deleteFingerprint(incomingSerialData.substring(7).toInt());
      }
      else if (incomingSerialData.startsWith("DELETE_ALL")) {
        deleteAllFingerprints();
      }

      // ========== VERSÃO A — COMPARE 1:1 ==========
      else if (incomingSerialData.startsWith("COMPARE_1_1")) {
        compare1v1();
      }

      // ========== IMPORTAÇÃO DE TEMPLATE ==========
      else if (incomingSerialData.startsWith("IMPORT_TEMPLATE:")) {
        importTargetID = incomingSerialData.substring(16).toInt();
        receivingTemplate = true;
        templateBufferHex = "";
        Serial.println("{\"event\":\"recv_template_start\"}");
      }
      else if (incomingSerialData.startsWith("TEMPLATE_DATA:")) {
        templateBufferHex += incomingSerialData.substring(14);
      }
      else if (incomingSerialData.startsWith("TEMPLATE_END")) {
        finalizeTemplateImport();
      }

      incomingSerialData = "";
    }
    else {
      incomingSerialData += c;
    }
  }
}



// =========================================================
// ======================= Setup ===========================
// =========================================================

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  lcd.init();
  lcd.backlight();
  lcdMsg("Iniciando...", "DY-50");
  Serial.println("[BOOT] UFCGuard DY50 inicializando...");

  mySerial.begin(57600);
  delay(200);
  finger.begin(57600);
  delay(200);
  
  if (finger.verifyPassword()) {
    Serial.println("[OK] Sensor biometrico pronto.");
    lcdMsg("Sensor pronto", "Aguardando");
  } else {
    Serial.println("[ERRO] DY-50 falhou");
    lcdMsg("ERRO sensor","Checar conex.");
    while(true) { delay(1); }
  }

  finger.getTemplateCount();
  Serial.print("[INFO] Sensor contem ");
  Serial.print(finger.templateCount);
  Serial.println(" templates.");
}


// =========================================================
// ======================== Loop ===========================
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

