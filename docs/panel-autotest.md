# Abertura e fechamento automático das bandejas

Ensaio de 19/11/2026, solicitado para testar os acessos rápidos e o launcher
reais, em vez da cena de barras de `display-scan.md`.

## Caminho exercitado

O perfil `panel_autotest` herda o firmware padrão com cache RLE, cobertura
opaca e 16 máscaras, em release/O2. Não habilita a bancada de barras nem TE.
`platform/panel_autotest.c` substitui temporariamente a leitura do ponteiro
por coordenadas programadas. O LVGL faz hit test, seleção de alvo, propagação
de eventos, gestos e animações normais; o módulo não posiciona bandejas por
`lv_obj_set_y` nem chama funções privadas de abrir/fechar. O desenho e a
transmissão passam pelo driver físico, preservando os buffers `1/20`, pool
PSRAM/TLSF de 128 KiB, refresh de 20 ms, QSPI de 2 KiB e drain síncrono.

Após oito segundos de inicialização, um contato curto acorda a interface
pelo caminho normal. Em seguida executa sete gestos, nas velocidades lenta
e rápida, três vezes: **42 casos**. Todos usam x=206.

| Caso | Arraste y | Estado esperado ao terminar |
| --- | --- | --- |
| Abrir acessos rápidos | 20 → 230 | Painel em y=0, mostrador ativo |
| Fechar acessos rápidos | 200 → 60 | Painel oculto em y=−412, mostrador ativo |
| Cancelar abertura dos acessos | 20 → 90 | Painel oculto, mostrador ativo |
| Abrir launcher | 300 → 60 | Launcher em y=0 e app `apps` ativo |
| Cancelar fechamento do launcher | 30 → 100 | Launcher retorna a y=0 |
| Fechar launcher | 30 → 230 | Mostrador ativo e launcher oculto |
| Cancelar abertura do launcher | 300 → 245 | Mostrador ativo e launcher oculto |

O painel rápido não possui cancelamento por um arraste curto de fechamento:
uma vez reconhecido o arraste para cima fora do arco, fecha ao soltar. O teste
preserva esse contrato e testa o cancelamento apenas na abertura desse painel.
No launcher, exercita os dois cancelamentos previstos.

Cada contato começa com 90 ms na posição inicial, seguido de movimento
de 900 ms ou 180 ms. O roteiro garante uma amostra pressionada na posição
final antes de soltar e espera 650 ms para a animação concluir. Esses são
prazos solicitados; o logger registra a duração e os intervalos realmente
observados pelo LVGL, que dependem do tempo de renderização.

## Verificações e medidas

Cada caso verifica a posição final/visibilidade, o app ativo, existência de
deslocamento intermediário, tela acesa e preservação de brilho, volume, ECO
e perfil de inatividade. A verificação de movimento detecta também um salto
direto ao destino, que somente a checagem do estado final não identificaria.
O primeiro teste no host detectou uma conversão de sinal na interpolação
ascendente do roteiro; ela foi corrigida antes de qualquer gravação de teste.

Os callbacks de render/flush medem número de refreshes e faixas, pixels,
desenho médio, envio síncrono médio e maior duração por refresh. Os tempos
incluem os quadros do movimento e da animação de conclusão, sem atribuir os
650 ms parados à renderização. O profiling dos dois painéis também permanece
disponível para decompor raiz, filhos e máscara.

A entrada física fica reservada ao roteiro durante o ensaio. Contatos
sintéticos passam pela política normal de atividade apenas nesse perfil;
nenhum timer do firmware padrão passa a contar como interação real. Um modal
ou menu Energia interrompe o roteiro antes de enviar contatos a seus controles.
Ao terminar ou abortar, a leitura original é restaurada e a liberação do
contato é consumida. O ensaio termina no mostrador, sem alterar preferências
persistentes, nem executar ações dos apps.

Esse teste valida o caminho de gestos, a composição e o envio no hardware.
Ele não usa um dedo físico nem uma câmera do painel: não mede precisão do
sensor de toque e não comprova por si só ausência visual de tearing no vidro.
As capturas do host mostram a composição LVGL correspondente, separadamente
do comportamento óptico do display.

