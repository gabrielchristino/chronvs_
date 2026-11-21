# Aplicativos do Chronvs

O runtime mantém uma camada de conteúdo abaixo da interface global e cria cada
aplicativo apenas na primeira abertura. Um app é uma definição estática com ID,
nome, desenho de ícone, visibilidade no launcher, função de criação e
callbacks opcionais de entrada e saída.

## Estrutura

O perfil opt-in `tailtiny` substitui a entrada por um runtime de rede sem
LCD/touch/LVGL ou apps, para a montagem atual sem tela. Mantém uma sessão
Wi-Fi exclusiva, cadastra `chronvs` no Tailscale e oferece eco UDP na porta
9000 do túnel. Não inicia NTP/Clima do runtime normal; sincroniza somente
o relógio de sistema para validar certificados. O padrão não inclui TailTiny.
Configuração, persistência, restauração e validação em [`tailtiny.md`](tailtiny.md).

As linhas do launcher desativam `LV_OBJ_FLAG_SCROLL_ON_FOCUS`: o LVGL não
deve animar automaticamente o conteúdo para enquadrar o botão ao tocá-lo.
Esse ajuste podia concorrer com o arraste/inércia e deslocar a listagem mesmo
com dedo parado. A curva continua atualizada no timer de 20 ms; os gestos da
bandeja e os callbacks de ciclo de vida não mudaram.
O launcher mantém o alvo do contato com `LV_OBJ_FLAG_PRESS_LOCK` nas linhas,
lista e faixa de fechamento. A seleção exige toque na linha original visível,
sem excursão maior que 8 px em qualquer eixo. Movimento ou scroll invalida o
clique até soltar; parar a inércia e contatos iniciados até 150 ms após rolagem
por touch não selecionam app. Não há timer adicional nem alteração global da
sensibilidade do ponteiro; a atividade continua vinculada por `app_input`.
O teste com linhas FLOATING e filtro visual Q8 de 50 ms foi retirado após
desaparecimento dos itens ao soltar no dispositivo. O padrão mantém scroll
nativo e curva pelo timer de 20 ms, pausado fora do launcher e sem renderização
com tela apagada. Não há mais duas posições distintas para o mesmo scroll.
Na segunda versão de teste, `app_list_touch_event` suaviza somente o vetor
vertical de entrada, com filtro Q8 de 50 ms e atraso limitado a 48 px. O LVGL
mantém geometria, limites, processamento de soltura e inércia. Os pontos reais
não são alterados e continuam determinando hit test, limite de toque de 8 px
e fechamento pelo topo. Sem novo timer ou alteração no driver de touch.
A implementação depende da ordem de eventos do LVGL **8.3.11 fixado**:
PRESSING ocorre depois do cálculo de `proc.types.pointer.vect` e antes de
`_lv_indev_scroll_handler`. Ajusta o vetor Y e os vetores de inércia Y nesse
intervalo. Uma atualização de LVGL exige rever esse ponto e repetir os testes.
Ao receber RELEASED/PRESS_LOST de um contato de rolagem do launcher, limita
os dois vetores Y de inércia a ±16 px, antes de o LVGL iniciar a desaceleração.
Não limita `vect.y` durante o arraste nem altera o `scroll_throw` compartilhado.
A verificação de `lv_indev_get_scroll_obj(input)==list` restringe a mudança à lista.
Na configuração adotada, a lista também limpa SCROLL_MOMENTUM. O LVGL
zera a velocidade na soltura, portanto não há movimento de inércia, apesar de
o limitador anterior continuar no código. É a única opção de comportamento
alterada em relação ao candidato com proteção de bordas; filtro, geometria,
gestos e driver permanecem iguais. O usuário confirmou no relógio o resultado
da versão sem inércia em 19/11/2026; essa opção permanece no padrão.
O launcher agora recicla as linhas para rolagem circular nos dois sentidos.
O mesmo timer de 20 ms reposiciona linhas fora da área visível e normaliza
o scroll para uma volta intermediária, preservando a fase e o espaçamento.
Padding inferior reserva espaço para o scroll nativo; não há árvore duplicada
para o catálogo atual. Catálogos pequenos repetem voltas inteiras até cobrir
a tela e a margem de reciclagem. Não há mais bloqueio de borda nem zona de
inversão de 12 px. IDs e callbacks continuam vinculados às próprias linhas;
nenhuma tarefa, leitura de hardware ou timer adicional é criado.
O fade compõe apenas o disco de 60 × 60 px com `opa_layered`, abrangendo
callbacks vetoriais que ignoram a opacidade de estilo. Nome e fundo pressionado
recebem a mesma opacidade separadamente, sem compor a linha inteira. O LVGL
reserva sua camada temporária no pool em PSRAM apenas durante fades parciais;
linhas totalmente transparentes são puladas com `opa_layered=0`.
`LV_COLOR_SCREEN_TRANSP=1` habilita essas camadas alfa no renderizador software;
o driver continua com tela opaca RGB565 e os mesmos buffers parciais.
Build padrão, suíte de interface `-System` e gravação com hashes verificados
passaram em 19/11/2026. O teste de pixels confirmou metade do contraste na
metade do fade para todos os ícones testados, no topo e na base. O usuário
aprovou no relógio a rolagem circular e o fade corrigido na mesma data.

No diagnóstico `panel_autotest`, a inicialização instala um roteiro temporário
no ponteiro LVGL, e o loop principal consulta seu avanço depois de cada
`lv_timer_handler`. Os apps e os handlers de gestos são os normais. O probe do
painel rápido é somente leitura e existe apenas nesse build. No padrão, as
chamadas do roteiro são no-ops. Ver [`panel-autotest.md`](panel-autotest.md).
O perfil separado `launcher_scroll_autotest` reutiliza esses pontos de entrada
para testar apenas o scroll no display físico, com o catálogo completo e
handlers normais. Substitui temporariamente os callbacks de entrada e medição;
restaura todos ao concluir e retorna ao mostrador. Não altera a política de
atividade no padrão. Casos, métricas e limites em [`launcher-scroll.md`](launcher-scroll.md).
Após os gestos, o diagnóstico compara desenhos omitindo uma parte por vez em
fases e área de refresh fixas. `chronvs_app_list_cost_frame` e seus seletores
existem somente com `CHRONVS_LAUNCHER_SCROLL_AUTOTEST`; o padrão não compila
esse caminho nem recebe setters ou timers adicionais.

