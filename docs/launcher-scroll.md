# Revisão do scroll no relógio

O perfil `launcher_scroll_autotest` testa a lista real no ESP32-S3 e no display
QSPI. Herda a configuração atual do firmware: O2, cache de instruções de
32 KiB, ícones vetoriais, fade do disco, filtro de entrada e scroll sem inércia.
Não habilita os experimentos antigos de ícones, recorte ou posições FLOATING.

## O que está sendo preservado

| Tratativa atual | Efeito e custo restante |
| --- | --- |
| Linhas recicladas | O catálogo atual usa oito linhas, sem duplicar árvores a cada volta. |
| Curva com timer de 20 ms e flag dirty | Não recalcula os estilos em repouso; pausa fora do launcher e retorna com tela apagada. |
| Comparação dos estilos anteriores | Evita setters e invalidações quando posição horizontal e opacidade não mudaram. |
| Fade só no disco de 60 px | Evita compor a linha inteira; ainda exige camada temporária alfa durante o fade. |
| Subárvores invisíveis ignoradas | Não desenha ícone/nome quando a linha está totalmente transparente. |
| Scroll nativo sem elasticidade/inércia | Mantém geometria e hit test nativos; soltar deve produzir zero deslocamento adicional. |
| Filtro Q8 de 50 ms, atraso limitado a 48 px | Suaviza a entrada; parado durante o contato ainda pode completar o atraso do filtro. |
| PRESS_LOCK, limite de toque e guarda de 150 ms | Evitam seleção por deslize ou contato logo após scroll. |
| Sem SCROLL_ON_FOCUS | Tocar uma linha não deve animar um segundo scroll para enquadrá-la. |

O limitador antigo de velocidade de soltura permanece no código, mas não
produz inércia com SCROLL_MOMENTUM desligado. O filtro depende da ordem de
eventos do LVGL 8.3.11. O launcher seleciona até oito apps visíveis, compatível
com o limite atual de dez apps do runtime e seus dois apps internos.

O LVGL invalida o objeto de lista em `_lv_obj_scroll_by_raw`; portanto reduzir
setters de estilos não significa reduzir proporcionalmente os pixels enviados.
É necessário medir desenho e flush separadamente, antes de escolher outra
otimização. O timer de 20 ms também não garante quadros de 20 ms no painel.

## Roteiro no hardware

Após oito segundos de boot e um contato de despertar, abre o launcher e usa
o ponteiro LVGL normal com coordenadas programadas. O serviço de voz e todos
os oito ícones são os do firmware real. Nenhum app é selecionado pelo roteiro.

São oito caminhos, repetidos três vezes: subir lento, subir rápido, subir e
segurar, inverter após subir, descer lento, descer rápido, descer e segurar,
inverter após descer. Os três arrastes no mesmo sentido acumulam distância
para atravessar a passagem entre último e primeiro. Cada contato começa
parado por 90 ms; os movimentos duram 900, 180, 250 ou 700 ms. Os casos com
pausa mantêm o ponto final por 700 ms antes de soltar. Após cada soltura,
espera 650 ms e exige fase inalterada, descontando a normalização circular.
Ao final, exige dois segundos sem refresh e passagens circulares nos dois sentidos.

Cada caso verifica app ativo, raiz em y=0, oito linhas retidas, espaçamento
módulo ciclo, conteúdo central visível, movimento e preferências preservadas.
Mede desenho médio, flush síncrono médio, maior refresh, pixels enviados e
maior intervalo entre leituras. Registra RAM interna livre, maior bloco DMA
e heap LVGL após as passagens do loop. Essas amostras não garantem capturar
o pico transitório de uma camada alfa dentro de um refresh.

Os tempos incluem o caminho real de desenho e envio ao painel, com buffers
duplos `1/20`, pool TLSF de 128 KiB em PSRAM, refresh de 20 ms e QSPI síncrono
de 2 KiB. A entrada física fica reservada durante o roteiro e é restaurada
ao terminar ou abortar. Um modal ou timeout interrompe o ensaio. As preferências
persistentes não são alteradas. O ensaio termina no mostrador.

