## Build/Flash:
### Build:
Para buildar o projeto em modo rode o comando: `cmake --build --preset <Debug | Release>`  
O arquivo de firmware **.bin** será gerado em `build/Debug/` ou `build/Release/`


O modo *Release* é mais dificil para debugging, mas é mais otimizado.

### Flashing pelo CMake:
Ao usar a tag `--target flash` no comando de build do CMake, uma conexão serial vai ser aberta usando *STM32_Programmer_CLI* e o novo firmware será gravado no módulo automaticamente.

Opções (passadas no configure, ex.: `cmake --preset Debug -DFLASH_PORT=/dev/ttyUSB1 -DFLASH_PROMPT=OFF`):
- `FLASH_PORT`: porta serial (padrão `/dev/ttyUSB0`).
- `FLASH_BAUD`: baud rate do bootloader (padrão `115200`).
- `FLASH_PROMPT`: pergunta a porta antes de gravar (padrão `ON`). Use `OFF` em IDEs sem terminal interativo, como o CLion.
- `FLASH_KILL_ALL`: encerra processos usando qualquer `/dev/ttyUSB*`/`/dev/ttyACM*` (padrão `OFF`). Desligado, só a porta escolhida é liberada.
- `FLASH_REQUEST_BOOT`: pede ao firmware em execução que entre no bootloader antes de gravar (padrão `ON`).

### Flashing pelo STM32_Programmer_CLI:
Se quiser somente dar flash na placa sem buildar, rode:  
```bash
STM32_Programmer_CLI \
        --connect port=<porta> br=115200 \
        --write build/Debug/STM32-LoRa.bin 0x08000000 \
        --verify --go 0x08000000
```

Substitua `<porta>` pela porta de comunicação a qual a placa está conectada. Exemplo de porta no linx: `/dev/ttyUSB0`

### Bootmode:
O firmware em execução ocupa a USART2, a mesma porta usada pelo bootloader da ST. Para gravar uma nova versão, o `--target flash` envia antes o quadro `ENTER_BOOTLOADER` (ver `docs/protocolo_serial.md`). O nó responde, reinicia e salta para o bootloader. Nenhum passo manual é necessário.

- Placa virgem, ou já parada no bootloader: o quadro pode atrapalhar a detecção de baud do bootloader. Nesse caso, resete a placa e grave com `-DFLASH_REQUEST_BOOT=OFF`.
- Manualmente: `tools/node_cli.py -p /dev/ttyUSB0 bootloader` e depois o comando do *STM32_Programmer_CLI*.

## Firmware Beacon/Sniffer
O mesmo firmware atende os dois papéis do TCC. O papel é definido pelo quadro `CONFIG` que o aplicativo envia ao conectar.

- **Protocolo:** `docs/protocolo_serial.md` (quadros, códigos e comportamento).
- **Código da aplicação:** `App/`, fora dos diretórios gerados pelo CubeMX:
  - `serial_link`: UART por DMA e enquadramento;
  - `node_app`: máquina de estados;
  - `radio_link`: rádio, com CAD antes de cada TX;
  - `nvm_store`: configuração gravada na última página da flash;
  - `supervisor`: watchdog.
- **Warmup:** ao ligar sem configuração gravada, o nó fica em `WAIT_CONFIG` e envia um `STATUS` por segundo até o aplicativo falar. Com configuração gravada, volta no último papel e retoma um alerta que estivesse ativo.

### Ferramenta de bancada (emula o aplicativo)
Requer pyserial: `python -m venv .venv && .venv/bin/pip install pyserial` (depois use `.venv/bin/python tools/node_cli.py ...`), ou `sudo pacman -S python-pyserial`.
```bash
tools/node_cli.py -p /dev/ttyUSB0 monitor
tools/node_cli.py -p /dev/ttyUSB0 config --role beacon --user-id 0x0A1B2C3D --sf 9 --power 10
tools/node_cli.py -p /dev/ttyUSB0 start --lat -27.81 --lon -50.32 --status 2 --msg "perdido" --follow
tools/node_cli.py -p /dev/ttyUSB1 config --role sniffer --follow
tools/node_cli.py -p /dev/ttyUSB0 selftest
```

### Testes no PC
Os módulos de protocolo (`App/Src/crc16.c`, `proto_frame.c`, `lora_toa.c`, `alert_packet.c`, `proto_msgs.c`) não dependem do HAL e são testados no PC. Os testes incluem os valores da Tabela 1 do TCC.
```bash
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
```

## Configuração de hardware (E77-900M22S)
O `.ioc` é a fonte da verdade; ao regenerar pelo CubeMX, mantenha o código próprio dentro dos blocos `USER CODE`.

- **Clock:** SYSCLK = MSI 48 MHz, com autocalibração pelo LSE. O cristal de 32 MHz (HSE) é usado só pelo rádio.
- **LSE 32,768 kHz:** cristal interno do módulo (PC14/PC15). Alimenta o RTC.
- **RTC:** modo binário com Alarm A e SSRU, exigidos pelo timer server do SubGHz_Phy.
- **Rádio:** `IS_TCXO_SUPPORTED = 1`, PA de alta potência (`RBI_CONF_RFO_HP`) e DCDC desligado. A referência de 32 MHz do rádio é alimentada pela saída VDD_TCXO. Testado em bancada em 08/10/2026: com 0, nem o CAD nem a TX terminam.
- **Clock com fallback:** se o cristal LSE não oscilar, o RTC passa para o LSI e a flag `LSE_FALLBACK` é marcada. É código editado à mão em `SystemClock_Config` e em `rtc.c`: uma regeneração pelo CubeMX desfaz.
- **VTOR:** fixado na flash em `diag_early_boot()`, para o firmware funcionar quando é iniciado pelo comando *Go* do bootloader.
- **Chave de RF:** PA6 = `RF_TXEN`, PA7 = `RF_RXEN`. Os dois nunca podem ficar em nível alto ao mesmo tempo.
