#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <PZEM004Tv30.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Auxiliares do Firebase
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// --- CONFIGURAÇÕES DE CONEXÃO ---
#define WIFI_SSID "consumo"
#define WIFI_PASSWORD "26803234"
#define API_KEY "AIzaSyA563ZWUCqu7RJLBa92TmMQ09eUCrD2SmQ"
#define DATABASE_URL "https://consumo-conciente-default-rtdb.firebaseio.com/"

// --- PINOS ---
#define I2C_SDA 21
#define I2C_SCL 22
#define PZEM_RX 16
#define PZEM_TX 17

// --- OBJETOS ---
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "a.st1.ntp.br", -10800);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- VARIÁVEIS DE CONTROLE ---
float consumo_inicial_dia = 0.0;
int dia_atual = -1;
bool signupOK = false;
bool base_recuperada = false;

// Controle de tempo independente
unsigned long ultimaVezFirebase = 0;
const unsigned long INTERVALO_FIREBASE = 900000; // 15 minutos em milissegundos

void setup() {
  Serial.begin(115200);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.print("Iniciando...");

  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  }
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  timeClient.begin();
  timeClient.update();

  // Recuperação de base para evitar zerar no reset
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = localtime((const time_t *)&epochTime);
  dia_atual = ptm->tm_mday;
  
  char dataHoje[11];
  sprintf(dataHoje, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  String pathReferencia = "leituras/" + String(dataHoje) + "/consumo_referencia_dia";

  lcd.clear();
  lcd.print("Sincronizando...");
  if (Firebase.RTDB.getFloat(&fbdo, pathReferencia)) {
    if (fbdo.dataType() == "float") {
      consumo_inicial_dia = fbdo.floatData();
      base_recuperada = true;
    }
  }

  lcd.clear();
  lcd.print("Sistema Pronto");
  delay(1000);
}

void loop() {
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = localtime((const time_t *)&epochTime);
  
  float voltage = pzem.voltage();
  float current = pzem.current();
  float power   = pzem.power();
  float energy_total = pzem.energy();

  if (isnan(voltage)) {
    lcd.setCursor(0, 0); lcd.print("Erro no PZEM    ");
  } else {
    // 1. Lógica de virada de dia e base de consumo
    if (ptm->tm_mday != dia_atual) {
      dia_atual = ptm->tm_mday;
      consumo_inicial_dia = energy_total;
      base_recuperada = false;
    }

    if (!base_recuperada) {
      char dataHoje[11];
      sprintf(dataHoje, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
      String pathRef = "leituras/" + String(dataHoje) + "/consumo_referencia_dia";
      if (Firebase.RTDB.setFloat(&fbdo, pathRef, energy_total)) {
        consumo_inicial_dia = energy_total;
        base_recuperada = true;
      }
    }

    float consumo_hoje = energy_total - consumo_inicial_dia;

    // 2. ATUALIZAÇÃO DO LCD (Sempre ocorre a cada ciclo do loop)
    lcd.setCursor(0, 0);
    lcd.printf("%.1fV %.2fA     ", voltage, current);
    lcd.setCursor(0, 1);
    lcd.printf("%.0fW H:%.3f  ", power, consumo_hoje);

    // 3. ENVIO PARA O FIREBASE (Apenas a cada 15 minutos)
    if (millis() - ultimaVezFirebase >= INTERVALO_FIREBASE) {
      if (Firebase.ready() && signupOK) {
        enviarDadosFirebase(voltage, current, power, energy_total, consumo_hoje, ptm);
        ultimaVezFirebase = millis(); // Reinicia o cronômetro
      }
    }
  }

  delay(2000); // O loop roda rápido (2 seg) para o LCD ser fluido
}

void enviarDadosFirebase(float v, float i, float p, float et, float eh, struct tm *ptm) {
  char dataStr[11];
  sprintf(dataStr, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  char horaStr[9];
  sprintf(horaStr, "%02d-%02d-%02d", ptm->tm_hour, ptm->tm_min, ptm->tm_sec);

  String path = "/leituras/" + String(dataStr) + "/" + String(horaStr);
  
  FirebaseJson json;
  json.add("voltagem", v);
  json.add("corrente", i);
  json.add("potencia", p);
  json.add("consumo_total_pzem", et);
  json.add("consumo_diario", eh);

  Serial.println("Enviando log de 15min ao Firebase...");
  Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json);
}