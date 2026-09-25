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

## Revisão NTP: tarefa temporária e prazo de suspensão

A tarefa NTP foi convertida em uma sessão temporária: mantém os 6.144 bytes
de pilha necessários durante rede e gravação do RTC, mas chama `vTaskDelete`
após liberar o Wi-Fi. A tarefa idle do FreeRTOS recupera essa memória, em vez
de mantê-la reservada durante as 12 horas entre tentativas. Não foi movida
pilha para PSRAM nem criada uma tarefa adicional de agendamento.

O loop principal consulta um prazo monotônico e cria no máximo uma sessão.
Seu indicador de atividade impede light sleep desde a criação da tarefa,
inclusive antes de adquirir o Wi-Fi compartilhado com Clima. O prazo de NTP
também limita o tempo de suspensão. As 12 horas continuam contadas após o
fim da tentativa, inclusive em falha de sincronização; falha de alocação da
tarefa tem retry de 60 s, sem tentativas em cada passagem do loop. Sem
credenciais não há tarefa nem despertar adicional. Resultados pendentes
continuam disponíveis à UI após o fim da tarefa e falhas posteriores.

`tests/run_ntp_tests.ps1` cobre o serviço real com periféricos simulados:
inicialização idempotente, exclusão da tarefa, limite de despertar, tempo
avançando durante suspensão, falhas Wi-Fi/NTP/RTC/criação de tarefa, nova
tentativa, BCD escrito no RTC e resultado pendente. A exclusão Wi-Fi entre
trabalhadores permanece coberta pelo teste de sessão compartilhada.
Build padrão PlatformIO, testes NTP e testes de Clima/sessão Wi-Fi passaram.

Após testar esta revisão no relógio, o usuário confirmou: "tudo certo nos
testes". A etapa NTP passa a integrar a base validada pelo usuário. O relato
não discrimina os cenários executados nem confirma individualmente a espera
de 12 horas. A recuperação de memória é deduzida do ciclo de vida da tarefa;
não houve medição fornecida de RAM interna, maior bloco DMA, consumo ou FPS.

## Próxima revisão: hora compartilhada nos apps

Após o NTP validado e publicado em `3015f4c`, Clima e Calendario passaram
a usar `chronvs_Relogio_time()` para consultar idade dos dados e data atual.
Foram removidas as leituras extras de RTC desses apps na abertura, nos
resultados, em Hoje, após despertar e nas consultas periódicas. Calendario
também deixou de reancorar a hora a partir de uma leitura própria.

O loop principal continua lendo RTC uma vez por segundo com tela acesa e
após despertar, com a política existente de validação/fallback. O serviço
compartilhado incorpora as correções NTP e avança por tempo monotônico,
incluindo light sleep. Falhas posteriores do RTC não retiram uma referência
válida dos apps. Sem referência desde o boot, os estados de hora/data
indisponível permanecem. A cadência visual dos apps não mudou.

Build padrão e suítes de Calendario, Clima e interface integrada passaram.
Os testes verificam zero chamadas de RTC pelos apps, data após suspensão,
rejeição de data inválida preservando a referência anterior, idade dos dados,
gestos, inatividade e memória estável. O custo evitado são as operações I2C
extras, cujo timeout configurado é 100 ms; não se afirma um ganho fixo de
100 ms por abertura. A leitura periódica do main ainda pode bloquear e
alterá-la exige uma avaliação separada.

Após testar esta revisão no relógio, o usuário confirmou: "tudo certo nos
testes". A hora compartilhada no Clima e Calendario passa a integrar a base
validada pelo usuário, sem detalhamento individual dos cenários ou medição
de latência e consumo. Próximo candidato: medir e consolidar as invalidações
do mostrador em repouso, preservando a prévia dos gestos. Comparação com
CrowPanel e mudanças na cadência do touch continuam adiadas.

## Revisão de 25/09/2026: mostrador, launcher, RTC e diagnóstico

Após a base validada `fd1650b`, o mostrador passou a ter uma única fonte de
invalidação periódica. Seu timer lê a hora compartilhada e redesenha a cada
segundo quando visível; atualizar a referência não invalida novamente.
`on_show` atualiza imediatamente e as prévias de navegação continuam atendidas.
O launcher guarda deslocamento e opacidade de cada linha, reaplicando estilos
somente quando esses valores mudam, sem mudar sua curva ou gestos.

O serviço RTC concentra a política antes mantida no main: consulta no boot,
ao acordar e a cada 60 s após obter referência válida. Sem referência, tenta
novamente após 1 s. Com tela apagada não lê I2C. A validação e o fallback
monotônico permanecem; correções NTP continuam entregues ao serviço comum.
Não foi criada tarefa. A consulta ainda pode bloquear até seu timeout de
100 ms; a redução de frequência não elimina esse risco em uma leitura.

