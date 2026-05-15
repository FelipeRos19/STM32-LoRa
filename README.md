## Build/Flash:
### Build:
Para buildar o projeto em modo rode o comando: `cmake --build --preset <Debug | Release>`  
O arquivo de firmware **.bin** será gerado em `build/Debug/` ou `build/Release/`


O modo *Release* é mais dificil para debugging, mas é mais otimizado.

### Flashing pelo CMake:
Ao usar a tag `--target flash` no comando de build do CMake, uma conexão serial vai ser aberta usando *STM32_Programmer_CLI* e o novo firmware será gravado no módulo automaticamente.

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