## Executar, analisar e restaurar

```powershell
./tests/run_Relogio_ui.ps1 -System -PanelAutoTest
pio run -e panel_autotest -t upload
python scripts/capture_display_scan.py --seconds 105 --output logs/panel-autotest-device.log
python scripts/analyze_panel_autotest.py logs/panel-autotest-device.log
# Ao concluir, voltar ao firmware padrão sem roteiro nem profiling:
pio run -t upload
```

As capturas `panel-auto-quick-moving.bmp`, `panel-auto-quick-open.bmp`,
`panel-auto-launcher-moving.bmp` e `panel-auto-launcher-open.bmp` ficam em
`.pio/host-tests/`. São salvas de quadros já desenhados, sem forçar refresh
ou mudar o tempo do roteiro. Logs, resumos e binários são locais/ignorados.
O analisador exige `DONE`, sequência completa de resultados, preferências
preservadas e todos os casos aprovados; uma captura incompleta não passa.

A referência padrão anterior ao ensaio foi verificada em
`logs/firmware-references/20261119T122216377762Z-waveshare_esp32_s3_touch_lcd_146/`.
## Resultado no relógio — 19/11/2026

O build `panel_autotest` passou e foi gravado pela COM3 com hashes das quatro
imagens verificados. O roteiro terminou aos **70,652 s de uptime**, com
`completed=42 total=42 failed=0 physical_touch_restored=1`. O resumo do
analisador tem `complete=true`: sete casos × duas velocidades × três repetições,
todos aprovados. Cada caso preservou as preferências e o app esperado; houve
deslocamento intermediário e a leitura física foi restaurada. A captura não
contém erros de transmissão SPI, drain, assertion ou panic nos padrões buscados.

Tempos aproximados por refresh, ponderados pelo número de quadros dos três
ensaios de cada velocidade. A soma inclui desenho e envio síncrono; não inclui
o intervalo entre passagens no loop nem equivale a FPS geral. Os pixels e os
quadros presentes diferem conforme a trajetória, portanto uma média maior na
velocidade rápida não significa que cada operação ficou proporcionalmente lenta.

| Gesto | Lento: desenho + envio | Rápido: desenho + envio | Maior refresh observado |
| --- | --- | --- | --- |
| Abrir acessos rápidos | 70,7 ms | 107,9 ms | 141,1 ms |
| Fechar acessos rápidos | 67,5 ms | 45,2 ms | 140,4 ms |
| Cancelar abertura dos acessos | 25,7 ms | 21,7 ms | 54,0 ms |
| Abrir launcher | 58,2 ms | 79,1 ms | 99,7 ms |
| Cancelar fechamento do launcher | 93,9 ms | 91,9 ms | 97,6 ms |
| Fechar launcher | 86,9 ms | 94,5 ms | 132,9 ms |
| Cancelar abertura do launcher | 18,8 ms | 19,9 ms | 31,1 ms |

O maior intervalo observado entre amostras do contato sintético foi **160 ms**,
na abertura lenta dos acessos rápidos. O resultado funcional é correto, mas
essas medidas mostram que as bandejas ainda têm quadros muito acima dos 20 ms
configurados. Os 23 ms de desenho da bancada anterior do mostrador não devem
ser apresentados como custo dos menus reais, nem como prova de fluidez total.
Esta execução localiza o custo no fluxo solicitado, sem atribuir automaticamente
os picos a uma primitiva ou à varredura do painel.

Também passou a suíte no host com os mesmos 42 gestos. Foram inspecionadas as
quatro capturas das bandejas em movimento e abertas, salvas sem avançar o roteiro
artificialmente. Isso confirma a composição correspondente no LVGL; a limitação
de observação óptica do painel descrita acima continua válida.

Artefatos locais:

- `logs/panel-autotest-device.log`: captura completa;
- `logs/panel-autotest-results.log`: somente marcadores do roteiro;
- `logs/panel-autotest-summary.json`: resultados, agrupamentos e validação;
- `logs/panel-autotest-host.log`: suíte no host;
- `logs/panel-autotest-build.log` e `panel-autotest-upload.log`: build/gravação;
- `logs/panel-autotest-default-build.log`: build padrão aprovado após as mudanças.

