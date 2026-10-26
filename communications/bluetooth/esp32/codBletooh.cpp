#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include "RTClib.h"

// --- Inclusão do módulo de comunicação BLE do professor ---
#include "ICC316Bluetooth.h" 

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

void setup() {
    Serial.begin(115200);
    
    // Liga o hardware interno (Cartão SD e Sensores)
    pinMode(VEXT_CTRL, OUTPUT);
    digitalWrite(VEXT_CTRL, LOW);
    delay(50); 

    // Inicializa I2C e RTC
    Wire.begin(I2C_SDA, I2C_SCL);
    rtc.begin();

    // Inicializa e lê o Sensor de Temperatura
    sensors.begin();
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    // Lê o horário atual e formata o Timestamp em ISO8601
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

        // --- ENVIO REMOTO (Gateway Bluetooth) ---
        Serial.println(F("[BLE] Aguardando Raspberry Pi conectar..."));
        
        // Chama a função do professor. Se o Raspberry demorar mais que o Timeout definido
        // em config.h (30 segundos por padrão), a função retorna false e o ESP32 dorme.
        bool sent = sendMeasurement(timestamp, tempC, NAN, NAN);
        
        if (sent) {
            Serial.println(F("[BLE] Sucesso! Raspberry Pi coletou os dados e enviou o ACK."));
        } else {
            Serial.println(F("[BLE] Timeout ou Falha. O Raspberry Pi não conectou a tempo."));
        }
    } else {
        Serial.println(F("[APP] Erro de leitura no sensor DS18B20."));
    }

    // Desliga pinos físicos e dorme
    Serial.println(F("[APP] Entrando em Deep Sleep por 15 minutos..."));
    Serial.flush(); 
    digitalWrite(VEXT_CTRL, HIGH); 
    
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

void loop() {
}