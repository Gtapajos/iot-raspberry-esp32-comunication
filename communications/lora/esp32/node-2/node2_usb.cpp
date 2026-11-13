#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <math.h>
#include <string.h>

// =============================================================
// CONFIGURACAO GERAL
// =============================================================

#define NODE_ID "grupo_02"

#define SERIAL_BAUDRATE 115200

#define USB_ACK_TIMEOUT_MS 3000
#define USB_MAX_RETRIES 3

// =============================================================
// CONFIGURACAO DO LORA
// HELTEC WIFI LORA 32 V3 - SX1262
// =============================================================

#define LORA_CS_PIN     8
#define LORA_DIO1_PIN   14
#define LORA_RST_PIN    12
#define LORA_BUSY_PIN   13

#define LORA_FREQUENCY_MHZ 915.0

SX1262 radio = new Module(
    LORA_CS_PIN,
    LORA_DIO1_PIN,
    LORA_RST_PIN,
    LORA_BUSY_PIN
);

// =============================================================
// COMUNICACAO USB
// =============================================================

// Envia JSON ao Raspberry Pi e aguarda ACK.

bool sendMeasurement(
    const String& timestamp,
    float temperature,
    float ph,
    float turbidity,
    float rssi
)
{
    // Criar documento JSON

    JsonDocument doc;

    doc["node_id"] = NODE_ID;
    doc["timestamp"] = timestamp;
    doc["rssi"] = rssi;

    if (isfinite(temperature)) {
        doc["temperature"] = temperature;
    } else {
        doc["temperature"] = nullptr;
    }

    if (isfinite(ph)) {
        doc["ph"] = ph;
    } else {
        doc["ph"] = nullptr;
    }

    if (isfinite(turbidity)) {
        doc["turbidity"] = turbidity;
    } else {
        doc["turbidity"] = nullptr;
    }

    // Serializar JSON

    String payload;

    serializeJson(doc, payload);

    // Tentar enviar ate o limite de tentativas

    for (int tentativa = 1;
         tentativa <= USB_MAX_RETRIES;
         tentativa++) {

        // Limpar dados antigos da recepcao serial

        while (Serial.available()) {
            Serial.read();
        }

        // Enviar JSON ao Raspberry Pi

        Serial.println(payload);
        Serial.flush();

        // Aguardar confirmacao ACK

        const unsigned long inicio = millis();

        String resposta;

        while (millis() - inicio < USB_ACK_TIMEOUT_MS) {

            while (Serial.available()) {

                const char caractere = Serial.read();

                if (caractere == '\n') {

                    resposta.trim();

                    if (resposta == "ACK") {

                        return true;
                    }

                    resposta = "";

                } else if (caractere != '\r') {

                    resposta += caractere;
                }
            }

            delay(5);
        }

        // Timeout

        if (tentativa < USB_MAX_RETRIES) {
            delay(200);
        }
    }

    return false;
}

// =============================================================
// PROCESSAR MENSAGEM RECEBIDA VIA LORA
// =============================================================

void processLoRaMessage(const String& mensagem, float rssi)
{
    // Interpretar JSON recebido via LoRa

    JsonDocument doc;

    const DeserializationError error =
        deserializeJson(doc, mensagem);

    if (error) {
        return;
    }

    // Verificar campos obrigatorios

    if (doc["timestamp"].isNull() ||
        doc["temperature"].isNull() ||
        doc["ph"].isNull() ||
        doc["turbidity"].isNull()) {

        return;
    }

    // Extrair timestamp

    const char* timestampReceived =
        doc["timestamp"].as<const char*>();

    if (timestampReceived == nullptr ||
        strlen(timestampReceived) == 0) {

        return;
    }

    const String timestamp = timestampReceived;

    // Extrair valores dos sensores

    const float temperature =
        doc["temperature"].as<float>();

    const float ph =
        doc["ph"].as<float>();

    const float turbidity =
        doc["turbidity"].as<float>();

    // Validar valores

    if (!isfinite(temperature) ||
        !isfinite(ph) ||
        !isfinite(turbidity)) {

        return;
    }

    // Encaminhar dados ao Raspberry Pi

    const bool sent = sendMeasurement(
        timestamp,
        temperature,
        ph,
        turbidity,
        rssi
    );

    // Registrar resultado sem enviar texto pela USB

    if (sent) {
        // Dados confirmados pelo Raspberry Pi.
    } else {
        // Falha na comunicacao USB.
    }
}

// =============================================================
// SETUP
// =============================================================

void setup()
{
    // Inicializar comunicacao USB

    Serial.begin(SERIAL_BAUDRATE);

    delay(1000);

    // Inicializar LoRa

    const int16_t estado = radio.begin(
        LORA_FREQUENCY_MHZ,
        125.0,
        7,
        5,
        0x12,
        14,
        8,
        1.8
    );

    if (estado != RADIOLIB_ERR_NONE) {

        while (true) {
            delay(1000);
        }
    }

    // Sistema pronto
}

// =============================================================
// LOOP
// =============================================================

void loop()
{
    String mensagem;

    // Aguardar mensagem LoRa

    const int16_t estado = radio.receive(mensagem);

    if (estado == RADIOLIB_ERR_NONE) {

        // Processar mensagem recebida

        processLoRaMessage(mensagem, radio.getRSSI());

    } else if (estado != RADIOLIB_ERR_RX_TIMEOUT) {

        // Erro na recepcao LoRa
    }
}
