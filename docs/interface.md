# Interface, gestos e renderização

Este documento descreve o comportamento observável da interface e os limites
que devem ser preservados no display SPD2010 de 412 × 412 pixels.

## Camadas da interface

```text
lv_scr_act()
├── content_layer                 apps ativos e prévias de navegação
│   ├── watch                     mostrador
│   ├── apps                      lista de aplicativos
│   ├── aion                      cronômetro, timer e alarmes
│   ├── mnemo                     notas, editor e teclado multi-tap
│   ├── calculator                calculadora
│   └── weather                   clima de São Paulo
└── settings_panel                painel global de acessos rápidos

lv_layer_top()
├── wake_guard                    consome o primeiro toque com tela apagada
└── aion_alert                    aviso opaco em tela cheia
```

`content_layer` é administrada por `core/app_manager.c`. Cada app é criado
preguiçosamente na primeira abertura e depois apenas ocultado ou exibido. O
`settings_panel` é global e fica acima dessa camada; por isso ele continua
disponível sobre o mostrador sem pertencer a um app.

## Navegação por gesto

| Tela atual | Gesto | Resultado |
| --- | --- | --- |
| Mostrador | Arrastar da borda superior para baixo | Abre os acessos rápidos sobre o mostrador, acompanhando o dedo com ganho visual 2×; menos de 120 px cancela a abertura. |
| Mostrador | Arrastar para cima | Revela a lista de apps sobre o mostrador, acompanhando o dedo desde a borda inferior. |
| Acessos rápidos | Arrastar para cima | Fecha o painel. O limiar é 5 px e vale também sobre o arco de brilho. |
| Lista de apps | Tocar uma linha | Abre o app selecionado. |
| Lista de apps | Arrastar para baixo a partir do topo | Fecha a lista acompanhando o dedo; o gesto precisa avançar 120 px. |
| Aion | Arrastar 80 px para a direita | Volta à lista de apps. |
| Aion: cronômetro / timer | Arrastar 80 px para cima | Avança para timer / alarmes. |
| Aion: timer / alarmes | Arrastar 80 px para baixo | Retorna para cronômetro / timer. Pode começar no meio da tela ou sobre uma linha de alarme quando a lista está no topo. Se estiver rolada, o gesto dentro da lista apenas rola o conteúdo; um novo arraste no topo retorna ao timer. Fora da lista, o retorno é direto. |
| Aion: criação / detalhe de alarme | Arrastar 80 px para a direita | Volta à etapa anterior / lista de alarmes. Os arcos recebem o toque exclusivamente para selecionar o valor. |
| Mnemo: lista | Arrastar para a direita | Volta ao launcher, inclusive iniciando sobre uma linha. |
| Mnemo: editor / leitura | Arrastar para a direita | Salva e volta à lista de notas, inclusive sobre texto e teclado. Falha de gravação mantém o editor aberto. |
| Mnemo: confirmação de exclusão | Arrastar para a direita | Cancela a confirmação. |
| Clima | Arrastar para a direita | Volta ao launcher, inclusive começando no ícone ou nos textos. |

Todo novo app deve adotar o retorno da esquerda para a direita do Aion, sem
botão Voltar no topo. O gesto confirma com deslocamento horizontal maior que
80 px e maior que o deslocamento vertical absoluto mais 20 px. Ao confirmar,
consome o contato até soltar, evitando acionar o controle sob o dedo. A tela
inicial volta ao launcher; subetapas retornam à anterior e confirmações são
canceladas. Exceções para controles com arraste próprio devem ser explícitas.

Os movimentos visuais são limitados a uma atualização a cada 20 ms. Isso evita
invalidar a árvore LVGL em cada amostra do touch e mantém o painel próximo ao
dedo sem formar uma fila de quadros antigos.

## Padrão dos controles

Os estilos de controle ficam em `ui/control_style.c`, incluindo a distribuição
2–3–2, cores, bordas e estados. A revisão cobre mostrador, acessos rápidos,
launcher, cronômetro, timer, lista/criação/detalhe de alarmes e os dois avisos.

