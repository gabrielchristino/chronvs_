# Consistência das bandejas e custo dos ícones

Continuação de 19/11/2026. O usuário confirmou **“melhorou muito a fluidez”**
no padrão com barra de brilho e percebeu diferenças entre as bandejas. Pediu
preservar a identidade dos ícones e escolheu padronizar somente a animação ao
soltar, conservando os limites dos gestos.

## Diferenças confirmadas no código

| Etapa | Acessos rápidos | Launcher |
| --- | --- | --- |
| Acompanhar abertura | Posição y absoluta do dedo × 2 | Deslocamento desde o toque × 2 |
| Confirmar abertura | y final ≥ 120 px | Deslocamento ≥ 120 px |
| Começar fechamento | Mais de 5 px para cima | Mais de 20 px para baixo, começando nos 60 px superiores |
| Confirmar fechamento | Completa depois de reconhecer o gesto | Exige 120 px; caso contrário, retorna |
| Concluir ao soltar, antes desta mudança | Animação de 220 ms com `ease_out` | Troca direta de posição/app |

O padrão agora usa **220 ms nominais com `ease_out` nas duas bandejas**, para
abertura, fechamento e cancelamento. Os limiares e a relação dedo/posição
permanecem como na tabela. A lista continua distinguindo rolagem e retorno.
As cargas de desenho são diferentes: duração nominal igual não garante igual
quantidade de quadros ou fluidez óptica idêntica.

`ui/panel_motion.h` concentra a duração e a curva. O launcher usa
`chronvs_app_settle_y` para animar a raiz visível e efetivar a navegação somente
na conclusão. Outra navegação cancela a animação e oculta a prévia antiga,
evitando commits atrasados. O layout é atualizado antes de ler a posição
inicial; isso evita começar de uma coordenada anterior ainda pendente no LVGL.
Novos contatos na bandeja durante a conclusão contam como atividade e são consumidos até
soltar, evitando cliques ou arrastes sobre um destino que está mudando.

## Ícones: custo real e duas tentativas sem ganho global

Os ícones conservam suas silhuetas, cores e contêineres de 44 × 44 px.
O custo inclui primitivas, antialiasing, objetos, recorte, opacidade e acesso
aos pixels; quantidade de detalhes isoladamente não determina o tempo do menu.
Foram testadas duas alternativas, primeiro com a conclusão antiga fixada por
`CHRONVS_PANEL_SNAP_REFERENCE`, para não misturar a mudança de animação com
a comparação do desenho:

1. `panel_autotest_icons`: snapshot RGB565 de cada ícone sobre a cor fixa do
   badge, com chroma key nos pixels de fundo. Vetores continuam no fade.
2. `panel_autotest_icons_opaque`: verifica os dois pixels da borda do snapshot.
   Quando são inteiramente fundo, recorta somente essa margem vazia e copia
   uma imagem opaca de 40 × 40 px, centralizada no mesmo contêiner. Esse
   quadrado cabe no interior opaco do badge; os demais ícones usam a alternativa
   anterior. Não há redimensionamento do desenho. No catálogo do host,
   quatro de sete ícones percorreram esse caminho rápido.

Ambas mantêm reserva de 3.872 bytes por ícone em PSRAM, pois a captura inicial
é de 44 × 44 px, além dos objetos/descritivos. Falha de reserva/snapshot ou
colisão com a chave de cor conserva o vetor. O acesso LVGL permanece na tarefa
da interface. Não são framebuffer nem buffers DMA.

**160 capturas coincidiram pixel a pixel** contra os vetores em cada
alternativa, incluindo opacidades, estados pressionados e telas dos apps.
As suítes integradas e as falhas de alocação simuladas passaram. No host,
editor mais aviso deixaram 8.728 bytes livres no pool LVGL com o cache,
contra 10.960 sem ele. Esse consumo extra não foi compensado por ganho global.

Os dois roteiros físicos concluíram 42/42, preservando preferências e
restaurando a leitura física. Comparação por refresh, com desenho e envio
síncrono, nas trajetórias lentas:

| Operação | Vetores | Cache com chave | Variante opaca |
| --- | --- | --- | --- |
| Abrir launcher | 39,08 ms | 39,85 ms | 39,53 ms |
| Cancelar fechamento | 62,60 ms | 63,96 ms | 62,22 ms |
| Fechar launcher | 58,41 ms | 59,78 ms | 58,27 ms |

As subárvores dos ícones acumularam 1.031.329 us com vetores, 618.694 us com
chroma key e 581.436 us na variante opaca, no roteiro completo. Portanto houve
redução local, sem redução consistente do menu inteiro. Os tempos externos
ao trecho medido, a composição e o tráfego de memória também importam; não
foram medidos contadores de cache ou uma causa exclusiva dessa diferença.
Quadros, áreas e hora do mostrador variam entre boots; diferenças pequenas
não comprovam ganho perceptivo.

