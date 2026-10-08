# Protocolo serial nó ↔ aplicativo (v1)

Referência para o firmware (`App/`) e para os aplicativos Beacon e Sniffer.
As constantes ficam em `App/Inc/proto_defs.h` e `App/Inc/proto_msgs.h`. Mantenha os três em sincronia.

- **Enlace:** UART 115200 bps, 8N1, sem controle de fluxo (CH340X da placa E77-900MBL-01).
- **Inteiros:** todos em **big-endian**. Os sinais (`i8`, `i16`, `i32`) estão em complemento de dois.
- **Implementação de referência:** `tools/node_cli.py`, em Python, emula o aplicativo.

## 1. Enquadramento

```
+------+------+-----+-----------------+-----------+
| SOF  | type | len | payload[len]    | crc16     |
| 0x7E | u8   | u8  | 0..250 bytes    | u16 (BE)  |
+------+------+-----+-----------------+-----------+
```

- **CRC-16/CCITT-FALSE:** polinômio 0x1021, valor inicial 0xFFFF, sem reflexão e sem XOR final. É calculado sobre `type`, `len` e `payload`. Vetor de teste: `"123456789"` → `0x29B1`.
- **Sem byte stuffing:** `0x7E` pode aparecer dentro do quadro. O receptor se ressincroniza pelo CRC:
  - quando um quadro falha (CRC errado ou `len > 250`), volta a procurar `0x7E` a partir do byte seguinte ao SOF que falhou;
  - se passar mais de 100 ms sem bytes no meio de um quadro, os bytes pendentes são reavaliados e o restante é descartado.
- **Recomendação ao aplicativo:**
  - enviar cada quadro de uma vez, sem pausas no meio;
  - aguardar o STATUS de confirmação, com timeout de 500 ms;
  - repetir até 3 vezes em caso de timeout.

## 2. Tipos de quadro

| Código | Nome | Sentido | Resposta |
|---|---|---|---|
| `0x01` | CONFIG | app → nó | STATUS |
| `0x02` | ALERT_START | app → nó | STATUS |
| `0x03` | UPDATE | app → nó | STATUS |
| `0x04` | ALERT_STOP | app → nó | STATUS |
| `0x05` | GET_STATUS | app → nó | STATUS |
| `0x0F` | ENTER_BOOTLOADER | app → nó | STATUS, e em seguida o nó entra no bootloader da ST |
| `0x81` | RX_ALERT | nó → app | — |
| `0x82` | STATUS | nó → app | — |
| `0x83` | LOG | nó → app | — (somente em build Debug) |

Todo quadro app → nó recebe exatamente **um** STATUS com `ack_type` igual ao tipo recebido. Um tipo desconhecido recebe `result = E_TYPE`.

### 2.1 CONFIG (`0x01`), 15 bytes

| Offset | Campo | Tipo | Faixa válida |
|---|---|---|---|
| 0 | `proto_ver` | u8 | `1` |
| 1 | `role` | u8 | `1` = Beacon, `2` = Sniffer |
| 2 | `user_id` | u32 | ≠ 0 no papel Beacon (ignorado no Sniffer) |
| 6 | `interval_s` | u16 | 30–3600 no Beacon (padrão 300) |
| 8 | `sf` | u8 | 7–12 |
| 9 | `bw` | u8 | `0` = 125 kHz, `1` = 250 kHz, `2` = 500 kHz |
| 10 | `cr` | u8 | 1 (4/5) – 4 (4/8) |
| 11 | `tx_power` | i8 | −9 a 22 dBm |
| 12 | `preamble` | u16 | 6–1024 símbolos |
| 14 | `hop` | u8 | `0` (salto de frequência ainda não disponível) |

- Payloads maiores que 15 bytes são aceitos, e os bytes extras são ignorados. Isso permite compatibilidade com versões futuras do aplicativo.
- A configuração é recusada com `E_TOA` se o cabeçalho fixo do pacote de rádio (17 B) passar de 400 ms no ar. Com BW de 125 kHz, isso acontece em SF11 e SF12.
- Se aceita, a configuração é gravada na flash e passa a valer imediatamente.
- O STATUS de resposta traz `max_msg_len`, o maior texto livre aceito com essa configuração. Exemplos com BW 125 kHz, CR 4/5 e preâmbulo 8: **49 B em SF9**, **7 B em SF10**.