No padrão, a criação do launcher vincula `ui/circle_fill.c` à raiz. O módulo
grava apenas o fundo em RLE, antes dos filhos, usando as faixas já renderizadas.
Reserva até 16 KiB em PSRAM fora do pool LVGL e libera os dados ao excluir a
raiz. Reutiliza-os apenas quando a lista está totalmente aberta, sozinha na
camada de apps, com composição compatível e mesmo fundo dos ancestrais.
Prévia do mostrador, deslocamento, máscara externa, transparência, estilo
incompatível e falha de reserva mantêm o desenho original. Cor/fundo alterado
invalida o cache. Mantém o `clip_corner` e a máscara dos filhos no LVGL.
O contrato depende de `DRAW_PART_END` preceder os filhos no LVGL 8.3.11;
atualizar LVGL exige repetir a equivalência de pixels e os testes físicos.

As variantes `panel_autotest_clip`, `panel_autotest_cache64`,
`panel_autotest_icache32` e `panel_autotest_lvgl_os` separam recorte e
configuração de cache/compilação. O recorte seletivo vincula subárvores
explicitamente, sem mudar hit test ou criar tarefas; continua experimental.
`panel_autotest_icache32_weather` acrescenta, após os 42 gestos, uma abertura
normal do Clima, com seu serviço HTTPS e persistência de cache usuais. A entrada
fica reservada até resultado ou timeout de 60 s; tela apagada ou modal aborta.
O roteiro não gera atividade artificial e retorna ao mostrador ao concluir.
Isso não integra a navegação de uso nem ocorre no firmware padrão.
O perfil `panel_autotest_parts` mede subárvores de controles no mesmo roteiro,
com uma tabela de 64 registros em PSRAM e saída acumulada somente no final.
`panel_autotest_arc_clip` testa um recorte do centro transparente do arco de
brilho; a função software original continua responsável pelos pixels das bordas.
Detalhes e limites em `panel-autotest.md`.
O círculo inferior esquerdo alterna brilho 35%, 50%, 75% e 100%, desativando ECO.
O círculo da bateria alterna ECO: entrar fixa 35%; sair mantém 35% sem
restaurar intensidade anterior. A única preferência `light_mode`
define tanto a intensidade como a política ECO; chaves antigas servem para
migração quando o modo não existe ou é inválido. Os modos de referência
`CHRONVS_BRIGHTNESS_ARC_REFERENCE` e `CHRONVS_BRIGHTNESS_SLIDER_REFERENCE`
conservam os controles anteriores apenas para diagnósticos comparativos.
`panel_autotest_lighting` exercita o controle novo com os mesmos 42 gestos e
Clima. O círculo usa `chronvs_ui_style_control`, sem tarefa ou cache adicional,
ocupando um dos sete slots da grade 2–3–2.
Sol e bateria são símbolos vetoriais estáticos de 24 px, acima dos valores,
como no volume. Só a bateria usa `%`; brilho mostra 35/50/75/100. Os símbolos
não capturam o toque, que continua no botão, sem novos timers ou bitmaps.

As duas bandejas usam `ui/panel_motion.h` para concluir o movimento em 220 ms
com `ease_out`. O launcher usa `chronvs_app_settle_y`: anima a raiz ativa ou
de prévia e só ao terminar confirma o destino ou cancela a prévia. O runtime
atualiza o layout antes de ler a posição inicial e cancela sua animação quando
há outra navegação, uma nova prévia ou reposicionamento explícito. Abrir outro
app também oculta a prévia antiga, evitando uma conclusão atrasada sobre o
destino novo. `on_show`/`on_hide` continuam ocorrendo na troca efetiva de app.
Novos contatos nas bandejas durante a conclusão são consumidos até soltar, com atividade
real registrada. `CHRONVS_PANEL_SNAP_REFERENCE` conserva a troca direta somente
nos controles diagnósticos; não integra o padrão.

Os ambientes isolados `display_scan`/`display_scan_te` de 19/11/2026 instalam
observação de TE e métricas após inicializar a placa. Após abrir o mostrador,
criam uma bancada temporária sobre ele; direita remove sua árvore e timer e
abre o launcher, mantendo a instrumentação dos menus. Não registram app novo.
No padrão, essas chamadas são no-ops compilados, sem ISR, timer ou objetos
adicionais. Interação, métricas e restauração em [`display-scan.md`](display-scan.md).

O padrão e o diagnóstico `display_scan_scene` habilitam `CHRONVS_WATCH_SCENE_CACHE`:
o mostrador registra linhas RGB565 já produzidas pelo redraw normal em um
cache RLE limitado a 96 KiB em PSRAM, fora do pool LVGL. Redesenhos incidentais
reutilizam essa imagem congelada. Hora/temperatura invalidam o cache; posição
deslocada, máscara externa, captura incompleta, falta de reserva ou limite
excedido conservam o vetor. Não cria renderização adicional, timer ou tarefa.
Mantém leitura de RTC apenas nos fluxos existentes e não trabalha com tela apagada.
O padrão usa também `LV_CIRCLE_CACHE_SIZE=16` pelas flags do PlatformIO; o
fallback de oito entradas em `lv_conf.h` preserva os controles antigos. A suíte
`run_Relogio_ui.ps1 -System` acompanha o padrão; `-WatchReference` seleciona
explicitamente o mostrador anterior, sem esse cache e com oito máscaras.
O padrão usa cache de instruções de 32 KiB e dados de 32 KiB/linha de 32 bytes,
após comparação física das bandejas e do Clima/HTTPS. Isso consome 16 KiB
internos adicionais: considerar o maior bloco DMA ao ampliar serviços. Não
altera o pool LVGL ou acrescenta reservas por aplicativo. Ver `panel-autotest.md`.