**Nenhum cache de ícone foi adotado no padrão.** Os experimentos permanecem
reproduzíveis. Isso evita gastar memória em uma troca que não melhorou o
resultado global e mantém a identidade original. A investigação seguinte deve
medir composição, camadas e redraws do launcher antes de simplificar símbolos.
Os 20 ms são a cadência solicitada ao LVGL, não um prazo garantido de desenho.

## Validação da animação escolhida

A suíte `run_Relogio_ui.ps1 -System` verifica posições intermediárias antes
de 220 ms, estados finais, cancelamento ao abrir outro app e consumo de um
novo toque durante a conclusão. Também passaram os 42 gestos no host com
`-System -PanelAutoTest`, sem os caches experimentais.

`panel_autotest_motion` usa o padrão com barra, vetores, instruções de 32 KiB,
dados de 32 KiB/linha de 32 bytes e O2. Não define a referência de salto direto.
O roteiro físico concluiu **42/42**, e a consulta real do Clima terminou com
sucesso: `DONE` aos 71,692 s, preferências preservadas e entrada física
restaurada. Durante Clima foram observados mínimos de **79.871 bytes internos
livres** e **31.744 bytes no maior bloco DMA**. São observações desse fluxo,
não reservas seguras universais. Não houve correspondências nos padrões
buscados de falha SPI/TLS, panic ou assertion.

A nova animação muda a quantidade e a distribuição dos quadros após soltar;
suas médias não devem ser comparadas às da troca direta como se fossem a mesma
carga, nem usadas como prova de aceleração de CPU. Não há câmera do painel
nesta etapa nem medição de autonomia. Pool PSRAM/TLSF de 128 KiB, buffers duplos
de 1/20, refresh de 20 ms, QSPI de 2 KiB e espera síncrona foram preservados.

## Reproduzir e consultar os artefatos

```powershell
./tests/run_Relogio_ui.ps1 -System
./tests/run_Relogio_ui.ps1 -System -PanelAutoTest
./tests/run_Relogio_ui.ps1 -System -LauncherIconCache -SceneCache -CircleCache16
./tests/run_Relogio_ui.ps1 -System -OpaqueIconCache -SceneCache -CircleCache16
pio run -e panel_autotest_icons
pio run -e panel_autotest_icons_opaque
pio run -e panel_autotest_motion
```

Logs e binários são locais e ignorados:

- `panel-icons-{build,upload,device,candidate-host,fallback-host}.log`;
- `panel-icons-{summary,comparison,pixels,draw-summary}.json`;
- `panel-icons-opaque-{build,upload,device,host,fallback-host}.log` e resumos;
- `panel-motion-{reference-host,final-host,autotest-host,build,upload,device}.log`;
- `panel-motion-summary.json`, com gestos e HTTPS aprovados;
- capturas em `.pio/host-tests/` e referências locais sob `logs/`.

O diagnóstico da barra e o histórico anterior estão em
[`panel-autotest.md`](panel-autotest.md).

## Firmware normal entregue

O build padrão passou em 153,10 s, com **1.688.222 bytes de programa** e
**55.216 bytes estáticos**. O header gerado confirmou instruções de 32 KiB,
dados de 32 KiB/linha de 32 bytes, console NONE e logs zero. Não habilita
snapshot/cache de ícones, referência de salto, roteiro ou profiling.

O binário foi preservado e verificado em
`logs/firmware-references/20261119T170702173472Z-waveshare_esp32_s3_touch_lcd_146/`.
A referência de 16:30 conserva o padrão anterior à animação comum. O novo
padrão foi gravado na COM3 em **63,81 s**, com quatro hashes verificados e
reset por RTS. O relógio terminou com barra de brilho, ícones originais e
conclusão comum das bandejas. Logs: `panel-motion-default-build.log`,
`panel-motion-default-upload.log` e `panel-motion-default-reference.txt`.

## Ajuste posterior: brilho no topo

A pedido do usuário, a barra passou do rodapé para a parte superior: trilha
em y=70–75, legenda em y=36, mantendo largura, alça e área de toque. Os
atalhos continuam nas mesmas posições; a área ampliada termina em y=92,
antes dos círculos superiores, que começam em y=99.

O build padrão e `run_Relogio_ui.ps1 -System` passaram. Os testes usam o novo
local para arraste, saída da área, ECO e primeiro toque de despertar. A captura
`13-quick-settings.bmp` foi inspecionada e não mostra sobreposição. Essa é uma
mudança de posição; não foi feita uma nova medição física de fluidez. Os
números anteriores da barra no rodapé continuam identificando aquela versão.
Registros locais: `logs/brightness-top-{build,host,upload}.log` e
`brightness-top-reference.txt`.
O firmware normal foi gravado na COM3 em 63,85 s, com quatro hashes verificados
e reset por RTS. Referência preservada em
`logs/firmware-references/20261119T172513846189Z-waveshare_esp32_s3_touch_lcd_146/`.