O padrão também deixou de executar a sondagem I2C do boot. Os diagnósticos
preservam o scanner e acrescentam duração das leituras de touch, intervalos
entre amostras e quadros durante contato e espera até o próximo refresh.
As definições e limitações estão em `performance.md`: esses dados não medem
latência física e ainda precisam ser capturados no relógio. Não foram alterados
cadência/debounce do touch, buffers, heap LVGL, QSPI ou período de refresh.

`tests/run_rtc_tests.ps1` passou cobrindo retry inicial, cadência de 60 s,
sono simulado de 24 horas, despertar, fallback e correção NTP.
`tests/run_Relogio_ui.ps1 -System` passou incluindo os wrappers de diagnóstico,
gestos, controles, editor, aviso, AUTO/ECO e primeiro toque ao acordar.
O mostrador produziu 10 refreshes em 10 s apesar de atualizações de hora
deslocadas do timer; eventos repetidos de scroll sem movimento não produziram
refresh no launcher. Esses números são do host, não do painel físico.
Os três builds PlatformIO passaram: padrão, `display_profile_o2` e
`display_profile`. O padrão ocupa 1.660.656 bytes de flash; esse tamanho
não representa uma medição de fluidez ou de RAM interna disponível.

Após testar esta revisão no relógio, o usuário confirmou: "tudo funcionando
perfeito, podemos seguir". A etapa passa a integrar a base validada pelo
usuário. O relato não discrimina cada cenário nem quantifica FPS, latência
ou consumo. O próximo passo é capturar os cenários instrumentados de
`performance.md` para avaliar gargalos antes de mudar parâmetros do touch.
Comparação com CrowPanel e medição de autonomia permanecem para outra etapa.

## Retirada do atraso de áudio

A base validada de mostrador/launcher/RTC foi publicada em `1fdd93b`.
Resta da análise original o atraso artificial de 2 s antes de solicitar som,
usado no isolamento das listras. A revisão seguinte remove somente essa espera
e seu estado pendente: o aviso solicita áudio ao terminar de criar seus objetos.
I2S continua sob demanda; não há promessa de latência física zero ou ganho de FPS.
Parar/adicionar tempo/concluir interrompe o som; falha ao salvar a conclusão
de um lembrete mantém o aviso e a reprodução. O driver QSPI não muda.

Após testar a retirada no relógio, o usuário confirmou: "deu tudo certo aqui".
A alteração passa a integrar a base validada pelo usuário. O relato não
discrimina cada cenário nem mede latência física do áudio. A base `1fdd93b`
preserva a versão com atraso. Medição de touch, latência e autonomia continua
pendente de dados físicos; a comparação com CrowPanel permanece adiada.

Build padrão e testes de UI do Relogio e da interface integrada passaram.
Os testes verificam o pedido de som no primeiro poll do aviso, interrupção
ao dispensar/adicionar tempo, ausência de reinício tardio e continuidade
quando a gravação da conclusão falha. Esses mocks não validam o painel/I2S.

Após a publicação de `5f3feed`, o usuário confirmou também: "tudo certo com
o touch aqui". O funcionamento do toque está validado pelo relato no relógio;
a cadência de 30 ms e o debounce permanecem iguais. Não há regressão de toque
relatada que motive alterá-los. A captura de métricas permanece disponível
para uma investigação futura; latência, FPS e autonomia não foram medidos.

## Continuação: análise das medições de gestos

Após `cbce002`, a preparação da etapa quantitativa ganhou
`scripts/analyze_display_profile.py`. O utilitário resume arquivos de captura
por cenário e otimização anunciada, com médias ponderadas pelo número de
quadros, máximos de tempo e mínimos de memória amostrada. Recusa janelas
incompletas e mantém explícita a ausência de métricas nos logs antigos.
Os cinco testes automatizados passaram com dados sintéticos; não representam
medições do relógio. O procedimento de captura está em `performance.md`.

Esta etapa não altera o firmware. A próxima decisão de otimização depende
dos logs de mostrador, painel e launcher no build de diagnóstico O2. Cadência
do touch e espera adaptativa do loop são candidatos à avaliação, não mudanças
já adotadas; FPS, latência física e autonomia continuam sem medição fornecida.

## Primeira captura física de desempenho