| Uso | Formato e medidas |
| --- | --- |
| Atalhos, tempos e dias | Círculos de 70 × 70 px; sete opções em 2–3–2, com os mesmos deslocamentos relativos em todas as telas. |
| Ação isolada: Criar, Excluir, Cancelar, Parar | Pílula de 140 × 54 px. |
| Duas ações: Iniciar/Zerar, Voltar/Proximo, +5 min/Parar | Duas pílulas de 120 × 54 px, com 12 px entre elas. |
| Novo alarme | Pílula de 180 × 54 px para comportar o rótulo. |
| Linhas de lista | Pílulas de 56 px nos alarmes. No launcher, linhas transparentes de 64 px com ícone de 44 px em disco de 60 px. |

Botões usam Montserrat 18 e caixa normal. Títulos de apps e avisos usam
Montserrat 24, amarelo, a 34 px do topo. Valores de tempo usam Montserrat 48;
legendas e estados compactos, Montserrat 12. Os valores dos atalhos podem
usar 18/24 conforme seu conteúdo. O mostrador mantém sua composição vetorial:
submostradores redondos são instrumentos de leitura, não botões de ação.

O teclado do Mnemo tem **20 círculos de 44 px em 6–6–5–3**, abaixo do texto,
em um contêiner de 304 × 188 px em (54, 204). As linhas começam em y=204,
252, 300 e 348, com 52 px entre centros na horizontal e linhas centradas
em x=206. Essa é uma exceção específica ao tamanho dos atalhos. As 13 teclas
de letras contêm duas letras-base cada, com acentos nos grupos correspondentes;
os grupos de gh a yz contêm, também, os números de 1 a 9 e 0.
Os grupos estão em `docs/apps.md`; o multi-tap confirma após 800 ms.
Maiúsculas só aparecem com Shift, acionado por toque em tecla própria. O estado
amarelo e os rótulos maiúsculos persistem durante a composição da próxima letra.

O editor usa uma área de texto de 280 × 112 px em (66, 82), Montserrat 18 com
suplemento de acentos e linhas de 25 px, mostrando cerca de quatro linhas. O
contador mostra até 512 caracteres e o estado de salvamento. O botão com ícone
de teclado em (90, 42), de 36 × 36 px, alterna leitura, ampliando o texto para
218 px de altura. Apagar, nova linha e espaço ficam nas duas últimas linhas
do teclado. `#+=` troca as 13 teclas de letras por grupos de símbolos; `abc`
restaura as letras. As ações compactas fazem parte do editor; ações textuais
continuam com as pílulas padrão de 54 px.

Tocar o texto reposiciona o cursor com as métricas da fonte e a rolagem atual.
Arrastar verticalmente rola sem mudar o cursor; arrastes horizontais não movem
o cursor, e o arraste para a direita confirma o retorno. Inserir/apagar acompanha o cursor sem animação de
rolagem. O caractere ainda em composição fica sublinhado e o cursor amarelo
pisca a cada 500 ms, somente com app e tela ativos. Segurar apagar repete a
exclusão antes do cursor.

Não há botão Voltar no topo. O botão de lixeira em (286, 42), de 36 × 36 px,
abre confirmação com
Cancelar/Excluir em pílulas de 120 × 54 px. Na lista, linhas de 280 × 56 px
mostram a primeira linha das notas mais recentes; Nova nota usa 180 × 54 px
em y=314 e fica desabilitado ao atingir 12 notas. O primeiro toque com a tela
apagada continua reservado ao despertar pela camada global.

Toques, arrastes e contatos prolongados no Mnemo reiniciam o prazo global de
inatividade, incluindo lista, editor, teclado e diálogos. Sem contato, os
perfis normais e ECO continuam reduzindo e apagando a iluminação. Animação do
cursor e salvamento automático não prolongam o prazo. Novos apps devem manter
esse contrato usando o tratamento compartilhado de `ui/app_input.h`.

Arcos de brilho, hora e minuto usam a mesma espessura de 14 px, trilha
cinza-esverdeada, indicador amarelo e alça clara. Seus raios e limites variam
conforme a função. No launcher, tocar no ícone ou no nome aciona a mesma linha.

O preenchimento normal é cinza-esverdeado, com texto claro e borda fina.
Ações secundárias usam fundo da tela e contorno. Seleção usa amarelo e texto
escuro; toque escurece o preenchimento; controles desabilitados usam fundo
da tela, contorno e texto atenuado. Esses estados são explícitos, sem depender
das transições e cores padrão do tema LVGL. Os espaços reservados do painel
rápido mantêm o contorno e não recebem toque. Os dias no detalhe do alarme
mantêm a indicação de seleção, mas não recebem cliques de edição.

