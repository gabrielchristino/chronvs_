# Gelatina: desenho, transmissão e varredura do painel

**Estado final em 19/11/2026:** após o teste de `display_scene_quiet`, o usuário
confirmou **“Melhorou bastante”** nos menus. Cache RLE, cobertura opaca e 16
máscaras foram incorporados ao build padrão, sem espera TE. A confirmação
registra melhora perceptível, não ausência total de gelatina nem ganho de
autonomia. As seções abaixo preservam a ordem dos ensaios e seus limites.

Validação da adoção: build padrão (`platformio.exe run`) e suíte
`run_Relogio_ui.ps1 -System` aprovados com a nova configuração. O firmware
reportou 1.687.090 bytes de programa e 55.200 bytes estáticos; os 96 KiB de
cache são uma reserva dinâmica separada em PSRAM. A inspeção do ELF confirmou
o codec e ausência de bancada, profiling e ISR TE no padrão. Upload padrão na
COM3 concluído com quatro hashes verificados e reset por RTS, em 63,70 s.
Logs: `logs/display-cache-default-build.log`, `display-cache-default-ui.log`
e `display-cache-default-upload.log`. Referência final preservada em
`logs/firmware-references/20261119T122216377762Z-waveshare_esp32_s3_touch_lcd_146/`.
O backup anterior ao trabalho também permanece disponível para restauração.

Investigação iniciada em 19/11/2026, após preservar e publicar a integração
GPS/Corrida/A-GNSS no commit `a872c69` em `origin/develop`. O objetivo é separar
queda de cadência, atualização parcial visível e corrupção de pixels, sem
repetir as mudanças de buffers/driver que já falharam no relógio.

## O que o código permite concluir

