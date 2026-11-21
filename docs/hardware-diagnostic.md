# Diagnóstico sem tela

O perfil temporário `hardware_diagnostic` substitui somente a entrada do runtime
por uma bancada serial USB. Não inicializa LCD, touch ou LVGL; não altera o
firmware padrão, parâmetros do display, preferências do usuário, configurações persistentes do GNSS
ou arquivos existentes do SD. É adequado para verificar a montagem com a tela
desconectada. Restaurar o firmware padrão após o ensaio.

## Seleção de firmware e isolamento

Os diagnósticos e o TailTiny ficam no repositório como perfis opcionais de
compilação. A seleção é feita pelo ambiente PlatformIO (`-e`), sem editar
variáveis para `true` no código e sem opção para ligá-los na interface.

| Firmware | Comando para compilar | Seleção em tempo de compilação |
| --- | --- | --- |
| Principal, com mostrador e apps | `pio run` | Ambiente padrão `waveshare_esp32_s3_touch_lcd_146`; nenhuma das três macros abaixo |
| Diagnóstico de hardware sem tela | `pio run -e hardware_diagnostic` | `CHRONVS_HARDWARE_DIAGNOSTIC` |
| Diagnóstico Bluetooth LE sem tela | `pio run -e bluetooth_diagnostic` | `CHRONVS_BLUETOOTH_DIAGNOSTIC` |
| Experimento VPN sem tela | `pio run -e tailtiny` | `CHRONVS_TAILTINY` e opção CMake do mesmo nome |

`platformio.ini` passa a macro correspondente com `-D`. Os blocos `#ifdef` /
`#if defined(...)` selecionam a entrada em `src/main.c` e o conteúdo do fonte
de cada bancada. É a **presença da macro**, não um valor booleano em runtime,
que faz essa seleção: definir uma macro como `0` não a desativa num `#ifdef`.
Use os perfis prontos, em vez de combinar flags manualmente.

No firmware principal, os corpos das funções dos dois diagnósticos e do
serviço TailTiny ficam excluídos pelo pré-processador. Não criam tarefas,
timers, conexões ou reservas de memória para essas bancadas. A pilha BLE
permanece desabilitada e o componente externo TailTiny não é incluído pelo
CMake. O Wi-Fi normal de NTP/Clima continua com sua política própria.

Cada perfil de teste substitui o loop normal por sua bancada; eles não rodam
juntos dos apps. Bluetooth tem configuração `sdkconfig` própria e habilita
controlador/Bluedroid somente em seu ambiente. As configurações geradas dos
perfis são ignoradas pelo Git, assim como logs e referências de firmware.

**Compilar não grava o relógio.** Para instalar um perfil, acrescente
`-t upload` ao comando dele. Para voltar ao principal, execute
`pio run -t upload`. O uploader mantém o mesmo mapa de flash e não apaga a
NVS; preservar uma referência antes dos ensaios continua recomendado.

Na sequência de testes de 21/11/2026, o principal foi restaurado após os
diagnósticos de hardware e BLE descritos abaixo. Depois foi instalado o
perfil `tailtiny`, que é o último firmware gravado nesta sessão. Documentar,
compilar, fazer commit ou push não troca o firmware do dispositivo.
O experimento VPN está documentado separadamente em [`tailtiny.md`](tailtiny.md).

Antes de gravar, preservar a referência local (ela pode conter credenciais):

```powershell
python scripts/firmware_reference.py --save waveshare_esp32_s3_touch_lcd_146
pio run -e hardware_diagnostic -t upload
python scripts/capture_hardware_diagnostic.py --output logs/hardware-device.log
```

O diagnóstico começa oito segundos após o boot e verifica:

- Identificação do processador/flash, tamanho da PSRAM e três padrões em 64 KiB
  de uma reserva PSRAM própria, sem sobrescrever memória do firmware.
- Leituras I2C em 0x20 (expansor), 0x51 (RTC), 0x53 (touch), 0x6A/0x6B (IMU).
  Touch desconectado deve ficar sem resposta; ACK não valida toque ou painel.
- Montagem FAT sem formatação, criação exclusiva de arquivo temporário aleatório,
  gravação/fsync/reabertura/comparação de 16 KiB e remoção apenas desse arquivo.
- NVS sem apagar/migrar dados, varredura Wi-Fi passiva sem conexão, credenciais
  ou registro dos nomes de redes. Encerra o rádio ao terminar.
  O driver pode atualizar sua própria calibração PHY na NVS durante a inicialização.
- UART1 RX44, com baud automático entre as seis taxas já usadas pelo projeto,
  checksum NMEA, fix, satélites, HDOP e C/N0. Não transmite comandos ao receptor
  nem registra coordenadas. Fix e antena requerem recepção com visão do céu.
