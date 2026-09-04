# Chronvs — ESP32-S3 Touch LCD 1.46

Bring-up em PlatformIO/ESP-IDF para a **Waveshare ESP32-S3-Touch-LCD-1.46**. A placa é uma ESP32-S3R8 com 16 MB de Flash e 8 MB de PSRAM OPI; portanto não é compatível com a definição genérica `esp32-s3-devkitc-1` N8.

O firmware atual inicializa o barramento I2C, o expansor de GPIO, a tela redonda SPD2010 por QSPI e o touch. Em seguida, mostra o padrão de cores do driver e registra no monitor serial os periféricos internos e os toques detectados.

## Estado validado

- Compilação com PlatformIO `espressif32 @ 6.9.0` e ESP-IDF 5.3.1.
- Configuração correta: ESP32-S3R8, 16 MB Flash e 8 MB OPI PSRAM.
- I2C detectado: TCA9554 (`0x20`), PCF85063 RTC (`0x51`), touch SPD2010 (`0x53`) e QMI8658 (`0x6B`).
- A tela recebe comandos QSPI e exibe o padrão de cores.
- O QMI8658 em `0x6A` sem resposta é esperado nesta unidade: o endereço ativo é `0x6B`.

## Pinagem interna confirmada

| Recurso | Interface | GPIO/endereço |
| --- | --- | --- |
| Tela SPD2010 | QSPI | SCK 40, D0 46, D1 45, D2 42, D3 41, CS 21, TE 18, backlight 5; reset pelo TCA9554 EXIO2 |
| Touch SPD2010 | I2C | SDA 11, SCL 10, INT 4, endereço 0x53; reset pelo TCA9554 EXIO1 |
| IMU QMI8658 | I2C | SDA 11, SCL 10, endereço ativo 0x6B |
| RTC PCF85063 | I2C | SDA 11, SCL 10, endereço 0x51 |
| Expansor TCA9554 | I2C | endereço 0x20; controla resets da tela/touch |
| Microfone | I2S | WS 2, SCK 15, SD 39 |
| Speaker PCM5101 | I2S | DIN 47, LRCK 38, BCK 48 |

## Preparação em outra máquina

Instale o PlatformIO Core ou a extensão PlatformIO IDE do VS Code. O projeto usa uma cópia local do exemplo oficial da Waveshare para os drivers nativos ESP-IDF. Ela não é versionada porque contém aproximadamente 600 MB e possui seu próprio repositório Git.

Na raiz do projeto, obtenha a referência oficial antes de compilar:

```powershell
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46.git .vendor-reference
```

O arquivo `scripts/add_waveshare_drivers.py` compila somente os fontes indispensáveis dessa referência: I2C, TCA9554, touch, inicialização da tela e o painel SPD2010. A alteração local em `Display_SPD2010.c` faz o padrão de 16 cores ocupar os 412 pixels de altura completos; antes ela preenchia 400 linhas e deixava as 12 linhas inferiores com pixels residuais.

## Build, upload e monitor serial

Compile com:

```powershell
pio run
```

O `platformio.ini` deixa o ambiente configurado para `COM3` e 460800 baud. Atualize `upload_port` se o Windows atribuir outra porta.

No momento, o target genérico `pio run -t upload` pode falhar porque o bootloader ESP-IDF desta configuração ultrapassa o offset padrão de partições. A tabela foi movida para `0x10000` em `sdkconfig.defaults`, com a aplicação em `0x20000`. Use este upload explícito após o build:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 .pio\build\waveshare_esp32_s3_touch_lcd_146\bootloader.bin 0x10000 .pio\build\waveshare_esp32_s3_touch_lcd_146\partitions.bin 0x20000 .pio\build\waveshare_esp32_s3_touch_lcd_146\firmware.bin
```

Abra o monitor serial com:

```powershell
pio device monitor -p COM3 -b 115200
```

Se a COM3 desaparecer, desconecte e reconecte o USB. Para entrar no bootloader, mantenha **BOOT** pressionado enquanto conecta o cabo ou pressione **BOOT** e depois **RESET**. Confirme a nova porta em Gerenciador de Dispositivos e substitua `COM3` no comando.

## Decisões técnicas e observações

- O driver Arduino-ESP32 distribuído com esta versão do PlatformIO não expõe o modo QSPI de quatro linhas necessário ao SPD2010. Por isso o projeto usa ESP-IDF e o driver oficial, que define `quad_mode = 1`.
- `build_flags = -O0` é intencional: evita um erro interno do compilador observado com otimização ao compilar esta combinação de ESP-IDF/toolchain.
- A partição e o Flash são explicitamente configurados para 16 MB; a mensagem de “Expected 16MB, found 2MB” deixa de ocorrer com `sdkconfig.defaults` aplicado.
- A fonte de referência da Waveshare está em `.vendor-reference/`, ignorada pelo Git. Não a remova enquanto quiser compilar localmente.

O diretório `.vendor-reference` é o checkout do exemplo oficial da Waveshare. O projeto compila apenas os arquivos de driver indispensáveis a partir dele, preservando a sequência de inicialização específica do display. Pequenas adaptações no driver fazem a API Arduino 3.1.1 do exemplo funcionar com Arduino-ESP32 2.0.17, que é a versão distribuída pelo PlatformIO.
