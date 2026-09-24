# Análise de desempenho e estabilidade — 24/09/2026

Base analisada: `934a138`, com LVGL em `-O2` e logs/consoles desativados.
O usuário confirmou excelente fluidez nessa base. A análise original identifica trabalho desnecessário,
casos ainda descobertos pelos testes e oportunidades de manutenção.

A revisão usou fontes da aplicação, drivers locais Waveshare, LVGL 8.3.11,
ESP-IDF 5.3.1 e o `sdkconfig.h` efetivamente gerado. Não houve upload nem
medição física de CPU, FPS, consumo ou memória nesta análise.

## 1. Prioridade alta: a espera do loop principal resulta em zero ticks

Em `src/main.c:111`, o loop chama `vTaskDelay(pdMS_TO_TICKS(5))`.
O build atual usa `CONFIG_FREERTOS_HZ=100`. A macro real do FreeRTOS calcula
`5 * 100 / 1000`, com divisão inteira: **zero ticks**.
O kernel trata `vTaskDelay(0)` como uma oportunidade de reagendamento, sem
bloquear a tarefa por um intervalo. Portanto não existe a espera pretendida.

O loop volta a consultar NTP, alarmes, lembretes e LVGL continuamente quando
não há outro bloqueio. Isso não significa redraw contínuo: o LVGL ainda
respeita seus timers. É desperdício potencial de CPU e dificulta o trabalho
das tarefas de prioridade inferior, inclusive idle; o uso real precisa ser medido.

Melhoria: garantir ao menos um tick de bloqueio e, depois, avaliar um prazo
adaptativo baseado em `lv_timer_handler()` e nos serviços. Um tick corresponde
nominalmente a 10 ms nesta configuração, não a uma espera exata de 10 ms.
Não aumentar a frequência global do FreeRTOS como primeira solução.
Validar latência de toque, alarmes e fluidez com esse novo escalonamento.

## 2. Prioridade alta: o cronômetro deixa de contar o tempo em light sleep

`src/apps/Relogio_app.c:106` calcula o tempo decorrido com `lv_tick_elaps()`.
`src/platform/board.c:136` suspende o timer LVGL antes do sono e o retoma
depois sem acrescentar o tempo dormido. O cronômetro, portanto, não usa uma
base de tempo que continue avançando durante o sono.

Uma prova no host compilou o app e o LVGL reais, com hardware simulado:

```text
After 5s awake + 60s simulated sleep: expected=65000 ms actual=5000 ms
```

A simulação reproduz explicitamente a suspensão do tick feita pela placa;
não é um teste físico de sono. Timer regressivo e alarmes usam outro serviço
com `esp_timer_get_time()` e não compartilham esse defeito do cronômetro.

Melhoria: calcular o cronômetro por tempo monotônico de 64 bits, conservando
o tick LVGL apenas para cadência visual. Testar iniciar, pausar, continuar,
sair do app, apagar/acordar e zerar, mantendo a tela sem renderização no sono.

## 3. Prioridade alta: ERROR do Vox permite reiniciar antes do cleanup

Em `src/services/voice_lab_service.c:265`, `fail()` publica ERROR antes de
liberar microfone, modelo e demais recursos. `chronvs_voice_lab_start()`
aceita ERROR como estado inicializável (`:390`). Uma nova tentativa pode,
assim, começar enquanto a tarefa antiga ainda está em cleanup (`:373`).

A tarefa antiga também escreve os estados e `stop_requested` globais no fim.
Isso abre uma condição de corrida com a sessão nova e com a lista global de
comandos do ESP-SR. É um risco demonstrado pela ordem das operações no código,
não um travamento reproduzido no dispositivo nesta análise.

Melhoria: separar estado exibido de propriedade dos recursos/tarefa ativa;
só permitir reinício após conclusão da limpeza. Testar falha de alocação,
erro de I2S e repetição rápida de iniciar/parar/reabrir.

## 4. Prioridade alta para reprodução: defaults diferem do firmware validado

`sdkconfig.defaults:7` pede `CONFIG_SPIRAM_USE_CAPS_ALLOC=y`, mas o
`sdkconfig` local validado usa `CONFIG_SPIRAM_USE_MALLOC=y`, com limiar de
16.384 bytes e reserva interna de 32.768 bytes. As pilhas locais de main
(8.192 bytes) e eventos (4.096 bytes) e os buffers Wi-Fi estáticos também
não estão todos representados nos defaults versionados.

O arquivo local é ignorado pelo Git. Uma configuração criada do zero pode
ter comportamento e orçamento de memória diferentes, mesmo no mesmo commit.
Os diagnósticos copiam o padrão local, mas não resolvem a reprodução em uma
máquina nova. Os números acima descrevem configurações, não limites seguros
de RAM livre ou DMA.