Arquivo local `logs/device-monitor-260925-110205.log`, analisado com o
utilitário acima: 24 janelas completas, totalizando 48.528 ms observados e
278 atualizações. O banner de otimização não está na captura; o ambiente
gravado foi confirmado pelo anexo do usuário: upload de `display_profile_o2`
concluído com sucesso, hashes verificados e monitor salvando esse mesmo arquivo.
O texto "Building in debug mode" não contradiz o O2 seletivo aplicado ao LVGL.
O arquivo contém vários cenários,
portanto a média global não caracteriza a fluidez de um gesto específico.

| Trecho (timestamp do log) | Janelas / quadros | Refresh médio aproximado | Flush médio aproximado | Refresh máximo |
| --- | --- | --- | --- | --- |
| Inicial, 35.959–45.969 ms | 6 / 13 | 273,77 ms | 17,08 ms | 274 ms |
| Launcher, 52.089–58.119 ms | 4 / 69 | 54,65 ms | 14,29 ms | 74 ms |

O primeiro trecho é compatível com mostrador em repouso: cerca de uma
atualização por segundo, tela inteira de 169.744 pixels por quadro e só
um quadro durante contato. Não há marcador explícito do cenário. O segundo
fica entre os eventos de abertura de `apps` em 49.959 ms e de `watch` em
59.279 ms; isso confirma o app ativo, mas não a continuidade dos arrastes.

Em toda a captura, a leitura do touch levou no máximo 2.009 us. O maior
intervalo entre amostras pressionadas chegou a 280.334 us. Isso distingue
custo do leitor de atraso para voltar a executá-lo na tarefa da interface.
O menor heap interno amostrado foi 143.639 bytes e o menor maior bloco DMA,
40.960 bytes; são amostras, não limites seguros nem ausência comprovada de
picos de alocação entre elas.

Os dados apontam para trabalho de renderização fora do flush como principal
custo do trecho inicial (diferença aproximada de 257 ms). Isso inclui desenho,
layout e preempções, não apenas o callback do mostrador. A hipótese prioritária
é investigar o desenho vetorial e o trabalho repetido por faixa, antes de
alterar polling do touch ou QSPI. `input_refresh_max_us` atingiu 931.003 us,
mas não mede latência física nem garante relação causal com o próximo refresh.
Nenhum parâmetro do firmware foi alterado a partir desta captura.

## Experimento seguinte: geometria fixa do mostrador

Confirmado o diagnóstico O2 pelo upload anexado, a primeira alteração isolada
guarda as coordenadas de minutos/datas, antes calculadas para cada faixa
parcial. O cache estático ocupa 616 bytes e é refeito quando o centro muda;
trocar o dia recalcula somente o anel de datas. Os números são constantes,
eliminando também 42 chamadas de `snprintf` por passagem por essas escalas.
Nenhum pixel é armazenado. Hora interpolada, ordem de desenho, clipping,
touch, QSPI, cadência e tamanhos dos buffers permanecem iguais.

A suíte integrada passou e as 16 capturas nomeadas por ela ficaram idênticas
em comparação SHA-256 com a base sem cache. Isso verifica os cenários do host,
não o comportamento físico. A RAM estática reportada no build padrão passou
de 54.284 para 54.900 bytes, correspondendo aos 616 bytes do cache.
O usuário repetiu o upload `display_profile_o2` e enviou a captura
`logs/device-monitor-260925-111638.log`, com sucesso confirmado no anexo.
Nas duas primeiras janelas (25.960 e 27.960 ms), os quatro quadros de tela
inteira tiveram média de 237 ms, máximo de 238 ms e flush médio de 16,99 ms.
São cerca de 37 ms ou 13,5% menos que os 274 ms do trecho inicial anterior.
Não houve contato nessas duas janelas; o trecho final sem contato ficou em
236 ms. O horário/desenho e os gestos não foram rigidamente controlados:
isso é evidência de redução observada, não benchmark isolado ou ganho de FPS
do launcher. O transporte permaneceu perto de 17 ms.

Build padrão e `display_profile_o2` passaram. O teste
`tests/run_watch_geometry_tests.ps1` cobre reutilização sem novas chamadas
trigonométricas, alinhamento da data atual nos 31 dias, deslocamentos horizontal
e vertical e reconstrução somente do anel de datas ao trocar o dia. Coordenadas
em meio pixel são verificadas com tolerância de arredondamento de 0,5 px.
O cache foi confirmado em 616 bytes também no teste do host. A execução física
e a comparação quantitativa foram realizadas; o envio de logs não constitui
uma confirmação explícita da ausência de todos os possíveis artefatos visuais.

## Descarte de primitivas fora da faixa

