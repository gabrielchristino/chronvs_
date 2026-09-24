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

A diferença entre refresh e flush é apenas uma estimativa do trabalho fora
do driver; os relógios têm resoluções diferentes e incluem preempções.
Não mede latência completa do toque, tempo dos demais timers ou abertura
do app fora do refresh. O logging também tem custo: compare ambos os builds
instrumentados e confirme o resultado final sem instrumentação.
Com tela apagada, as amostras são descartadas e não há resumo nem consulta
de heap. Nenhum timer ou tarefa extra é criado.

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