## Ajuste posterior: brilho e ECO no mesmo controle

O padrão substitui a barra superior por uma pílula de 180 × 54 px, em y=38,
com o ciclo **ECO 35% → BRILHO 50% → BRILHO 100% → ECO 35%**. O círculo da
bateria passa a mostrar somente a porcentagem. ECO mantém os tempos de 5 s
para reduzir e 15 s para apagar; os demais modos seguem o perfil normal.
Os detalhes de gestos e migração da NVS estão em `interface.md`.

O clique ao soltar avança e salva `light_mode`. Arrastar para cima fecha a
bandeja sem trocar o modo; segurar registra atividade sem repetir a ação.
A barra anterior permanece apenas em `CHRONVS_BRIGHTNESS_SLIDER_REFERENCE`,
assim como o arco em sua referência isolada. A conclusão comum de 220 ms,
os ícones vetoriais e os parâmetros de tela foram preservados.

Validação desta mudança:

- `run_Relogio_ui.ps1 -System`: ciclo, saída de ECO, bateria sem ação,
  primeiro toque para despertar, contato prolongado e fechamento a partir
  da pílula passaram. As capturas normal/ECO foram inspecionadas.
- Seis inicializações no host cobriram preferências antigas, chave válida
  prevalecendo sobre os valores antigos e chave inválida com fallback.
  Cada caso percorreu os três modos e verificou a preferência gravada.
- O cenário editor mais alerta deixou 11.584 bytes livres no pool LVGL do
  host, contra 10.960 na versão anterior. Isso não mede RAM DMA no relógio.
- `panel_autotest_lighting` executou no relógio os 42 gestos, sem falhas,
  preservando o modo de iluminação durante as bandejas. Clima concluiu o
  HTTPS (`state=2`, `fetching_seen=1`, sem timeout); mínimos amostrados:
  RAM interna 79.875 bytes e maior bloco DMA 31.744 bytes. Nenhuma ocorrência
  dos padrões de falha SPI/TLS, panic ou assert na captura.

O roteiro físico injeta coordenadas pelo caminho do touch: não substitui
inspeção visual do painel nem ensaio manual do ciclo após reiniciar. O ciclo
e a persistência foram verificados no host; não foi demonstrado um ganho de
tempo de quadro desta troca. Logs locais: `lighting-mode-verified-host.log`,
`lighting-mode-boot-verified-host.log`, `lighting-mode-device.log` e
`lighting-mode-summary.json`.

O build normal passou em 151,24 s: 1.687.662 bytes de programa e 55.200 bytes
estáticos. O header confirmou caches de instruções/dados de 32 KiB, linha de
32 bytes, console NONE e logs zero. A referência local foi preservada e
verificada em
`logs/firmware-references/20261119T175200188824Z-waveshare_esp32_s3_touch_lcd_146/`.
O build está em `lighting-mode-default-build.log`; a cópia anterior com barra
no topo continua disponível na referência de 17:25.
O firmware normal foi gravado na COM3 em 63,84 s, com os quatro hashes
verificados e reset por RTS (`lighting-mode-default-upload.log`). O relógio
terminou com o diagnóstico desligado e o controle combinado ativo.

## Ajuste posterior: seletor circular na grade

A pedido do usuário, o controle combinado passou para o slot inferior
esquerdo, antes reservado. Usa o mesmo construtor e estilo dos outros círculos:
70 × 70 px, centro x=163/y=278. As legendas são `ECO`, `50%` e `100%`;
ECO representa 35% e mantém o preenchimento amarelo. A pílula superior foi
removida. São exatamente sete círculos em 2–3–2, com bateria independente e
o slot inferior direito ainda reservado. A chave `light_mode`, os tempos de
energia e a animação das bandejas não mudaram.

`run_Relogio_ui.ps1 -System` passou usando as coordenadas novas, incluindo
ciclo, bateria sem ação, despertar, contato prolongado e fechamento iniciado
no círculo sem troca de modo. O teste confere tamanho, posição e contagem dos
sete círculos. Uma inicialização adicional com ECO salvo percorreu o ciclo
completo e verificou o modo persistido. Capturas ECO/100% foram inspecionadas,
sem sobreposição. O pool LVGL do host deixou 11.944 bytes livres no cenário
editor mais alerta; esse valor não mede RAM DMA nem tempo de desenho.

