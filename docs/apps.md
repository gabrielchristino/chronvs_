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
- O primeiro app incluído é `Aion`, o cronômetro. Ele atualiza décimos de
  segundo apenas enquanto está visível e volta à lista com um arraste para a
  direita.

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
