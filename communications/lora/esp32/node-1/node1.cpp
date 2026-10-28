#include <Arduino.h>
#include <RadioLib.h>
#include <ArduinoJson.h>

// =============================================================
// CONFIGURACAO DO LORA - HELTEC WIFI LORA 32 V3 (SX1262)
// =============================================================

#define LORA_CS_PIN    8
#define LORA_DIO1_PIN  14
#define LORA_RST_PIN   12
#define LORA_BUSY_PIN  13

#define LORA_FREQUENCY_MHZ 915.0

// =============================================================
// OBJETO DO RADIO
// =============================================================

SX1262 radio = new Module(
    LORA_CS_PIN,
    LORA_DIO1_PIN,
    LORA_RST_PIN,
    LORA_BUSY_PIN
);

// =============================================================
// CONFIGURACAO DO ENVIO
// =============================================================

#define TRANSMISSION_INTERVAL_MS 15000

unsigned long ultimoEnvio = 0;
unsigned long contador = 0;

// =============================================================
// FUNCAO PARA OBTER TIMESTAMP
// =============================================================

// IMPORTANTE:
// Este timestamp e um exemplo.
// Substitua por um horario real obtido de um RTC ou de outra
// fonte de tempo, caso precise registrar a data e hora exatas.

String obterTimestamp()
{
    return "2026-10-26T14:45:00";
}

// =============================================================
// FUNCAO PARA LER OS SENSORES
// =============================================================

// Substitua os valores de exemplo pelas leituras reais
// dos sensores de qualidade da agua.

float lerTemperatura()
{
    return 28.5;
}

float lerPH()
{
    return 7.2;
}

float lerTurbidez()
{
    return 12.4;
}

// =============================================================
// MONTAR E TRANSMITIR OS DADOS VIA LORA
// =============================================================

void transmitirDados()
{
    // 1. Obter dados dos sensores

    const float temperatura = lerTemperatura();
    const float ph = lerPH();
    const float turbidez = lerTurbidez();

    const String timestamp = obterTimestamp();

    // 2. Criar documento JSON

    JsonDocument doc;

    doc["timestamp"] = timestamp;
    doc["temperature"] = temperatura;
    doc["ph"] = ph;
    doc["turbidity"] = turbidez;

    // 3. Converter JSON para String

    String mensagem;

    serializeJson(doc, mensagem);

    // 4. Exibir os dados no Monitor Serial

    Serial.println();
    Serial.println("================================");
    Serial.println("[TX] Preparando dados para envio");

    Serial.print("Contador: ");
    Serial.println(contador);

    Serial.print("Timestamp: ");
    Serial.println(timestamp);

    Serial.print("Temperatura: ");
    Serial.println(temperatura);

    Serial.print("pH: ");
    Serial.println(ph);

    Serial.print("Turbidez: ");
    Serial.println(turbidez);

    Serial.println();
    Serial.println("[LoRa] Enviando mensagem:");

    Serial.println(mensagem);

    // 5. Transmitir mensagem via LoRa

    const int16_t estado = radio.transmit(mensagem);

    // 6. Verificar resultado

    if (estado == RADIOLIB_ERR_NONE) {

        Serial.println("[LoRa] Transmissao concluida!");

        contador++;

    } else {

        Serial.print("[LoRa] Erro na transmissao: ");
        Serial.println(estado);
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
    Serial.println("ESP32 TRANSMISSOR LORA");
    Serial.println("MONITORAMENTO DA QUALIDADE DA AGUA");
    Serial.println("================================");

    // Inicializar radio SX1262

    Serial.println("[LoRa] Inicializando SX1262...");

    const int16_t estado = radio.begin(
        LORA_FREQUENCY_MHZ,
        125.0,  // Largura de banda em kHz
        7,      // Spreading factor
        5,      // Coding rate 4/5
        0x12,   // Sync word
        14,     // Potencia de transmissao em dBm
        8,      // Comprimento do preambulo
        1.8     // Tensao do TCXO
    );

    if (estado != RADIOLIB_ERR_NONE) {

        Serial.print("[LoRa] Erro ao inicializar: ");
        Serial.println(estado);

        while (true) {
            delay(1000);
        }
    }

    Serial.println("[LoRa] SX1262 iniciado com sucesso!");

    Serial.println("[TX] Transmissor pronto.");
    Serial.println("[TX] Envio a cada 15 segundos.");
}

// =============================================================
// LOOP
// =============================================================

void loop()
{
    const unsigned long agora = millis();

    if (agora - ultimoEnvio >= TRANSMISSION_INTERVAL_MS) {

        ultimoEnvio = agora;

        transmitirDados();
    }
}