Melhoria: versionar nos defaults as escolhas realmente validadas, conferir
um build isolado a partir do zero e comparar as opções geradas antes de testar
no relógio. Não substituir a configuração local nem limpar `.pio` para isso.

## 5. Cronômetro parado redesenha dez vezes por segundo

`src/apps/Relogio_app.c:126` chama `update_display()` a cada 100 ms na página
do cronômetro, mesmo quando não está contando. A função reatribui três labels.
Nesta versão do LVGL, `lv_label_set_text()` e `lv_label_set_text_fmt()`
invalidam o objeto e manipulam a memória do texto; não comparam o conteúdo
novo para descartar uma atualização idêntica.

A prova no host, com LVGL e app reais e monitor de refresh, produziu:

```text
Paused stopwatch, no touch: 10 refreshes in 1 simulated second
```

Melhoria: atualizar tempo apenas quando o décimo exibido mudar; atualizar
status e botão apenas na transição de estado. Um cronômetro parado deve
ficar visualmente ocioso. Validar também os estados inicial, pausado e zerado.

## 6. Vox: timeout I2S e política de tela apagada

Em `src/services/voice_lab_service.c:343`, `i2s_channel_read()` recebe
`pdMS_TO_TICKS(100)`. O cabeçalho e a implementação do ESP-IDF instalado
definem o argumento como **milissegundos** e fazem a conversão internamente.
Com o tick atual, são passados **10 ms**, em vez dos 100 ms aparentes.
Além disso, o caminho de timeout ignora `bytes_read`; o driver pode retornar
timeout depois de copiar uma parte do pedido. A incidência de descarte de
áudio depende do tamanho do chunk e precisa ser medida.

O loop principal só consulta a atividade Wi-Fi antes de entrar em light sleep.
Não há uma condição equivalente para captura/modelo Vox. O `poll()` visual
do Vox (`src/apps/voice_lab_app.c:70`) verifica se o app está visível, mas
não se o backlight está apagado. Quando há execução nesse estado, pode
consumir resultados e alterar labels em uma tela apagada.

Melhoria: corrigir a unidade do timeout, definir tratamento de leituras
parciais e escolher explicitamente entre parar a escuta ao apagar ou manter
o processador acordado sem renderizar. Não simular toques para impedir
inatividade. Testar escuta com AUTO/ECO e durante sessão Wi-Fi.

## 7. Redesenho do mostrador tem duas origens periódicas

`main.c` fornece a hora aproximadamente uma vez por segundo e
`chronvs_watch_app_set_time()` invalida o mostrador quando ela muda.
`watch_app.c:424` também mantém um timer independente de 1 segundo que
invalida o mesmo objeto. Dependendo da fase dos timers, as invalidações
podem se fundir ou resultar em atualizações distintas.

O mostrador não pausa esse timer em `on_hide`. Objetos ocultos são tratados
pelo LVGL, portanto isso não prova transmissão de pixels oculta; é importante
distinguir execução de callback de redraw efetivo.

Melhoria: escolher um único responsável pela cadência, preservar a hora de
fallback quando o RTC falha e pausar trabalho visual oculto, reativando-o
também nas prévias de gesto. Medir a contagem de refresh em repouso primeiro.

## 8. Trabalho periódico e memória mantidos em repouso

- Depois do primeiro som, `sound_service.c:33` consulta flags a cada 20 ms
  enquanto estiver ocioso. A tarefa de 3.072 bytes e o canal I2S continuam
  alocados; o canal é desabilitado, mas não destruído. Notificações de tarefa
  poderiam eliminar esse polling. Não se deve concluir que ele acorda o
  chip a cada 20 ms durante light sleep explícito: o achado é sobre CPU acordada.
- `chronvs_Relogio_poll()` reavalia calendários de lembretes e alarmes em
  cada passagem pelo loop, inclusive dentro do mesmo segundo. Após corrigir
  a espera do loop, medir se compensa armazenar o vencimento dos lembretes
  e separar a agenda civil dos prazos monotônicos de timer/soneca.
- A tarefa NTP mantém 6.144 bytes de pilha enquanto espera 12 horas entre
  sincronizações. Uma tarefa temporária poderia liberar essa reserva; isso
  exige integrar o prazo de sincronização ao sono, sem perder a exclusão Wi-Fi.
- O scanner I2C em `board.c:27` ainda faz sondagens no boot, embora os logs
  tenham sido eliminados. Torná-lo exclusivo do diagnóstico pouparia esse
  trabalho de inicialização, sem promessa de ganho na navegação normal.