O roteiro usa coordenadas sintéticas **no hardware**. Não mede ruído do sensor,
percepção de fluidez ou tearing óptico. Esses itens exigem dedo/observação do
painel e não podem ser concluídos pelos logs nem pelas capturas no computador.
Os logs do diagnóstico também diferem do padrão silencioso.

## Executar e restaurar

Preserve primeiro a referência padrão com `scripts/firmware_reference.py`.

```powershell
pio run -e launcher_scroll_autotest -t upload
python scripts/capture_display_scan.py --seconds 150 --stop-on-done --output logs/launcher-scroll-device.log
python scripts/analyze_launcher_scroll.py logs/launcher-scroll-device.log --output logs/launcher-scroll-summary.json
# Retornar ao firmware padrão silencioso, sem roteiro:
pio run -t upload
```

A captura deve começar imediatamente após o upload. O analisador só aprova
uma sequência completa de 24 resultados, três por caminho, com oito linhas,
zero deslocamento após soltar, repouso sem refresh e leitura física restaurada.
Logs e referências de firmware permanecem locais e ignorados pelo Git.

## Isolamento do custo de desenho

O perfil agora acrescenta uma comparação controlada após os 24 gestos e o
repouso. A lista real passa pelas mesmas 32 fases (`i * 23 mod 656`), três
vezes em cada variante. Cada quadro invalida a mesma área completa de
412 × 412 e exige exatamente 169.744 pixels enviados; isso elimina diferenças
de área suja e de quantidade de amostras de toque da comparação entre desenhos.
O isolamento inicial usou 864 quadros. A comparação de correção acrescenta
`full_reference`, com o desenho vetorial anterior, totalizando **960 quadros**
medidos e um aquecimento por variante/rodada.

| Variante | Única remoção em relação à lista completa, salvo controles de fundo |
| --- | --- |
| `full_reference` | Desativa somente a otimização do fundo para comparar a referência no mesmo binário. |
| `full` | Composição completa atual. |
| `no_icons` | Desenho dentro dos ícones de 44 px; conserva discos e fade. |
| `no_labels` | Nomes; conserva a geometria das linhas. |
| `no_badges` | Subárvore completa dos discos e ícones, incluindo sua composição alfa. |
| `no_fade` | Opacidade parcial; conserva o mesmo limite de invisibilidade e as posições. |
| `no_clip` | Recorte circular dos filhos na raiz; conserva o fundo redondo. |
| `background` | Controle sem discos/ícones e nomes, mantendo o fundo redondo e recorte. |
| `square_background` | Mesmo controle de fundo, agora retangular e sem recorte. |
| `full_check` | Repete a referência ao final de cada rodada. |

As diferenças medem o efeito de remover cada parte **na composição atual**;
não são parcelas independentes para somar, porque máscaras, camadas alfa e
cobertura interagem. Os controles sem conteúdo isolam o custo residual da
composição de fundo. As variantes alteram temporariamente o visual apenas
nesse diagnóstico; nenhuma é adotada como otimização.

Um hook compilado somente no diagnóstico atualiza imediatamente curva,
posição e opacidade antes de cada quadro, pelo código real do launcher.
Mede-se separadamente esse preparo e o desenho/envio. Essa etapa usa posições
programadas e refresh completo, não a cadência natural do arraste: serve para
comparar custos na mesma geometria, não para declarar FPS de uso.
Um contato sintético parado na faixa superior mantém a política normal de
atividade durante a comparação, sem alterar preferências. Ao terminar, o
desenho completo e os callbacks de entrada são restaurados.

## Resultado físico em 19/11/2026

Build e gravação pela COM3 passaram, com hashes das quatro imagens verificados.
O roteiro concluiu aos 48,919 s de uptime: **24/24**, nenhuma falha, oito linhas,
três passagens circulares por sentido e leitura física restaurada. Foram
277 refreshes durante os casos. Todos terminaram com zero deslocamento após
soltar; o repouso final teve zero refresh em dois segundos. O analisador
retornou `complete=true`, sem os padrões de erro de SPI/panic/assertion buscados.

Médias ponderadas pelos refreshes das três repetições; os tempos de desenho
e envio não incluem os intervalos parados do roteiro nem representam FPS total.