O rodapé de criação/detalhe e do aviso de timer posiciona a ação a 314 px do
topo. Legendas ficam abaixo, dentro do recorte circular. O cronômetro usa
o mesmo par de pílulas do aviso de alarme. Não há labels de próxima página.

### Acessos rápidos

O painel usa fundo retangular opaco `#26302B`, botões `#748173`, texto claro e
acento amarelo `#F2B84B`, a mesma linguagem visual da lista de apps.

- O arco externo seleciona brilho de 10% a 100%.
- O botão superior esquerdo alterna os perfis `15s`, `30s` e `ON`.
- O botão superior direito mostra a bateria e alterna o modo `ECO`.
- O botão central abre a lista de apps.
- O círculo à esquerda do centro controla o volume: cada toque avança por
  `0 → 1 → 2 → 3 → 4 → 5 → 0`. São cinco níveis audíveis mais o modo mudo.
  O símbolo de alto-falante e o número mostram o nível; zero usa o símbolo mudo.
  A preferência é salva na NVS e restaurada no boot. Sem preferência, o nível 1
  preserva a amplitude anterior. O ganho PCM cresce até 32760 no nível 5;
  mudanças durante um aviso são aplicadas pela tarefa de áudio. No zero,
  avisos visuais continuam e I2S permanece desabilitado. Cada clique nos níveis
  1 a 5 toca um bip de 1 kHz por 120 ms no novo volume, sem bloquear a interface.
  No zero não há bip. Cliques rápidos reiniciam a prévia, sem acumular sons;
  avisos ativos têm prioridade. I2S é inicializado no primeiro som solicitado.
  Restaurar a preferência no boot não toca o bip.
- O círculo à direita de `APPS` mostra somente o cache salvo do Clima: ícone
  vetorial de 30 × 30 px centralizado 12 px acima do centro e temperatura
  arredondada em Celsius (`23°`), Montserrat 18, 17 px abaixo. Sem cache válido,
  mostra apenas `CLIMA`, Montserrat 12 centralizada, sem temperatura nem ícone
  de condição. Tocar no círculo, ícone ou temperatura abre o app Clima e fecha
  o painel, inclusive sem cache. A abertura do app solicita sua atualização normal;
  apenas exibir o atalho não solicita Wi-Fi. O primeiro toque com tela apagada
  somente acorda, e um arraste para cima fecha o painel sem abrir Clima.
  O cache persiste entre reinícios; abrir o painel não exige abrir Clima outra vez.
  A leitura ocorre ao revelar o painel e a cada 500 ms enquanto ele estiver
  visível com tela acesa, usando o timer global existente. Só mudanças de
  temperatura ou condição alteram objetos; não há leitura de RTC nem consumo
  da caixa de resultados do app.
- Os dois círculos restantes são espaços reservados para futuros atalhos; não recebem
  toque.

Brilho, perfil de tempo e modo econômico são gravados na NVS. Quando o modo
ECO limita o brilho a 35%, o valor do arco também reflete esse teto.

## Política de energia

| Perfil | Reduz brilho | Apaga backlight |
| --- | ---: | ---: |
| `15s` | 15 s | 45 s |
| `30s` | 30 s | 2 min |
| `ON` | nunca | nunca |
| `ECO` | 5 s | 15 s |

Ao apagar, o PWM do backlight é configurado para 0%, o timer de atualização do
mostrador é pausado e o loop principal deixa de consultar RTC e bateria. LVGL e
touch continuam ativos somente para receber o toque que acorda a tela. Esse
primeiro toque apenas acorda o relógio; não aciona controles.

Uma superfície transparente global intercepta o primeiro toque com backlight
apagado também dentro dos apps. Timer ou alarme vencido acorda a tela sem
exigir toque e mantém a iluminação ativa enquanto o aviso estiver pendente,
respeitando o limite de brilho do modo ECO. Ao dispensar o último aviso, o
tempo de inatividade começa novamente.

## Aion: cronômetro, timer e alarmes

O app abre sempre no cronômetro. A contagem existente continua ao sair.
Deslizar de baixo para cima abre o timer; repetir abre os alarmes. Deslizar
de cima para baixo volta de alarmes para timer e de timer para cronômetro.
Não há labels com setas indicando a próxima página. Um gesto confirmado consome o contato para evitar
acionar um botão ao soltar o dedo.