O build padrão passou em 34,06 s, com 1.687.606 bytes de programa e 55.200
bytes estáticos. Referência local preservada e verificada em
`logs/firmware-references/20261119T180019847481Z-waveshare_esp32_s3_touch_lcd_146/`.
Registros: `lighting-circle-{build,host,boot-host,upload}.log` e
`lighting-circle-reference.txt`. Esta mudança de geometria não repetiu o
roteiro físico de 42 gestos nem mediu ganho de fluidez; a sensação e o toque
na nova posição ainda precisam de avaliação manual no relógio.
O firmware normal foi gravado na COM3 em 63,79 s, com os quatro hashes
verificados e reset por RTS. Não há roteiro de diagnóstico ativo.

### Inclusão de 75%

O ciclo passou a **ECO → 50% → 75% → 100% → ECO**, sem mudar geometria,
estilo, política de energia ou gestos. Os IDs NVS 0/1/2 conservam ECO/50/100;
75% usa ID 3 e uma tabela define a ordem de avanço. Isso evita reinterpretar
100% já salvo como 75%. A migração das preferências anteriores foi mantida.

O build padrão passou em 31,55 s. `run_Relogio_ui.ps1 -System` passou com
os quatro níveis e uma captura de 75% foi inspecionada. Quatro inicializações
adicionais cobriram cada ID salvo; todas percorreram o ciclo completo com
verificação de brilho, legenda e valor persistido. Logs locais:
`lighting-75-{build,host,boot-host,upload}.log` e `lighting-75-reference.txt`.
Não houve nova medição de fluidez nem repetição do roteiro físico de bandejas.
Referência preservada e verificada em
`logs/firmware-references/20261119T180404230476Z-waveshare_esp32_s3_touch_lcd_146/`.
O padrão foi gravado na COM3 em 63,82 s, com quatro hashes verificados e
reset por RTS. Programa: 1.687.610 bytes; dados estáticos: 55.200 bytes.

## Separação de brilho e ECO

O círculo de brilho agora percorre somente **50% → 75% → 100% → 50%**.
A bateria alterna ECO: entrar seleciona 35%; sair mantém 35% com o perfil
normal de inatividade. Não guarda nem restaura o brilho anterior. Tocar no
brilho em 35%, inclusive durante ECO, seleciona 50% e desativa a economia.
A intensidade sempre aparece no círculo inferior esquerdo; a bateria conserva
a porcentagem e acrescenta a legenda ECO e seleção amarela somente quando ativo.

Continua usando uma única gravação NVS `light_mode`, preservando os IDs antigos;
o novo ID 4 representa 35% sem ECO. Isso permite reiniciar depois de desligar
ECO sem alterar o brilho. Os controles são separados e a persistência mantém
um estado coerente. Geometria 2–3–2, política ECO de 5/15 s, perfil normal,
animações e parâmetros de tela não foram alterados.

Registros locais desta mudança: `battery-eco-{build,final-build,host,final-host,verified-host,boot-host,upload}.log`
e `battery-eco-reference.txt`. Não houve nova medição de fluidez nem roteiro
físico de 42 gestos; os novos controles ainda exigem avaliação manual no painel.

O teste prolongado identificou inatividade durante contato parado na bateria;
o handler do painel passou a registrar atividade em PRESSED/PRESSING para todos
os controles. A execução final `verified-host` passou: ciclo de brilho,
ECO pela bateria, saída sem restaurar intensidade, primeiro toque apenas para
despertar, contato de 16 s sem repetir ação e arraste a partir de ambos os
controles sem clique ao soltar. Os cinco estados salvos foram inicializados
em processos separados, conferindo brilho, legenda ECO e avanço do seletor.
Capturas de ECO e 35% normal foram inspecionadas; a porcentagem usa texto
escuro sobre amarelo e mantém as cores de alerta de bateria fora do ECO.

O build final passou em 32,97 s: programa de 1.688.018 bytes e dados estáticos
de 55.216 bytes. Referência local preservada e verificada em
`logs/firmware-references/20261119T181407044852Z-waveshare_esp32_s3_touch_lcd_146/`.
O firmware normal foi gravado na COM3 em 63,82 s, com quatro hashes
verificados e reset por RTS, sem roteiro diagnóstico ativo.

### 35% no ciclo normal de brilho

A pedido do usuário, 100% agora avança para 35% sem ECO. O ciclo normal é
**35% → 50% → 75% → 100% → 35%**. A bateria continua sendo o controle de ECO;
escolher 35% no brilho não altera a política normal de inatividade. O estado
35% sem ECO já usava ID 4 na NVS, portanto não houve mudança no formato salvo.
Tocar no brilho durante ECO continua selecionando 50% e saindo da economia.