| Caminho | Desenho médio | Envio QSPI médio | Maior refresh |
| --- | ---: | ---: | ---: |
| Subir lento | 41,99 ms | 13,03 ms | 64,63 ms |
| Subir rápido | 37,66 ms | 11,95 ms | 63,27 ms |
| Subir e segurar | 37,22 ms | 11,04 ms | 64,34 ms |
| Inverter após subir | 41,20 ms | 12,90 ms | 64,74 ms |
| Descer lento | 41,27 ms | 12,57 ms | 64,89 ms |
| Descer rápido | 38,27 ms | 11,34 ms | 65,09 ms |
| Descer e segurar | 36,13 ms | 11,41 ms | 64,52 ms |
| Inverter após descer | 39,68 ms | 12,39 ms | 64,13 ms |

O maior intervalo entre leituras programadas foi **72 ms**. Desenho + envio
ficou em aproximadamente **47,5–55,0 ms por refresh**, apesar do período
configurado de 20 ms. O desenho respondeu por cerca de três quartos desse
custo. O volume médio foi de 136.530 a 161.999 pixels por refresh, cerca de
80–95% dos 169.744 pixels do buffer lógico da tela. Assim, a lista ainda
redesenha áreas grandes durante scroll; economizar setters em repouso não
elimina esse custo durante movimento.

As mínimas observadas entre passagens do loop foram **136.643 bytes internos
livres**, maior bloco DMA **32.768 bytes**, heap LVGL livre **112.196 bytes** e
maior bloco LVGL **111.516 bytes**. Os quatro valores foram iguais nos 24 casos;
não apareceu crescimento de memória retida nessa sequência. A medição não
captura necessariamente reservas transitórias durante o desenho nem valida
concorrência com voz, HTTPS ou todos os apps já criados.

O diagnóstico aponta o desenho como prioridade de investigação. Ainda não
separou quanto pertence ao recorte circular, fade alfa, textos ou ícones.
Antes de mudar o visual, a próxima comparação deve instrumentar esses custos
no próprio scroll e alterar uma variável por vez, mantendo este roteiro como
referência. Nenhuma otimização de produção foi adotada a partir deste ensaio.

Artefatos locais: `logs/launcher-scroll-device.log`,
`logs/launcher-scroll-summary.json`, `logs/launcher-scroll-build.log` e
`logs/launcher-scroll-upload.log`. Referência padrão preservada e verificada em
`logs/firmware-references/20261119T201524831273Z-waveshare_esp32_s3_touch_lcd_146/`.

Ao concluir, o build padrão passou em 160,30 s e o firmware silencioso foi
restaurado pela COM3 em 92,94 s, com os quatro hashes de gravação verificados
e reset por RTS. Registros em `logs/launcher-scroll-default-build.log` e
`logs/launcher-scroll-restore.log`. O relógio não ficou com o roteiro instalado.

## Resultado do isolamento no hardware — 19/11/2026

O segundo ensaio passou novamente os 24 gestos e concluiu **27/27 grupos de
comparação**, com **864/864 quadros medidos**, exatamente 169.744 pixels por
quadro. Três repetições de cada variante, sempre nas mesmas fases. A referência
completa reaplicada ao fim de cada rodada mediu 43,300 ms, contra 43,283 ms
no início: diferença de apenas 0,017 ms. O analisador aprovou a sequência
completa, o repouso, as passagens circulares e a restauração da entrada física.

| Composição | Desenho médio | Redução frente à completa | Envio QSPI médio |
| --- | ---: | ---: | ---: |
| Completa | 43,283 ms | — | 13,548 ms |
| Sem vetores dos ícones, conservando discos/fade | 35,453 ms | 7,830 ms | 13,252 ms |
| Sem nomes | 35,594 ms | 7,689 ms | 13,290 ms |
| Sem discos e ícones, incluindo sua composição | 28,699 ms | 14,584 ms | 13,061 ms |
| Sem opacidade parcial/fade | 38,736 ms | 4,547 ms | 13,348 ms |
| Sem recorte dos filhos na raiz | 42,199 ms | 1,084 ms | 13,520 ms |
| Apenas fundo redondo e estrutura | 22,765 ms | 20,518 ms | 12,979 ms |
| Apenas fundo retangular e estrutura, sem recorte | 9,545 ms | 33,738 ms | 12,941 ms |
| Completa reaplicada ao final | 43,300 ms | −0,017 ms | 13,553 ms |