## 9. Candidatos secundários que exigem medição

O touch herda período de leitura de 30 ms do LVGL, enquanto redraw e gestos
são agrupados em 20 ms. São cadências distintas; isso não é um defeito por si
só. Testar leitura em 20 ms pode melhorar acompanhamento do dedo, mas aumenta
consultas I2C e muda o tempo efetivo do debounce de duas amostras. Preservar
o debounce e fazer comparação física isolada, incluindo o primeiro toque.

O launcher reatribui translação/opacidade de todas as linhas quando a curva
fica suja. O setter de estilo desta versão do LVGL solicita refresh mesmo
para o mesmo valor. Guardar os últimos valores pode reduzir trabalho,
sobretudo para linhas fora do arco; medir antes de introduzir caches maiores.

Leituras RTC síncronas na tarefa da interface admitem timeout de 100 ms.
Além da leitura global, Clima e Calendario fazem consultas próprias.
Um relógio compartilhado com âncora monotônica pode reduzir I2C e limitar
pausas em falhas, mas deve manter correções NTP, virada de dia e fallback.

O atraso de 2 segundos antes do som ainda existe em `ui/Relogio_alert.c:11`
como medida histórica de isolamento. Não bloqueia a UI. Só vale removê-lo
em um teste físico isolado que repita os fluxos de listras já documentados.

## Ordem sugerida

1. Espera real do loop e repetição dos gestos/avisos no relógio.
2. Base de tempo do cronômetro e eliminação dos seus redraws em repouso.
3. Serialização do Vox, unidade do timeout e política de sono.
4. Defaults reproduzíveis e comparação de memória em build isolado.
5. Só então medir polling de áudio, agenda, RTC, mostrador e launcher.

Manter refresh de 20 ms, buffers duplos de 1/20, pool TLSF em PSRAM de
128 KiB, QSPI de 2 KiB e conclusão síncrona. Não há evidência nesta revisão
que justifique reabrir esses parâmetros já validados.

As provas originais ficaram em `.pio/audit`. A reprodução do cronômetro e
dos redraws usa simulação de tempo e não fornece FPS físico.

## Correções aplicadas após a análise

- O loop bloqueia por pelo menos um tick (10 ms na configuração atual).
- O cronômetro usa tempo monotônico de 64 bits, inclui light sleep e evita
  reatribuir textos inalterados. Testes cobrem pausa, retomada, tela apagada,
  retorno ao app e duração superior ao limite de milissegundos de 32 bits.
- Vox mantém a exclusão até concluir a limpeza, libera o vocabulário,
  aceita cancelamento no carregamento e acumula leituras parciais I2S com
  timeout de 100 ms. AUTO/ECO encerra a captura antes de light sleep;
  a UI não atualiza seus objetos com a tela apagada.
- Defaults explicitam a política de PSRAM, pilhas, buffers TX Wi-Fi e nomes
  FATFS usados na configuração local validada. Uma geração isolada com
  `kconfgen` do ESP-IDF 5.3.1, sem carregar o sdkconfig local, reproduziu
  todas as entradas ativas dessa configuração.

Validação automatizada: build padrão PlatformIO, testes de serviço e UI
do Relogio e testes de ciclo de vida/captura do Vox. Os parâmetros críticos
do painel permanecem preservados. Não houve upload desta revisão.

A suíte ampliada `run_Relogio_ui.ps1 -System` encontrou falha de alocação
ao vincular eventos no editor de lembretes, após reter todos os apps, no
host Windows de 64 bits com pool de 128 KiB. O mesmo ponto falha ao substituir
o app Relogio pela implementação de `934a138`. Isso não prova falta de memória
no ESP32 de 32 bits nem permite considerar a suíte ampliada aprovada.
O runner agora aborta em assert LVGL, com símbolos de depuração, em vez de
ficar preso no loop de assert embarcado. Investigar o consumo dessa sequência
separadamente; não aumentar o pool validado para mascarar o resultado.

Em 24/09/2026, após testar esta revisão no relógio, o usuário confirmou:
"tudo certo nos meus testes". A revisão passa a ser a base validada no
dispositivo. O relato é qualitativo, sem medições de FPS, consumo ou uma
lista individual dos cenários executados; a pendência da suíte no host
permanece separada dessa confirmação.
Os demais candidatos desta análise continuam dependentes de medição.

## Próxima revisão: memória de controles e eventos