O timer oferece **1, 5, 10, 15, 30, 60 e 120 minutos**, no padrão hexagonal
**2–3–2**: `1 / 5`, `10 / 15 / 30` e `60 / 120`. A opção de 1 minuto facilita os
testes de aviso e áudio. Tocar uma opção inicia uma única
contagem, exibida como `mm:ss`, com botão `Cancelar`. A contagem continua com
outro app aberto ou com a tela apagada. Ao terminar, um aviso global opaco
exibe `Timer acabou`, sete círculos em 2–3–2 e uma quarta linha para parar:

```text
    +1   +5
 +10  +15  +30
   +60  +120
     Parar
```

As opções são minutos: silenciam o aviso e iniciam a nova contagem. `Parar`
cancela o aviso e silencia. O aviso pode aparecer sobre o painel rápido ou
qualquer app, preservando a tela anterior quando fechado.

O padrão hexagonal **2–3–2** é a identidade para conjuntos de sete opções
circulares que caibam na tela, como acessos rápidos, tempos e dias da semana.
Novas telas devem seguir essa distribuição; ações complementares ficam abaixo.
O painel rápido e as telas do Aion compartilham os deslocamentos dos sete
círculos em `ui/control_style.c`.

A lista de alarmes possui rolagem e um botão fixo `Novo alarme`. Até 12 alarmes
ficam salvos na NVS; ao atingir esse limite, o botão fica desabilitado. A
criação usa arco de **00–23 horas**, depois arco de **00–59 minutos**, com
`Voltar` e `Proximo`. A última etapa oferece sete círculos selecionáveis e
o botão de criação em pílula na quarta linha:

```text
   dom  seg
 ter qua qui
   sex  sab
     Criar
```

Dias selecionados ficam amarelos; é necessário selecionar ao menos um.
O alarme se repete semanalmente nos dias escolhidos. Tocar uma linha mostra
`hh:mm`, os dias selecionados e `Excluir`, sem edição. Excluir também cancela
um adiamento pendente. Falhas de gravação mantêm a tela e mostram uma mensagem.
Os rótulos usam os caracteres disponíveis nas fontes Montserrat embarcadas.

Ao tocar, o alarme mostra a hora em tela cheia, com `+5 min` para adiar essa
ocorrência e `Parar` para silenciá-la, mantendo sua recorrência. Timer e
alarmes simultâneos permanecem pendentes: dispensar um revela o próximo,
com prioridade para o timer. Cada alarme dispara no máximo uma vez por data
na execução normal; criar um alarme para o minuto atual agenda a próxima
ocorrência. Correções de horário usam o novo minuto, sem recuperar alarmes
de minutos anteriores que tenham sido saltados.

O speaker PCM5101 usa I2S nos GPIOs 48/38/47. O aviso repete quatro bips de
1 kHz, 120 ms cada, com intervalo de 80 ms, em um ciclo de 1,6 s. O áudio é
gerado numa tarefa própria para não bloquear LVGL; permanece desligado entre
avisos. A configuração usa a
[API I2S do ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/i2s.html)
e a pinagem da referência Waveshare. Volume, audibilidade, estalos na parada
e desempenho com som ainda exigem validação no speaker físico.

Após a inclusão do áudio, foi relatado relógio com listras pretas desde o
boot, embora o toque continuasse funcionando. Os buffers `1/20`, QSPI de
2 KiB e refresh de 20 ms foram conferidos e permanecem iguais. Como medida
de isolamento, I2S, seus buffers DMA e a tarefa de som agora só são criados
no primeiro aviso sonoro. No teste seguinte, o usuário confirmou despertar,
bip audível e botão `Parar` funcional, mas as listras voltaram no aviso.
As listras continuaram na navegação após parar o som. Uma leitura de 8 s da
COM3, sem solicitar reset, não recebeu mensagens de diagnóstico.
Para separar o desenho da ativação do I2S, o aviso agora aparece imediatamente
e o som começa **dois segundos depois**. Parar ou adicionar tempo nesse
intervalo cancela o som pendente. Essa espera é uma medida de diagnóstico;
observar se a imagem fica listrada antes ou somente após o início do áudio.
Naquela etapa, a causa ainda não estava confirmada; build e simulação LVGL
não reproduziam o problema físico.

