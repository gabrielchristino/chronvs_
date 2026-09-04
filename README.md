# Chronvs — ESP32-S3 Touch LCD 1.46

Bring-up em PlatformIO/ESP-IDF para a **Waveshare ESP32-S3-Touch-LCD-1.46**. A placa é uma ESP32-S3R8 com 16 MB de Flash e 8 MB de PSRAM OPI; portanto não é compatível com a definição genérica `esp32-s3-devkitc-1` N8.

O firmware atual inicializa o barramento I2C, o expansor de GPIO, a tela redonda SPD2010 por QSPI e o touch. Em seguida, apresenta um mostrador orbital inspirado no Ressence Type 3, com hora, data, dia da semana e segundos lidos diretamente do RTC PCF85063.

## Estado validado

- Compilação com PlatformIO `espressif32 @ 6.9.0` e ESP-IDF 5.3.1.
- Configuração correta: ESP32-S3R8, 16 MB Flash e 8 MB OPI PSRAM.
- I2C detectado: TCA9554 (`0x20`), PCF85063 RTC (`0x51`), touch SPD2010 (`0x53`) e QMI8658 (`0x6B`).
- A tela recebe comandos QSPI e exibe o mostrador de relógio.
- Interface vetorial LVGL validada no painel circular de 412 × 412 pixels, incluindo animação contínua, submostradores orbitais e contra-rotação da tipografia.
- O target padrão `pio run -t upload` grava as três imagens no mapa correto e reinicia a placa automaticamente; o conteúdo gravado foi confirmado por checksum.
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

O arquivo `scripts/add_waveshare_drivers.py` compila somente os fontes indispensáveis dessa referência: I2C, TCA9554, touch, inicialização da tela, painel SPD2010 e LVGL 8.3.11. Durante o build, o script corrige o preenchimento das 412 linhas e substitui o padrão de 16 cores do exemplo por uma limpeza preta uniforme do GRAM.

## Build, upload e monitor serial

Compile com:

```powershell
pio run
```

O `platformio.ini` deixa o ambiente configurado para `COM3` e 460800 baud. Atualize `upload_port` se o Windows atribuir outra porta.

Use o Upload do PlatformIO normalmente:

```powershell
pio run -t upload
```

`scripts/upload_waveshare.py` configura esse comando para gravar bootloader em `0x0`, tabela de partições em `0x10000` e firmware em `0x20000`. Assim, o esptool controla automaticamente o reset antes/depois do upload; não é necessário usar o botão `BOOT`, o botão `PWR` ou reconectar o USB em condições normais.

Abra o monitor serial com:

```powershell
pio device monitor -p COM3 -b 115200
```

Se a COM3 desaparecer, desconecte e reconecte o USB. Para entrar no bootloader, mantenha **BOOT** pressionado enquanto conecta o cabo ou pressione **BOOT** e depois **RESET**. Confirme a nova porta em Gerenciador de Dispositivos e substitua `COM3` no comando.

## Mostrador orbital

O mostrador usa o LVGL 8.3.11 distribuído pela própria Waveshare e é renderizado por primitivas vetoriais em um único objeto de 412 × 412 pixels. Essa abordagem evita um framebuffer permanente e permite atualizar a animação a cada 100 ms usando os buffers parciais da porta oficial.

A composição segue estas relações:

- O aro externo de data gira independentemente para alinhar o dia atual ao triângulo fixo em 6 horas.
- A escala `5, 15, 25, 35, 45, 55` permanece fixa no chassi.
- O disco-mãe e seu ponteiro longo completam uma volta por hora e indicam os minutos.
- O centro do mostrador de horas fica sempre a 180° do ponteiro de minutos.
- Dia da semana, temperatura ambiente e runner de segundos ocupam posições fixas no referencial do disco-mãe e orbitam junto com ele.
- As faces dos submostradores são desenhadas no referencial global. Isso equivale à contra-rotação de `-ângulo_minutos` e mantém textos e escalas sempre verticais.
- O mostrador semanal destaca o dia atual em vermelho e apresenta `AM` ou `PM`.
- A temperatura ambiente usa uma escala provisória de −20 a 60 °C e pode ser atualizada por `chronvs_set_ambient_temperature()`.
- Se o RTC não responder ou contiver valores inválidos, uma hora de demonstração mantém a interface animada e testável.

As fontes Montserrat 12, 18, 24 e 48 estão habilitadas. O heap interno do LVGL foi ampliado para 128 KiB para comportar o mostrador e futuras extensões. A configuração `LV_COLOR_16_SWAP=1`, também presente no exemplo oficial, é obrigatória para o RGB565 transmitido ao SPD2010; sem ela, cores e pixels das fontes ficam corrompidos.

O RTC é somente lido nesta etapa. Se a data/hora ainda não tiver sido ajustada — por exemplo, na primeira alimentação sem bateria de RTC — a tela exibirá os valores armazenados pelo chip. A próxima etapa é implementar o ajuste inicial e a sincronização por NTP via Wi-Fi.

### Halo de pixels na borda

O exemplo oficial desenhava 16 faixas coloridas no GRAM durante `LCD_Init()`. Como o mostrador é circular e antialiasado, pixels extremos dessa tela de teste podiam sobreviver ao recorte e formar um halo colorido ao redor da interface.

A correção possui três camadas:

1. O script de build troca o teste colorido por uma limpeza preta uniforme de todas as 412 linhas.
2. O callback de desenho preenche todos os 412 × 412 pixels com a cor opaca do bezel antes de renderizar o mostrador.
3. O círculo externo usa overscan de quatro pixels, mantendo a borda antialiasada fora da abertura física do painel.

Não reintroduza o padrão colorido de `test_draw_bitmap()` sem garantir que todos os pixels periféricos serão sobrescritos depois.

## Decisões técnicas e observações

- O driver Arduino-ESP32 distribuído com esta versão do PlatformIO não expõe o modo QSPI de quatro linhas necessário ao SPD2010. Por isso o projeto usa ESP-IDF e o driver oficial, que define `quad_mode = 1`.
- `build_flags = -O0` é intencional: evita um erro interno do compilador observado com otimização ao compilar esta combinação de ESP-IDF/toolchain.
- `build_type = debug` reforça o workaround do compilador durante a compilação dos componentes ESP-IDF.
- `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` permite que os buffers LVGL sejam alocados corretamente na PSRAM OPI.
- A partição e o Flash são explicitamente configurados para 16 MB; a mensagem de “Expected 16MB, found 2MB” deixa de ocorrer com `sdkconfig.defaults` aplicado.
- A fonte de referência da Waveshare está em `.vendor-reference/`, ignorada pelo Git. Não a remova enquanto quiser compilar localmente.
