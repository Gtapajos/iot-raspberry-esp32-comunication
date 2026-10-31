#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <math.h>
#include <string.h>

// =============================================================
// CONFIGURACAO DO WIFI
// =============================================================

#define WIFI_SSID       "WiFi_SSID"
#define WIFI_PASSWORD   "PASSWORD"

// =============================================================
// CONFIGURACAO DO SERVIDOR
// =============================================================

#define GATEWAY_IP      "10.208.3.19"
#define GATEWAY_PORT    5000
#define GATEWAY_PATH    "/dados"

#define NODE_ID         "grupo_01"

// Timeouts
#define WIFI_TIMEOUT_MS 10000
#define HTTP_TIMEOUT_MS 5000

// =============================================================
// CONFIGURACAO DO LORA - HELTEC WIFI LORA 32 V3 (SX1262)
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
// CONECTAR AO WIFI
// =============================================================

bool ensureWiFiConnection()
{
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    Serial.println("[WiFi] Conectando...");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long startTime = millis();

    while (WiFi.status() != WL_CONNECTED) {

        if (millis() - startTime >= WIFI_TIMEOUT_MS) {
            Serial.println("[WiFi] Tempo limite excedido.");
            return false;
        }

        delay(100);
    }

    Serial.print("[WiFi] Conectado. IP: ");
    Serial.println(WiFi.localIP());

    return true;
}

// =============================================================
// ENVIAR MEDICAO PARA O SERVIDOR
// =============================================================

