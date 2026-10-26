#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include "RTClib.h"

// =============================================================
// Configurações de Wi-Fi e Gateway
// =============================================================
#define WIFI_SSID       "SEU_WIFI"
#define WIFI_PASSWORD   "SUA_SENHA_WIFI"
#define GATEWAY_IP      "192.168.1.100"
#define GATEWAY_PORT    5000
#define GATEWAY_PATH    "/dados"
#define NODE_ID         "grupo_01"
#define WIFI_TIMEOUT_MS 10000
#define HTTP_TIMEOUT_MS 5000

// =============================================================
// Configurações de Hardware (Pinos do Heltec V3)
// =============================================================
#define I2C_SDA 1
#define I2C_SCL 2
#define SD_CS   3
#define SD_SCK  4
#define SD_MOSI 5
#define SD_MISO 6
#define ONE_WIRE_BUS 7
#define VEXT_CTRL 36 

// =============================================================
// Configurações de Energia (Deep Sleep)
// =============================================================
#define uS_TO_S_FACTOR 1000000ULL 
#define TIME_TO_SLEEP  900 // 900 segundos = 15 minutos

// Instâncias Globais
SPIClass spiSD(FSPI);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
RTC_DS3231 rtc; 

// =============================================================
// Funções de Comunicação Wi-Fi e HTTP
// =============================================================
bool ensureWiFiConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    Serial.println(F("[WiFi] Conectando..."));
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime >= WIFI_TIMEOUT_MS) {
            Serial.println(F("[WiFi] Timeout de conexao."));
            return false;
        }
        delay(100);
    }
    Serial.print(F("[WiFi] Conectado. IP: "));
    Serial.println(WiFi.localIP());
    return true;
}

bool sendMeasurement(const String& timestamp, float temperature, float ph, float turbidity) {
    if (!ensureWiFiConnection()) {
        Serial.println(F("[HTTP] Wi-Fi indisponivel."));
        return false;
    }

    const String url = String("http://") + GATEWAY_IP + ":" + String(GATEWAY_PORT) + GATEWAY_PATH;

    // Monta o JSON (ArduinoJson 7)
    JsonDocument doc;
    doc["node_id"] = NODE_ID;
    doc["timestamp"] = timestamp;

    if (isfinite(temperature)) doc["temperature"] = temperature;
    else doc["temperature"] = nullptr;

    if (isfinite(ph)) doc["ph"] = ph;
    else doc["ph"] = nullptr;

    if (isfinite(turbidity)) doc["turbidity"] = turbidity;
    else doc["turbidity"] = nullptr;

    String payload;
    serializeJson(doc, payload);

    Serial.println(F("[HTTP] Enviando medicao:"));
    Serial.println(payload);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(url)) {
        Serial.println(F("[HTTP] Falha ao iniciar conexao."));
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    const int httpCode = http.POST(payload);
    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
        const String response = http.getString();
        Serial.print(F("[HTTP] Resposta do Gateway: "));
        Serial.println(response);

        JsonDocument responseDoc;
        const DeserializationError error = deserializeJson(responseDoc, response);

        if (!error) {
            const char* status = responseDoc["status"] | "";
            if (strcmp(status, "ok") == 0) {
                success = true;
                Serial.println(F("[HTTP] Medicao confirmada pelo servidor."));
            } else {
                Serial.println(F("[HTTP] Servidor nao confirmou a medicao."));
            }
        } else {
            Serial.println(F("[HTTP] Resposta JSON invalida."));
        }
    } else if (httpCode > 0) {
        Serial.printf("[HTTP] Codigo de status inesperado: %d\n", httpCode);
    } else {
        Serial.print(F("[HTTP] Falha na requisicao: "));
        Serial.println(http.errorToString(httpCode));
    }

    http.end();
    return success;
}

// =============================================================
// Fluxo Principal (Executado a cada vez que acorda)
// =============================================================
void setup() {
    Serial.begin(115200);
    
    // 1. Liga o hardware interno (Cartão SD, Sensores, etc.)
    pinMode(VEXT_CTRL, OUTPUT);
    digitalWrite(VEXT_CTRL, LOW);
    delay(50); 

    // 2. Inicializa I2C e RTC
    Wire.begin(I2C_SDA, I2C_SCL);
    rtc.begin();

    // 3. Inicializa e lê o Sensor de Temperatura
    sensors.begin();
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    // 4. Lê o horário atual e formata o Timestamp em ISO8601
    DateTime now = rtc.now();
    char tsBuffer[25];
    snprintf(tsBuffer, sizeof(tsBuffer), "%04d-%02d-%02dT%02d:%02d:%02d", 
             now.year(), now.month(), now.day(), 
             now.hour(), now.minute(), now.second());
    String timestamp = String(tsBuffer);

    if (tempC != DEVICE_DISCONNECTED_C) {
        
        // --- GRAVAÇÃO LOCAL (Cartão SD) ---
        spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        if (SD.begin(SD_CS, spiSD)) {
            File dataFile = SD.open("/datalog.csv", FILE_APPEND);
            if (dataFile) {
                char sdBuffer[64];
                snprintf(sdBuffer, sizeof(sdBuffer), "%02d/%02d/%d,%02d:%02d:%02d,%.2f", 
                         now.day(), now.month(), now.year(), 
                         now.hour(), now.minute(), now.second(), tempC);
                dataFile.println(sdBuffer);
                dataFile.close();
                Serial.println(F("[APP] Dado gravado no SD."));
            }
            SD.end(); 
        }

        // --- ENVIO REMOTO (Gateway Raspberry) ---
        // Passamos tempC real, e NAN para pH e turbidez que você não possui
        bool sent = sendMeasurement(timestamp, tempC, NAN, NAN);
        if (sent) {
            Serial.println(F("[APP] Sucesso total no ciclo!"));
        } else {
            Serial.println(F("[APP] Falha ao enviar para o Raspberry Pi."));
        }
    } else {
        Serial.println(F("[APP] Erro de leitura no sensor DS18B20."));
    }

    // 5. Prepara para dormir
    Serial.println(F("[APP] Desligando Wi-Fi e entrando em Deep Sleep por 15 minutos..."));
    Serial.flush(); 
    
    // Desliga Wi-Fi, desliga pinos físicos e dorme
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    digitalWrite(VEXT_CTRL, HIGH); 
    
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

void loop() {
    // Vazio devido ao Deep Sleep
}