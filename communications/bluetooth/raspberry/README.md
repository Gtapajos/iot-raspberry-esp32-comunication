# Gateway Bluetooth BLE no Raspberry Pi

## Instalação e execução

Instale as dependências no Raspberry Pi:

```bash
sudo apt update
sudo apt install bluez python3-bleak
```

Use os arquivos `gateway_bluetooth.py` e `config.py` desta pasta do repositório.
Confirme o adaptador Bluetooth com `bluetoothctl list` e ajuste
`BLUETOOTH_INTERFACE` em `config.py`, se necessário. Depois, execute:

```bash
python3 gateway_bluetooth.py
```

O gateway procura continuamente dispositivos cujo nome comece com
`ICC316-G`, conecta-se ao ESP32 e grava cada medição válida antes de enviar o
ACK `OK`. O ESP32 e o gateway devem usar os mesmos UUIDs.

## Detecção de mensagens não recebidas

Depois da primeira medição válida de cada nó, o gateway continua procurando
novas transmissões. Por padrão, espera-se uma medição a cada 15 segundos, com
5 segundos de tolerância. Ajuste `EXPECTED_INTERVAL_S` e `MISSED_GRACE_S` em
`config.py` para que correspondam ao firmware gravado no ESP32. Se o prazo
expirar, uma linha com `status=NOT_RECEIVED` é acrescentada ao CSV, com o
horário esperado e os campos de medição vazios. O monitoramento de cada nó só
começa depois de sua primeira medição e não continua enquanto o processo do
gateway estiver parado.

O arquivo `gateway.csv` tem as colunas:

```text
node_id,timestamp,temperature,ph,turbidity,status
```

Medições recebidas têm `status=RECEIVED`; valores `ph` ou `turbidity` nulos
são gravados como `NA`. Um CSV preexistente com o formato antigo é atualizado
automaticamente e os registros existentes recebem `status=RECEIVED`.