O ESP32-S3 não determina sozinho a qualidade da apresentação. Interface,
controlador, velocidade efetiva e sincronização com a varredura são parte do
caminho. A documentação da
[Espressif sobre tearing](https://github.com/espressif/esp-iot-solution/blob/master/docs/en/display/lcd/lcd_screen_tearing.rst)
distingue painéis RGB com framebuffer no processador dos SPI/QSPI com GRAM
no próprio painel. No segundo caso, dois buffers de desenho no LVGL não
equivalem a dois quadros que o painel troca atomicamente.

No Chronvs, cada buffer tem 8.487 pixels/16.974 bytes, e cabem 20 linhas de
412 pixels. Um refresh retangular completo precisa de 21 faixas. Cada faixa
é desenhada e enviada antes da seguinte: `lv_refr.c:refr_area` chama
`refr_area_part` sequencialmente; nosso flush só libera o buffer depois do
drain síncrono QSPI. Timers de UI não são executados entre essas faixas. Portanto,
a posição do menu não é recalculada por timer no meio desse refresh. A imagem
visível pode combinar conteúdo antigo e novo na GRAM enquanto o envio progride.

O header Waveshare configura QSPI em 80 MHz, quatro linhas, RGB565. Um quadro
retangular tem 339.488 bytes; a conta ideal só do payload é 8,49 ms a 40 MB/s.
Não é tempo medido nem garantia de FPS: faltam comandos, transações de 2 KiB,
DMA/cópias de PSRAM, desenho e escalonamento. Uma tela fisicamente maior pode
ter menos pixels ou usar RGB paralelo. Comparar polegadas e CPU não compara
esses custos.

A inicialização do SPD2010 já contém `35h/00h` na página de usuário zero;
o header define TE no GPIO18. O caminho normal não configura esse GPIO como
entrada de sincronismo e não aguarda TE. Isso justifica medir o sinal, sem
assumir frequência, polaridade efetiva ou que um pulso sozinho resolva tearing.
Direção da varredura e prazo entre primeira e última escrita também importam.
Os dados e conexões da placa estão nos
[recursos Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46/Resources-And-Documents).

Os 78–82 ms e os ganhos qualitativos de fundo plano/menus simplificados são
históricos (`performance.md`). Não medem o padrão atual com mostrador reduzido
e release/O2. Não se deve usar a estimativa ideal de QSPI ou um limite de 20 ms
configurado como prova de que esses tempos são atingidos.

## Candidato e controle preparados

`platform/display_scan.c` instala wrappers nos callbacks LVGL apenas nos
ambientes experimentais. O controle `display_scan` observa TE. O candidato
`display_scan_te` acrescenta uma espera por borda de subida **nova**, antes da
primeira faixa de cada refresh. Não espera entre faixas. Descarta o pulso antigo
do semáforo binário, limita cada espera a 30 ms e desabilita a espera após três
timeouts consecutivos. Ausência de TE não bloqueia indefinidamente a interface.
É um candidato de sincronização, não uma correção visual já demonstrada.

O ISR só registra pulsos/períodos e sinaliza o semáforo; não desenha, transmite
ou acessa LVGL. Não instala tarefa nem reserva framebuffer. Durante tela apagada,
o poll desabilita a interrupção TE e descarta a janela, sem trabalho visual.
Ao acordar, reativa a entrada sem aceitar um pulso anterior como sincronização.
Se a configuração de TE falhar, conserva os callbacks originais.

Ambos herdam release/O2, PSRAM/TLSF de 128 KiB, buffers duplos `1/20`, refresh
de 20 ms, QSPI de 2 KiB, RGB565 com swap e drain síncrono. A preparação de
drivers continua exclusivamente no script existente, sem novos patches.
Logs são só USB Serial/JTAG, deixando TX43 livre para GNSS. A comparação antiga
`display_profile_o2` herda debug/O0 da aplicação; não é o controle deste ensaio.

## Cena comparável e interação

`platform/display_bench.c` abre uma superfície de diagnóstico sobre o mostrador.
Três barras retangulares, em alturas diferentes, movem-se juntas; a posição é
fixada pelo timer de 20 ms, nunca por faixa de desenho. O refresh invalida o
mesmo retângulo completo nas duas cenas. `FUNDO: LISO` cobre o mostrador;
alterna automaticamente a cada oito segundos para `FUNDO: MOSTRADOR`, com
transparência sobre o vetor real. Toques não mudam a cena; isso evita que
contatos encurtem a janela de comparação. A velocidade usa o tempo decorrido,
e não o número de quadros, para não mascarar quedas de cadência.
Compare também `px_avg` para confirmar áreas iguais. Os textos da bancada são
constantes dentro de cada cena; só o fundo e as barras são recompostos.

Arrastar para a direita com o contrato global sai da bancada e abre o launcher.
Nesse momento a animação é removida e os menus normais continuam instrumentados.
Um reset reabre a bancada; não há registro de app extra nem mudança persistente.
Enquanto a bancada ocupa a tela, ela intercepta os gestos de abrir painéis para
não misturar as cenas; isso é uma exceção apenas do firmware experimental.

A árvore usa `ui/app_input.h`, vinculada uma vez. Contatos reais contam como
atividade; o timer não conta. AUTO/ECO e o primeiro toque reservado ao despertar
permanecem. Após sair, pode-se escolher ON/ECO desligado para comparar menus;
anotar e restaurar a preferência. Não inferir consumo de um teste pelo USB.

## Medidas e interpretação

A cada janela com quadros e pelo menos 2 s, `display_scan` informa:

| Campo | Significado |
| --- | --- |
| `frames`, `slices`, `px_avg` | Refreshes completos, flushes e pixels médios por refresh |
| `render_avg_us` | Do início de render até monitor, menos flush e espera TE; inclui preempções |
| `flush_avg_us` | Soma dos envios síncronos por refresh; não inclui espera TE |
| `wait_avg_us` | Espera TE média por refresh |
| `present_avg_us`, `present_max_us` | Da primeira escrita à conclusão da última, incluindo desenho entre faixas |
| `frame_max_us` | Maior tempo total render+flush+espera da janela |
| `te_edges`, `te_period_us` | Pulsos de subida e intervalo médio observado entre eles |
| `timeouts`, `sync_on` | Esperas expiradas e se a espera segue habilitada |

Os tempos não incluem layout anterior ao `render_start_cb`, leitura do toque ou
o intervalo entre passagens no loop principal. Não são FPS nem latência final
do toque. O período TE é observação do GPIO, não prova de polaridade correta,
de um pulso por quadro ou da direção da varredura. Ignorar janela que misture
cenas, rede, montagem de SD, entrada/saída ou despertar.

Se a cena lisa já deformar, o conteúdo vetorial não é condição necessária.
Se o mostrador elevar o custo, há contribuição de desenho. Redução visual com
TE, com sinal estável e tempos equivalentes, sustenta contribuição do sincronismo.
Se o envio espalhado exceder a janela de varredura, só esperar TE no primeiro
flush não basta: reduzir o desenho entre envios continua necessário. A direção
da escrita e da varredura deve ser confirmada antes de mudar MADCTL ou frequência.
Não promover o candidato se piorar acompanhamento, despertar ou estabilidade.

## Reproduzir e restaurar

Antes do ensaio foi salva uma referência local, verificada por SHA-256, em
`logs/firmware-references/20261119T105529006013Z-waveshare_esp32_s3_touch_lcd_146/`.
Não publicar essas imagens: podem incorporar as credenciais locais.
O uploader existente verifica as quatro imagens e preserva o mapa de flash.

```powershell
pio run -e display_scan -t upload
python scripts/capture_display_scan.py --seconds 40
# Deixar as cenas alternarem sem tocar; depois sair para medir os menus reais.
# Não misturar cenas na mesma análise.
pio run -e display_scan_te -t upload
python scripts/capture_display_scan.py --seconds 40
# Voltar ao firmware de uso ao terminar:
pio run -t upload
```

O capturador não solicita reset e cria um arquivo novo em `logs/`, ignorado.
Ele mostra apenas as linhas de display no terminal; o arquivo local completo
pode conter informações de rede do SDK. Encerre a captura antes de gravar.
Se AUTO/ECO apagar a tela, a conexão USB pode cair durante light sleep;
reconectar não é evidência de falha do driver.

Validação no host: `tests/run_display_scan_tests.ps1` cobre preservação dos
callbacks e conclusão síncrona, pulso antigo descartado, uma espera por refresh
de 21 faixas, três timeouts/fallback, contabilidade e suspensão da interrupção.
Build e mocks não comprovam ausência de gelatina; resultados físicos devem
ser registrados abaixo com ambiente, cena, tempos e observação visual.

## Primeiro ensaio físico — 19/11/2026

O relógio apareceu na COM3 após o usuário conectá-lo. Controle `display_scan`
e candidato `display_scan_te` compilaram em release/O2 e foram gravados pelo
uploader customizado, com hashes das quatro imagens verificados. Os binários
e logs completos permanecem locais/ignorados. Nenhum parâmetro protegido de
display foi alterado. Passaram os testes de scan nos dois modos e a integração
LVGL `run_Relogio_ui.ps1 -System -DisplayBench`; as capturas dos dois fundos
foram inspecionadas no host. O build padrão obrigatório após as mudanças também
passou; a análise de símbolos do ELF não encontrou hooks de scan/bancada ou ISR
TE no firmware normal. A investigação permanece uma alteração local separada
do commit das pendências já publicado; não foi promovida ao padrão.

No controle, as janelas estáveis após o marcador `scene=watch` apresentaram
aproximadamente **114–115 ms de renderização**, **16,0–16,3 ms de flush** e
**127,8–129,0 ms da primeira à última escrita**, para 169.744 pixels e 21
faixas por refresh. São tempos de recomposição retangular completa do mostrador
atual nessa bancada, não tempos dos menus nem FPS geral. O intervalo de subida
de TE ficou próximo de **16,84 ms** (cerca de 59,4 Hz, se cada subida representar
um quadro do painel). Esse sinal foi efetivamente observado no GPIO18.

Os primeiros controles com troca por toque tiveram cenas lisas encurtadas por
contatos. Essas janelas mistas não estabelecem o custo de uma cena lisa estável.
A bancada final remove essa ação: alterna somente pelo prazo de oito segundos.
Na gravação do candidato, a bancada saiu por gesto aos 3,695 s, antes da primeira
alternância. As primeiras duas janelas tiveram TE ativo e **zero timeouts**,
mas incluem boot/rede e saída para o launcher; não são uma comparação limpa
de velocidade ou uma comprovação visual da correção. A captura posterior dos
menus não recebeu refreshes no período observado. Ausência de refresh parado
é esperada e não mede a fluidez durante gesto.

Capturas: `logs/display-scan-auto-observe.log`,
`logs/display-scan-auto-te.log`, `logs/display-scan-te-menus.log`, além dos
primeiros ensaios em `logs/display-scan-observe-*.log`. Os logs de upload e build
têm o mesmo prefixo. O candidato TE ficou disponível para avaliação visual dos
menus. No retorno posterior, o usuário informou **não ter visto a bancada** e
que a gelatina **continua parecida** com TE. Isso não valida a apresentação
da bancada no hardware nem sua saída como gesto intencional do usuário: o log
registra que o software reconheceu um gesto, mas sua origem não foi confirmada.
O candidato TE isolado não teve ganho perceptível e não deve ser promovido.

## Retorno do usuário e continuação — 19/11/2026

O perfil `display_scan_detail` desliga a espera TE e reutiliza o profiling
existente das primitivas e painéis, mantendo **release/O2 em toda a aplicação**
e USB sem console UART0. A bancada automatizada desse perfil fica reservada
à medição por 40 segundos de tela ativa: alterna fundos a cada oito segundos
e sai automaticamente para o launcher. Durante esse intervalo, toques registram
atividade, mas não navegam; o menu global Energia permanece disponível. Essa
exceção pertence somente ao teste automático, não a um app nem ao firmware
padrão. Apagar a tela pausa o trabalho visual e a contagem do ensaio.

O intervalo limitado evita depender de gestos ou atribuir uma saída antecipada
ao usuário. Não confirma por si só que o conteúdo foi visível: essa observação
continua separada dos logs de desenho/envio. Após o ensaio, os menus normais
continuam instrumentados. O próximo candidato deve ser escolhido pelos custos
das primitivas atuais, não pelos números da composição antiga.

### Ensaio automático completo e perfil das primitivas

`display_scan_detail` completou o ensaio e retornou automaticamente ao launcher.
A captura `logs/display-scan-detail-device.log`, resumida com o analisador
existente em `logs/display-scan-detail-summary.json`, contém 114 refreshes com
desenho do mostrador. Em janelas estáveis, a cena lisa ficou em aproximadamente
10–11 ms de render e 14–14,5 ms de envio; a cena com mostrador ficou perto de
117 ms de render e 16,5 ms de envio. Ambas atualizaram 169.744 pixels por quadro.
Janelas de boot, troca e saída não foram usadas para esses intervalos.

Tempos médios por refresh do mostrador, acumulados pelas seções existentes:

| Seção | Tempo aproximado |
| --- | --- |
| Face do disco central, dentro de MOTHER | 23,36 ms |
| MOTHER completo, incluindo ponteiro e centro | 28,52 ms |
| Anéis e datas | 17,34 ms (datas: 11,53 ms) |
| Horas completo | 16,11 ms (face: 8,61 ms; escala: 5,95 ms) |
| Semana | 10,41 ms |
| Temperatura | 8,41 ms |

Não somar face/escala aos grupos que já as incluem. Esses tempos abrangem
preempções e instrumentação. A diferença até o render total também inclui
LVGL, a bancada e outros desenhos. A amostra registrou no mínimo 143.575 bytes
internos livres e maior bloco DMA de 49.152 bytes, sem estabelecer mínimos seguros.

O candidato seguinte, `display_scan_circles`, altera somente
`LV_CIRCLE_CACHE_SIZE` de 8 para 16 sobre `display_scan_detail`. Ele não aumenta
o pool TLSF de 128 KiB nem os buffers de pixels. O objetivo é evitar expulsar e
recriar repetidamente máscaras de raios diferentes no mesmo refresh. O LVGL 8.3.11
libera esse cache ao terminar cada refresh; portanto não é um framebuffer nem
uma reserva de imagens entre quadros. Equivalência de pixels, pressão do heap
com todos os apps e ganho físico precisam ser comparados antes de promovê-lo.

O teste de 60 cenários com 8/16 máscaras manteve o mesmo SHA-256 de pixels,
reduziu misses de 5.749 para 1.217 e aumentou o pico amostrado do heap LVGL
de 20.096 para 22.480 bytes no host. A integração completa com 16 máscaras
passou, inclusive editor e aviso. O ensaio físico `display_scan_circles`
reduziu o render estável do mostrador de aproximadamente 117 para 110 ms;
envio e cena lisa ficaram semelhantes. É ganho pequeno, insuficiente para
concluir a correção da gelatina. Dados em `logs/display-scan-circles-device.log`.

### Candidato de reutilização do mostrador congelado

`display_scan_scene` acrescenta somente `CHRONVS_WATCH_SCENE_CACHE` sobre o
candidato de 16 máscaras. `ui/row_cache.c` codifica RGB565 por sequências
fim/cor com índices por linha, dentro de uma única reserva de **96 KiB em
PSRAM**. Essa reserva contém dados comprimidos, não um framebuffer de pixels
de 412 × 412. Os buffers LVGL e o transporte continuam exatamente iguais.
Reutiliza a linha temporária de 824 bytes que já atendia os anéis.

O cache nasce durante a renderização normal do mostrador: ao terminar cada
faixa, captura somente linhas completas, sem oclusão ou máscara externa.
Só publica o cache após receber sequencialmente todas as 412 linhas. Não força
um redraw extra para montá-lo. Enquanto hora e temperatura permanecem iguais,
redesenhos incidentais reproduzem essas linhas, recortadas à região pedida.
Isso evita rasterizar novamente ponteiros, texto, círculos e arcos durante
movimentos dos painéis sobre o mostrador estático.

Alteração da hora exibida ou da temperatura invalida a captura. Mudança de
posição ou backend, máscara externa e reserva indisponível mantêm o vetor;
linhas fora de sequência não publicam uma imagem parcial. Limite excedido
interrompe a tentativa até a próxima invalidação, sem ampliar a reserva ou
repetir compressão a cada quadro. A reserva é liberada ao excluir o objeto.
Nenhum timer visual passa a contar como atividade. Não há trabalho de cache
com a tela apagada, pois depende do desenho já permitido pelo fluxo normal.

No profiling desse candidato, `watch_setup_us` também inclui a reprodução
da face congelada; os grupos das primitivas ficam zerados nos hits. Avaliar
o render total e a duração da janela de apresentação para comparar o ganho,
separando a primeira construção dos redesenhos com cache válido.

Verificação: `run_display_scan_tests.ps1` inclui o codec (linhas parciais,
stride, invalidação, ordem e falta de capacidade); `run_watch_optimization_tests.ps1
-SceneCache` compara o vetor e o cache em 142 cenários, com redraws adicionais
para exercitar hits e mudanças de temperatura. `run_Relogio_ui.ps1 -System
-SceneCache -CircleCache16` verifica a integração com todos os apps e energia.
Nessa etapa o padrão ainda não continha as mudanças; a adoção após confirmação
do usuário está registrada no início deste documento.

O primeiro limite experimental de 64 KiB foi excedido na comparação de pixels;
o cache ficou bloqueado e o vetor continuou, conforme projetado. A reserva fixa
do candidato foi ajustada para 96 KiB antes da gravação para cobrir a composição
testada. Não há crescimento automático ao exceder esse novo limite. Essa
reserva de dados não altera os 128 KiB do pool de objetos nem os buffers LVGL.

Com o cache válido, o ensaio físico em
`logs/display-scan-scene-device.log` mediu **aproximadamente 27 ms de render e
14,8 ms de envio**, contra 117/16,5 ms no controle de oito máscaras. As janelas
estáveis do mostrador tiveram cerca de 42 ms de refresh, contra 134 ms no controle.
Isso representa cerca de 77% menos tempo de render e 69% menos tempo por refresh
nessa cena retangular, sem medir latência de toque ou FPS de todos os menus.
A primeira construção continua vetorial e teve pico próximo de 142 ms.

Os 142 cenários e redraws comparados mantiveram o SHA-256 de pixels
`E30E83A1593123662D48191EFFDA4CA2D484B7AFF276D2AB535BB7AAB9C1D604`.
A integração com todos os apps, energia, editor e aviso passou; o maior bloco
LVGL livre após editor+aviso foi 11.040 bytes antes do ajuste de cobertura.
Essa observação do host não mede folga de DMA no relógio.

O candidato também passa a informar `LV_EVENT_COVER_CHECK` corretamente: o
mostrador preenche seu retângulo com pixels opacos, embora sua classe não tenha
um fundo de estilo. Sem essa informação, LVGL desenha ancestrais atrás dele.
O callback só declara cobertura com opacidade total, sem transformação/máscara
externa e com a região pedida dentro do objeto. Equivalência de pixels e
integração são repetidas após esse ajuste.

`display_scene_quiet` prepara a avaliação perceptiva com interface normal,
cache e 16 máscaras, sem bancada, profiling, console ou espera TE. O padrão
permaneceu como referência de restauração durante o ensaio; a imagem anterior
continua preservada no diretório de referências, mesmo após adoção no padrão.

A captura final com cobertura opaca,
`logs/display-scan-scene-cover-device.log`, manteve nas janelas estáveis do
mostrador aproximadamente **23,23–23,27 ms de render**, **14,62–14,64 ms de
envio** e **36,65–36,70 ms da primeira à última escrita**. O refresh ficou
próximo de 38 ms. Em relação ao controle de oito máscaras, são aproximadamente
80% menos tempo de desenho e 72% menos tempo por refresh nessa cena; não são
percentuais medidos nos menus nem na autonomia. A equivalência de pixels foi
repetida com o mesmo hash, e a integração passou com 10.992 bytes como maior
bloco LVGL livre após editor+aviso no host. O teste confirmou a reprodução por
faixas, com primitivas vetoriais zeradas nas janelas de cache válido.

Para repetir as verificações e avaliar a versão silenciosa:

```powershell
./tests/run_display_scan_tests.ps1
./tests/run_watch_optimization_tests.ps1 -SceneCache
./tests/run_Relogio_ui.ps1 -System -SceneCache -CircleCache16
pio run -e display_scene_quiet -t upload
# Restaurar o padrão, se necessário:
pio run -t upload
```

### Resultado e limites

O controle recompunha vetores de um mostrador que não havia mudado. O cache
elimina a maior parte desse custo nos redraws e preserva o desenho completo
quando hora/temperatura mudam. O ganho de tempo foi demonstrado no relógio;
ausência de gelatina ainda exige avaliação visual dos menus no candidato final.

TE isolado não melhorou o efeito, conforme relato do usuário. A redução de
desenho não transforma a GRAM em duas imagens com troca atômica: se persistir
um artefato, medir os menus com o cache válido, separando custo do painel e
apresentação. Não assumir que o custo dos menus seja igual ao desta bancada.
Os caches anteriores de face principal e ícones continuam experimentos separados.

Buffers, pool TLSF, período de refresh, transferências e drain permanecem
preservados. A captura dos dois fundos foi completada automaticamente e o
usuário confirmou melhora clara na versão silenciosa. A configuração foi
adotada no padrão; o controle `display_scan` mantém explicitamente as flags
anteriores de oito máscaras e sem cache. Faltam medições de autonomia e de
eventual gelatina residual. Não há ganho de corrente quantificado nem promessa
de 50 FPS. TE permanece restrito ao perfil experimental.