O experimento `launcher_icon_cache` gera snapshots dos ícones fixos na criação
do launcher e os mantém em PSRAM enquanto o app permanece retido. Usa vetor
durante fade e em falha de reserva/snapshot; não muda `create_icon` dos apps.
Somente o experimento habilita snapshot LVGL. Limites em `performance.md`.

```text
src/
├── apps/       catálogo, interfaces e estado visual de cada aplicativo
├── core/       registro, ciclo de vida e navegação
├── platform/   inicialização específica da placa
├── services/   dados compartilhados de RTC, bateria e futuros sensores
├── main.c      composição do firmware e loop principal
└── ui/         painel rápido e política global da tela
```

Aplicativos devem consumir hardware por meio de `services/`; não devem acessar
I2C, ADC ou drivers da placa diretamente. Objetos LVGL devem ser criados como
filhos do `parent` recebido e usados somente na tarefa que executa o loop LVGL.

## App mínimo

```c
#include "apps/app_catalog.h"
#include "ui/app_input.h"

static void return_to_apps(void) { chronvs_app_open("apps"); }
static chronvs_ui_app_input_t input = {.back = return_to_apps};

static void create_timer_icon(lv_obj_t *parent) {
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, "T");
    lv_obj_center(icon);
}

static lv_obj_t *create_timer_app(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Timer");
    lv_obj_center(title);
    chronvs_ui_app_input_bind(root, &input);
    return root;
}

const chronvs_app_t chronvs_timer_app = {
    .id = "timer",
    .name = "Timer",
    .create_icon = create_timer_icon,
    .launcher_visible = true,
    .create = create_timer_app,
    .on_show = NULL,
    .on_hide = NULL,
};
CHRONVS_REGISTER_APP(chronvs_timer_app)
```

O runtime descobre automaticamente os descritores registrados, sem um vetor
central.

