# Diagnóstico de fluidez

O ambiente padrão compila o LVGL com `-O2` e não instala callbacks de diagnóstico.
Também desativa logs da aplicação/ESP-IDF e do bootloader de software em
compilação (`LOG_DEFAULT_LEVEL=0`, `LOG_MAXIMUM_LEVEL=0`) e os consoles UART
e USB (`ESP_CONSOLE_NONE`, `ESP_CONSOLE_SECONDARY_NONE`). Os `printf` diretos
dos drivers usados são eliminados pela política de compilação aplicada em
`scripts/add_waveshare_drivers.py`, sem avaliar seus argumentos.
Não há REPL da aplicação registrado. As mensagens iniciais da ROM do chip
podem permanecer; não alteramos eFuses, strapping ou o mecanismo de upload.
As bibliotecas pré-compiladas podem conservar código de formatação interno;
desabilitar o console não recompila esses arquivos.
Os ambientes opcionais `display_profile` e `display_profile_o2` permitem
comparar o mesmo fluxo no relógio. O segundo altera somente a compilação dos
fontes LVGL para `-O2`; o primeiro preserva explicitamente o LVGL em debug
(`custom_lvgl_optimization = -O0`, resultando em `-Og` no PlatformIO atual).
Aplicação, ESP-IDF e drivers mantêm as opções anteriores em todos os ambientes.
O workaround histórico do compilador não é removido globalmente.
Embora o INI declare `-O0`, o PlatformIO em modo debug aplica `-Og` nos objetos
inspecionados. Portanto a comparação real é baseline debug versus LVGL `-O2`,
e não se deve inferir ausência total de otimização apenas pelo INI.
Antes da configuração CMake, os dois diagnósticos copiam o `sdkconfig` do
ambiente padrão local, incluindo seus ajustes de pilhas, Wi-Fi e PSRAM.
Depois, `scripts/configure_logging.py` reativa logs INFO e os consoles UART/USB
somente nos diagnósticos. Assim, a comparação entre os dois diagnósticos tem
a mesma política de logs; o padrão não paga esse custo de saída. O script
aplica a política também a `sdkconfig` já existente, sem alterar opções de hardware.
Compile primeiro o ambiente padrão; alterações manuais nos `sdkconfig` dos
diagnósticos são substituídas na próxima compilação.

Ambos preservam atualização de 20 ms, buffers duplos de 1/20 em PSRAM,
pool TLSF de 128 KiB em PSRAM, transferências de 2 KiB, espera síncrona QSPI
e debounce do touch. Não há alteração visual ou de gesto.
O scanner de endereços I2C do boot é executado somente nos diagnósticos;
o padrão inicializa normalmente os dispositivos, sem sondagens extras.