O maior intervalo entre a menor e maior média de desenho entre as três
repetições da mesma variante foi 0,053 ms. O preparo da geometria/estilos da
composição completa ficou em **2,552 ms por quadro**, medido separadamente
do desenho. O maior refresh completo observado nessa comparação foi de
63,645 ms, incluindo envio.

### O que os resultados permitem concluir

- O fundo redondo com a estrutura remanescente já custa **22,765 ms**.
  Trocá-lo pelo controle retangular reduz **13,220 ms**; essa diferença envolve
  geometria circular, máscaras e decisões de cobertura do LVGL. Não corresponde
  a uma medição exclusiva de uma chamada de preenchimento.
- Discos e ícones juntos acrescentam um efeito de **14,584 ms** nessa composição.
  O desenho vetorial dos ícones sozinho responde por uma redução de **7,830 ms**
  quando omitido, mantendo os discos e seu fade.
- Os nomes também pesam: omiti-los reduz **7,689 ms**.
- O fade contribui, mas não domina: removê-lo reduz **4,547 ms**, cerca de 10,5%
  do desenho. Retirar apenas o recorte dos filhos reduz **1,084 ms**, cerca de
  2,5%. O recorte dos filhos é diferente do desenho do fundo arredondado.
- O trabalho de preparar posições/estilos é bem menor que os 43,283 ms de desenho;
  o QSPI continua exigindo cerca de 13 ms quando a área enviada é mantida fixa.

As reduções **se sobrepõem e não devem ser somadas**. Por exemplo, omitir os
discos já elimina os ícones e parte do trabalho de fade. Nenhum controle vazio
ou sem fade foi adotado como interface final. O próximo candidato deve reduzir
o trabalho de fundo circular/redesenho e composição de discos preservando a
aparência, antes de remover recursos visuais por suposição.

Artefatos locais: `logs/launcher-cost-device.log`,
`logs/launcher-cost-summary.json`, `logs/launcher-cost-build.log`,
`logs/launcher-cost-upload.log` e `.pio/host-tests/launcher-cost-host.log`.
Referência padrão preservada e verificada em
`logs/firmware-references/20261119T203406759626Z-waveshare_esp32_s3_touch_lcd_146/`.
O diagnóstico concluiu aos 97,130 s de uptime; não houve erro nos padrões
SPI/panic/assertion buscados. A suíte integrada de interface também passou,
assim como o build padrão, que manteve 1.694.046 bytes de programa e
55.400 bytes estáticos.

O firmware padrão silencioso foi restaurado pela COM3 em **91,70 s**, com
os quatro hashes verificados e reset por RTS. Registro em
`logs/launcher-cost-restore.log`; build padrão em
`logs/launcher-cost-default-build.log` (29,67 s). O relógio voltou ao padrão,
sem os seletores de desenho do diagnóstico.

## Correção do fundo: tentativas e contrato

O primeiro candidato dividia o fundo em preenchimento retangular interno e
duas bordas pelo LVGL. As 110 capturas ficaram idênticas, mas no relógio o
desenho subiu de **43,277 para 46,922 ms** por quadro. Foi descartado. Logs em
`logs/launcher-fill-device.log` e `logs/launcher-fill-summary.json`.

O segundo candidato registra somente o fundo antes dos filhos, a partir das
faixas já desenhadas normalmente. Usa RLE RGB565 em uma reserva limitada de
**16 KiB em PSRAM**, fora do pool LVGL, sem framebuffer de tela inteira ou
renderização extra. O fundo medido ocupa **9.724 bytes** codificados. A primeira
cópia por pixel caiu de **43,058 para 37,498 ms** no mesmo binário. A versão
final usa `lv_color_fill` para copiar os trechos iguais com a rotina do LVGL.

`ui/circle_fill.c` conserva os estilos e o `clip_corner` do launcher. Reaproveita
o fundo apenas em `(0,0)`, tamanho 412 × 412, com o launcher sozinho na camada
de apps, mesma cor e fundo dos ancestrais, renderizador software RGB565 opaco,
sem máscara externa, gradiente, imagem, borda, sombra ou outline. Se uma dessas
condições não for atendida, desenha pelo LVGL original. Isso preserva a
composição sobre o mostrador durante abertura, fechamento e cancelamento.