No teste com atraso de áudio, o despertar ocorreu sem as faixas pretas
anteriores, mas com riscos amarelos à direita. Tocar `+1` foi seguido de
faixas pretas persistentes, inclusive após apagar/acordar e no próximo aviso.
O firmware de teste agora drena a fila de pixels do QSPI dentro de
`panel_spd2010_draw_bitmap`, antes de retornar ao callback LVGL. Na
[API LCD do ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/peripherals/lcd.html),
`esp_lcd_panel_io_tx_param(io, -1, NULL, 0)` permite aguardar a fila sem
enviar um comando; isso foi conferido também no backend SPI local da versão
5.3.1. `lv_disp_flush_ready()` continua no callback síncrono original,
com dois buffers PSRAM de `1/20`, QSPI de 2 KiB e refresh de 20 ms.

A espera é aplicada por `scripts/add_waveshare_drivers.py` e pode reduzir
o paralelismo entre desenho e transmissão. O firmware anterior
está em `.pio/recovery/before-qspi-drain-*` (bootloader, partições e aplicação).
Para reconstruir com o comportamento anterior, defina temporariamente
`$env:CHRONVS_QSPI_DRAIN = '0'` antes de executar `pio run`; remova a variável
com `Remove-Item Env:CHRONVS_QSPI_DRAIN` para voltar ao teste com espera.
Validar a imagem no boot, o aviso, `+1`, `Parar` e apagar/acordar, além de
arrastes com áudio ativo. A demora de 2 s antes do som foi mantida para
permitir comparar o comportamento visual antes e durante os bips.

**Validação no relógio confirmada pelo usuário:** boot sem linhas, opções do
timer corretas, primeiro aviso sem artefatos, extensão `+1` e segundo aviso
também sem artefatos. A espera síncrona QSPI passa a integrar a configuração
validada e deve ser preservada. A confirmação abrange esse fluxo; não é uma
medição de desempenho nem um teste exaustivo de todas as telas.

Alarmes salvos sobrevivem ao reinício; timer, adiamentos e avisos pendentes
são voláteis. O relógio precisa estar ligado: não há despertar de deep sleep
nem alarme com o aparelho desligado. Enquanto a tela está apagada, a agenda
avança pelo contador monotônico e pelo último horário válido, sem ler RTC ou
bateria e sem atualizar os objetos visuais do app. Uma correção NTP chega por
uma caixa de mensagem protegida, inclusive com a tela apagada.

Validação no dispositivo: criar alarme para o minuto seguinte, sair do Aion,
aguardar a tela apagar e confirmar despertar, som, adiamento e parada; repetir
com timer. Confirmar que o primeiro toque apagado não inicia nem exclui algo,
que todos os círculos são tocáveis no painel redondo e que a lista persiste
após reinício. Conferir recorrência, avisos simultâneos e gestos sobre botões,
lista e arcos. A simulação LVGL no host não mede fluidez nem áudio físicos.

## Renderização e desempenho

O projeto usa **LVGL 8.3.11**, distribuído na referência oficial da Waveshare.
Embora muitos guias recentes tratem de LVGL 9, os princípios de reduzir áreas
invalidadas e evitar trabalho por quadro continuam válidos, mas os detalhes do
driver não podem ser copiados diretamente.

- `LV_DISP_DEF_REFR_PERIOD` é 20 ms, com alvo de até 50 FPS durante um gesto.
  O mostrador normal continua atualizado uma vez por segundo.
- Há dois buffers LVGL em PSRAM. O driver SPD2010 deve permanecer no tamanho
  original de `1/20` da tela e em transferências QSPI de 2 KiB.
- O script `scripts/add_waveshare_drivers.py` reaplica essas escolhas ao
  exemplo da Waveshare antes de cada build. Não edite apenas a cópia em
  `.vendor-reference/`, pois o script é a fonte persistente da configuração.
- Wi-Fi é usado pelo NTP e por uma consulta ao abrir Clima, em sessões
  exclusivas; o rádio é desligado ao encerrar cada sessão. Apps
  devem pausar timers em `on_hide` quando não forem necessários; `Aion` já faz
  isso para seu timer de interface.
- O desenho vetorial do mostrador recorta as regiões cobertas por superfícies
  retangulares opacas acima dele (painel rápido e lista de apps). O recorte
  acompanha as coordenadas atuais dessas superfícies durante o arraste e a
  animação; ao recuarem, a região exposta volta a ser desenhada normalmente.