O padrão continuou com 1.687.090 bytes de programa e 55.200 bytes estáticos,
sem habilitar o roteiro ou profiling. Os logs e binários permanecem ignorados.

Ao terminar, o firmware padrão foi restaurado pela COM3, com quatro hashes
verificados e reset por RTS, em 63,71 s. Registro em
`logs/panel-autotest-restore.log`. O relógio não ficou com o roteiro automático
instalado; o comportamento do padrão permanece o anterior a este ensaio.

## Continuação: custo das bandejas e recorte seletivo

O perfil das bandejas reais localizou cerca de 31,5 ms por quadro no desenho
dos acessos e 30,4 ms no launcher. Dentro desses intervalos, a superfície raiz
consumiu aproximadamente 8,1 e 10,9 ms, respectivamente. O restante inclui
controles, textos, ícones, recorte e travessia; não é uma medida exclusiva de
uma primitiva. O mostrador reutilizou seu cache durante os gestos: sua etapa
SETUP ficou em cerca de 7,5 ms por quadro do mostrador. São denominadores
diferentes, portanto esses valores não devem ser somados como um quadro típico.

Foi medido `panel_autotest_clip`, que altera somente o recorte das duas
bandejas (`CHRONVS_PANEL_SELECTIVE_CLIP`). `ui/circle_clip.c` conserva o fundo
redondo e o hit test; instala a mesma máscara LVGL nas subárvores que chegam
à borda e dispensa a máscara no interior conservador. As linhas transparentes
do launcher recortam separadamente a superfície pressionada, badge e nome.
O teste de cobertura impede começar o redraw por um filho fora do círculo.
Esse helper exige descendentes contidos nos limites da subárvore vinculada,
sem overflow ou scrollbars; não deve ser aplicado indiscriminadamente a apps.

No host, a suíte integrada passou; **102 capturas coincidiram pixel a pixel**
com a referência, incluindo movimento das duas bandejas, 21 posições de fade,
linhas pressionadas e símbolos/temperaturas extremos. O roteiro no host
também terminou com 42/42. No relógio, foram 42/42, preferências preservadas e
entrada física restaurada. O resultado de desempenho foi negativo:

| Gesto lento | Referência: desenho + envio | Recorte seletivo |
| --- | --- | --- |
| Abrir acessos | 70,8 ms | 71,3 ms |
| Fechar acessos | 67,6 ms | 70,2 ms |
| Abrir launcher | 58,3 ms | 59,1 ms |
| Fechar launcher | 87,0 ms | 87,6 ms |

O pico continuou em 141,5 ms. O LVGL já verifica em
`lv_draw_mask_is_any(area)` se uma área está dentro da máscara de raio; a
retirada antecipada não proporcionou ganho que compense os callbacks. O
candidato consumiu mais 936 bytes do pool LVGL na suíte integrada. Permanece
isolado e **não é adotado no padrão**. Não atribuir o custo alto apenas à
existência de `clip_corner`.

Artefatos ignorados: `logs/panel-clip-{build,upload,device,summary}.log/json`,
`panel-clip-comparison.json`, `panel-clip-pixels.json`,
`panel-clip-reference-expanded-host.log`, `panel-clip-candidate-expanded-host.log`
e `panel-clip-autotest-host.log`. Reproduzir com
`./tests/run_Relogio_ui.ps1 -System -SelectiveClip` e o ambiente
`panel_autotest_clip`; a referência omite `-SelectiveClip`.

## Acesso à memória: linhas de cache de dados

