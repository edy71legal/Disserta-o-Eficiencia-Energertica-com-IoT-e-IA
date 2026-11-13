#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <PZEM004Tv30.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// --- CONFIGURAÇÕES ---
#define WIFI_SSID "consumo"
#define WIFI_PASSWORD "26803234"
#define API_KEY "AIzaSyA563ZWUCqu7RJLBa92TmMQ09eUCrD2SmQ"
// IMPORTANTE: Adicionada a barra "/" no final
#define DATABASE_URL "https://consumo-conciente-default-rtdb.firebaseio.com/"

#define I2C_SDA 21
#define I2C_SCL 22
#define PZEM_RX 16
#define PZEM_TX 17

// Inicializamos a Serial2 manualmente para garantir estabilidade
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "a.st1.ntp.br", -10800);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

void setup() {
  Serial.begin(115200);
  
  // Inicializa LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.print("Iniciando...");

  // Inicializa Serial2 para o PZEM
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);

  // Conecta Wi-Fi com feedback no LCD
  lcd.setCursor(0, 1);
  lcd.print("Conectando WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) { // Timeout de 10 segundos
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if(WiFi.status() != WL_CONNECTED){
    lcd.clear();
    lcd.print("WiFi Falhou!");
    while(1); // Para o código se não conectar
  }

  Serial.println("\nWiFi OK!");
  timeClient.begin();

  // Configurações Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  lcd.clear();
  lcd.print("Autenticando...");

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Serial.println("Firebase SignUp OK");
  } else {
    Serial.printf("Erro SignUp: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback; 
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  lcd.clear();
  lcd.print("Sistema Pronto");
  delay(1000);
}

void loop() {
  timeClient.update();

  float voltage = pzem.voltage();
  float current = pzem.current();
  float power   = pzem.power();
  float energy  = pzem.energy();

  if (isnan(voltage)) {
    Serial.println("Erro de leitura no PZEM");
    lcd.setCursor(0, 0);
    lcd.print("Erro PZEM       ");
    lcd.setCursor(0, 1);
    lcd.print("Verifique AC    ");
  } else {
    // Atualiza LCD
    lcd.setCursor(0, 0);
    lcd.printf("%.1fV %.2fA     ", voltage, current);
    lcd.setCursor(0, 1);
    lcd.printf("%.1fW %.2fkWh   ", power, energy);

    // Envia para o Firebase
    if (Firebase.ready() && signupOK) {
      enviarDadosFirebase(voltage, current, power, energy);
    }
  }

  // Delay inteligente de 1 minuto
  unsigned long start = millis();
  while (millis() - start < 60000) {
    // Você pode colocar aqui uma atualização rápida do LCD se quiser
    yield();
  }
}

void enviarDadosFirebase(float v, float i, float p, float e) {
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = localtime((const time_t *)&epochTime);
  
  char dataStr[11]; // YYYY-MM-DD
  sprintf(dataStr, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  
  char horaStr[9];  // HH-MM-SS
  sprintf(horaStr, "%02d-%02d-%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

  // Monta o caminho exato que combinamos: /leituras/DATA/HORA
  String path = "/leituras/" + String(dataStr) + "/" + String(horaStr);
  
  FirebaseJson json;
  json.add("voltagem", v);
  json.add("corrente", i);
  json.add("potencia", p);
  json.add("consumo", e);

  Serial.print("Enviando para: ");
  Serial.println(path);

  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("Firebase: Sucesso!");
  } else {
    Serial.print("Firebase Erro: ");
    Serial.println(fbdo.errorReason());
  }
}