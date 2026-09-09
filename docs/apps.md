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
#include "ui/app_input.h"

static void return_to_apps(void) { chronvs_app_open("apps"); }
static chronvs_ui_app_input_t input = {.back = return_to_apps};

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
Durante a rolagem vertical, a posição horizontal de cada linha acompanha
um arco calculado a partir da sua altura visível. A geometria e os gestos
estão em [`interface.md`](interface.md#lista-de-apps-em-arco).

## Navegação e apps instalados

`watch` e `apps` são apps internos de navegação e usam
`.launcher_visible = false`; portanto, não aparecem na própria lista. Os apps
destinados ao launcher usam `.launcher_visible = true` e entram na lista automaticamente.

Calculadora, Aion e Mnemo são apps nativos registrados individualmente com
`CHRONVS_REGISTER_APP`. A calculadora mantém sua avaliação aritmética em
`core/calculator.c` e sua interface em `apps/calculator_app.c`.

A calculadora é compilada junto ao firmware e funciona sem cartão. Não há
carregador, instalador ou interpretador de apps externos. A tela é criada na
primeira abertura e reutilizada; cada abertura inicia uma expressão vazia.
O layout e o ícone foram validados no relógio antes da migração para app nativo.
Após a migração, passaram os testes de aritmética e integração LVGL (toques,
energia, parênteses e exclusão), a compilação e a gravação com hashes verificados.
Os testes estão em `tests/run_calculator_tests.ps1` e
`tests/run_aion_ui.ps1 -System`. O firmware não acessa os antigos arquivos no SD.

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

### Contrato para novos apps

Todo novo app deve voltar com arraste da esquerda para a direita, sem botão
Voltar no topo: mais de 80 px horizontais e predominância de 20 px sobre o
deslocamento vertical absoluto. Na tela inicial, abra `apps`; em subetapas,
retorne à etapa anterior; em confirmações, cancele. Salve edições antes de
sair e preserve o rascunho quando houver falha. O contato é consumido até a
liberação, evitando cliques acidentais. Exceções de arraste próprio, como os
arcos do Aion, devem estar documentadas em `docs/interface.md`.

`ui/app_input.h` fornece `chronvs_ui_app_input_t` com callback `back` e
`chronvs_ui_app_input_bind(subtree, &state)`. Mantenha o estado durante toda a
vida do app e vincule cada nova subárvore uma única vez, depois de criar seus
controles e callbacks locais; diálogos criados depois precisam de vinculação
própria. O helper observa cada alvo sem duplicar eventos propagados, registra
atividade no início, durante e ao liberar o contato e reconhece o retorno
durante o arraste. O callback pode excluir a página atual. Mnemo é a referência
de uso. Contatos prolongados também devem manter a tela ativa; timers de
animação e salvamento não são atividade. A proteção global continua consumindo
o primeiro toque com a tela apagada.

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

## Mnemo — Notas Rápidas

O app `mnemo`, exibido como **Mnemo**, remete à memória e mantém a família
conceitual de `Aion`. É registrado automaticamente com `CHRONVS_REGISTER_APP`.
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

`core/mnemo_text.c` mantém o buffer e a composição sem dependência de LVGL.
`services/mnemo_service.c` persiste cada nota separadamente na NVS, namespace
`mnemo`, chaves `note0_v1` a `note11_v1`. Cada registro contém revisão e até
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
alterar objetos visuais. A fonte `ui/mnemo_font.c` complementa Montserrat 18
com os 24 acentos portugueses do teclado; pode ser reproduzida pelo script
`scripts/generate_mnemo_font.py` usando Pillow e a fonte da referência local.
A licença e os créditos estão em `docs/licenses/Mnemo-font-OFL.txt`.

Validação: `tests/run_mnemo_tests.ps1` verifica grupos completos, Shift,
limites, edição UTF-8, relógio de multi-tap e falhas/reabertura da persistência.
Com `-UI`, simula toque/arraste no texto, teclado, lista e diálogo, confere as
20 teclas dentro do recorte circular, alternância de símbolos, repetição de
apagar e retorno sem clique acidental. Verifica também pausa com tela apagada
e repetição de entrada/saída sem vazamento de memória LVGL.
`tests/run_aion_ui.ps1 -System` inclui o registro do Mnemo e o orçamento de
memória com os outros apps criados, além do controle real de energia com
toques simulados: 50 s de digitação em AUTO, 20 s de contato contínuo em ECO,
redução/apagamento após inatividade e primeiro toque reservado ao despertar.
Precisam de validação física a precisão
das teclas de 44 px, o tempo de multi-tap, o despertar e a persistência após
reiniciar o relógio.