O teste integrado verifica a passagem 100% → 35%, a ausência da indicação ECO
e a permanência do brilho após 6 s, além dos cenários existentes de gestos,
despertar e inatividade. Logs locais: `brightness-35-{build,host,boot-host,upload}.log`
e `brightness-35-reference.txt`. Não houve mudança de geometria nem nova
medição física de desempenho.

O build passou em 31,71 s (1.688.018 bytes de programa, 55.216 estáticos),
assim como a suíte integrada e as cinco inicializações com estados salvos.
Referência preservada e verificada em
`logs/firmware-references/20261119T181924539497Z-waveshare_esp32_s3_touch_lcd_146/`.
Gravado na COM3 em 63,77 s, com quatro hashes verificados e reset por RTS.

### Identificação por sol e bateria

Para diferenciar os dois controles, brilho agora mostra um sol vetorial e o
nível **sem `%`** (35/50/75/100). A bateria mostra o contorno de uma bateria
e sua porcentagem. Ambos seguem o volume: símbolo acima, valor abaixo.
ECO mantém a bateria amarela e a legenda abaixo da porcentagem; seus três
elementos são reposicionados dentro dos mesmos 70 px. A intensidade não é
representada no desenho dos símbolos: são estáticos, sem bitmap ou timer.

O build passou em 33,70 s: 1.688.566 bytes de programa e 55.216 estáticos.
A suíte integrada passou, inclusive toque diretamente sobre os símbolos,
fechamento por arraste, despertar, contato prolongado e os estados de ECO.
As cinco inicializações com preferências salvas também passaram. Capturas
normal/ECO foram inspecionadas no host, com valores legíveis e sem sobreposição.
Referência preservada e verificada em
`logs/firmware-references/20261119T182538136855Z-waveshare_esp32_s3_touch_lcd_146/`.
Registros locais: `quick-symbols-{build,host,boot-host,upload}.log` e
`quick-symbols-reference.txt`. Não foi medido impacto físico na fluidez nem
repetido o roteiro de 42 gestos; a legibilidade no painel exige avaliação manual.
Gravado na COM3 em 63,78 s, com quatro hashes verificados e reset por RTS.

## Correção de saltos na rolagem da lista

O usuário relatou oscilação das linhas, com a bandeja parada. Foi reproduzido
no LVGL real do host: lista em scroll=60, pressão em (210,390) sobre uma linha
parcialmente visível e dedo parado. A posição vertical da lista mudava durante
o contato, enquanto y da raiz permanecia zero. O teste falhou antes da mudança
(`launcher-scroll-before-host.log`) e passou após desativar
`LV_OBJ_FLAG_SCROLL_ON_FOCUS` somente nas linhas (`launcher-scroll-after-host.log`).

No LVGL 8.3.11 local, o construtor de botão ativa essa flag, e FOCUSED chama
`lv_obj_scroll_to_view_recursive(..., LV_ANIM_ON)`. Esse ajuste automático
introduzia uma animação de rolagem alheia ao dedo. A correção mantém foco,
toque para abrir, inércia, atualização da curva, limites de fechamento e
animação da bandeja. Não altera drivers ou parâmetros de renderização.

O cenário reproduz um mecanismo de salto; eliminar todas as manifestações
intermitentes relatadas ainda depende de confirmação no dispositivo.