- Submostradores inteiramente fora da faixa de desenho são descartados antes
  de calcular seus textos, escalas e ponteiros. O horário usado na renderização
  só avança na atualização do relógio, não entre as faixas de um mesmo quadro.
  Essas otimizações preservam o visual, os gestos e os buffers parciais; o
  ganho de fluidez e a ausência de rastros ainda precisam ser medidos no painel.

Para validar no dispositivo, abra e feche os acessos rápidos lentamente e
rapidamente (também pelo arco), revele a lista e cancele o gesto, e retorne da
lista ao relógio. Observe especialmente a borda móvel e os submostradores que
reaparecem. Repita após apagar e acordar a tela, verificando que o primeiro
toque apenas acorda e que a hora é atualizada.

### CPU e biblioteca gráfica

A configuração em teste usa apenas a CPU a 240 MHz. Os caches permanecem nos
valores validados de 16 KiB para instruções e 32 KiB para dados; flash e PSRAM
continuam a 80 MHz, com linhas de cache de 32 bytes. LVGL permanece no fluxo
original de compilação, com `-O0` e `LV_MEMCPY_MEMSET_STD=0` (padrão da
biblioteca). Antes de testar outro ajuste, é necessário confirmar no relógio
que display, toque, gestos, serial e sincronização NTP continuam funcionando.

O teste anterior conjunto de CPU a 240 MHz, caches 32/64 KiB, LVGL com `-O2` e
rotinas de memória da plataforma compilou, mas foi seguido de tela apagada e
ausência de saída serial no dispositivo. Esses ajustes foram revertidos em
conjunto para recuperação; a causa individual ainda não foi isolada. Não
reaplique o conjunto. Futuras experiências devem alterar uma opção por vez,
verificando boot, display e serial antes de prosseguir. A otimização de recorte
vetorial anterior, confirmada pelo usuário, foi preservada.

As escolhas de CPU e cache estão em `sdkconfig.defaults`. Em um checkout já
configurado, atualize também as mesmas opções no `sdkconfig.waveshare_esp32_s3_touch_lcd_146`
local: defaults não substituem opções já salvas. Confira os valores efetivos
em `.pio/build/waveshare_esp32_s3_touch_lcd_146/config/sdkconfig.h` após compilar.
Para recuperar o dispositivo, use `pio run -t upload`: ele grava bootloader,
partições e aplicação com o mesmo build, nos endereços corretos. Não grave
apenas a aplicação ao trocar configurações de cache. Se o reset automático
não funcionar, entre no bootloader com BOOT pressionado ao acionar RESET,
solte BOOT e confira a porta USB antes de repetir o upload.

O alvo de 50 FPS não é uma medição: compare os mesmos arrastes no hardware.
O monitor opcional `LV_USE_PERF_MONITOR` do LVGL mede atividade do renderizador,
não a utilização total dos dois núcleos, e sua sobreposição também gera desenho.
Deixe-o desativado no firmware normal para preservar a política de tela apagada.

Referência: [otimização de velocidade do ESP-IDF 5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/performance/speed.html).

### Configurações que não devem ser reintroduzidas

O heap de objetos LVGL deve permanecer com **128 KiB em PSRAM**, via
`LV_MEM_POOL_ALLOC` e `platform/lvgl_memory.c`, mantendo TLSF e uma única
reserva reutilizável. Não confundir esse pool com os dois buffers de pixels
`1/20`. Ao abrir Clima com o pool estático na RAM interna, o SPI falhava antes
da rede e o TLS falhava por falta de memória. A mudança liberou 128 KiB internos;
o usuário confirmou dados e ausência de listras, inclusive sem atraso de rede.