- I2S de falante e microfone: três tons de 1 kHz intercalados com silêncio,
  amplitude baixa, estatísticas AC do microfone e contagem de transferências.
  Transferência I2S não comprova som audível; confirmar os tons fisicamente.
- Bateria por ADC e curva existentes, RTC e leituras de aceleração/rotação a
  cada cinco segundos por 90 segundos, além dos níveis dos botões e IRQ touch.
  A IMU usa configuração volátil de 4g/256 graus/s, ~120 Hz; os controles lidos
  antes do teste são restaurados ao concluir. Movimentar a placa para verificar
  a resposta dinâmica; repouso sozinho não valida o giroscópio.

Bluetooth não é exercitado por este perfil. Tensão com USB não valida bateria
presente, carga, capacidade, autonomia ou funcionamento sem USB. Display quebrado,
touch funcional, indicadores luminosos, botões pressionados e qualidade acústica
exigem confirmação física. A bancada fica ociosa após `hw_test: DONE`, sem
reiniciar automaticamente. Repetir reiniciando a placa; restaurar o padrão com
`pio run -t upload` após preservar os logs.

Após `DONE`, enviar `a` seguido de nova linha pela mesma console USB repete
somente o ensaio de áudio. O expansor recebe a mesma inicialização do boot
normal (`EXIO_Init`), sem inicializar display ou alterar o ganho de áudio.

## Montagem após reposicionar o falante — 21/11/2026

Primeira rodada física pela COM3, sem display:

- Flash identificada com 16 MiB; PSRAM de 8 MiB, reserva de 64 KiB aprovada
  com três padrões. Isso não constitui varredura integral de toda a memória.
- SD: 16 KiB gravados e relidos corretamente, arquivo temporário removido;
  FAT com 15.176 MiB totais e 15.175 MiB disponíveis.
- Wi-Fi: varredura passiva retornou 65 redes, sem conectar ou registrar SSIDs.
- GPS: 9600 baud, 999 sentenças válidas, um erro de checksum inicial que não
  aumentou durante a observação. Dentro de casa, sem fix, satélites ou C/N0.
  Confirma comunicação; não valida antena, aquisição ou exatidão de posição.
- Bateria: 4,092–4,095 V no ADC com USB conectado; estimativa em torno de 89%.
  Não valida presença/carga/capacidade nem funcionamento sem USB.
- IMU em 0x6B (WHO_AM_I=0x05): leituras responderam à inclinação/rotação,
  chegando a 182,72 graus/s em X. Em repouso, norma próxima de 1,06 g;
  nenhum ajuste/calibração foi aplicado.
- RTC: comunica e avança continuamente, mas está em 01/01/2000; hora não
  confiável. Este perfil não sincroniza NTP nem modifica os registradores de hora.
- Microfone: 96.000 amostras recebidas em seis segundos, 300/300 transferências;
  RMS AC variou de 72,8 a 181,2 nos períodos com/sem tom.
- Falante: 300/300 transferências I2S, porém o usuário não ouviu os tons.
  Portanto, saída acústica pendente; não declarar falante aprovado.
- Expansor em 0x20 respondeu; touch em 0x53 sem resposta, compatível com tela
  desconectada. Botões apenas observados em repouso, sem teste de pressão.
- O roteiro concluiu sem os padrões panic/assert/brownout buscados no log.

Registro local: `logs/hardware-diagnostic-device-20261121.log`. Essa rodada
inicial não chamou `EXIO_Init`; foi preparada repetição com o boot do expansor
igual ao padrão e repetição de áudio por comando, para evitar concluir sobre
o falante a partir de uma bancada com inicialização diferente.

Na segunda rodada, com `EXIO_Init` igual ao boot padrão e o mesmo ganho baixo,
o usuário confirmou que ouviu os tons. Falante validado acusticamente nessa
montagem; a ausência relatada na primeira rodada não demonstra defeito ou
problema de inicialização. Não houve alteração dos fios, volume ou amplificador
pelo firmware entre as duas rodadas. Transferências de áudio novamente 300/300
em cada sentido. Registro: `logs/hardware-diagnostic-repeat-device-20261121.log`.

A repetição terminou com 1.000 sentenças GPS válidas e zero erros de checksum,
sem fix/satélites no ambiente interno. Wi-Fi encontrou 57 redes nessa varredura.
BOOT foi observado em nível baixo aos 75/80 s; PWR em nível baixo aos 85 s;
ambos estavam soltos aos 90 s. O usuário confirmou que pressionou os dois.
Isso verifica leitura dos botões, sem executar desligamento ou testar RESET.

