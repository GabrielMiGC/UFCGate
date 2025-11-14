// ========== Inclusões ==========
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
// Biblioteca LCD I2C
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pinos RX/TX conectados ao módulo DY-50
SoftwareSerial mySerial(52, 53); // RX, TX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// LCD: endereço comum 0x27 ou 0x3F.
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ========== Globais ==========
unsigned long lastStatusTime = 0;
const unsigned long statusInterval = 5000; // 5 segundos

// Buffer para comandos recebidos via Serial
String incomingSerialData;
bool commandInProgress = false; // Trava para não tentar verificar durante um cadastro

// ========== Funções do LCD ==========

/**
 * @brief Exibe mensagens de 2 linhas no LCD.
 */
void lcdMsg(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

// ========== Funções de Gerenciamento do Sensor ==========

/**
 * @brief Obtém uma imagem válida da digital.
 * @return O código de status da biblioteca.
 */
uint8_t getFingerprintImage() {
  uint8_t p = finger.getImage();
  while (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) {
      // Sem dedo, continua esperando...
      delay(50);
      return p;
    } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      Serial.println("{\"status\":\"error\", \"msg\":\"Erro comunicacao\"}");
      return p;
    } else if (p == FINGERPRINT_IMAGEFAIL) {
      Serial.println("{\"status\":\"error\", \"msg\":\"Erro imagem\"}");
      return p;
    } else {
      Serial.println("{\"status\":\"error\", \"msg\":\"Erro desconhecido\"}");
      return p;
    }
    p = finger.getImage();
  }
  return FINGERPRINT_OK;
}

/**
 * @brief Processo de cadastro de uma nova digital em um ID (slot)
 */
void enrollFingerprint(int id) {
  commandInProgress = true;
  // CORREÇÃO: Adicionado .c_str() para converter a String
  lcdMsg("Modo Cadastro", ("ID: " + String(id)).c_str());
  Serial.println("{\"status\":\"enroll_starting\", \"id\":" + String(id) + "}");
  delay(1000);

  // --- Primeira Leitura ---
  lcdMsg("Coloque o dedo", "Aguardando...");
  Serial.println("{\"status\":\"enroll_prompt_1\"}");
  
  while (getFingerprintImage() != FINGERPRINT_OK);
  uint8_t p = finger.image2Tz(1); // Salva no buffer 1
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Leitura 1", "");
    Serial.println("{\"status\":\"enroll_failed\", \"msg\":\"Erro Img1\"}");
    commandInProgress = false;
    return;
  }
  lcdMsg("Digital 1 OK", "Remova o dedo");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  // --- Segunda Leitura ---
  lcdMsg("Coloque de novo", "Mesmo dedo...");
  Serial.println("{\"status\":\"enroll_prompt_2\"}");

  while (getFingerprintImage() != FINGERPRINT_OK);
  p = finger.image2Tz(2); // Salva no buffer 2
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Leitura 2", "");
    Serial.println("{\"status\":\"enroll_failed\", \"msg\":\"Erro Img2\"}");
    commandInProgress = false;
    return;
  }
  lcdMsg("Digital 2 OK", "Combinando...");

  // --- Combina e Salva ---
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Combinar", "Tente de novo");
    Serial.println("{\"status\":\"enroll_failed\", \"msg\":\"Erro Combinar\"}");
    commandInProgress = false;
    return;
  }

  p = finger.storeModel(id);
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Salvar", "ID ja existe?");
    Serial.println("{\"status\":\"enroll_failed\", \"msg\":\"Erro Salvar\"}");
    commandInProgress = false;
    return;
  }

  // CORREÇÃO: Adicionado .c_str() para converter a String
  lcdMsg("Cadastro OK!", ("ID: " + String(id)).c_str());
  Serial.println("{\"status\":\"enroll_success\", \"id\":" + String(id) + "}");
  delay(2000);
  commandInProgress = false;
}

/**
 * @brief Deleta um único modelo de digital do sensor.
 */
void deleteFingerprint(int id) {
  commandInProgress = true;
  // CORREÇÃO: Adicionado .c_str() para converter a String
  lcdMsg("Deletando...", ("ID: " + String(id)).c_str());
  
  uint8_t p = finger.deleteModel(id);
  
  if (p == FINGERPRINT_OK) {
    // CORREÇÃO: Adicionado .c_str() para converter a String
    lcdMsg("Deletado OK!", ("ID: " + String(id)).c_str());
    Serial.println("{\"status\":\"delete_success\", \"id\":" + String(id) + "}");
  } else {
    lcdMsg("Erro ao Deletar", "ID nao existe?");
    Serial.println("{\"status\":\"delete_failed\", \"id\":" + String(id) + "}");
  }
  delay(2000);
  commandInProgress = false;
}