Após o commit validado `191db9b`, a investigação da suíte integrada encontrou
duas reservas evitáveis: callbacks de contato em rótulos não clicáveis e
cópias dos mesmos estilos em cada botão. O helper de input agora registra
somente alvos clicáveis, ainda percorrendo todos os descendentes. Os estilos
dos controles são compartilhados, mantendo estados, valores e overrides locais.

Com apenas a redução de callbacks, o editor abriu, mas restaram 4.976 bytes
livres e maior bloco de 3.472 bytes. Compartilhando estilos, restaram 31.072
bytes livres, maior bloco de 29.560; com o aviso sobre o editor, 28.912 bytes
livres em um único bloco. São medidas do teste Windows de 64 bits, sem
equivalência direta com a economia no ESP32. O pool permanece em 128 KiB.

Também foi corrigida uma suposição do teste: reabrir Calendario não move sua
raiz para o final dos filhos; a verificação agora usa a raiz visível.
`run_Relogio_ui.ps1 -System` passou integralmente, incluindo editor e aviso,
gestos, digitação, contato prolongado, AUTO/ECO e primeiro toque ao acordar.
O build padrão PlatformIO e a suíte específica de UI do Relogio passaram;
nessa suíte, o heap ocupado caiu de 20.504 para 17.848 bytes. Após testar
essa revisão no relógio, o usuário confirmou "tudo perfeito nos meus testes
aqui". A redução de memória passa a integrar a base validada fisicamente;
o relato não quantifica memória, FPS ou autonomia no dispositivo.

## Próxima revisão: áudio ocioso

Após a base validada `c9363e3`, a espera periódica de 20 ms do áudio foi
substituída por notificação de tarefa. Com canal desabilitado e sem som,
a tarefa bloqueia até receber mudança de estado ou pedido de prévia. As
notificações ficam pendentes se chegarem antes da espera; flags atômicas
preservam o comando mais recente e pedidos repetidos de prévia continuam
reiniciando o bip. Repetir o estado atual do alerta não gera notificação.

Isso elimina o polling ocioso, que antes podia ocorrer 50 vezes por segundo
enquanto a CPU estava acordada. Não mede economia de bateria nem reduz a
pilha de 3.072 bytes ou a reserva do canal I2S. Forma de onda, ganhos,
duração dos bips e espera histórica de 2 s do aviso permanecem iguais.

Os testes do serviço real com I2S/FreeRTOS simulados cobrem prévia finita,
reinício, mute durante reprodução, comando na fronteira da espera,
retomada de alerta e ausência de delay periódico no fluxo normal.
O build padrão PlatformIO passou com essa alteração.
O usuário testou as últimas alterações de áudio e confirmou que estava
tudo certo. Essa etapa passa a integrar a base validada no relógio, sem
medição quantitativa de consumo e sem detalhamento individual dos cenários.
Os próximos candidatos continuam sendo agenda, NTP, RTC, mostrador e launcher;
mudanças que alterem sua cadência exigem medição e um teste isolado.

## Próxima revisão: consultas da agenda civil

Após o áudio validado em `c418efb`, `Relogio_service` passou a consultar
alarmes e lembretes uma vez por segundo civil, invalidando a consulta após
alterações persistidas. Timer e soneca são verificados antes desse filtro;
o cálculo de despertar e a lógica de recorrência permanecem iguais.

O teste `agenda_poll_test.c` executa o serviço real e conta conversões de
data: com 12 lembretes e 100 polls dentro de um segundo, foram 12 conversões
em vez das 1.200 que o loop anterior exigia. O teste verifica alterações no
mesmo segundo, correção do horário e timer/soneca vencendo no meio de um segundo.
A suíte de serviço cobre também soneca, recorrência, viradas de calendário,
persistência, falhas de gravação e lembretes vencidos após reinício.
Essa redução não é uma medição de FPS ou autonomia.
O build padrão PlatformIO e os testes de serviço passaram.

Após testar esta etapa no relógio, o usuário confirmou: "tudo certo com os
testes aqui". A otimização da agenda passa a integrar a base validada no
dispositivo. O relato não detalha cada cenário nem quantifica desempenho
ou autonomia. A comparação com a CrowPanel e a medição dos gestos ficam
para depois, conforme combinado com o usuário.

## Próximo passo após consolidar a agenda

Revisar a tarefa NTP e sua reserva de pilha durante as 12 horas entre
sincronizações. Primeiro levantar o ciclo de vida e os caminhos de falha;
avaliar uma tarefa temporária sem perder o prazo, o resultado pendente para
a UI ou a exclusão Wi-Fi compartilhada com Clima. Validar reconexão, falha
de rede, suspensão e repetição de sessões antes de adotar a mudança.
RTC e trabalho visual de mostrador/launcher permanecem na fila posterior.