O ensaio independente `panel_autotest_cache64` mantém aplicação, LVGL e
recorte originais. Define `custom_data_cache_line = 64`: o script de configuração
seleciona `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`, inclusive em sdkconfig já
existente. Conserva cache de dados de 32 KiB, cache de instruções de 16 KiB,
CPU de 240 MHz, PSRAM OPI de 80 MHz e todos os parâmetros de tela.
Essa direção consta na [orientação de desempenho LCD da Espressif](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html#how-can-i-improve-the-display-frame-rate-of-lcd-screens).

O roteiro físico terminou aos 70,109 s, com 42/42, sem mudança de preferências
e com entrada física restaurada. A melhora foi pequena nesta comparação:

| Gesto lento | Referência | Linha de 64 bytes |
| --- | --- | --- |
| Abrir acessos | 70,8 ms | 69,4 ms |
| Fechar acessos | 67,6 ms | 66,4 ms |
| Abrir launcher | 58,3 ms | 55,7 ms |
| Fechar launcher | 87,0 ms | 84,6 ms |

O pico durante abrir acessos caiu de 141,1 para 138,4 ms. O mínimo observado
de RAM interna livre foi 143.991 bytes; o maior bloco DMA teve mínimo de
49.152 bytes, iguais à referência. Esses valores são observações deste
roteiro, não limites seguros para outros serviços. O horário real do mostrador
varia entre boots: diferenças pequenas não provam isoladamente um ganho de
percepção ou de autonomia. Não apresentar esse ajuste como solução da gelatina.
Artefatos locais: `logs/panel-cache64-build.log`, `panel-cache64-upload.log`,
`panel-cache64-device.log`, `panel-cache64-summary.json`,
`panel-cache64-draw-summary.json` e `panel-cache64-comparison.json`.

## Cache de instruções: ganho e custo de RAM

`panel_autotest_icache32` altera somente o cache de instruções de 16 para
32 KiB (`custom_instruction_cache = 32`). O cache de dados permanece em
32 KiB com linhas de 32 bytes; renderizador em O2, desenho, drivers e buffers
são os da referência. O header gerado confirmou os valores efetivamente
usados. O roteiro concluiu 42/42 aos 68,026 s, sem mudar preferências.

| Gesto lento | Referência | Instruções: 32 KiB |
| --- | --- | --- |
| Abrir acessos | 70,8 ms | 47,5 ms |
| Fechar acessos | 67,6 ms | 48,0 ms |
| Abrir launcher | 58,3 ms | 38,6 ms |
| Cancelar fechamento do launcher | 94,0 ms | 61,7 ms |
| Fechar launcher | 87,0 ms | 57,7 ms |

O maior refresh durante os gestos passou de 141,1 para 96,7 ms; o maior
intervalo de amostragem passou de 160 para 100 ms. Isso é evidência de custo
associado ao caminho de instruções nesse renderizador, sem medir diretamente
misses de cache nem provar uma causa exclusiva. O relógio real e a quantidade
de quadros por trajetória continuam variáveis entre boots.

O mínimo observado de RAM interna livre caiu de 143.991 para **127.607 bytes**;
o mínimo do maior bloco DMA caiu de 49.152 para **32.768 bytes**. Portanto há
uma troca concreta de memória por tempo. O sucesso nas bandejas não valida
automaticamente o mesmo orçamento durante TLS, GPS ou Vox; o perfil é
mantido para reproduzir a comparação. A adoção após a validação adicional
do Clima está registrada abaixo. Os buffers e o pool LVGL não foram aumentados.
Artefatos em `logs/panel-icache32-{build,upload,device}.log`,
`panel-icache32-summary.json`, `panel-icache32-draw-summary.json` e
`panel-icache32-comparison.json`.

## LVGL com código menor: comparação em -Os

`panel_autotest_lvgl_os` mantém cache de instruções de 16 KiB e dados com
linhas de 32 bytes. Somente a biblioteca LVGL é compilada com `-Os`; aplicação
e IDF continuam em release/O2. O pre-script admite esse nível e o post-script
aplica a escolha ao ambiente isolado do LVGL, depois de sincronizar os flags.
O banner físico confirmou `LVGL optimization=-Os`.

Foram 42/42 no relógio. Abrir/fechar acessos lentamente ficou em 68,2/63,9 ms;
abrir/fechar launcher, 55,4/81,5 ms. Os picos continuaram acima de 130 ms.
É um ganho pequeno frente ao cache de instruções maior, e esse ambiente
também permanece experimental. Não equivale a trocar o firmware inteiro
para otimização por tamanho.

A suíte integrada no host passou com `-Os`, com **102 capturas idênticas** à
referência. Nesse teste do host, o flag foi aplicado à compilação conjunta
(salvo pragmas por arquivo); no firmware, somente ao LVGL. As capturas incluem
movimento, fade e superfícies pressionadas. Artefatos locais:
`logs/panel-lvgl-os-{build,upload,device,host}.log`, `panel-lvgl-os-pixels.json`,
`panel-lvgl-os-summary.json` e `panel-lvgl-os-comparison.json`.

## Validação adicional e escolha do padrão

`panel_autotest_icache32_weather` repetiu os 42 gestos com cache de instruções
de 32 KiB e depois abriu o Clima pelo runtime normal. O app iniciou sua
requisição HTTPS usual, consumiu o resultado e persistiu o cache. O diagnóstico
espera até 60 s, sem simular atividade; tela apagada ou modal aborta. Ao
concluir, retorna ao mostrador e restaura a leitura física. Essa extensão não
existe no firmware de uso.

A repetição concluiu **42/42 e HTTPS aprovado** aos 71,766 s. Durante os gestos,
o maior refresh foi 97,347 ms, e o maior intervalo de amostragem foi 110 ms:
valores próximos ao primeiro ensaio, sem afirmar repetibilidade exata de cada
quadro. O polling observou mínimo de **79.923 bytes internos livres** e
**31.744 bytes no maior bloco DMA** durante o Clima. A marca do serviço
confirmou TLS conectado com 89.411 bytes internos livres; HTTPS e parada do
Wi-Fi terminaram normalmente. Não houve correspondências para os padrões de
falha SPI/TLS, panic ou assertion buscados na captura. Isso valida esse fluxo,
não estabelece uma reserva segura universal para outros serviços simultâneos.

O analisador agora exige os 42 casos na ordem completa (sete gestos × duas
velocidades × três repetições). Uma execução com total reduzido não passa.
Quando há `WEATHER BEGIN`, também exige resultado explícito de sucesso, estado
SUCCESS, passagem por FETCHING e ausência de timeout. Cinco testes sintéticos
cobrem execução incompleta, caso trocado, preferência alterada e falhas dessa
extensão. São testes do analisador, separados da evidência física.

O padrão adota **cache de instruções de 32 KiB**, mantendo cache de dados de
32 KiB/linha de 32 bytes, aplicação/LVGL/IDF em O2, PSRAM de 80 MHz e os
parâmetros de tela anteriores. `sdkconfig.defaults` cobre instalações novas;
`custom_instruction_cache = 32` no script cobre sdkconfig existente. Os
controles históricos `panel_autotest` e `display_scan` fixam instruções em
16 KiB para conservar suas referências. Recorte seletivo, linha de 64 bytes
e LVGL em -Os continuam fora do padrão.

O primeiro build padrão encontrou erro de metadados temporários do CMake
(lista de componentes ausente). Repetir o comando retomou a compilação, sem
limpeza da `.pio` ou alteração do SDK. A primeira análise da serial foi feita
antes de o arquivo da captura ser fechado e indicou incompleta; após o término,
o resumo definitivo confirmou `complete=true`, incluindo o Clima.

Artefatos locais: `logs/panel-icache32-weather-{build,upload,device}.log`,
`panel-icache32-weather-summary.json`, `panel-icache32-weather-draw-summary.json`
e `panel-instruction-default-retry-build.log`. Os binários e logs ficam ignorados.

O build padrão final passou em 148,51 s, com **1.687.070 bytes de programa** e
**55.200 bytes estáticos**. O header gerado confirmou instruções de 32 KiB,
dados de 32 KiB/linha de 32 bytes, console NONE e nível de log zero. O binário
final foi preservado e verificado em
`logs/firmware-references/20261119T133434996233Z-waveshare_esp32_s3_touch_lcd_146/`;
a referência anterior de 12:22 permanece disponível para restauração.

O padrão silencioso foi gravado pela COM3 em **63,78 s**, com quatro imagens
e hashes verificados e reset por RTS. Registro em
`logs/panel-instruction-default-upload.log`. O relógio terminou com o firmware
de uso, sem roteiro, profiling, TE ou consulta automática do Clima. A redução
de tempo foi medida em hardware; não houve câmera do painel nesta etapa e
não se declara eliminação óptica da gelatina ou validação de autonomia.

## Continuação: perfil por controle e centro vazio do arco

`panel_autotest_parts` conserva a configuração atual de instruções de 32 KiB
e executa os mesmos 42 gestos. Acrescenta callbacks de desenho em 49 subárvores:
arco, controles e rótulos dos acessos; linhas, badges, ícones e nomes do launcher.
A tabela de até 64 registros fica em PSRAM, sem reserva por quadro. Os tempos
acumulados são publicados somente ao terminar o roteiro, em `panel_part`.
O firmware padrão não instala esses callbacks nem aloca a tabela.

O roteiro passou 42/42. A soma das subárvores diretamente sob os acessos foi
4.152.257 us; o arco respondeu por **1.965.595 us (47,34%)** desse conjunto.
Esse denominador exclui o fundo raiz, o mostrador e o envio QSPI, e acumula
todo o roteiro; não significa metade de cada refresh. Tempos inclusivos de
filhos e pais não devem ser somados entre si. Os nomes e os ícones do launcher
continuam medidos separadamente. Logs em `panel-parts-device.log`, resumo
funcional em `panel-parts-summary.json` e extração em
`panel-parts-components.json`, todos sob `logs/`.

O candidato `panel_autotest_arc_clip` acrescenta somente
`CHRONVS_ARC_INNER_CLIP`. Durante o desenho do arco de brilho, seu callback
delega ao renderizador software original duas áreas laterais quando existe
um retângulo conservador inteiramente dentro do disco transparente interno.
Margens preservam o antialiasing e as pontas arredondadas; faixas próximas aos
polos e raios pequenos usam o caminho original. A função original é restaurada
ao terminar o desenho do controle. Não cria bitmap, cache de pixels, tarefa
ou timer e não muda hit test, valor, ângulos, largura ou preferências do brilho.

A suíte no host passou e **159 capturas coincidiram pixel a pixel** com o
controle sem esse recorte. Inclui 19 valores de brilho de 10 a 100 em três
posições verticais, além dos testes anteriores de transição, fade, controles,
AUTO/ECO e despertar. As preferências não são alteradas pelo laço de pintura.
Roteiro: `./tests/run_Relogio_ui.ps1 -System` para a referência e
`-System -ArcClip` para o candidato. Resultados em
`logs/panel-arc-expanded-{reference,candidate}-host.log` e
`panel-arc-expanded-pixels.json`.

A captura serial agora descarrega o arquivo a cada linha e inclui `panel_part`
na saída selecionada. Isso permite analisar um `DONE` já recebido sem aguardar
o fechamento do arquivo; ausência de `DONE` continua sendo captura incompleta.

O recorte do centro **não foi adotado**. O roteiro físico passou 42/42 e
preservou as preferências, mas abrir acessos lentamente ficou em 47,73 ms
contra 47,14 ms da referência; fechar, 47,61 contra 48,12 ms. O acumulado do
arco subiu de 1.965.595 para 2.052.758 us. As chamadas adicionais não reduziram
o custo total. Dados em `logs/panel-arc-clip-{device,summary,comparison}.log/json`.

Também foram preparados build e host de `panel_autotest_memops`, usando
`LV_MEMCPY_MEMSET_STD=1`: 159 capturas idênticas e suíte aprovada. Não houve
execução desse candidato no relógio nesta etapa: o usuário pediu priorizar
a substituição do arco. Portanto não existe conclusão física sobre seu ganho,
e essa flag continua fora do padrão.

## Substituição solicitada: barra de brilho no rodapé

O padrão usa agora uma barra horizontal, com estilo compartilhado em
`ui/control_style.c`, no lugar do arco externo. Mantém a escala 10–100%,
percentual visível, teto ECO, persistência, despertar sem alterar o brilho
no primeiro toque e exclusividade do contato até soltar. Os sete atalhos
conservam posições e disposição 2–3–2. Geometria e área ampliada estão em
[`interface.md`](interface.md#acessos-rápidos).

`CHRONVS_BRIGHTNESS_ARC_REFERENCE` preserva o arco nos controles históricos;
`panel_autotest_parts` é a referência anterior e `panel_autotest_slider` mede
a barra. Ambos usam instruções de 32 KiB, dados de 32 KiB/linha de 32 bytes,
O2 e a mesma instrumentação por subárvore. Não foi combinada a troca de
controle com recorte seletivo, rotinas alternativas de memória ou ajuste QSPI.

A suíte integrada passou com a barra, incluindo arraste para fora da área,
movimento vertical sem fechar, limite de 10%, teto ECO de 35%, primeiro toque
somente para despertar, retorno ao gesto de fechamento fora da barra, volume,
Notas, Calendario, avisos e menu Energia. A captura `13-quick-settings.bmp`
foi inspecionada: barra e legenda cabem abaixo dos círculos, dentro da tela.
O teste mostra 19 valores de brilho em três posições; seu visual é
intencionalmente diferente da referência com arco, não uma equivalência de
pixels entre esses controles. Em editor mais aviso, sobraram 10.960 bytes no
pool LVGL do host, contra 10.992 na referência inicial; não é medição de DMA.

No relógio, **42/42 gestos passaram**, com preferências preservadas e leitura
física restaurada. Tempos médios de desenho + envio por refresh:

| Gesto lento | Arco | Barra | Redução |
| --- | --- | --- | --- |
| Abrir acessos | 47,14 ms | 39,26 ms | 16,7% |
| Fechar acessos | 48,12 ms | 40,93 ms | 14,9% |
| Cancelar abertura dos acessos | 18,72 ms | 16,58 ms | 11,4% |
| Abrir launcher | 38,97 ms | 39,08 ms | Sem ganho |
| Fechar launcher | 58,38 ms | 58,41 ms | Sem ganho |

O pico ao abrir acessos caiu de **91,54 para 70,23 ms**. O custo acumulado do
controle no roteiro caiu de 1.965.595 para **241.135 us (87,73%)**. Esse último
número inclui uma área muito menor e diferente quantidade de passagens do
desenho; não significa 88% menos tempo para o painel inteiro. O launcher
permanece o próximo gargalo: seu fechamento ainda chegou perto de 99 ms.
Não há comprovação de ausência óptica da gelatina ou de ganho de autonomia.

Artefatos ignorados: `logs/panel-slider-{build,final-build,upload,device}.log`,
`panel-slider-{summary,draw-summary,comparison,control}.json`,
`panel-slider-final-host.log` e as capturas em `.pio/host-tests/`.

No roteiro físico da barra, o mínimo observado de RAM interna livre foi
127.599 bytes e o mínimo do maior bloco DMA foi 32.768 bytes, próximo da
referência instrumentada com arco (127.571 e 32.768). Esses números não são
limites de segurança gerais. Não houve correspondências para os padrões de
falha SPI/TLS, panic ou assertion buscados nessa captura.

O build padrão final passou em 150,51 s, com **1.687.278 bytes de programa**
e **55.200 bytes estáticos**. O header confirmou instruções de 32 KiB,
dados de 32 KiB/linha de 32 bytes, console NONE e logs zero. A primeira
tentativa encontrou ausência de `component_requires.temp.cmake`; repetir
o build resolveu a configuração, sem limpar a `.pio` ou alterar o SDK.

A referência final foi preservada e verificada em
`logs/firmware-references/20261119T163005719673Z-waveshare_esp32_s3_touch_lcd_146/`.
A referência de 13:34 conserva o padrão anterior com arco. O novo padrão
silencioso foi gravado na COM3 em **63,76 s**, com quatro hashes verificados e
reset por RTS. O relógio terminou com a barra, sem roteiro, profiling ou teste
de memória. Logs finais: `panel-slider-default-retry-build.log`,
`panel-slider-default-upload.log` e `panel-slider-default-reference.txt`,
todos sob `logs/`. O teste de controle no host e o roteiro físico de menus
não substituem uma observação óptica do painel ou validação do dedo no sensor.