O cache e as duas capturas físicas foram consolidados em `b9d6f61`.
A próxima mudança antecipa o descarte de textos e linhas fora do clip atual,
evitando preparar descritores e chamar o desenho LVGL nesses casos. O texto
usa o mesmo retângulo que a biblioteca já testa. Linhas usam margem conservadora
de largura + 2 px, preservando pontas arredondadas, antialiasing e segmentos
que cruzam a faixa mesmo com ambas as extremidades fora dela.

Não há nova reserva estática nem alteração de geometria, cadência, touch,
QSPI ou buffers. A suíte integrada passou, mantendo idênticas por SHA-256 as
16 imagens produzidas por ela em relação à versão com cache. A captura física
seguinte está analisada abaixo; os logs não substituem avaliação visual de
números e linhas durante abertura/fechamento de painel e launcher.

O primeiro build encontrou metadados CMake sem o alvo `__idf_src`. A cópia
local do cache foi preservada em `.pio/audit` e regenerada, sem limpar `.pio`
nem mudar configuração versionada. O build padrão então passou.
O diagnóstico `display_profile_o2` também passou, assim como o teste de
geometria ampliado para descarte fora da faixa e preservação de bordas e
segmentos que a atravessam. RAM estática permaneceu em 54.900 bytes no padrão.

O anexo seguinte contém 39 janelas completas, 78.332 ms observados e 212
atualizações, correspondentes ao monitor `device-monitor-260925-182240.log`.
A análise usou o texto anexado, evitando depender do arquivo local que pode
continuar sendo escrito. O comando do monitor seleciona `display_profile_o2`,
mas este anexo não contém upload nem banner de compilação; só selecionar o
ambiente do monitor não comprova qual firmware está gravado.

Para comparar repouso, foram selecionadas janelas com dois quadros de tela
inteira (169.744 pixels médios), sem quadros durante contato e sem espera de
input pendente registrada. Esse filtro não identifica todos os possíveis
estados da UI, mas evita misturar os arrastes conhecidos:

| Captura | Janelas / quadros filtrados | Refresh médio aproximado | Flush médio aproximado |
| --- | --- | --- | --- |
| Anterior, 11:16:38 | 3 / 6 | 236,67 ms | 17,00 ms |
| Nova, 18:22:40 | 26 / 52 | 234,12 ms | 17,13 ms |

A diferença observada é 2,55 ms (1,1%). Na nova captura, as médias de repouso
variam de 230 a 236 ms; o horário e a geometria do mostrador também mudaram.
Não há evidência suficiente para atribuir um ganho causal a este descarte.
A cadência de repouso segue próxima de 1 Hz; 234 ms é duração de renderização,
não o intervalo pretendido entre atualizações nem FPS máximo do painel.

O maior bloco DMA amostrado foi 31.744 bytes, ante 40.960 na captura anterior;
o heap interno mínimo ficou em 143.015 bytes. Não se deduz vazamento dessa
diferença entre sessões, nem se trata esses números como margem segura.
O leitor do touch levou no máximo 2.023 us em toda a captura. O principal
custo continua fora do flush. Próximo passo: instrumentar separadamente os
grupos de desenho vetorial antes de escolher outra otimização; não alterar
touch, QSPI ou buffers a partir dessa diferença pequena.

## Instrumentação por grupo do mostrador

Após `65611d4`, os diagnósticos passam a acumular tempos de setup, fundo,
geometria, aros/data, disco central, escala de minutos, quatro submostradores
e marcador. Totais de todas as faixas ficam na mesma janela do refresh.
`watch_frames` permite calcular médias sem diluí-las com quadros de outros
apps; `watch_slices` identifica quantas vezes o callback foi executado.
O analisador aceita tanto logs novos completos quanto antigos sem esses campos.

A instrumentação não muda o desenho ou seus parâmetros. É removida por macros
no padrão, cujo build preservou RAM estática de 54.900 bytes e flash de
1.661.040 bytes. Nos diagnósticos, os tempos incluem preempções e custo dos
marcadores; não devem ser tratados como uma medição sem interferência.
Definições e procedimento da próxima captura estão em `performance.md`.

Builds padrão e O2 passaram. Os seis testes do analisador cobrem médias
ponderadas, campos ausentes/incompletos e janelas sem mostrador. A suíte
integrada executou o mostrador com instrumentação, verificou acumulação de
duas faixas em um refresh e descarte dos contadores com tela apagada, e passou.
As 16 imagens permaneceram idênticas por SHA-256. O diagnóstico O2 usa
58.160 bytes de RAM estática, 96 bytes a mais que antes. Falta a captura física
com os novos campos para localizar o grupo dominante; não há ganho de
renderização alegado nesta etapa de instrumentação.