bool sendMeasurement(
    const String& timestamp,
    float temperature,
    float ph,
    float turbidity
)
{
    // 1. Verificar conexao Wi-Fi

    if (!ensureWiFiConnection()) {
        Serial.println("[HTTP] Wi-Fi indisponivel.");
        return false;
    }

    // 2. Montar URL do servidor

    const String url =
        String("http://") +
        GATEWAY_IP + ":" +
        String(GATEWAY_PORT) +
        GATEWAY_PATH;

    // 3. Criar documento JSON

    JsonDocument doc;

    doc["node_id"] = NODE_ID;
    doc["timestamp"] = timestamp;

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

    // 4. Serializar JSON

    String payload;
    serializeJson(doc, payload);

    Serial.println("[HTTP] Enviando medicao:");
    Serial.println(payload);

    // 5. Inicializar cliente HTTP

    HTTPClient http;

    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(url)) {
        Serial.println("[HTTP] Falha ao iniciar conexao.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // 6. Enviar POST

    const int httpCode = http.POST(payload);

    bool success = false;

    // 7. Verificar resposta do servidor

    if (httpCode == HTTP_CODE_OK) {

        const String response = http.getString();

        Serial.print("[HTTP] Resposta do servidor: ");
        Serial.println(response);

        JsonDocument responseDoc;

        const DeserializationError error =
            deserializeJson(responseDoc, response);

        if (!error) {

            const char* status = responseDoc["status"] | "";

            if (strcmp(status, "ok") == 0) {

                success = true;

                Serial.println(
                    "[HTTP] Medicao confirmada pelo servidor."
                );

            } else {

                Serial.println(
                    "[HTTP] Servidor nao confirmou a medicao."
                );
            }

        } else {

            Serial.println("[HTTP] Resposta JSON invalida.");
        }

    } else if (httpCode > 0) {

        Serial.print("[HTTP] Codigo HTTP inesperado: ");
        Serial.println(httpCode);

    } else {

        Serial.print("[HTTP] Erro na requisicao: ");
        Serial.println(http.errorToString(httpCode));
    }

    // 8. Liberar recursos

    http.end();

    return success;
}

// =============================================================
// PROCESSAR MENSAGEM RECEBIDA VIA LORA
// =============================================================

void processLoRaMessage(const String& mensagem)
{
    Serial.println();
    Serial.println("================================");
    Serial.println("[LoRa] Mensagem recebida:");
    Serial.println(mensagem);

    // Criar documento JSON para interpretar a mensagem

    JsonDocument doc;

    const DeserializationError error =
        deserializeJson(doc, mensagem);

    if (error) {
        Serial.print("[LoRa] Erro ao interpretar JSON: ");
        Serial.println(error.c_str());
        Serial.println("[LoRa] Mensagem descartada.");
        return;
    }

    // Verificar se os campos obrigatorios existem

    if (doc["timestamp"].isNull() ||
        doc["temperature"].isNull() ||
        doc["ph"].isNull() ||
        doc["turbidity"].isNull()) {

        Serial.println(
            "[LoRa] Erro: campo obrigatorio ausente ou nulo."
        );

        Serial.println("[LoRa] Mensagem descartada.");
        return;
    }

    // Extrair os dados recebidos

    const char* timestampReceived =
        doc["timestamp"].as<const char*>();

    if (timestampReceived == nullptr ||
        strlen(timestampReceived) == 0) {

        Serial.println("[LoRa] Timestamp invalido.");
        return;
    }

    const String timestamp = timestampReceived;

    const float temperature =
        doc["temperature"].as<float>();

    const float ph =
        doc["ph"].as<float>();

    const float turbidity =
        doc["turbidity"].as<float>();

    // Validar os valores numericos

    if (!isfinite(temperature) ||
        !isfinite(ph) ||
        !isfinite(turbidity)) {

        Serial.println("[LoRa] Valores numericos invalidos.");
        return;
    }

    // Exibir os dados interpretados

    Serial.println();
    Serial.println("[LoRa] Dados interpretados:");

    Serial.print("Timestamp: ");
    Serial.println(timestamp);

    Serial.print("Temperatura: ");
    Serial.println(temperature);

    Serial.print("pH: ");
    Serial.println(ph);

    Serial.print("Turbidez: ");
    Serial.println(turbidity);

    // Enviar os dados recebidos para o servidor

    Serial.println();
    Serial.println("[Gateway] Encaminhando dados para o servidor...");

    const bool sent = sendMeasurement(
        timestamp,
        temperature,
        ph,
        turbidity
    );

    if (sent) {

        Serial.println(
            "[Gateway] Dados enviados com sucesso!"
        );

    } else {

        Serial.println(
            "[Gateway] Falha ao enviar os dados para o servidor."
        );
    }

    Serial.println("================================");
}

// =============================================================
// SETUP
// =============================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32 - GATEWAY LORA PARA WIFI");
    Serial.println("================================");

    // Inicializar LoRa

    Serial.println("[LoRa] Inicializando SX1262...");

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

        Serial.print("[LoRa] Erro ao inicializar: ");
        Serial.println(estado);

        while (true) {
            delay(1000);
        }
    }

    Serial.println("[LoRa] SX1262 inicializado com sucesso!");

    // Inicializar Wi-Fi

    ensureWiFiConnection();

    Serial.println();
    Serial.println("[Gateway] Sistema pronto.");
    Serial.println("[Gateway] Aguardando dados via LoRa...");
}

// =============================================================
// LOOP
// =============================================================

void loop()
{
    String mensagem;

    // Aguardar uma mensagem LoRa

    const int16_t estado = radio.receive(mensagem);

    if (estado == RADIOLIB_ERR_NONE) {

        // Mostrar qualidade do sinal

        Serial.print("[LoRa] RSSI: ");
        Serial.print(radio.getRSSI());
        Serial.println(" dBm");

        Serial.print("[LoRa] SNR: ");
        Serial.print(radio.getSNR());
        Serial.println(" dB");

        // Processar e encaminhar a mensagem

        processLoRaMessage(mensagem);

    } else if (estado != RADIOLIB_ERR_RX_TIMEOUT) {

        Serial.print("[LoRa] Erro na recepcao: ");
        Serial.println(estado);
    }
}
 