Validação final: `launcher-scroll-verified-host.log` passou na suíte completa,
incluindo o caso de dedo parado e 12 rolagens em direções alternadas, com
retomadas durante a inércia. Cada contato parado mantém o scroll; cada arraste
segue sua direção sem deslocar a raiz nem abrir app ao soltar. Os testes de
toque para abrir e fechar/cancelar a bandeja continuaram aprovados.
Build padrão aprovado em 31,32 s, programa de 1.688.578 bytes e dados estáticos
de 55.216 bytes (`launcher-scroll-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T183330035244Z-waveshare_esp32_s3_touch_lcd_146/`.
Não foi repetido o roteiro físico de 42 gestos nem alterada a inércia para
mascarar o problema; a reprodução e a regressão foram verificadas no host.
O firmware normal foi gravado na COM3 em 63,82 s, com quatro hashes verificados
e reset por RTS (`launcher-scroll-upload.log`).

## Seleção de app protegida contra deslizes

Após a primeira correção, o usuário confirmou melhora, mas relatou oscilação
residual e abertura de app ao deslizar. Um teste reproduziu a abertura num
movimento horizontal (150,206) → (190,206), sem rolagem vertical. A suíte
anterior falhou em `launcher-tap-before-host.log`: o launcher confiava somente
no CLICKED do LVGL, emitido quando nenhum objeto entrou em scroll. Um deslize
horizontal ainda podia ser clique. O caso passou em `launcher-tap-after-host.log`.

As linhas, lista e faixa superior agora mantêm o alvo até soltar, evitando
reinicializar o gesto ao atravessar espaços ou linhas reposicionadas pela curva.
Abrir exige a linha original visível, sem excursão maior que 8 px em nenhum
eixo; a rejeição vale até soltar mesmo se o dedo voltar ao início. Movimento no
RELEASED também é conferido. Rolagem por touch invalida o clique; um contato
iniciado até 150 ms da última rolagem serve para parar/continuar a lista, sem
abrir app. Rolagem programática não aciona essa janela. A inércia, curva, foco
sem enquadramento e limites da bandeja permanecem preservados.

A oscilação residual não foi reproduzida separadamente no host. Fixar o alvo
evita reinícios de contato durante o movimento, mas eliminar o sintoma físico
ainda depende de confirmação no relógio; não foi alterado o driver de touch.

Validação final: suíte completa aprovada em `launcher-tap-verified-host.log`,
incluindo deslize do espaço entre linhas, movimento horizontal de 40 px,
excursão de 9 px com retorno ao início, contato sobre uma linha durante a
inércia e as 12 rolagens alternadas da regressão anterior. Os toques normais
no ícone/nome e os gestos de fechar/cancelar a bandeja continuaram passando.
Build padrão aprovado em 31,21 s: programa 1.688.918 bytes; dados estáticos
55.232 bytes (`launcher-tap-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T183952325419Z-waveshare_esp32_s3_touch_lcd_146/`.

Essa validação usa o LVGL real no host, sem ensaio físico automatizado novo.
O firmware padrão foi gravado na COM3 em 63,83 s, com quatro hashes verificados
e reset por RTS (`launcher-tap-upload.log`).

## Teste de suavização da listagem

**Descartado após teste físico.** A aprovação em host abaixo não validou o
candidato no relógio. O usuário relatou desaparecimento das linhas ao soltar.

O usuário autorizou testar suavização leve após confirmar nova melhora com a
proteção dos toques. A listagem recebe um filtro visual de primeira ordem em
Q8, constante nominal de 50 ms no timer existente de 20 ms. Limita o atraso
espacial a 64 px e termina resíduos menores que 1 px exatamente no alvo.
Não é um atraso fixo de 50 ms: a acomodação e a resposta física dependem dos
quadros produzidos. Curva horizontal e opacidade acompanham o mesmo scroll
visual, sem filtrar coordenadas de touch nem modificar o driver.

O scroll lógico continua sob LVGL, com sua inércia. As linhas usam FLOATING
para não receber também a translação imediata do pai, e o evento de tamanho
próprio fixa a altura do conteúdo. Isso impede realimentação dos limites de
scroll pela suavização. Os hitboxes são os próprios objetos já reposicionados.
Novo toque interrompe a inércia pelo LVGL e congela a posição visual pendente,
mantendo a proteção contra abrir app. Posições programáticas e `on_show`
usam o destino exato. Não foi criado timer, tarefa, bitmap ou cache novo.

O primeiro ensaio da suíte existente passou (`launcher-smooth-host.log`).
A sensação no painel e o eventual ganho contra oscilação exigem comparação
manual; a referência anterior sem suavização permanece em
`logs/firmware-references/20261119T183952325419Z-waveshare_esp32_s3_touch_lcd_146/`.

A suíte ampliada também passou (`launcher-smooth-verified-host.log`), com
posição intermediária, convergência e limites de scroll constantes. O build
passou em 31,89 s, com 1.689.278 bytes de programa e 55.240 estáticos. O
candidato foi preservado em `20261119T184837458478Z` e gravado em 63,78 s.
Esses resultados não demonstraram segurança física do caminho de soltura.

### Reversão após desaparecimento dos itens

No relógio, soltar o dedo fazia os itens desaparecerem; fechar e reabrir a
bandeja os restaurava. Isso é uma regressão do candidato de suavização.
Os testes do host não reproduziram o sintoma; a causa exata não foi confirmada.
Não foi instalado outro candidato sem isolamento físico nem atribuída a falha
automaticamente ao driver ou à QSPI.

O uploader verificou a referência anterior completa e restaurou o firmware de
18:39 (`20261119T183952325419Z`), mantendo foco sem enquadramento e proteção
contra abertura por deslize. Upload na COM3 em 63,71 s, quatro hashes e reset
por RTS: `launcher-smooth-rollback-upload.log`.

No código, foram removidos filtro Q8, linhas FLOATING, altura sintética e
ajuste do scroll ao retocar. O teste específico de interpolação foi retirado;
permanecem as regressões de foco, gestos e seleção. A suíte completa passou
(`launcher-smooth-rollback-host.log`) e o build padrão passou em 31,59 s
(`launcher-smooth-rollback-build.log`). O padrão voltou ao scroll nativo, sem
alterar buffers, driver, parâmetros de tela ou controles de energia.

## Segunda tentativa: suavização do vetor de entrada

Depois de confirmar a restauração, o usuário autorizou nova tentativa. O
filtro atua no vetor vertical durante PRESSING, no próprio launcher. Os pontos
do touch são preservados para hit test, gestos e rejeição de cliques. Só o
vetor entregue à rolagem e sua velocidade de inércia são suavizados, em Q8,
constante nominal de 50 ms e atraso espacial máximo de 48 px. Novo contato
reinicia o filtro; contatos no topo não são filtrados. RELEASED/PRESS_LOST e
`on_hide` desligam o filtro, deixando a soltura e inércia nas mãos do LVGL.
Não há FLOATING, altura sintética, translação visual independente ou timer novo.

Na análise foi identificado um erro de tipos: dividir um delta negativo por
`50 + elapsed` com `elapsed` unsigned converte o numerador para unsigned,
produzindo um incremento muito grande. A expressão também existia na primeira
tentativa e é compatível com o desaparecimento dos itens; não houve isolamento
físico que prove ser a única causa. A primeira execução da segunda tentativa
falhou na rolagem para cima (`launcher-input-smooth-host.log`); a divisão com
denominador explicitamente signed passou (`launcher-input-smooth-fixed-host.log`).

A mudança é específica para a ordem PRESSING/scroll do LVGL 8.3.11 local e
não altera sua cópia em `.vendor-reference`. Ver a dependência em `apps.md`.
A referência anterior de 18:39 permanece disponível para restauração imediata.

Validação final: suíte completa em `launcher-input-smooth-release-host.log`
aprovada, incluindo as regressões anteriores de foco, seleção, inércia e
bandeja. O teste novo cobre os dois sinais do vetor: um movimento inicial de
40 px produz scroll entre 0 e 40 px, seguido de mais 40 px e soltura ainda em
movimento. Por 30 amostras após soltar, confere bounds, raiz imóvel, linhas
visíveis e coordenadas iguais às posições nativas (sem FLOATING). Ao final,
compara a imagem incremental com uma renderização completa; as capturas dos
dois sentidos foram inspecionadas. Também passou o ensaio com pausa antes de
soltar (`launcher-input-smooth-verified-host.log`).

Build aprovado em 32,48 s: programa 1.689.234 bytes; dados estáticos 55.248
bytes (`launcher-input-smooth-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T190157661372Z-waveshare_esp32_s3_touch_lcd_146/`.
Esses ensaios usam LVGL real no host e não substituem a comparação manual no
relógio, especialmente após a regressão física anterior. Não foi feita nova
medição física de tempo de quadro nem repetição do roteiro de 42 bandejas.
Gravado na COM3 em 63,82 s, com quatro hashes verificados e reset por RTS
(`launcher-input-smooth-upload.log`).

## Limite de inércia ao soltar perto da borda

O usuário relatou que deslizar para cima e soltar perto do topo às vezes
lançava toda a listagem. O LVGL calcula velocidade em pixels por amostra;
uma amostra final grande pode manter inércia excessiva mesmo com a suavização
do deslocamento. O cenário de regressão usa (210,340) → 280 → 200 → 100 → 65
e soltura, começando no início da lista. O sentido oposto também é testado.
Antes da correção, o teste excedeu 98 px de deslocamento após a soltura
(`launcher-coast-before-host.log`). Isso reproduz um mecanismo de excesso de
inércia, sem atribuir todos os relatos intermitentes a erro do sensor.

A correção limita somente a velocidade inicial de inércia a ±16 px por passo
nativo, em RELEASED/PRESS_LOST, e apenas quando o objeto rolado é a lista.
O arraste continua com o mesmo filtro de entrada; a desaceleração, os limites
e as posições dos itens ficam sob LVGL. Não modifica o driver ou seu parâmetro
global de decaimento. O gesto de fechar pelo topo e outros apps não recebem
esse limite. A distância já percorrida pelo dedo não é limitada.

Após a mudança, ambos os cenários terminaram com **81 px** de inércia,
aproximadamente uma linha de 82 px (`launcher-coast-verified-host.log`). A
suíte completa também passou, incluindo visibilidade após soltura, correção
dos dois sinais, renderização incremental igual à completa, proteção contra
deslizes, retomadas durante inércia e gestos da bandeja.

Build padrão aprovado em 31,12 s, programa de 1.689.294 bytes e dados estáticos
de 55.248 bytes (`launcher-coast-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T190817398933Z-waveshare_esp32_s3_touch_lcd_146/`.
O resultado de 81 px é do LVGL no host; a sensação no relógio e o relato
intermitente ainda exigem confirmação física. Não houve medição de tempo de
quadro ou modificação da taxa de leitura do touch.
Gravado na COM3 em 63,83 s, com quatro hashes verificados e reset por RTS
(`launcher-coast-upload.log`).

## Retorno indesejado ao puxar além do limite

O usuário confirmou melhora da velocidade, mas relatou um estilingue para
cima ao puxar para baixo e alcançar o fim. Foi reproduzido no host um retorno
após puxar contra a borda, segurar 250 ms e relaxar o dedo 8 px antes de soltar.
O teste anterior falhou por sair da posição limite
(`launcher-boundary-before-host.log`). A elasticidade LVGL já estava desligada;
o caso mostra que a inversão pequena no fim do contato ainda virava entrada
de scroll, sem provar todos os detalhes do relato físico.

O launcher agora descarta o filtro e a velocidade acumulada ao alcançar uma
borda, tanto antes de um passo para fora quanto no SCROLL que chega ao limite.
Enquanto o dedo insiste para fora, ancora no ponto mais externo e não armazena
impulso. Uma inversão de até 12 px é ignorada nessa borda; maior que 12 px
libera a volta no mesmo contato, usando só o excedente. O meio da lista, a
suavização, o limite de 16 px da inércia e os gestos da bandeja não mudam.

O primeiro teste com ambas as bordas passou (`launcher-boundary-after-host.log`).
A confirmação no relógio continua necessária: o cenário do host reproduz
relaxamento pequeno no limite, sem captura dos pontos reais do gesto relatado.

Validação ampliada aprovada (`launcher-boundary-verified-host.log`): começa
30 px antes de cada borda, puxa até o limite, continua para fora, relaxa 8 px
e solta. A posição permanece exatamente no extremo. Depois testa inversão
intencional de 40 px no mesmo contato, que deve liberar a volta. A suíte
completa manteve 81 px de inércia nas solturas rápidas do meio da lista,
visibilidade após soltar, seleção segura e gestos de fechar/cancelar.

Build aprovado em 32,97 s: programa de 1.689.650 bytes e dados estáticos de
55.248 bytes (`launcher-boundary-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T191528242514Z-waveshare_esp32_s3_touch_lcd_146/`.
Não houve alteração de driver, elasticidade global, refresh ou buffers.
Gravado na COM3 em 63,82 s, com quatro hashes verificados e reset por RTS
(`launcher-boundary-upload.log`).

## Isolamento da inércia na lista

O usuário ainda observou vai e volta/estilingue e destacou que brincar com as
bandejas não apresenta o mesmo efeito. Isso não prova falha do LVGL nem do
sensor, mas separa o caminho de scroll/inércia da lista do controle de posição
das bandejas. O próximo teste altera **uma única opção**: limpa
`LV_OBJ_FLAG_SCROLL_MOMENTUM` na lista. Mantém filtro de entrada, bordas,
geometria, hit test e gestos. O limitador anterior continua no código; o LVGL
passa a zerar a velocidade na soltura, sem movimento adicional.

A regressão foi ajustada para exigir zero deslocamento após solturas rápidas
nos dois sentidos e nenhuma rolagem ativa após liberar o contato. Antes da
mudança, o teste falhou (`launcher-no-coast-before-host.log`). Esse caso testa
somente a inércia; os relatos de oscilação durante o contato ainda exigem
distinção e observação no dispositivo.

O usuário esclareceu que o vai e volta ocorre **principalmente depois de
soltar**, reforçando a pertinência desse isolamento. Após a mudança, a suíte
completa passou (`launcher-no-coast-verified-host.log`), com **0 px** adicionais
nas duas solturas rápidas, nenhuma rolagem ativa após liberar o contato e
regressões de borda, inversão intencional, visibilidade e seleção aprovadas.

Build padrão aprovado em 32,22 s: programa 1.689.650 bytes; dados estáticos
55.248 bytes (`launcher-no-coast-build.log`). Referência preservada e verificada:
`logs/firmware-references/20261119T192520945679Z-waveshare_esp32_s3_touch_lcd_146/`.
O teste de host mede a soltura reconhecida pelo LVGL. Ainda falta confirmar
no relógio a percepção após tirar o dedo e a eventual influência das amostras
finais do touch; não foi modificado o debounce ou o driver.
Gravado na COM3 em 63,83 s, com quatro hashes verificados e reset por RTS
(`launcher-no-coast-upload.log`).

### Confirmação física e configuração adotada

Em 19/11/2026, após testar a versão sem inércia, o usuário confirmou:
“agora sim!!”. A configuração foi mantida: arraste suavizado, posições nativas
e SCROLL_MOMENTUM desligado somente na lista. O gesto de fechar a bandeja,
a proteção contra abertura acidental e os controles de energia permanecem.
Essa confirmação valida a melhora percebida nesse fluxo; não é uma medição
de tempo de quadro nem prova de ausência de qualquer falha futura.
