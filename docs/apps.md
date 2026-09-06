# Aplicativos do Chronvs

O runtime mantém uma camada de conteúdo abaixo da interface global e cria cada
aplicativo apenas na primeira abertura. Um app é uma definição estática com ID,
nome, desenho de ícone, visibilidade no launcher, função de criação e
callbacks opcionais de entrada e saída.

## Estrutura

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

static lv_obj_t *create_timer_app(lv_obj_t *parent) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Timer");
    lv_obj_center(title);
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
```

Registre a definição com `CHRONVS_REGISTER_APP` após sua declaração. O runtime
descobre automaticamente os descritores, sem um vetor central:

```c
const chronvs_app_t chronvs_timer_app = {
    .id = "timer", .name = "Timer", .create_icon = create_timer_icon,
    .launcher_visible = true,
    .create = create_timer_app, .on_show = NULL, .on_hide = NULL,
};
CHRONVS_REGISTER_APP(chronvs_timer_app)
```

O catálogo registra todos os apps durante o boot. O app `apps` fornece uma
lista curva, com ícone à esquerda e nome ao lado, e pode ser aberto com
`chronvs_app_open("apps")`.

## Navegação e apps instalados

`watch` e `apps` são apps internos de navegação e usam
`.launcher_visible = false`; portanto, não aparecem na própria lista. Os apps
instaláveis usam `.launcher_visible = true` e entram na lista automaticamente.

- No mostrador, arrastar para cima revela a lista de apps sobre o mostrador e
  acompanha o dedo desde a borda inferior durante o gesto.
- Na lista, tocar uma linha abre o app.
- Um arraste para baixo só inicia o retorno quando começa nos 60 px superiores
  da lista. Assim, puxar um conteúdo a partir do meio não fecha o launcher;
  são necessários 120 px para confirmar o retorno ao mostrador.
- `Aion` reúne cronômetro, timer e alarmes. Abre no cronômetro e navega para
  timer e alarmes com arrastes para cima; arrastes para baixo voltam de
  alarmes para timer e de timer para cronômetro. Na lista de alarmes, o
  retorno funciona também sobre as linhas quando o conteúdo está no topo.
  Volta à lista de apps com um
  arraste para a direita; durante a criação/detalhe de um alarme, esse gesto
  volta à etapa anterior. A interface está detalhada em `docs/interface.md`.

`create_icon` recebe um objeto LVGL de 44 × 44 pixels, sem estilo próprio. O
ícone deve desenhar todos os elementos dentro desse objeto; `Aion` usa
`LV_EVENT_DRAW_MAIN` para desenhar o corpo, coroa, aro e ponteiro do
cronômetro sem depender de uma imagem bitmap.

Abra o app a partir de um launcher ou atalho:

```c
chronvs_app_open("timer");
```

`create` é chamado uma única vez e o objeto retornado é preservado ao alternar
entre apps. `on_show` serve para atualizar dados ou retomar timers; `on_hide`
serve para pausar trabalho que não deve continuar fora da tela. O gerenciador
aceita atualmente oito apps e rejeita IDs repetidos.

## Agenda e avisos do Aion

`services/aion_service.c` mantém uma contagem regressiva e até 12 alarmes
semanais persistidos em `aion/alarms_v1` na NVS. É inicializado no boot, depois
da inicialização de NVS pelo sistema, independentemente da abertura do app.
Todas as APIs são chamadas pela tarefa principal. `main.c` fornece horários
válidos já lidos do RTC e correções NTP entregues por
`chronvs_time_sync_take_update`, e chama o serviço em cada iteração.

Prazos de timer e adiamento usam `esp_timer_get_time()`, sem depender de
ajustes NTP. A agenda semanal extrapola o último horário civil válido pelo
mesmo contador, incluindo mudanças de data e ano bissexto, sem consultar o
hardware com a tela apagada. Não há renderização dentro desse serviço.

`ui/aion_alert.c` consulta os avisos pendentes, acorda a tela e cria uma
sobreposição opaca em `lv_layer_top()`. Avisos simultâneos permanecem na
agenda até serem dispensados; a interface não precisa abrir o app Aion.
`services/sound_service.c` isola o I2S e gera bips numa tarefa FreeRTOS, com
pedido de reprodução atômico; essa tarefa nunca acessa LVGL. Se o speaker
falhar ao inicializar, a interface continua funcionando e o erro vai ao log.
I2S e a tarefa de som são criados sob demanda no primeiro pedido de bip,
sem inicialização de áudio no boot. Após a espera síncrona QSPI aplicada no
driver, o usuário confirmou boot, aviso e extensão `+1` sem artefatos físicos.
Durante o diagnóstico, `ui/aion_alert.c` espera 2 s após criar o aviso antes
de solicitar áudio. A espera usa ticks e não bloqueia LVGL; dispensar o aviso
cancela o pedido. O teste no host cobre também esse cancelamento.

`apps/aion_pages.c` implementa timer e criação/lista/detalhe dos alarmes como
filhos do app. Um timer visual de 20 ms agrupa mudanças de página e texto do
arco. O cronômetro atualiza décimos a cada 100 ms, a regressiva somente quando
muda o segundo. `on_hide` pausa o timer visual; com backlight apagado seu
callback retorna sem alterar objetos. `on_show` reabre o cronômetro, enquanto
o estado dos serviços continua vivo.

`tests/run_aion_tests.ps1` compila o serviço real com GCC no host e relógio/NVS
simulados: prazos, extensões, recorrência, dias, adiamento, concorrência de
avisos, viradas de calendário e falha/persistência de gravação.
`tests/run_aion_ui.ps1` compila LVGL 8 e as telas reais para testar criação,
seleção múltipla, exclusão e avisos, produzindo imagens em `.pio/host-tests`.
Ambos são complementares ao build PlatformIO e ao teste físico do display.

Os controles visuais compartilham `ui/control_style.c` com o painel rápido e
o launcher. Em Aion, `chronvs_aion_circle` cria opções de 70 px no padrão
2–3–2; `chronvs_aion_action` cria ações em pílula de 54 px de altura. O
construtor genérico fica reservado às linhas de lista com medidas próprias.
`tests/run_aion_ui.ps1 -System` renderiza também os fontes reais do mostrador,
painel rápido e launcher com hardware simulado, para revisão visual completa.