/**
 * @brief Apaga TODAS as digitais da memória do sensor.
 */
void deleteAllFingerprints() {
  commandInProgress = true;
  lcdMsg("!! CUIDADO !!", "Apagando TUDO?");
  delay(3000); // Chance de cancelar (desligando)
  lcdMsg("Confirmado", "Apagando...");

  uint8_t p = finger.emptyDatabase();
  
  if (p == FINGERPRINT_OK) {
    lcdMsg("Memoria Limpa!", "");
    Serial.println("{\"status\":\"delete_all_success\"}");
  } else {
    lcdMsg("Erro ao Limpar", "");
    Serial.println("{\"status\":\"delete_all_failed\"}");
  }
  delay(2000);
  commandInProgress = false;
}


/**
 * @brief Função principal de verificação 1:N.
 * É chamada continuamente pelo loop().
 */
void runVerificationScan() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return; // Sem dedo, sai

  // Converte imagem e salva no buffer 1
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    lcdMsg("Erro Leitura", "");
    delay(500);
    return;
  }

  // CORREÇÃO: O nome correto da função é fingerSearch()
  p = finger.fingerSearch();
  
  if (p == FINGERPRINT_OK) {
    // --- MATCH ENCONTRADO ---
    // CORREÇÃO: Adicionado .c_str() para converter a String
    lcdMsg("Acesso Liberado", ("ID: " + String(finger.fingerID)).c_str());
    // Envia o JSON de sucesso para o bridge
    Serial.print("{\"event\":\"match_found\", \"sensor_id\":");
    Serial.print(finger.fingerID);
    Serial.print(", \"confidence\":");
    Serial.print(finger.confidence);
    Serial.println("}");
    delay(2000); // Aguarda 2s antes de ler de novo
  } 
  else if (p == FINGERPRINT_NOTFOUND) {
    // --- SEM MATCH ---
    lcdMsg("Acesso Negado", "Tente de novo");
    Serial.println("{\"event\":\"match_failed\"}");
    delay(1000);
  } 
  else {
    // --- ERRO ---
    lcdMsg("Erro Sensor", "Comunicao?");
    delay(1000);
  }
}

/**
 * @brief Verifica se há comandos chegando do Python (Bridge).
 * Protocolo simples: "COMANDO:VALOR\n"
 */
void checkSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      // Comando recebido, vamos processar
      incomingSerialData.trim();
      
      if (incomingSerialData.startsWith("ENROLL:")) {
        int id = incomingSerialData.substring(7).toInt();
        if (id > 0) {
          enrollFingerprint(id);
        }
      } 
      else if (incomingSerialData.startsWith("DELETE:")) {
        int id = incomingSerialData.substring(7).toInt();
        if (id > 0) {
          deleteFingerprint(id);
        }
      }
      else if (incomingSerialData.startsWith("DELETE_ALL")) {
        deleteAllFingerprints();
      }
      
      incomingSerialData = ""; // Limpa o buffer de comando
    } else {
      incomingSerialData += c;
    }
  }
}

// ========== Setup e Loop ==========

void setup() {
  Serial.begin(9600); // Comunicação com o PC/Bridge
  while (!Serial) {}
  
  // LCD init
  lcd.init();
  lcd.backlight();
  lcdMsg("Iniciando...", "DY-50");
  Serial.println("[BOOT] UFCGuard DY50 inicializando...");

  mySerial.begin(57600); // Comunicação com o sensor
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
  
  // Imprime o número de templates salvos no sensor
  finger.getTemplateCount();
  Serial.print("[INFO] Sensor contem ");
  Serial.print(finger.templateCount);
  Serial.println(" templates.");
}

void loop() {
  // 1. Ouve por comandos do servidor (ex: "ENROLL:10")
  checkSerialCommands();

  // 2. Se um comando estiver em execução (ex: cadastro), não faz verificação
  if (commandInProgress) {
    return;
  }
  
  // 3. Loop de verificação (modo padrão)
  runVerificationScan();
  
  // 4. Envia status "Ativo" para o Serial a cada 5s se nada acontecer
  if (millis() - lastStatusTime > statusInterval) {
    Serial.println("[STATUS] Ativo...");
    lcdMsg("Aguardando", "Coloque dedo");
    lastStatusTime = millis();
  }
}