O catálogo registra todos os apps durante o boot. O app `apps` fornece uma
lista curva, com ícone à esquerda e nome ao lado, e pode ser aberto com
`chronvs_app_open("apps")`.
Durante a rolagem vertical, a posição horizontal de cada linha acompanha
um arco calculado a partir da sua altura visível. A geometria e os gestos
estão em [`interface.md`](interface.md#lista-de-apps-em-arco).

## Navegação e apps instalados

`watch` e `apps` são apps internos de navegação e usam
`.launcher_visible = false`; portanto, não aparecem na própria lista. Os apps
destinados ao launcher usam `.launcher_visible = true` e entram na lista automaticamente.

Calculadora, Relogio, Notas, Clima e Calendario são apps nativos registrados individualmente com
`CHRONVS_REGISTER_APP`. A calculadora mantém sua avaliação aritmética em
`core/calculator.c` e sua interface em `apps/calculator_app.c`.

O app Clima está implementado em `apps/weather_app.c`; sua especificação e
validação estão em [`weather.md`](weather.md). `on_show` restaura o cache e
solicita uma consulta; reabrir durante uma consulta existente acompanha essa
operação, sem enfileirar outra. `on_hide` pausa o timer visual. A tarefa de rede
termina e salva o resultado mesmo fora da tela, sem acessar LVGL. A atualização
fica pendente até a próxima leitura pela UI; não muda o app ativo.

O app Vox usa `services/voice_lab_service.c` para carregar o MultiNet6 apenas
quando o usuário inicia a escuta. O serviço entrega à UI o campo `string` do
resultado, sem mapear o ID para uma palavra portuguesa, e registra a mesma
saída na serial com prefixo `VOX:`. A escuta continua até o usuário interromper
ou sair do app; `on_hide` solicita a parada e o timer visual é pausado.
Apagar a tela em AUTO/ECO também solicita a parada. O loop só entra em light
sleep depois que `chronvs_voice_lab_active()` indica o fim da limpeza. Esse
indicador protege toda a vida da tarefa, inclusive após publicar um erro,
impedindo reinício enquanto modelo, vocabulário e I2S ainda são liberados.
A UI não altera objetos enquanto a tela está apagada. Leituras I2S usam
timeout de 100 ms e acumulam dados parciais até completar um quadro.
`tests/run_voice_tests.ps1` cobre cancelamento, erro, reinício, exclusão
durante limpeza e leituras parciais com timeout usando o serviço real.
Essa saída serial, assim como os logs de apps e memória, só fica habilitada
nos builds de diagnóstico. O firmware padrão mantém o texto do Vox na tela,
com logs e consoles desativados; veja [`performance.md`](performance.md).

O ambiente opcional `vox_capture` substitui somente o worker do Vox por coleta
PCM16/16 kHz de dois segundos, sem criar MultiNet. Reserva 64.000 bytes em
PSRAM por amostra, fecha I2S e envia linhas `VOXPCM` pela console; depois
libera o buffer. Preserva a mesma propriedade exclusiva da tarefa, fila de
resultados e cancelamento ao sair/apagar a tela. Não acessa LVGL no worker.
Esse ambiente mantém stdout ativo com logs e profiling desativados, evitando
texto intercalado nos pacotes. O envio cede um tick por pacote para atender
o watchdog; a captura I2S continua sendo concluída antes da transmissão.
O fluxo de coleta pessoal, treinamento em CPU e avaliação reservada está em
[`voice-training.md`](voice-training.md). O candidato exportado ainda não é
carregado pelo firmware padrão e não é um modelo ESP-SR.

`services/weather_service.c` mantém cache NVS e uma caixa de resultado sob mutex.
`services/weather_data.c` valida JSON, horário civil e códigos WMO sem acessar
hardware. `services/wifi_session_service.c` centraliza rádio, credenciais e
handlers; seu mutex é inicializado no boot antes da tarefa NTP. A inicialização
da pilha de rede ocorre somente na primeira sessão. NTP e Clima adquirem a sessão
exclusiva em suas tarefas e desligam o rádio antes de liberar a próxima operação.
Apps não chamam APIs globais de Wi-Fi. Não há nova consulta periódica de clima.

NTP cria uma tarefa temporária de 6.144 bytes de pilha por sessão. Ao terminar,
inclusive por falha de rede/NTP/RTC, libera a sessão Wi-Fi e exclui a tarefa;
o FreeRTOS recupera sua memória na tarefa idle. O próximo prazo é monotônico,
12 horas após o término da tentativa, e limita o light sleep junto da agenda.
`chronvs_time_sync_poll()` roda na UI antes da decisão de suspensão e nunca
executa rede ali. O indicador de atividade cobre criação, espera pela sessão
exclusiva e limpeza. Falha ao criar a tarefa agenda nova tentativa em 60 s.
Chamadas repetidas de `start` não duplicam tarefas. A caixa de resultado
persiste fora da tarefa até a UI consumir a correção, mesmo com tela apagada.

O painel rápido usa somente `chronvs_weather_init` e `chronvs_weather_get_snapshot`
para exibir temperatura e condição salvas; não dispara consulta nem consome a
caixa de resultados do app. Sem cache, omite a temperatura. `ui/weather_icon.c`
compartilha os símbolos vetoriais entre app, launcher e painel rápido.
Tocar no atalho abre `weather` pelo gerenciador e fecha o painel. O `on_show`
do app solicita a atualização; a visualização do atalho continua usando só cache.

Clima e Calendario consultam `chronvs_Relogio_time()` para idade dos dados e
data atual. Somente o loop principal faz a leitura periódica de RTC, com
tela acesa, e alimenta o serviço compartilhado; NTP entrega suas correções
pela caixa de resultado. Os apps não leem I2C nem reancoram a hora. A abertura,
Hoje e a atualização após despertar reutilizam a contagem monotônica,
preservando a referência válida quando uma leitura posterior de RTC falha.

`chronvs_rtc_refresh()` concentra essa política na tarefa da UI: consulta no
boot, ao acordar e a cada 60 s com referência válida. Sem referência, tenta
novamente após 1 s; com tela apagada não acessa I2C. A contagem compartilhada
continua avançando por tempo monotônico entre consultas. A leitura ainda é
síncrona, com timeout de 100 ms. O mostrador guarda uma amostra da hora para
todas as faixas de desenho, sem interpolação durante renderizações incidentais.
`on_show`, despertar e primeira referência válida no boot solicitam uma
atualização. Seu timer permanece pausado em repouso e em `on_hide`; a UI
global o ativa a cada segundo somente durante pressão prolongada de 600 ms,
pausando ao soltar, cancelar por movimento ou apagar a tela. O serviço de
tempo e os prazos de alarmes continuam independentes da imagem estática.

Os centros dos três submostradores usam deslocamentos constantes relativos
ao centro do objeto, preservando a composição anterior dos minutos em zero.
Não há cálculo de órbitas por faixa de desenho. Os ângulos dos ponteiros
continuam usando a mesma amostra de hora; o ponteiro de minutos é desenhado
antes das faces fixas. Isso remove a trigonometria de posicionamento, sem
adicionar cache, alocação ou tarefa. O ganho de fluidez ainda não foi medido
no painel; o restante do desenho vetorial e das transferências é preservado.

O submostrador de segundos e seu cálculo de ângulo foram removidos. A escala
de minutos armazena somente 12 posições de números, sem geometria ou desenho
dos 48 traços intermediários. Os segundos da amostra de hora continuam
refinando os ângulos dos ponteiros de minutos e horas. A política de
atualização da imagem e os serviços de tempo permanecem os mesmos.

O mostrador retém uma linha de 824 bytes no pool LVGL em PSRAM para reproduzir
os dois anéis externos a partir de dados RLE constantes na flash. Não aloca
por quadro e mantém o caminho vetorial se a linha não estiver disponível,
se o mostrador estiver deslocado ou houver máscara externa. O gerador e a
comparação de pixels estão documentados em `performance.md`; mudanças na
paleta ou geometria dos anéis exigem regenerar esses dados.

No experimento `display_profile_mother`, a face fixa do disco principal
reutiliza essa mesma linha, sequencialmente na tarefa da interface. São
9.982 bytes adicionais constantes na flash, sem novo timer, tarefa, leitura
de hardware ou alocação. O padrão continua vetorial para essa face. O cache
experimental só atende o centro estacionário (205,5; 205,5), com renderizador
software e sem máscara externa; nos demais casos, ou sem a linha temporária,
usa o vetor. Paleta, raio, borda ou fundo dos anéis alterados exigem regenerar
o cache e repetir a comparação antes de habilitá-lo.

O diagnóstico de listras usou uma espera de 2 s antes da sessão Wi-Fi.
Após corrigir a pressão de memória e confirmar a tela no relógio, o atraso
foi retirado; os logs de memória nas etapas de rede/TLS foram mantidos.
Consulte os resultados em [`weather.md`](weather.md).

O pool de objetos LVGL tem 128 KiB em PSRAM, alocado uma vez por
`platform/lvgl_memory.c` via `LV_MEM_POOL_ALLOC`. Mantém TLSF e reutiliza a
reserva ao reinicializar LVGL. Essa mudança libera RAM interna após falhas de
SPI e alocação TLS observadas ao abrir Clima; os buffers de desenho são separados
e conservam os parâmetros validados. O usuário confirmou Clima sem listras e
com dados. Também confirmou cache após reinício sem rede, recuperação da conexão,
abertura durante NTP, saída durante a consulta e apagar/acordar a tela, conforme
o registro em `weather.md`. Medições de consumo, fluidez e picos de memória
permanecem avaliações separadas.

A calculadora é compilada junto ao firmware e funciona sem cartão. Não há
carregador, instalador ou interpretador de apps externos. A tela é criada na
primeira abertura e reutilizada; cada abertura inicia uma expressão vazia.
O layout e o ícone anterior foram validados no relógio antes da migração para app nativo.
Após a migração, passaram os testes de aritmética e integração LVGL (toques,
energia, parênteses e exclusão), a compilação e a gravação com hashes verificados.
Os testes estão em `tests/run_calculator_tests.ps1` e
`tests/run_Relogio_ui.ps1 -System`. O firmware não acessa os antigos arquivos no SD.

- No mostrador, arrastar para cima revela a lista de apps sobre o mostrador e
  acompanha o dedo desde a borda inferior durante o gesto.
- Na lista, tocar uma linha abre o app.
- Um arraste para baixo só inicia o retorno quando começa nos 60 px superiores
  da lista. Assim, puxar um conteúdo a partir do meio não fecha o launcher;
  são necessários 120 px para confirmar o retorno ao mostrador.
- `Relogio` reúne cronômetro, timer e alarmes. Abre no cronômetro e navega para
  timer e alarmes com arrastes para cima; arrastes para baixo voltam de
  alarmes para timer e de timer para cronômetro. Na lista de alarmes, o
  retorno funciona também sobre as linhas quando o conteúdo está no topo.
  Na criação de alarmes, subir fora do arco avança da hora para os minutos e
  para os dias; descer retorna à etapa anterior. O arco mantém o arraste para
  selecionar o valor.
  Volta à lista de apps com um
  arraste para a direita; durante a criação/detalhe de um alarme, esse gesto
  volta à etapa anterior. A interface está detalhada em `docs/interface.md`.

`create_icon` recebe um objeto LVGL de 44 × 44 pixels, sem estilo próprio. O
ícone deve desenhar todos os elementos dentro desse objeto; `Relogio` usa
`LV_EVENT_DRAW_MAIN` para desenhar o corpo, coroa, aro e ponteiro do
cronômetro sem depender de uma imagem bitmap.

### Contrato para novos apps

Todo novo app deve voltar com arraste da esquerda para a direita, sem botão
Voltar no topo: mais de 80 px horizontais e predominância de 20 px sobre o
deslocamento vertical absoluto. Na tela inicial, abra `apps`; em subetapas,
retorne à etapa anterior; em confirmações, cancele. Salve edições antes de
sair e preserve o rascunho quando houver falha. O contato é consumido até a
liberação, evitando cliques acidentais. Exceções de arraste próprio, como os
arcos do Relogio, devem estar documentadas em `docs/interface.md`.

`ui/app_input.h` fornece `chronvs_ui_app_input_t` com callback `back` e
`chronvs_ui_app_input_bind(subtree, &state)`. Mantenha o estado durante toda a
vida do app e vincule cada nova subárvore uma única vez, depois de criar seus
controles e callbacks locais; diálogos criados depois precisam de vinculação
própria. Defina `LV_OBJ_FLAG_CLICKABLE` antes de vincular. O helper só aloca
callbacks nos objetos que podem receber toque, mas percorre todos os filhos,
inclusive os de contêineres não clicáveis. Rótulos decorativos não precisam
de registros próprios: o hit test do LVGL entrega o contato ao controle.
O helper observa cada alvo sem duplicar eventos propagados, registra
atividade no início, durante e ao liberar o contato e reconhece o retorno
durante o arraste. O callback pode excluir a página atual. Notas é a referência
de uso. Contatos prolongados também devem manter a tela ativa; timers de
animação e salvamento não são atividade. A proteção global continua consumindo
o primeiro toque com a tela apagada.

Os estilos de controles em `ui/control_style.c` são compartilhados e
inicializados uma única vez na tarefa da interface. Estados normal,
pressionado, selecionado e desabilitado mantêm as mesmas propriedades;
os apps podem sobrescrevê-las com setters locais. Não modifique os estilos
compartilhados depois de inicializados. Isso evita copiar propriedades para
cada tecla/botão e preserva espaço para editores e avisos simultâneos.

Priorize gestos para navegar por páginas ou períodos nas novas implementações,
com subir para avançar, descer para retornar e direita para voltar um nível.
Toques selecionam conteúdo ou executam ações; evite setas redundantes e títulos
que apenas repetem o nome do launcher. Respeite rolagem e controles com arraste
próprio, documentando os conflitos. O callback opcional `vertical` de
`chronvs_ui_app_input_t` recebe `+1` ao subir e `-1` ao descer, com mais de 80 px
e predominância de 20 px; consome o contato antes de chamar o app. Sem callback,
o helper mantém somente o retorno horizontal existente.

Abra o app a partir de um launcher ou atalho:

```c
chronvs_app_open("timer");
```

`create` é chamado uma única vez e o objeto retornado é preservado ao alternar
entre apps. `on_show` serve para atualizar dados ou retomar timers; `on_hide`
serve para pausar trabalho que não deve continuar fora da tela. O gerenciador
aceita atualmente dez apps e rejeita IDs repetidos. O catálogo automático
também comporta dez registros, incluindo os dois apps internos.

## Corrida e serviços em segundo plano

`apps/run_app.c` registra `run`/Corrida. A UI consome `run_service`, pausa
seu timer no `on_hide` e libera somente a preparação não iniciada. Atividades
em gravação/pausa sobrevivem à navegação. `main.c` recebe UART, aplica fallback
de hora e chama `chronvs_run_poll` antes da decisão de sono. Os pedidos de
GPS de diagnóstico, corrida e hora são independentes; os dois últimos impedem
light sleep e continuam com tela apagada, sem ler RTC/bateria nem renderizar.

`sd_service` monta FAT/SDMMC sob demanda, sem formatação. `run_service` salva
CSV e GPX parcial em fluxo, sincroniza a cada 5 s, na pausa e ao finalizar,
e publica snapshot sem acessar LVGL. Erros fecham arquivos e liberam seu GPS;
parciais são preservados para recuperação no PC. O menu Energia tenta finalizar
a atividade antes de reiniciar/desligar, inclusive com outro app ativo.

`time_sync_service` conserva prioridade NTP e busca UTC do GPS após ausência/
falha de rede. Janela de 120 s, repetição após 1 h e correção a cada 12 h;
escrita no RTC usa UTC−3, aguarda tela acesa e repete falha após 60 s. Os filtros,
formatos, limites de recuperação e procedimentos estão em [`running.md`](running.md).

## GPS

`apps/gps_app.c` registra `gps` e consulta os serviços GPS e A-GNSS.
`on_show` solicita recepção; `on_hide` libera seu pedido e pausa o timer. Outros
clientes, incluindo assistência, podem manter o driver UART ativo.
O loop principal chama `chronvs_gps_poll(display_off)` antes de decidir pelo
light sleep: recebe no máximo 1.024 bytes por passagem, sem bloquear nem criar
tarefa, e remove o driver com a tela apagada se nenhum cliente de corrida/hora
mantiver recepção em segundo plano. Ao despertar com o app aberto,
reabre a recepção sem reutilizar uma posição anterior. O módulo pode continuar
alimentado; não há controle elétrico de energia pelo firmware.

UART1 usa RX44 com buffer de 4 KiB e TX43 com buffer de 512 bytes sob demanda.
O TX envia A-GNSS em pequenos frames, com consulta de conclusão de zero ticks,
sem aguardar toda a transferência na tarefa LVGL. Nos perfis com console UART0,
TX permanece intocado e a assistência não é iniciada. A velocidade percorre 9600, 38400, 115200, 4800,
19200 e 57600 baud, três segundos por tentativa; um NMEA com checksum válido
fixa a velocidade até a próxima sessão. Perda da serial não inicia nova busca
enquanto a sessão permanece aberta, para não esconder uma conexão intermitente.

`services/gps_data.c` valida checksum e interpreta RMC/GGA com talkers distintos
(`GP`, `GN`, `BD` etc.). Status sem fix ou coordenadas inválidas removem a
posição; posição, velocidade, altitude, HDOP e satélites têm expiração própria
de cinco segundos. O timer visual de um segundo só muda textos diferentes,
sem renderizar com tela apagada. Todas as chamadas são da tarefa da interface.
GSV mantém até quatro talkers independentes, com contagem visível e maior
C/N0 da sequência atual, inclusive antes do fix. Aceita signal ID opcional,
rejeita fragmentos fora de sequência e expira em 5 s, sem modificar o fix.
Não soma sistemas/sinais sobrepostos. O diagnóstico não altera a aquisição
nem envia configuração ao módulo.
O app de diagnóstico não grava coordenadas. GPS e a preparação de Corrida
solicitam `agnss_service` automaticamente, com limite de uma tentativa automática
a cada 10 minutos; a página de assistência permite repetir manualmente. O worker
temporário compartilha a sessão Wi-Fi exclusiva com NTP/Clima, valida o download
HTTPS em PSRAM e publica o resultado antes de terminar. Somente a tarefa UI
envia UART ou acessa LVGL. A operação mantém recepção e impede light sleep até
concluir ou falhar, inclusive com tela apagada e após sair do app.
O worker prioriza previsão orbital no SD/Wi-Fi e recorre a IGS/BKG quando ela
não serve. Descompacta RINEX e converte para CASIC no relógio, sem computador
ou servidor de conversão. Persiste a previsão completa e um pequeno cache IGS
no SD, sem formatar; seleciona no máximo um registro atual por satélite.
O lote continua após NACK orbital, repetindo o mesmo registro no máximo duas
vezes, com 200 ms entre tentativas. Registra resultado e tentativas por PRN;
ao terminar, consulta NAV-STATUS inclusive após rejeições. Um lote de previsão
com órbitas rejeitadas permite uma única tentativa IGS, nunca durante corrida.
O envio aguarda ACK correspondente, com timeout de 1,5 s por pacote;
não bloqueia a UI nem envia o pacote seguinte enquanto a confirmação estiver
pendente. Sem ACK interrompe o lote, conta o PRN sem confirmação e consulta
status; não avança nem inicia fallback, pois uma resposta atrasada não contém
PRN e poderia ser atribuída ao registro seguinte. AID-INI rejeitado também
interrompe. ACK, NACK e contagem por PRN têm significados separados de fix.
O snapshot preserva o total de PRNs distintos com ACK entre previsão e IGS
no mesmo pedido, além das contagens/estados por PRN da fonte atual.

Antes de injetar, exige hora confiável da sincronização do boot e órbitas
atuais, e não injeta numa corrida iniciada/pausada. Após AID-INI, exige GGA
novo com fix e ao menos quatro satélites em uso antes de voltar a publicar
posição. Reabrir a UART não remove essa proteção: o módulo continua alimentado.
Não grava configuração permanente nem reinicia o receptor. Mensagens binárias
CASIC e NMEA são separadas, com limite e timeout para pacotes binários. ACK/NACK
e NAV-STATUS são diagnóstico, sem produzir fix por si mesmos.
Procedimento, referência regional e limites em [`agnss.md`](agnss.md) e [`gps.md`](gps.md).

O ambiente opcional `gps_standby_test` acrescenta uma terceira página de teste por
gesto vertical. `chronvs_gps_query_version` envia CAS06 e captura TXT;
`chronvs_gps_test_standby` envia CAS12/60 s uma única vez. O serviço observa
NMEA por tempo monotônico, sem bloquear por todo o prazo nem criar tarefas.
As operações exigem serial recente e não podem se sobrepor ao teste ativo.
Ao sair/apagar, marca teste interrompido e libera UART, preservando o resultado
na próxima sessão. Não automatiza standby nem grava configuração no módulo.
Estado de silêncio não é tratado como confirmação de economia; consumo exige
medição. A variante padrão tem dados e assistência, sem o comando de standby.

## Calendario — Calendário

`apps/Calendario_app.c` registra `Calendario` automaticamente, com ícone vetorial e
duas páginas reutilizadas: mês e detalhe da data. A grade inteira é desenhada
em um objeto LVGL, com hit-test por célula, evitando 42 botões e seus labels.
`core/calendar.c` calcula dias do mês, ano bissexto, dia da semana e distância
entre datas civis de 2000 a 2099, sem timezone, rede ou persistência.
No mês, o callback `vertical` avança ao subir e retorna ao descer, uma vez por
contato, inclusive sobre a grade e Hoje; no detalhe, não troca o mês. Não há
setas. O retorno para a direita permanece hierárquico.

`on_show` abre o mês atual. O timer de 250 ms consulta a hora compartilhada na
abertura, em Hoje, ao despertar e a cada minuto com tela ativa; só redesenha
quando os dados mudam. `on_hide` pausa o timer e a tela apagada impede leituras
e alterações visuais. A troca de data atualiza destaque e distância sem mudar
o mês que o usuário está consultando. Datas impossíveis, inclusive 31/02,
são rejeitadas mesmo que o serviço RTC marque a leitura como válida.

Sem data válida na primeira abertura, mostra `Sem data` e aguarda recuperação.
Depois de obter uma referência válida, falhas do RTC preservam a contagem
compartilhada, o destaque e Hoje. Não usa horário de demonstração para cálculos.

`tests/run_Calendario_tests.ps1` testa todos os 36.525 dias do intervalo contra o
calendário da biblioteca C do host, navegação nos limites, bissextos, seis
semanas, retorno sem clique, contatos prolongados, recuperação do RTC e pausa
com tela apagada/oculta. Também gera capturas e verifica estabilidade da
memória LVGL. `tests/run_Relogio_ui.ps1 -System` inclui Calendario no launcher e no
orçamento compartilhado, além do primeiro toque reservado ao despertar.
Build PlatformIO e ambos os testes passaram na versão com gestos verticais.
No host, 100 ciclos de abertura/detalhe/retorno mantiveram a memória livre
estável; com todos os apps criados, o maior bloco LVGL livre foi de 30.576
bytes. Essa medição do pool LVGL simulado não representa RAM interna ou DMA
livres no dispositivo.
A validação física da legibilidade e precisão de seleção segue pendente.

## Agenda e avisos do Relogio

`services/Relogio_service.c` mantém uma contagem regressiva e até 12 alarmes
semanais persistidos em `aion/alarms_v1` na NVS; o namespace é mantido para
preservar os alarmes existentes. O serviço é inicializado no boot, depois
da inicialização de NVS pelo sistema, independentemente da abertura do app.
Todas as APIs são chamadas pela tarefa principal. `main.c` fornece horários
válidos já lidos do RTC e correções NTP entregues por
`chronvs_time_sync_take_update`, e chama o serviço em cada iteração.

Prazos de timer e adiamento usam `esp_timer_get_time()`, sem depender de
ajustes NTP. A agenda semanal extrapola o último horário civil válido pelo
mesmo contador, incluindo mudanças de data e ano bissexto, sem consultar o
hardware com a tela apagada. Não há renderização dentro desse serviço.
`chronvs_Relogio_next_wake_ms()` informa ao loop o prazo mais próximo entre timer,
adiamento, alarme e lembrete, limitado a 5 min; o loop LVGL continua atendendo o
touch após cada despertar.

`ui/Relogio_alert.c` consulta os avisos pendentes, acorda a tela e cria uma
sobreposição opaca em `lv_layer_top()`. Avisos simultâneos permanecem na
agenda até serem dispensados; a interface não precisa abrir o app Relogio.
`services/sound_service.c` isola o I2S e gera bips numa tarefa FreeRTOS, com
pedido de reprodução atômico; essa tarefa nunca acessa LVGL. Se o speaker
falhar ao inicializar, a interface continua funcionando e o erro vai ao log.
I2S e a tarefa de som são criados sob demanda no primeiro pedido de bip,
sem inicialização de áudio no boot. Após a espera síncrona QSPI aplicada no
driver, o usuário confirmou boot, aviso e extensão `+1` sem artefatos físicos.
Quando ociosa ou em mute, a tarefa desabilita o canal e bloqueia em uma
notificação FreeRTOS, sem polling periódico. Mudanças de volume/alerta e
pedidos de prévia a acordam; um comando anterior à espera fica pendente,
evitando perder a retomada. As flags atômicas continuam sendo a fonte do
estado atual. Canal e pilha permanecem reservados após a primeira utilização.
`ui/Relogio_alert.c` solicita áudio ao terminar de criar o aviso, sem a espera
artificial de 2 s usada no diagnóstico anterior. Dispensar ou adicionar tempo
interrompe o pedido; falha ao salvar a conclusão de um lembrete mantém o som
e o aviso. Os testes no host cobrem esses caminhos. Após testar a retirada
da espera no relógio, o usuário confirmou: "deu tudo certo aqui".

A agenda civil de alarmes/lembretes é consultada uma vez por segundo em
`Relogio_service`, ou na próxima passagem após criação, exclusão ou conclusão.
Uma mudança de segundo por correção do relógio também provoca nova consulta.
Timer e soneca usam prazos monotônicos e continuam verificados em cada poll,
inclusive quando vencem entre duas consultas civis. O cálculo do próximo
despertar permanece independente dessa redução de trabalho.

`apps/Relogio_pages.c` implementa timer e criação/lista/detalhe dos alarmes como
filhos do app. Um timer visual de 20 ms agrupa mudanças de página e texto do
arco. O cronômetro consulta décimos a cada 100 ms e só altera textos quando
o valor ou estado muda; pausado, não produz redraw periódico. A contagem usa
`esp_timer_get_time()` em `Relogio_service`, com acumulador de 64 bits,
incluindo light sleep, independentemente do tick LVGL. A regressiva muda somente quando
muda o segundo. `on_hide` pausa o timer visual; com backlight apagado seu
callback retorna sem alterar objetos. `on_show` reabre o cronômetro, enquanto
o estado dos serviços continua vivo.

`tests/run_Relogio_tests.ps1` compila o serviço real com GCC no host e relógio/NVS
simulados: prazos, extensões, recorrência, dias, adiamento, concorrência de
avisos, viradas de calendário e falha/persistência de gravação.
`tests/run_Relogio_ui.ps1` compila LVGL 8 e as telas reais para testar criação,
seleção múltipla, exclusão e avisos, produzindo imagens em `.pio/host-tests`.
Ambos são complementares ao build PlatformIO e ao teste físico do display.

Os controles visuais compartilham `ui/control_style.c` com o painel rápido e
o launcher. Em Relogio, `chronvs_Relogio_circle` cria opções de 70 px no padrão
2–3–2; `chronvs_Relogio_action` cria ações em pílula de 54 px de altura. O
construtor genérico fica reservado às linhas de lista com medidas próprias.
`tests/run_Relogio_ui.ps1 -System` renderiza também os fontes reais do mostrador,
painel rápido e launcher com hardware simulado, para revisão visual completa.

## Notas

O app `Notas` é registrado com `CHRONVS_REGISTER_APP`.
O editor reserva a parte superior para uma
nota multilinha e fixa o teclado na parte inferior, mantendo cursor, rolagem e
texto sempre visíveis. A lista reutiliza as linhas em pílula do launcher.

O teclado usa multi-tap em 6–6–5–3. Toques repetidos percorrem o grupo e uma
pausa confirma o caractere; tocar outra tecla confirma imediatamente:

```text
   [ab]   [cd]   [ef]   [gh 1] [ij 2] [kl 3]
   [mn 4] [op 5] [qr 6] [st 7] [uv 8] [wx 9]
      [yz 0] [Shift] [.,?!] [@#&] [apagar]
          [espaço] [nova linha] [#+=]
```

Os ciclos são `abáàãâ`, `cdç`, `eféê`, `gh1`, `ijí2`, `kl3`, `mn4`,
`opóôõ5`, `qr6`, `st7`, `uvú8`, `wx9` e `yz0`. Os números vêm depois das
letras e dos acentos e ficam nas dez teclas de gh a yz.
Maiúsculas existem somente com Shift, ativado por toque em tecla própria.
Shift fica amarelo e altera os rótulos das letras durante a composição; o
estado é liberado ao confirmar a próxima letra, após 800 ms ou ao trocar de
tecla. Símbolos e espaço não consomem um Shift ainda aguardando uma letra.
Tocar Shift novamente desativa o estado, confirmando uma composição pendente.

`#+=` alterna as 13 teclas superiores para os grupos `. ,`, `? !`, `: ;`,
aspas duplas/simples, `( )`, `[ ]`, `{ }`, `< >`, `+ - =`, asterisco/barra/
barra invertida, `_ @`, `# $ %` e `& | ~ ^` mais acento grave. `abc` retorna
às letras. A troca confirma a composição atual e preserva texto e cursor.

As duas linhas inferiores incluem `LV_SYMBOL_BACKSPACE` para apagar antes do
cursor (segurar repete), `LV_SYMBOL_NEW_LINE` para nova linha e uma tecla `_`
que insere espaço ASCII a cada toque. `LV_SYMBOL_KEYBOARD`, no topo, alterna
leitura com texto ampliado.
`LV_SYMBOL_TRASH` abre confirmação de exclusão com Cancelar/Excluir.
O salvamento é automático; não há uma etapa adicional para confirmar a nota.

Toque curto no texto reposiciona o cursor usando as métricas da fonte e o
offset de rolagem. Arraste vertical rola a nota; arraste para a direita volta
à lista de notas, inclusive sobre o texto e o teclado. Na lista, o gesto
retorna ao launcher e, na confirmação de exclusão, cancela. Arrastes curtos
não movem o cursor. Inserir e apagar mantém o cursor visível e acompanha a rolagem.
O cursor é uma barra amarela piscante. O caractere em composição fica
sublinhado. O texto é UTF-8, com limite de **512 caracteres por nota**, contando
acentos como um caractere lógico. A capacidade é de **12 notas**.

`core/Notas_text.c` mantém o buffer e a composição sem dependência de LVGL.
`services/Notas_service.c` persiste cada nota separadamente na NVS, namespace
`mnemo`, mantido para conservar as notas existentes, com chaves `note0_v1` a
`note11_v1`. Cada registro contém revisão e até
1024 bytes de texto, além do terminador. A revisão ordena as notas pela última
edição; a primeira linha fornece o título. Texto vazio remove a nota.
O cache das 12 notas é alocado na PSRAM somente na primeira abertura do app;
não reserva cerca de 12 KiB de RAM interna desde o boot. Se essa alocação
falhar, a lista informa falha de abertura e não permite criar notas.
Grava após 1,5 s sem editar e ao sair do editor/app, evitando uma gravação por
toque. Falhas preservam o rascunho, exibem estado de erro e impedem que o gesto
de retorno descarte a edição; uma nova tentativa automática ocorre após 10 s.
O serviço valida tamanho, terminação e UTF-8 ao carregar registros.

O app cria seus controles LVGL próprios e usa `lv_textarea` como área de texto,
com hit-test no label já posicionado pela rolagem. O sublinhado da composição
é desenhado localmente no label, sem habilitar seleção global de texto LVGL.
O timer visual pausa em `on_hide`;
com a tela apagada, apenas a composição e o salvamento podem avançar, sem
alterar objetos visuais. A fonte `ui/Notas_font.c` complementa Montserrat 18
com os 24 acentos portugueses do teclado; pode ser reproduzida pelo script
`scripts/generate_Notas_font.py` usando Pillow e a fonte da referência local.
A licença e os créditos estão em `docs/licenses/Notas-font-OFL.txt`.

Validação: `tests/run_Notas_tests.ps1` verifica grupos completos, Shift,
limites, edição UTF-8, relógio de multi-tap e falhas/reabertura da persistência.
Com `-UI`, simula toque/arraste no texto, teclado, lista e diálogo, confere as
20 teclas dentro do recorte circular, alternância de símbolos, repetição de
apagar e retorno sem clique acidental. Verifica também pausa com tela apagada
e repetição de entrada/saída sem vazamento de memória LVGL.
`tests/run_Relogio_ui.ps1 -System` inclui o registro do Notas e o orçamento de
memória com os outros apps criados, além do controle real de energia com
toques simulados: 50 s de digitação em AUTO, 20 s de contato contínuo em ECO,
redução/apagamento após inatividade e primeiro toque reservado ao despertar.
O usuário confirmou a validação física do Notas, incluindo a interação e
persistência. No Relogio, confirmou também a criação de alarmes por gestos
verticais, o retorno entre etapas e o ajuste dos arcos sem botões de navegação.
