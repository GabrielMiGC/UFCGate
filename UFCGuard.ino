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
LiquidCrystal_I2C lcd(0x27, 16, 2); // Ajuste endereço se necessário

// Pinos para comunicação com o sensor (SoftwareSerial)
#define FINGER_RX_PIN 50 // RX do Arduino (para TX do sensor) - escolha pinos compatíveis no Mega
#define FINGER_TX_PIN 51 // TX do Arduino (para RX do sensor)
SoftwareSerial fingerSerial(FINGER_RX_PIN, FINGER_TX_PIN);

// Instância da lib Adafruit
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// Buffer máximo esperado para o template (ajuste se seu módulo usa tamanho diferente)
#define TEMPLATE_MAX_SIZE 1024
uint8_t templateBuf[TEMPLATE_MAX_SIZE];
uint16_t templateLen = 0;

// Timeout em ms para aguardar resposta do servidor (quando testar)
#define SERVER_RESPONSE_TIMEOUT 5000UL

// ========== UTIL: Base64 encoding ==========
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t *data, size_t len) {
  String out;
  out.reserve(((len + 2) / 3) * 4 + 4);
  size_t i = 0;
  while (i + 2 < len) {
    uint32_t triple = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
    out += b64_table[(triple >> 18) & 0x3F];
    out += b64_table[(triple >> 12) & 0x3F];
    out += b64_table[(triple >> 6) & 0x3F];
    out += b64_table[triple & 0x3F];
    i += 3;
  }
  if (i < len) {
    uint8_t a = data[i++];
    uint8_t b = (i < len) ? data[i++] : 0;
    uint32_t triple = (a << 16) | (b << 8);
    out += b64_table[(triple >> 18) & 0x3F];
    out += b64_table[(triple >> 12) & 0x3F];
    if (i <= len) {
      out += (i == (len + 1)) ? '=' : b64_table[(triple >> 6) & 0x3F];
    } else {
      out += '=';
    }
    out += '=';
  }
  return out;
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
  - Depende do suporte da biblioteca / do módulo.
  - Aqui fazemos uma tentativa genérica: muitos sensores suportam comando "upload model" / "getModel".
  - Se a sua biblioteca não tiver função direta, você pode:
    * modificar a Adafruit_Fingerprint para expor 'getModel' ou 'downloadModel';
    * ou implementar o download usando os pacotes do protocolo (comandos 0x08/0x02/0x0A dependendo do módulo).
  - A função preenche templateBuf e templateLen e retorna true se obteve algo.
*/
bool getFingerprintTemplate(uint8_t bufferNum, uint8_t *outBuf, uint16_t &outLen) {
  // TENTATIVA 1: usar função hipotética da lib (se existir)
  // Muitas forks expõem finger.getModel() ou finger.downloadModel()
  // Exemplos (essas funções podem não existir na sua versão):
  //
  // if (finger.getModel(bufferNum, outBuf, &outLen) == FINGERPRINT_OK) { ... }
  //
  // Como fallback, tentamos usar readModel/downloadModel se estiver disponível.
  //
  // ---- IMPLEMENTAÇÃO GENÉRICA: ----
  // 1) pedir imagem
  if (finger.getImage() != FINGERPRINT_OK) {
    return false;
  }
  // 2) converter
  if (finger.image2Tz() != FINGERPRINT_OK) {
    return false;
  }
  // 3) exportar template do char buffer 1 ou 2 (dependendo do módulo)
  // Adafruit_Fingerprint não tem uma API pública padrão para fazer "download" do template
  // então abaixo tentamos chamar um método que algumas forks implementam.
  //
  // Atenção: se sua biblioteca NÃO possuir getModel/downloadModel, descomente e implemente
  // a versão de baixo nível (envio do comando de download ao sensor). Se quiser, eu
  // posso gerar essa rotina de baixo nível se você confirmar o modelo exato do sensor.

  // Exemplo hipotético:
  #if defined(ADAFRUIT_FINGERPRINT_GETMODEL)
    // se a lib tiver essa macro laços de compatibilidade
    int res = finger.getModel(bufferNum, outBuf, &outLen); // pseudocódigo
    if (res == FINGERPRINT_OK && outLen > 0) return true;
  #endif

  // Se a biblioteca NÃO suportar chamada direta, tentamos usar storeModel + upload from buffer (só ilustração)
  // Esta parte provavelmente NÃO funcionará sem adaptar a biblioteca:
  // return false para sinalizar que precisa de adaptação.
  return false;
}

// Envia o JSON com template base64 via Serial USB
void sendTemplateJson(const String &b64, uint16_t rawLen) {
  // Saída JSON compacta (uma linha)
  // {"template":"...","len":123}
  Serial.print("{\"template\":\"");
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
  fingerSerial.begin(57600); // velocidade típica (ajustar conforme módulo)
  delay(100);
  finger.begin(57600);

  // Verifica conexão com sensor (senha padrão 0x00000000 ou conforme o módulo)
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
  // Espera por uma impressão (modo simples)
  int id = getFingerprintIDez();
  if (id == -2) {
    // nenhuma leitura no momento - continuar
    delay(200);
    return;
  }

  // Se chegou aqui, detectou impressão (pode ter id se sensor encontrou match local)
  showReading();
  delay(200);

  // TENTAR extrair o template do caract buffer 1 (ou 0 conforme módulo)
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