Build padrão após a inclusão do diagnóstico: 1.694.998 bytes de programa,
55.400 bytes estáticos, aprovado. A primeira tentativa encontrou um cache CMake
local apontando para MinGW; somente os metadados de configuração foram
regenerados com a ferramenta Xtensa, preservando o firmware arquivado e sem
limpeza ampla de `.pio`. Registro: `logs/hardware-diagnostic-default-build-recovered.log`.

Ao concluir, o firmware padrão foi recompilado e restaurado pela COM3 em
92,75 segundos, com hashes das quatro imagens verificados e reset por RTS.
Registro: `logs/hardware-diagnostic-restore-default.log`. A bancada não ficou
ativa no dispositivo. Bluetooth, painel/touch e alimentação/autonomia sem USB
permanecem fora da validação desta sessão.

Após o ensaio, o usuário confirmou que a placa permaneceu ligada ao remover
o USB. Isso confirma alimentação pela bateria nessa montagem. Não mede carga,
capacidade, autonomia ou tensão sob uma carga definida.

## Bluetooth LE

O perfil separado `bluetooth_diagnostic` habilita controlador e Bluedroid apenas
na bancada. Usa USB e não inicializa LCD/LVGL, Wi-Fi ou serviços normais. Faz
varredura BLE ativa por 20 s, registra quantidade de pacotes/respostas e RSSI,
sem registrar nomes/endereço dos dispositivos próximos. Em seguida, anuncia
`Chronvs-Test` por 20 s, sem aceitar conexões ou pareamento. Encerra a pilha
antes de `hw_test: DONE`. Não apaga NVS; a inicialização do rádio pode atualizar
a calibração PHY. Gravar com `pio run -e bluetooth_diagnostic -t upload` e usar
o mesmo capturador. Restaurar o padrão ao concluir.

Receber anúncios valida recepção BLE. Receber respostas durante varredura ativa
exercita também solicitações de scan. Sucesso de início do advertising comprova
aceitação pelo controlador, não recepção por um segundo aparelho. Pareamento,
GATT, alcance e uso simultâneo com Wi-Fi permanecem fora desse roteiro.

### Resultado físico — 21/11/2026

Controlador e Bluedroid inicializaram; a varredura de 20 s recebeu **284 pacotes**,
**5 respostas de scan** e **12 endereços distintos** (endereços aleatórios não
identificam necessariamente aparelhos físicos diferentes). RSSI observado entre
**−99 e −46 dBm**. O controlador iniciou os anúncios `Chronvs-Test` com status
de sucesso. Isso confirma recepção e exercício da varredura ativa; anúncios
próprios não foram conferidos por um segundo receptor. Sem pareamento/GATT.

Após inicializar BLE, foram observados 200.467 bytes internos livres e maior
bloco DMA de 106.496 bytes. São amostras da bancada sem LVGL/Wi-Fi, não orçamento
para adicionar BLE ao runtime normal. A habilitação é exclusiva do perfil de teste.

O perfil precisa de `BT_BLE_42_FEATURES_SUPPORTED` para as APIs de scan e anúncio
usadas; habilitar somente os recursos BLE 5 não disponibiliza essas funções.
Build de diagnóstico e build padrão passaram, assim como os sete testes de
configuração. Logs locais: `logs/bluetooth-diagnostic-device-20261121.log`,
`logs/bluetooth-diagnostic-build-final.log` e `logs/bluetooth-default-build-final.log`.

O roteiro terminou com `BLE_scan_pass=1`, `advertising_started=1`, `ESP_OK` e
`async_error=0`. O padrão foi restaurado pela COM3 em 94,17 s, com os hashes
das quatro imagens verificados e reset por RTS. Registro em
`logs/bluetooth-restore-default.log`; Bluetooth continua desligado no padrão.

## Revisão para publicação — 21/11/2026

O build principal passou novamente com 1.694.998 bytes de programa e 55.400
bytes estáticos; oito testes de configuração passaram. A inspeção de símbolos
do `firmware.elf` principal confirmou ausência de
`chronvs_hardware_diagnostic_run`, `chronvs_bluetooth_diagnostic_run`,
`chronvs_tailtiny_run` e `tailtiny_init`; o `sdkconfig.h` gerado manteve
`CONFIG_BT_ENABLED` desabilitado. Isso verifica o isolamento no binário, além
dos blocos condicionais no fonte. Log local do build:
`logs/diagnostics-commit-default-build.log`.

Esta revisão não repetiu os ensaios físicos, não executou upload e não mudou
o perfil TailTiny que estava instalado. Permanecem os limites de validação
de painel/touch, GPS ao ar livre, pareamento/GATT e autonomia descritos acima.