Os eventos `DRAW_PART_BEGIN/END` gravam antes dos filhos e pulam apenas a
pintura de fundo já registrada. O LVGL ainda instala e remove sua máscara
normal para os filhos. A gravação exige linhas completas e sequenciais; falta
de espaço, captura incompleta ou falha de alocação mantém o vetor. A cor ou
o fundo dos ancestrais invalida o cache. A exclusão do objeto libera a reserva.
Não cria tarefas, timers, leituras de hardware ou atividade artificial no padrão.

A suíte integrada passou com cache e com falha simulada da reserva de 16 KiB.
As 110 capturas refeitas foram idênticas à referência, incluindo fade, estados
pressionados, scroll e bandeja deslocada. Logs em
`.pio/host-tests/launcher-background-fast-host.log`,
`.pio/host-tests/launcher-background-host-fallback.log` e
`logs/launcher-background-fast-pixels.json`. O teste físico continua sendo o
critério para desempenho e comportamento do painel.

### Resultado adotado: cópia dos trechos com LVGL

A versão final passou **24/24 gestos de scroll**, **30/30 grupos de comparação**
e **960/960 quadros**, sempre com 169.744 pixels enviados. A referência vetorial
e o candidato foram medidos no mesmo firmware, com as mesmas 32 fases repetidas
três vezes. O cache esteve válido, com **672 reutilizações por grupo completo**;
na referência vetorial o contador de reutilizações foi zero.

| Medida por quadro | Vetor anterior | Cache final |
| --- | ---: | ---: |
| Desenho médio | 43,238 ms | 34,160 ms |
| Envio síncrono QSPI médio | 13,663 ms | 13,714 ms |
| Desenho + envio | 56,901 ms | 47,874 ms |
| Maior refresh observado na comparação | 63,698 ms | 55,007 ms |
| Preparo de curva/estilos, separado do desenho | 2,531 ms | 2,530 ms |

O desenho caiu **21,0%**; desenho + envio caiu **15,86%**. A referência completa
reaplicada ao final mediu 34,168 ms, confirmando estabilidade. O fundo sem
conteúdo ficou em 13,024 ms. Permanecem os custos dos ícones, discos, nomes e
composição dos filhos; essa correção não deve ser apresentada como 50 FPS
ou ausência garantida de gelatina óptica. A leitura do sensor físico e a
percepção com dedo continuam distintas do roteiro de coordenadas no hardware.

Também passaram **42/42 transições das bandejas** no perfil
`panel_autotest_lighting`, incluindo cancelamentos e preservação das
preferências. O HTTPS real do Clima passou após o roteiro, com cache/persistência
normais. Foram observados 79.703 bytes internos livres e maior bloco DMA de
31.744 bytes durante a consulta: amostras desse fluxo, não limites seguros
para outras combinações de serviços. Nenhum dos dois logs apresentou os
padrões SPI/panic/assertion/abort buscados.

A suíte de interface acrescentou comparação direta entre cache e vetor em
12 cenários: cor do fundo e dos ancestrais, deslocamentos, preview com outro
objeto visível, tamanho, raio, opacidade, outline, máscara externa, retorno à
composição original e redraw parcial. Todos passaram, incluindo exclusão do
objeto. Registro em `.pio/host-tests/launcher-background-final-host.log`.

Artefatos locais da versão final:

- `logs/launcher-background-fast-device.log` e `launcher-background-fast-summary.json`;
- `logs/launcher-background-fast-panels-device.log` e `launcher-background-fast-panels-summary.json`;
- `logs/launcher-background-fast-default-build.log`;
- referência anterior verificada em
  `logs/firmware-references/20261119T205714177549Z-waveshare_esp32_s3_touch_lcd_146/`.

O build padrão passou em 154,47 s, com 1.694.998 bytes de programa e
55.400 bytes estáticos. A otimização foi incorporada ao padrão; os roteiros,
contadores e modos de referência continuam exclusivos do diagnóstico.

O padrão silencioso com a otimização foi gravado na COM3 em **91,67 s**, com
os quatro hashes verificados e reset por RTS. O relógio ficou com essa versão
de uso, sem os roteiros ativos. Registro em
`logs/launcher-background-fast-default-upload.log`.
