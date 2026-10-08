# Chronvs — ESP32-S3 Touch LCD 1.46

Bring-up em PlatformIO/ESP-IDF para a **Waveshare ESP32-S3-Touch-LCD-1.46**. A placa é uma ESP32-S3R8 com 16 MB de Flash e 8 MB de PSRAM OPI; portanto não é compatível com a definição genérica `esp32-s3-devkitc-1` N8.

O firmware inicializa o barramento I2C, o expansor de GPIO, a tela redonda SPD2010 por QSPI e o touch. Em seguida, apresenta um mostrador inspirado no Ressence Type 3, com submostradores fixos. O PCF85063 fornece a hora de referência. A imagem atualiza ao acordar ou voltar ao mostrador; pressionar e segurar permite acompanhar os ponteiros a cada segundo.

O código é organizado como um runtime de aplicativos: o núcleo registra e troca
apps, serviços isolam RTC e bateria, a camada de plataforma inicializa a placa e
o mostrador vive em `src/apps/watch_app.c` como o primeiro app. Consulte
[`docs/apps.md`](docs/apps.md) para criar e registrar novas telas e
[`docs/interface.md`](docs/interface.md) para a navegação, energia e limites
de renderização do painel.

Além do mostrador, Relogio reúne cronômetro, timer e alarmes. Notas oferece notas
persistentes com teclado multi-tap e edição de texto na tela circular; seu
layout e interação foram validados no dispositivo. A calculadora
é um app nativo com operações básicas, precedência e parênteses.
Calendario oferece calendário mensal offline, destaque de hoje e consulta do dia
da semana e da distância até uma data. Veja os detalhes em
[`docs/interface.md`](docs/interface.md#calendario--calendário).

Clima mostra as condições de São Paulo pela Open-Meteo, com atualização ao ser
aberto, cache persistente e fallback para a última leitura. Compartilha sessões
Wi-Fi exclusivas com o NTP. Consulte [`docs/weather.md`](docs/weather.md) para
o funcionamento, a correção de memória e os cenários funcionais validados no dispositivo.
As [dificuldades e regras de prevenção](docs/weather.md#dificuldades-soluções-e-prevenção)
registram o diagnóstico das listras e distinguem o heap de objetos dos buffers de pixels.

## Estado validado

- Firmware padrão release compilado e gravado no relógio em 08/10/2026, com hashes de gravação verificados. A proteção do arco de brilho passou no teste integrado LVGL; a interação corrigida ainda aguarda confirmação no dispositivo.

- LVGL em `-O2` e logs/consoles desativados melhoraram a fluidez do padrão. O teste silencioso P6 confirmou menus rápidos e responsivos, mas ainda há faixas do menu em posições diferentes durante o movimento (efeito gelatina). A otimização isolada do código dos painéis é experimental. Histórico, limites e builds em [`docs/performance.md`](docs/performance.md).
- Melhorias restantes do mostrador e dos painéis circulares, com prioridades
  e critérios de teste: [`docs/performance-plan.md`](docs/performance-plan.md).

- Compilação com PlatformIO `espressif32 @ 6.9.0` e ESP-IDF 5.3.1.
- Configuração correta: ESP32-S3R8, 16 MB Flash e 8 MB OPI PSRAM.
- I2C detectado: TCA9554 (`0x20`), PCF85063 RTC (`0x51`), touch SPD2010 (`0x53`) e QMI8658 (`0x6B`).
- A tela recebe comandos QSPI e exibe o mostrador de relógio.
- Interface vetorial LVGL de 412 × 412 pixels e composição com submostradores fixos confirmadas no dispositivo. A retirada dos contornos internos e a última correção do brilho ainda requerem avaliação visual e de interação no relógio.
- Sincronização NTP validada em hardware: conexão WPA2, horário local UTC−3 gravado no PCF85063 e rádio Wi-Fi desligado em seguida.
- Partição de aplicação ampliada de 1 MiB para 4 MiB.
- O target padrão `pio run -t upload` grava as três imagens no mapa correto e reinicia a placa automaticamente; o conteúdo gravado foi confirmado por checksum.
- Light sleep validado no relógio: toque no GPIO 4 acorda a tela e atualiza a hora mesmo após 5 minutos; um timer de 1 minuto disparou com a tela apagada. O modo ECO preserva o limite de brilho.
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
| Bateria | ADC1 | GPIO 8, divisor resistivo 3:1 |

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

O firmware padrão não emite logs nem habilita consoles UART/USB. Para
investigar um problema, grave `pio run -e display_profile_o2 -t upload` e
abra o monitor serial com:

```powershell
pio device monitor -p COM3 -b 115200
```

Se a COM3 desaparecer, desconecte e reconecte o USB. Para entrar no bootloader, mantenha **BOOT** pressionado enquanto conecta o cabo ou pressione **BOOT** e depois **RESET**. Confirme a nova porta em Gerenciador de Dispositivos e substitua `COM3` no comando.

## Mostrador com submostradores fixos

O mostrador usa o LVGL 8.3.11 distribuído pela própria Waveshare, com primitivas vetoriais e cache RLE dos anéis externos em um único objeto de 412 × 412 pixels. Usa os buffers parciais da porta oficial, sem framebuffer permanente. Atualiza ao acordar ou voltar; durante pressão prolongada, atualiza uma vez por segundo.

A composição segue estas relações:

- O aro externo de data gira independentemente para alinhar o dia atual ao triângulo fixo em 6 horas.
- A escala de minutos permanece fixa, com números de 5 em 5 e sem traços intermediários; a posição de 30 minutos é reservada ao triângulo de data.
- O disco principal permanece fixo; seu ponteiro longo completa uma volta por hora e indica os minutos, passando atrás dos submostradores.
- Horas, dia da semana e temperatura ambiente mantêm posições fixas na composição dos minutos em zero; somente seus ponteiros mudam de ângulo. O submostrador de segundos foi removido.
- A geometria e a interação estão descritas em [`docs/interface.md`](docs/interface.md#mostrador-com-submostradores-fixos).
- O mostrador semanal destaca o dia atual em vermelho e apresenta `AM` ou `PM`.
- A temperatura ambiente usa uma escala provisória de −20 a 60 °C e pode ser atualizada por `chronvs_set_ambient_temperature()`.
- Se o RTC não responder ou contiver valores inválidos, uma hora de demonstração mantém a interface animada e testável.

As fontes Montserrat 12, 18, 24 e 48 estão habilitadas. O heap de objetos do LVGL tem 128 KiB em PSRAM, liberando RAM interna para DMA e rede. A configuração `LV_COLOR_16_SWAP=1`, também presente no exemplo oficial, é obrigatória para o RGB565 transmitido ao SPD2010; sem ela, cores e pixels das fontes ficam corrompidos.

## Sincronização de hora por Wi-Fi

O relógio pode acertar automaticamente o PCF85063 por NTP. As credenciais ficam em um arquivo local ignorado pelo Git:

```powershell
Copy-Item src/chronvs_secrets.example.h src/chronvs_secrets.h
```

Edite `src/chronvs_secrets.h` e informe a rede:

```c
#define CHRONVS_WIFI_SSID "nome-da-rede"
#define CHRONVS_WIFI_PASSWORD "senha-da-rede"
```

Quando há credenciais, uma tarefa em segundo plano conecta ao Wi-Fi na inicialização, obtém a hora de `pool.ntp.org`, converte para o horário de São Paulo (UTC−3) e grava segundos, minutos, horas, data, dia da semana, mês e ano no PCF85063. O rádio Wi-Fi é desligado após a tentativa e só volta a ligar na próxima sincronização, a cada 12 horas.

Sem `src/chronvs_secrets.h`, ou com o SSID vazio, o firmware continua funcionando exclusivamente com o RTC e registra essa condição no monitor serial. A senha nunca deve ser adicionada ao repositório.

No boot validado, o scanner confirmou o RTC em `0x51`, o touch em `0x53` e o QMI8658 em `0x6B`; a sincronização ocorreu poucos segundos depois de a interface receber um endereço IP.

## Controles por toque e energia

O comportamento completo de navegação, atalhos, temporizadores de energia e
parâmetros de renderização está em [`docs/interface.md`](docs/interface.md).
O resumo abaixo descreve os controles persistentes.

O mostrador inteiro funciona como superfície de toque:

- Arrastar a partir da borda superior para baixo revela um painel circular de acessos rápidos sobre o mostrador. O painel acompanha o dedo e completa ou cancela a abertura conforme a distância percorrida.
- O arco externo do painel controla continuamente o brilho entre 10% e 100%. Arrastes iniciados nele ficam reservados ao ajuste até soltar; para fechar o painel, arraste para cima fora do arco.
- Os atalhos ocupam uma grade 2–3–2. O primeiro botão mostra `15s`, `30s` ou `ON` e alterna entre `AUTO 15s / 45s`, `AUTO 30s / 2min` e `SEMPRE LIGADA`. O segundo reúne bateria e economia. O círculo central abre os apps; à esquerda, o volume percorre `0 → 1 → 2 → 3 → 4 → 5 → 0`. Zero silencia os avisos e cinco é o máximo. À direita, o indicador Clima mostra ícone e temperatura apenas do cache salvo, sem ligar Wi-Fi; sem cache, mostra somente `CLIMA`. Dois slots continuam reservados.
- O modo `ECO`, indicado pela borda amarela e pelo texto no botão da bateria, preserva a preferência normal, mas limita temporariamente o brilho e o indicador do arco a 35%, reduz para 5% após 5 segundos e apaga a iluminação após 15 segundos — inclusive quando o perfil normal está em `ON`.
- Tocar no atalho Clima abre o app e fecha o painel; a abertura do app solicita a atualização dos dados.
- Arrastar o painel para cima acompanha o dedo e fecha os acessos rápidos,
  inclusive quando o gesto começa sobre o arco de brilho.
- Toques curtos e longos sobre o mostrador não alteram configurações.
- Um toque com a tela reduzida ou apagada apenas reativa o mostrador, evitando também alterar o brilho por acidente.
- Brilho, perfil e estado do modo econômico são salvos na NVS e restaurados após reiniciar o relógio.

A tensão da bateria é lida pelo ADC1 no GPIO 8, usando o divisor 3:1 da placa e a calibração do ESP-IDF. O firmware tira oito amostras, calcula uma estimativa por curva de descarga de uma célula Li-ion e atualiza o percentual uma vez por minuto ou imediatamente ao reativar a tela. A leitura fica suspensa enquanto a tela está apagada e o valor aparece somente no botão superior direito do painel.

O mostrador permanece estático sem interação, tanto em brilho normal quanto reduzido; pressionar por 600 ms inicia a atualização a cada segundo até soltar ou mover o dedo. Quando a iluminação é apagada, o PWM do backlight é colocado em 0%, o timer do mostrador, o tick periódico do LVGL e as leituras do RTC e bateria são suspensos. O ESP32-S3 entra em light sleep e acorda pelo touch, pelo botão power, por um aviso agendado ou pelo timeout máximo de 5 minutos. A hora continua a avançar durante o sono e o RTC é consultado ao reativar a tela. Sessões Wi-Fi mantêm o processador acordado até que o rádio seja desligado. A autonomia com bateria de 150 mAh ainda não foi medida.

### Halo de pixels na borda

O exemplo oficial desenhava 16 faixas coloridas no GRAM durante `LCD_Init()`. Como o mostrador é circular e antialiasado, pixels extremos dessa tela de teste podiam sobreviver ao recorte e formar um halo colorido ao redor da interface.

A correção possui três camadas:

1. O script de build troca o teste colorido por uma limpeza preta uniforme de todas as 412 linhas.
2. O callback de desenho preenche todos os 412 × 412 pixels com a cor opaca do bezel antes de renderizar o mostrador.
3. O círculo externo usa overscan de quatro pixels, mantendo a borda antialiasada fora da abertura física do painel.

Não reintroduza o padrão colorido de `test_draw_bitmap()` sem garantir que todos os pixels periféricos serão sobrescritos depois.

## Decisões técnicas e observações

O procedimento de comparação de fluidez e os builds opcionais de diagnóstico
estão em [`docs/performance.md`](docs/performance.md).

- O driver Arduino-ESP32 distribuído com esta versão do PlatformIO não expõe o modo QSPI de quatro linhas necessário ao SPD2010. Por isso o projeto usa ESP-IDF e o driver oficial, que define `quad_mode = 1`.
- O build padrão usa `release` e `-O2` na aplicação, drivers compilados e ESP-IDF, sem logs, console ou profiling. A ampliação de `-O2` para todo o firmware ainda requer validação funcional no relógio. O histórico e os diagnósticos estão em [`docs/performance.md`](docs/performance.md).
- `CONFIG_SPIRAM_USE_MALLOC=y` mantém a política de PSRAM validada no relógio; buffers e pool LVGL continuam solicitando PSRAM explicitamente. Os defaults preservam 32 KiB de reserva interna e o limiar de 16 KiB do alocador.
- A partição e o Flash são explicitamente configurados para 16 MB; a mensagem de “Expected 16MB, found 2MB” deixa de ocorrer com `sdkconfig.defaults` aplicado.
- A fonte de referência da Waveshare está em `.vendor-reference/`, ignorada pelo Git. Não a remova enquanto quiser compilar localmente.
- O CMake exclui o módulo RGB paralelo de `esp_lcd`, sem uso nesta placa QSPI, para evitar uma falha interna do GCC 13.2.0 no build sem logs. O transporte SPI/QSPI e o driver SPD2010 permanecem incluídos.
- `partitions.csv` mantém NVS em `0x11000`, dados PHY em `0x17000` e a aplicação em `0x20000`, agora com 4 MiB disponíveis dentro da Flash física de 16 MB.

## Carcaça

Os arquivos de modelo e projeto de impressão ficam em [`body/`](body/README.md).
O inventário distingue o STL atual, o projeto 3MF e o arquivo de referência;
encaixe, impressão e montagem ainda precisam de validação física.