Exemplo: Beacon, `user_id 0x0A1B2C3D`, 300 s, SF9/125 kHz, 4/5, 14 dBm, preâmbulo 8.
```
7E 01 0F 01 01 0A 1B 2C 3D 01 2C 09 00 01 0E 00 08 00 84 F3
```

### 2.2 ALERT_START (`0x02`) e UPDATE (`0x03`), 10 + N bytes

| Offset | Campo | Tipo | Observação |
|---|---|---|---|
| 0 | `lat` | i32 | graus × 10⁷, de −90° a +90° |
| 4 | `lon` | i32 | graus × 10⁷, de −180° a +180° |
| 8 | `status` | u8 | códigos da Seção 4 |
| 9 | `battery` | u8 | 0–100 %, ou `0xFF` = desconhecido |
| 10 | `msg` | N bytes | UTF-8, N ≤ `max_msg_len` (senão `E_TOA`) |

- Os dois quadros carregam o **estado completo** e podem ser repetidos sem efeito colateral.
- **ALERT_START** só é aceito no papel Beacon (senão `E_STATE`). Ativa o alerta e transmite imediatamente.
- **UPDATE**:
  - com alerta ativo, atualiza o estado; se `status` ou `msg` mudarem, transmite imediatamente;
  - com o alerta inativo, apenas guarda o estado, para que a próxima ativação já tenha a posição;
  - só posição ou bateria não disparam transmissão; o valor novo vai no próximo ciclo.
- Entre duas transmissões há um intervalo mínimo de 10 s. Uma transmissão imediata pedida antes disso é adiada até o fim desse intervalo.
- Bytes UTF-8 inválidos na mensagem são substituídos por `?`.

### 2.3 ALERT_STOP (`0x04`), vazio

Transmite um último pacote com `status = 0` (em segurança) e encerra o alerta. Sem alerta ativo, não faz nada e responde `OK`.

### 2.4 GET_STATUS (`0x05`), vazio

Apenas pede um STATUS.

### 2.5 ENTER_BOOTLOADER (`0x0F`), 4 bytes: `"BOOT"`

- O nó responde STATUS `OK` e salta para o bootloader de sistema da ST, que grava via UART.
- Exemplo: `7E 0F 04 42 4F 4F 54 F7 FE`.
- O `tools/flash_uart.sh` envia este quadro automaticamente antes de gravar.

### 2.6 RX_ALERT (`0x81`), 19 + N bytes, papel Sniffer

| Offset | Campo | Tipo |
|---|---|---|
| 0 | `user_id` | u32 |
| 4 | `seq` | u16 |
| 6 | `lat` | i32 |
| 10 | `lon` | i32 |
| 14 | `status` | u8 |
| 15 | `battery` | u8 |
| 16 | `rssi` | i16 (dBm) |
| 18 | `snr` | i8 (dB) |
| 19 | `msg` | N bytes (UTF-8 já saneado, N ≤ 231) |

Só chegam ao aplicativo pacotes da porta de alerta com coordenadas válidas. Repetições do mesmo `(user_id, seq)` dentro de 10 minutos são descartadas.

### 2.7 STATUS (`0x82`), 14 bytes

| Offset | Campo | Tipo | Observação |
|---|---|---|---|
| 0 | `ack_type` | u8 | tipo do quadro confirmado, ou `0x00` = evento espontâneo |
| 1 | `result` | u8 | Seção 3 |
| 2 | `state` | u8 | `0` WAIT_CONFIG, `1` BEACON_IDLE, `2` BEACON_ALERT, `3` SNIFFER_RX |
| 3 | `role` | u8 | `0` nenhum, `1` Beacon, `2` Sniffer |
| 4 | `fw_ver` | 3 × u8 | major, minor, patch |
| 7 | `proto_ver` | u8 | `1` |
| 8 | `max_msg_len` | u8 | texto máximo com a configuração atual |
| 9 | `last_seq` | u16 | `seq` do último pacote transmitido |
| 11 | `err_flags` | u16 | Seção 3, acumulados desde o último reset |
| 13 | `reset_cause` | u8 | `0` desconhecida, `1` power-on, `2` pino, `3` software, `4` IWDG, `5` WWDG, `6` low-power, `7` brown-out |