O cache de máscaras circulares do LVGL usa oito entradas (`LV_CIRCLE_CACHE_SIZE`),
para reutilizar raios entre as faixas de um mesmo refresh. Os dados de
antialiasing são alocados no pool TLSF existente em PSRAM e liberados pelo
LVGL ao terminar o refresh. Não há cache de framebuffer. A comparação com
quatro entradas está em `tests/run_watch_render_tests.ps1`: verifica SHA-256
dos pixels, recálculos de máscaras e memória amostrada durante o desenho.
O ganho de tempo no dispositivo ainda aguarda medição; veja a
[análise do cache](performance-audit.md#cache-de-máscaras-circulares).

## Comparação no dispositivo

Use o PlatformIO instalado em `C:\Users\gabri\.platformio\penv\Scripts\platformio.exe`
se `pio` não estiver no PATH. Execute na raiz:

```powershell
pio run -e display_profile -t upload
pio device monitor -p COM3 -b 115200
```

Após terminar NTP, repita durante cerca de 15 segundos cada etapa:
mostrador parado; abrir/fechar painel rápido; abrir/rolar/fechar launcher;
abrir/voltar de apps. Identifique as etapas junto aos logs. Feche o monitor
antes de outro upload. Repita os mesmos gestos, brilho e condições com:

```powershell
pio run -e display_profile_o2 -t upload
pio device monitor -p COM3 -b 115200
```

Valide também Clima com rede, aviso de timer com som e apagar/acordar.
Se surgirem listras, travamentos ou regressão de toque, registre a etapa e
retorne à referência debug instrumentada para isolar a otimização:

```powershell
pio run -e display_profile -t upload
```

Para uso normal, com LVGL otimizado e sem os logs de diagnóstico:

```powershell
pio run -e waveshare_esp32_s3_touch_lcd_146 -t upload
```

Cada ambiente possui sua própria pasta `.pio/build/<ambiente>`; os builds
experimentais não substituem os binários do ambiente padrão. Não execute
builds simultâneos: o script de preparação compartilha a referência Waveshare.

## Leitura dos logs

### Captura e resumo por cenário

Com o firmware `display_profile_o2` gravado, aguarde terminar o NTP e abra
o monitor abaixo. O filtro instalado `log2file` salva a saída em
`logs/device-monitor-<data-hora>.log` e imprime o caminho ao iniciar.
Essa pasta é ignorada pelo Git. O padrão sem diagnóstico não produz essas métricas.

```powershell
pio device monitor -e display_profile_o2 -p COM3 -b 115200 -f log2file
```

Faça uma captura separada de cerca de 15 s para cada cenário: mostrador
parado, abrir/fechar painel continuamente, rolar launcher continuamente.
Encerre com Ctrl+C entre capturas e anote qual arquivo corresponde a cada
cenário. Remova do arquivo de análise as linhas de transição entre cenários;
um resumo representa a janela inteira anterior, de aproximadamente 2 s.
Mantenha brilho, perfil de energia e ritmo dos gestos comparáveis. Feche o
monitor antes de trocar firmware. A coleta requer interação física no relógio.

Analise os arquivos (substituindo os nomes pelos caminhos salvos):

```powershell
python scripts/analyze_display_profile.py logs/mostrador.log logs/painel.log logs/launcher.log
```

Se Python não estiver no PATH, use
`C:\Users\gabri\.platformio\penv\Scripts\python.exe` com `&` no PowerShell.
O relatório JSON separa arquivos e a otimização informada no banner, quando
presente; `unknown` significa que o banner não foi capturado. Não deduz o
build a partir da velocidade observada. Use arquivos distintos também para
builds ou sessões diferentes.

- As médias de refresh, flush e pixels são ponderadas por quadros; o sufixo
  `_approx` lembra que o firmware já arredondou cada média para baixo.
- `refreshes_per_observed_second` considera apenas as janelas presentes no
  arquivo. Não é FPS máximo nem média de toda a sessão: intervalos apagados,
  sem redraw e logs ausentes não entram no denominador.
- Tempos máximos preservam o maior valor; memória usa o menor valor amostrado.
  Os mínimos não estabelecem um limite seguro de RAM/DMA.
- `touch_windows` informa quantas janelas contêm as métricas novas de touch.
  Logs antigos sem esses campos produzem `null`, não uma falsa medição zero.
- Linhas incompletas/inconsistentes geram aviso e saída com código 1; as
  janelas válidas ainda são resumidas. Um arquivo sem dados também falha.
  Perdas de linhas inteiras ou truncamento que preserve números válidos não
  são detectáveis sem numeração/checksum no protocolo.

A ferramenta não conecta à serial nem altera firmware. Validação:
`python tests/test_display_profile_analysis.py`, usando dados sintéticos para
ponderação, extremos, arquivos UTF-8/UTF-16, logs antigos e entradas inválidas.

### Campos emitidos pelo firmware

Os diagnósticos também separam o desenho vetorial do mostrador. Estes campos
ficam na mesma linha `display_perf` e são **totais em microssegundos por
janela**, não médias por faixa:

| Campo | Trecho medido |
| --- | --- |
| `watch_setup_us` | Contexto, recorte de superfícies opacas e atualização do cache |
| `watch_background_us` | Limpeza opaca do fundo |
| `watch_geometry_us` | Interpolação da hora, ângulos e posições orbitais |
| `watch_case_us` | Aros externos e números da data |
| `watch_rings_us` | Somente os dois círculos externos, com preenchimento e borda |
| `watch_dates_us` | Somente os 31 números da data |
| `watch_mother_us` | Disco central, ponteiro de minutos e centro |
| `watch_minutes_us` | Escala de minutos, números e traços |
| `watch_hours_us` | Submostrador de horas |
| `watch_weekday_us` | Submostrador de dia da semana |
| `watch_temperature_us` | Submostrador de temperatura |
| `watch_seconds_us` | Submostrador de segundos |
| `watch_marker_us` | Marcador fixo da data |

`watch_case_us` é a soma de `watch_rings_us` e `watch_dates_us`, preservada
para comparação com logs anteriores. Esses dois detalhes **não devem ser
somados novamente ao total**. O analisador os apresenta separadamente em
`watch_case_detail_avg_us`, com cobertura em `watch_case_detail_windows` e
`watch_case_detail_frames`. Só janelas com ambos os detalhes entram nessas
médias; logs antigos retornam `null`. Detalhes incompletos ou cuja soma difere
de `watch_case_us` tornam a linha inválida. O marcador triangular da data
continua em `watch_marker_us`, fora dessa soma.

`watch_slices` conta entradas no callback customizado, inclusive as que saem
por estarem cobertas. `watch_frames` conta atualizações concluídas em que ele
foi chamado, no máximo uma vez por refresh, mesmo com várias faixas.
Não é uma contagem de telas inteiras nem prova visibilidade no painel.
O analisador soma cada grupo e divide por `watch_frames`, produzindo
`watch_section_avg_us`. Janelas sem mostrador não diluem a média; logs antigos
ou capturas sem quadros do mostrador retornam `null`. `watch_windows` informa
a cobertura desses campos na captura.

Os tempos incluem preempções e pequeno custo da instrumentação. Os grupos
principais não se sobrepõem (os detalhes de `case` já estão incluídos nele),
mas sua soma não inclui todo o refresh LVGL nem o flush QSPI.
Não some esses valores novamente ao tempo de refresh. Os marcadores não
criam tarefa, alocação ou saída serial por faixa; o resumo continua a cada 2 s.
No build padrão, macros eliminam os marcadores e seus acessos ao relógio.

Para localizar o custo principal, repita o upload de `display_profile_o2` e
capture primeiro pelo menos 15 s do mostrador parado, depois painel/launcher
em arquivos separados. Envie também o resultado do upload, pois o parâmetro
`-e` do monitor não comprova o firmware gravado. Não compare pequenos ganhos
com uma captura sem essa instrumentação adicional como se o custo fosse igual.

O prefixo `display_perf` identifica resumos emitidos no máximo uma vez a cada
2 segundos, sem logging por frame ou por bloco QSPI:

- `window_ms`, `frames`: duração da janela e atualizações efetivas. Poucos
  frames em repouso são esperados; isso não mede a capacidade máxima do painel.
- `refresh_avg_ms`, `refresh_max_ms`: tempo LVGL de atualização, incluindo
  layout, desenho e flush síncrono, com resolução do tick LVGL.
- `over20`: atualizações cujo tempo excedeu 20 ms; não é contagem de frames perdidos.
- `flush_avg_us`: soma dos tempos dos flushes dividida pelas atualizações,
  incluindo chamadas do driver, envio e espera QSPI.
- `px_avg`: pixels processados por atualização, útil para comparar a carga.
- `internal_free`, `dma_largest`: RAM interna livre de 8 bits e maior bloco
  interno apto a DMA, em bytes, amostrados ao emitir o resumo.
- `touch_reads`, `touch_read_max_us`: chamadas ao leitor do touch e maior
  duração de uma chamada, incluindo o backend I2C quando ele é executado.
- `touch_gap_max_us`: maior intervalo entre leituras consecutivas enquanto
  o contato permanece pressionado.
- `interaction_frames`, `frame_gap_max_us`: atualizações concluídas durante
  contato e maior intervalo entre elas no mesmo contato. Um dedo parado
  pode não exigir novos quadros; intervalos longos não provam travamento.
- `input_refresh_max_us`: maior espera da primeira mudança de posição/estado
  ainda pendente até a próxima atualização concluída. Essa atualização pode
  ter outra causa; não mede latência física nem prova resposta ao gesto.

A diferença entre refresh e flush é apenas uma estimativa do trabalho fora
do driver; os relógios têm resoluções diferentes e incluem preempções.
Não mede latência completa do toque, tempo dos demais timers ou abertura
do app fora do refresh. O logging também tem custo: compare ambos os builds
instrumentados e confirme o resultado final sem instrumentação.
Com tela apagada, as amostras são descartadas e não há resumo nem consulta
de heap. Nenhum timer ou tarefa extra é criado.
As métricas de touch usam o callback original, sem alterar sua cadência de
30 ms nem o debounce. Contadores e intervalos são reiniciados por janela;
janelas sem atualizações não emitem resumo. Compare os intervalos durante
arrastes contínuos equivalentes, separando repouso, painel e launcher.

Compilação bem-sucedida, isoladamente, não confirma ganho de fluidez ou
estabilidade física. Novas alterações continuam exigindo comparação no painel.

## Validação de preparação

Os três ambientes compilaram com sucesso. As opções ativas dos `sdkconfig`
dos diagnósticos foram comparadas com o padrão local, sem diferenças.
A informação de compilação dos objetos confirmou `-Og` no LVGL de referência
e `-O2` no LVGL experimental; os objetos inspecionados de `main.c` e do driver
LVGL mantiveram `-Og` em ambos.

## Validação física e adoção no padrão

O usuário relatou que `display_profile_o2` ficou perceptivelmente mais fluido
e leve. Depois confirmou funcionamento nos fluxos propostos: Clima com rede,
timer com som, apagar/acordar repetidamente e abrir, rolar e fechar aplicativos.
Com essa validação, `-O2` foi adotado somente no LVGL do ambiente padrão,
sem habilitar `CHRONVS_DISPLAY_PROFILE`. Os diagnósticos continuam disponíveis
para comparação com a referência debug.

Após o upload do firmware padrão com LVGL em `-O2`, sem instrumentação e com
logs/consoles desativados, o usuário confirmou em 24/09/2026 que o relógio
está "extremamente fluido". Essa configuração passa a ser a base validada
para as próximas mudanças. Não houve alteração dos parâmetros do painel.

O ganho é qualitativo, relatado no dispositivo; não foram fornecidos logs
para quantificar FPS, latência ou autonomia, nem isolar o ganho adicional da
retirada de logs. O texto do Vox e demais informações da interface continuam
disponíveis na tela.

Validação da política de logs: os três builds passaram, e os dois testes de
`tests/test_logging_config.py` verificaram alternância, preservação das opções
de hardware, herança e reaplicação sem reescrita desnecessária. O cabeçalho de
configuração gerado confirmou níveis zero e consoles desativados no padrão.
Mensagens representativas de boot, navegação, Vox, drivers e profiling ficaram
ausentes do binário padrão e presentes nos diagnósticos (onde aplicável).
O binário padrão passou de 1.775.120 para 1.659.376 bytes, cerca de 113 KiB a
menos; essa redução não é uma medição de fluidez ou autonomia.

## Validação das correções de runtime

Em 24/09/2026, o usuário confirmou "tudo certo nos meus testes" após as
correções da espera do loop, contagem do cronômetro durante suspensão,
redraws em pausa e ciclo de vida/captura do Vox. Os defaults também foram
alinhados à configuração local validada. Essa revisão é a nova base física,
sem mudança nos parâmetros críticos do display e sem medição quantitativa
de desempenho. A análise, os testes automatizados e a pendência de memória
no teste integrado do host estão em [`performance-audit.md`](performance-audit.md).

Na revisão seguinte, estilos compartilhados e callbacks restritos a alvos
clicáveis resolveram a falha de memória da suíte integrada, mantendo o pool
de 128 KiB. Build e suítes de interface passaram. O usuário confirmou no
relógio: "tudo perfeito nos meus testes aqui". Essa é a nova base validada;
os números de memória do host estão na análise e não representam uma medição
equivalente no ESP32.

Também foram validadas no relógio a espera do áudio por notificações, em
vez de polling ocioso, e a consulta da agenda civil uma vez por segundo.
Timer e soneca preservam seus prazos monotônicos em cada passagem do loop.
Build e testes de serviço passaram; a redução de conversões da agenda foi
medida no host, sem quantificar ganho de autonomia no dispositivo.

O NTP passou a usar uma tarefa temporária por sessão, liberando a pilha
entre sincronizações. Build e testes de NTP/Clima/Wi-Fi passaram, e o usuário
confirmou "tudo certo nos testes" no relógio. O relato não discrimina cada
cenário nem mede memória recuperada; os limites estão na análise de desempenho.

A consolidação das atualizações do mostrador, o cache de estilos do launcher,
a consulta RTC por minuto e a retirada do scanner I2C do padrão também foram
testados no relógio pelo usuário: "tudo funcionando perfeito". Os três builds
e os testes de RTC/interface passaram. A próxima etapa é a captura das métricas
de touch e renderização descritas acima; ainda não há medidas físicas para
justificar alterações na cadência do touch ou quantificar o ganho desta revisão.