Antes de acrescentar HTTPS, tarefas ou grandes caches, confira memória interna
livre e maior bloco DMA durante a operação. O percentual de RAM no build não
mostra essa pressão. Preserve as correções QSPI e use fases/logs para diagnosticar;
um sintoma visual semelhante não comprova a mesma causa. A investigação e as
regras de prevenção estão em [`weather.md`](weather.md#dificuldades-soluções-e-prevenção).

Após a inclusão do Mnemo, foram relatadas listras novamente já no boot, antes
de abrir o app. Como teste isolado, a opção global `LV_LABEL_TEXT_SELECTION`
foi restaurada ao padrão anterior; o destaque da composição passou a ser um
sublinhado local do editor. Buffers, QSPI, caches e espera síncrona foram
preservados. O teste no relógio não eliminou as listras, portanto essa opção
não foi confirmada como causa. A revisão seguinte moveu o cache das notas
(12.384 bytes) da RAM interna estática para alocação tardia na PSRAM, liberando
memória interna antes de inicializar display e Wi-Fi. Essa hipótese de pressão
de memória ainda precisa de validação física; o protocolo QSPI permanece igual.

Foram testadas no hardware e apresentaram falhas:

| Alteração | Resultado no SPD2010 |
| --- | --- |
| Buffer de `1/10` ou maior | Linhas pretas grossas e tela listrada. |
| Dois buffers de tela inteira | O relógio inicia o desenho e trava. |
| Transferência DMA de 8 KiB | Instável quando combinada aos buffers grandes. |
| Chamar `lv_disp_flush_ready()` somente no callback assíncrono do QSPI | A tela deixa de atualizar após o primeiro quadro. |

Para melhorar a fluidez daqui em diante, prefira simplificar o conteúdo
redesenhado durante transições: menos objetos vetoriais ativos, menos texto ou
ticks e nenhuma tarefa de rede concorrendo com o gesto. Qualquer mudança no
driver deve ser testada isoladamente, com um firmware de recuperação pronto.

## Touch espúrio

O touch SPD2010 pode reportar um ponto isolado quando a tela não está sendo
tocada. O script de build aplica debounce no limite LVGL: um contato precisa
aparecer em duas amostras consecutivas, permanecer próximo e ter intensidade
válida. A liberação é imediata. Isso evita acordar a tela, alternar brilho ou
abrir painéis sem interação real.

## Verificação do volume

`tests/run_sound_tests.ps1` executa o gerador real com I2S simulado: confirma
silêncio em zero, inicialização tardia, cinco ganhos crescentes sem overflow,
canais estéreo iguais, silenciamento durante um aviso e prévia finita de
120 ms, sem fila e cancelada pelo nível zero.
`tests/run_aion_ui.ps1 -System` cobre restauração da preferência, ciclo completo,
salvamento, primeiro toque para despertar e arraste sobre o controle sem alterar
volume ou tocar prévia. O usuário confirmou o controle de volume no relógio.
O novo bip de confirmação e a ausência de distorção no nível máximo precisam
ser avaliados no speaker do relógio.

## Lista de apps em arco

O launcher mostra ícone e nome diretamente sobre o fundo, sem título fixo.
As linhas têm 64 px de altura e centros separados por 82 px. O centro dos
ícones acompanha o lado esquerdo de um arco de raio 166 px, centrado em
(206, 206): mais à esquerda no meio da tela e mais à direita nas extremidades.
A opacidade diminui nos últimos 48 px antes do limite vertical de 178 px.

Arrastar verticalmente no conteúdo rola a lista com inércia; os deslocamentos
horizontais são recalculados no máximo a cada 20 ms, apenas quando há mudança
e a tela está acesa. O primeiro e o último item podem alcançar o centro.
A primeira abertura centraliza o conjunto; reaberturas preservam a rolagem.
Ícone e nome abrem o mesmo app, e um arraste não gera clique na liberação.
Os 60 px superiores continuam reservados para puxar o launcher para baixo:
120 px confirmam o retorno ao relógio; movimentos menores cancelam.
Contatos reiniciam a inatividade; a animação de rolagem não conta como atividade.

Verificação: `tests/run_aion_ui.ps1 -System` cobre deslocamento pelo arco,
rolagem por toque sem abertura acidental, abertura por ícone/nome e fechamento
confirmado/cancelado. Build e gravação passaram. O usuário confirmou no relógio
que a lista em arco ficou correta, aprovando o comportamento visual e a interação.
As capturas ficam em `.pio/host-tests/19-launcher-arc.bmp`
e `.pio/host-tests/20-launcher-scrolled.bmp`.

## Clima

Clima abre numa tela única: título `CLIMA` em Montserrat 24 amarelo a y=34,
localização `São Paulo` a y=68 e ícone vetorial em área de 56 × 48 px a y=99.
A temperatura usa Montserrat 48 a y=151. Condição, sensação, umidade e
mínima/máxima ocupam y=210/241/266/291. Os textos de 18 px usam o complemento
de acentos já presente no Mnemo. Não há botões nem animação de carregamento.

A idade fica a y=324 e o estado a y=349: `Atualizando...`, `Falha ao atualizar`
com cache ou a causa resumida sem cache. Sem leitura válida, mostra `--°C` e
`Sem dados`. A fonte dos dados, `Open-Meteo`, aparece em Montserrat 12 a y=380.
As larguras variam de 140 a 300 px para caber no painel circular.

Após relato de listras imediatamente ao abrir, um teste com atraso de 2 s
mostrou falhas SPI antes da rede, pouca RAM interna livre e falha de alocação
no TLS. A correção move o pool de objetos LVGL de 128 KiB para PSRAM, mantendo
tamanho, TLSF, buffers de pixels e configuração QSPI. O usuário confirmou
Clima sem listras e com dados. O atraso de diagnóstico foi retirado após essa
confirmação; a consulta inicia normalmente ao abrir. O usuário também confirmou
o funcionamento da versão final sem atraso, com dados e sem listras. O procedimento e os logs estão em
[`weather.md`](weather.md#listras-ao-abrir--teste-de-isolamento).

O cache permanece visível durante a consulta e após falhas. A idade parte do
horário local da medição; inclui minutos, horas ou dias. Se o RTC estiver
inválido ou anterior à medição, mostra `Horário indisponível`. Um timer de 250 ms
aplica resultados e só altera textos quando mudam. A idade é revista na abertura,
no resultado, ao despertar e a cada minuto com tela ativa. Com backlight apagado,
não lê RTC nem altera objetos; `on_hide` pausa o timer. A consulta em andamento
pode terminar e persistir sem acordar a tela.

Arrastar mais de 80 px para a direita, com predominância horizontal de 20 px,
volta ao launcher. A árvore inteira é vinculada uma vez a `ui/app_input.h`;
o gesto consome o contato e toques prolongados registram atividade. A proteção
global consome o primeiro contato com a tela apagada.

`tests/run_weather_tests.ps1 -UI` cobre layout, cache, retorno sobre conteúdo
e ícone, contatos prolongados, pausa ao sair/apagar e resultados tardios.
`tests/run_aion_ui.ps1 -System` inclui Clima no launcher, no orçamento LVGL e
no teste do primeiro arraste após despertar. A renderização simulada foi
inspecionada; a consulta real e a memória durante TLS foram observadas no relógio.
Consumo, fluidez e picos de memória em outros cenários ainda exigem medição no
dispositivo. Os parâmetros do SPD2010 foram preservados.

## Calculadora

A calculadora nativa reúne tudo em uma tela, sem título. O visor ocupa
240 px de largura, centralizado em x=206, y=44, com texto alinhado à direita.
O teclado é uma exceção explícita ao padrão de atalhos: 20 teclas circulares
de 54 px. Números e operadores ficam em 4 colunas × 4 linhas, com centros
x=113/175/237/299 e y=175/233/291/349. As laterais têm dois controles cada:
`C` e `(` em x=51, `DEL` e `)` em x=361, ambos em y=175/233.
O intervalo é de 8 px na horizontal e 4 px na vertical; todas as teclas cabem
inteiramente no recorte circular, sem sobrepor áreas de toque. A antiga linha
de comandos acima do teclado fica livre, dando mais espaço ao visor.

```text
 C    7   8   9   /    DEL
 (    4   5   6   x     )
      1   2   3   -
      0   .   =   +
```

`C` limpa a expressão, `DEL` apaga o último caractere e `=` usa destaque
amarelo. O ícone segue o padrão vetorial dos apps nativos: contorno amarelo
de 2 px, fundo transparente, visor e teclas claros, corpo de 28 × 36 px como
o Mnemo. O desenho é feito diretamente no
contêiner de 44 px para evitar deslocamentos causados por bordas de filhos.

Arrastar mais de 80 px para a direita, com predominância horizontal de 20 px,
volta diretamente ao launcher. O gesto vale no conteúdo e em todas as
teclas, consome a liberação e não exige botão Voltar. A tela vincula sua árvore
a `ui/app_input.h` uma única vez. Toques e contatos prolongados reiniciam a
inatividade; cálculos não contam como atividade. O primeiro toque com o
backlight apagado continua reservado ao despertar. Não há animações em repouso.

A entrada aceita até 64 caracteres. A avaliação respeita precedência de
multiplicação e divisão, parênteses e sinal unário. Resultados usam até 12
algarismos significativos; erros preservam a expressão para correção com `DEL`.