**STATUS espontâneos (`ack_type = 0`):**
- **Warmup:** a cada 1 s em `WAIT_CONFIG`. Nos outros estados, a cada 1 s durante os primeiros 5 s após o boot. Param quando chega o primeiro quadro válido do aplicativo.
- **Após cada transmissão do Beacon:**
  - `result = OK` e `last_seq` atualizado, se a transmissão foi concluída;
  - `result = E_RADIO` se ela falhou.
- **Mudanças de estado iniciadas pelo próprio nó**, como a retomada de um alerta após um reset.

### 2.8 LOG (`0x83`), texto ASCII

Mensagens de depuração, emitidas só em build Debug. O aplicativo deve ignorá-las.

## 3. Códigos

**`result`:**

| Código | Nome | Significado |
|---|---|---|
| 0 | `OK` | comando aceito |
| 1 | `E_CRC` | (reservado) |
| 2 | `E_LEN` | payload curto ou longo demais |
| 3 | `E_PARAM` | campo fora da faixa |
| 4 | `E_STATE` | comando inválido no estado atual |
| 5 | `E_TOA` | pacote passaria de 400 ms no ar |
| 6 | `E_FLASH` | falha ao gravar a configuração |
| 7 | `E_TYPE` | tipo de quadro desconhecido |
| 8 | `E_BUSY` | nó ocupado; tente novamente |
| 9 | `E_RADIO` | falha de transmissão no rádio |

**`err_flags` (bits):**

| Bit | Significado |
|---|---|
| 0 | quadro serial corrompido recebido |
| 1 | estouro do buffer de RX da UART |
| 2 | configuração na flash inválida |
| 3 | falha de escrita na flash |
| 4 | timeout de TX no rádio |
| 5 | rádio reinicializado após erros |
| 6 | transmissão feita com o canal ocupado, depois de esgotar as tentativas de CAD |
| 7 | quadro descartado por fila de TX serial cheia |
| 8 | cristal LSE (32,768 kHz) não oscilou; RTC rodando no LSI |

## 4. Pacote de rádio (porta de alerta)

```
port u8 | user_id u32 | seq u16 | lat i32 | lon i32 | status u8 | battery u8 | msg[0..N]
```

- **Cabeçalho:** 17 bytes. O comprimento da mensagem vem do cabeçalho explícito do LoRa.
- **Portas:** `0x01` = alerta, `0x7F` = teste; as demais são reservadas.
- **`status`:**

  | Código | Estado |
  |---|---|
  | 0 | Em segurança (alerta encerrado) |
  | 1 | Perigo |
  | 2 | Perdido |
  | 3 | Ferido |
  | 4 | Incêndio próximo |
  | 5 | Ilhado |
  | 6 | Sem água ou suprimentos |

  Códigos novos podem ser criados sem mudar o formato do pacote.
- **`seq`:**
  - incrementa a cada transmissão;
  - é persistido em blocos de 16, e após um reset recomeça à frente do último valor usado;
  - dá a volta em 65535 → 0.

## 5. Comportamento do nó

| Situação | Comportamento |
|---|---|
| Boot sem configuração gravada | `WAIT_CONFIG`, rádio desligado, STATUS a cada 1 s |
| Boot com papel Sniffer gravado | `SNIFFER_RX` |
| Boot com papel Beacon gravado | `BEACON_IDLE`; se havia alerta ativo, `BEACON_ALERT`, retomado com a última posição gravada |
| CONFIG mudando de papel com alerta ativo | o alerta é encerrado, sem transmitir status 0 |
| Beacon com alerta ativo | transmite logo ao ativar e depois a cada `interval_s` ± 10 % (jitter do TRNG); antes de cada transmissão faz CAD, com até 5 adiamentos aleatórios de 50 a 500 ms |
| Sniffer | RX contínuo (modo boosted), pacotes validados → RX_ALERT |

O aplicativo deve sempre enviar CONFIG ao conectar, mesmo que o nó já esteja configurado. O papel gravado serve só para o nó continuar funcionando se reiniciar sem o aplicativo.
