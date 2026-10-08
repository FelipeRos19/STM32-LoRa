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
Após o flash desse firmware, a conexão UART feita pelo bootloader será barrada pelo USART2 do firmware. Para realizar o flash de uma nova versão do firmware é necessário realizar os seguintes passos:  
1. Conectar-se a placa via um terminal SERIAL/UART
2. Enviar o comando `B`, para que o modulo entre no modo Bootloader
3. Desconectar-se do termial SERIAL/UART para liberar a porta
4. Rodar *CMake* com a flag de flash ou o comando via *STM32_Programmer_CLI*

## Configuração de hardware (E77-900M22S)
O `.ioc` é a fonte da verdade; ao regenerar pelo CubeMX, mantenha o código próprio dentro dos blocos `USER CODE`.

- **Clock:** SYSCLK = MSI 48 MHz, com autocalibração pelo LSE. O cristal de 32 MHz (HSE) é usado só pelo rádio.
- **LSE 32,768 kHz:** cristal interno do módulo (PC14/PC15). Alimenta o RTC.
- **RTC:** modo binário com Alarm A e SSRU, exigidos pelo timer server do SubGHz_Phy.
- **Rádio:** `IS_TCXO_SUPPORTED = 0` (cristal, sem TCXO), PA de alta potência (`RBI_CONF_RFO_HP`) e DCDC desligado.
- **Chave de RF:** PA6 = `RF_TXEN`, PA7 = `RF_RXEN`. Os dois nunca podem ficar em nível alto ao mesmo tempo.
