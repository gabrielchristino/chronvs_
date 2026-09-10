# App Clima

Especificação implementada do app `weather`. A primeira versão usa São Paulo
como localização fixa e a API pública Open-Meteo, sem chave. Build e testes no
host passaram. Após mover o heap de objetos LVGL para PSRAM, o usuário confirmou
consulta real com dados e sem listras; os detalhes e limites estão abaixo.

## Objetivo

Ao abrir o app, fazer uma única atualização sob demanda. O Wi-Fi deve ser ligado somente para essa operação e desligado ao terminar. Não haverá atualização automática enquanto o app estiver aberto nem em segundo plano.

Se a atualização falhar, os últimos dados válidos permanecem visíveis, acompanhados da idade da atualização. Se nunca houver uma atualização válida, mostrar `Sem dados` e a causa resumida.

## Fluxo

```text
abrir -> mostrar cache + "Atualizando..." -> ligar Wi-Fi -> conectar
      -> consultar Open-Meteo -> validar -> persistir -> atualizar UI
      -> desligar Wi-Fi em qualquer caminho de saída
```

A rede deve rodar em tarefa própria e publicar o resultado para o app; essa tarefa nunca toca em LVGL. Ao fechar o app, o resultado tardio não pode substituir a tela de outro app.

NTP e clima usam `wifi_session_service`, com mutex de sessão exclusiva.
A espera por outro usuário ocorre na tarefa de rede, antes do prazo de conexão.
Somente o dono conecta e desliga o rádio; handlers apenas publicam eventos.
A liberação aguarda `STA_STOP` antes de admitir outra sessão, evitando eventos
da conexão anterior. O app não chama APIs globais de Wi-Fi diretamente.

## Localização e endpoint

- latitude: `-23.5505`
- longitude: `-46.6333`
- timezone: `America/Sao_Paulo`

```text
https://api.open-meteo.com/v1/forecast?latitude=-23.5505&longitude=-46.6333&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=America%2FSao_Paulo&forecast_days=1
```

Validar HTTP 200, presença/tipo dos campos, temperatura e umidade em faixas razoáveis e limite de tamanho da resposta. Mapear `weather_code` para rótulos curtos em português e ícones vetoriais, sem bitmaps.

## Serviço

`src/services/weather_service.{h,c}` expõe:

```c
typedef enum { CHRONVS_WEATHER_IDLE, CHRONVS_WEATHER_FETCHING,
               CHRONVS_WEATHER_SUCCESS, CHRONVS_WEATHER_ERROR }
               chronvs_weather_state_t;
typedef struct {
    float temperature_c, apparent_temperature_c, minimum_c, maximum_c;
    uint8_t humidity_percent;
    int16_t weather_code;
    int64_t updated_epoch;
    bool valid;
} chronvs_weather_snapshot_t;
bool chronvs_weather_init(void);
bool chronvs_weather_get_snapshot(chronvs_weather_snapshot_t *snapshot);
chronvs_weather_state_t chronvs_weather_state(void);
const char *chronvs_weather_error(void);
bool chronvs_weather_request_update(void);
bool chronvs_weather_take_result(chronvs_weather_snapshot_t *snapshot,
                                 char *error, size_t error_size);
```

O namespace NVS é `weather`, com chave versionada `snapshot_v1` e registro de
campos de largura fixa. A gravação só ocorre após validar a resposta inteira;
falhas de gravação preservam o cache em RAM e não removem a chave anterior.
`updated_epoch` representa `current.time`: segundos civis locais desde
1970-01-01, sem conversão de fuso implícita. Não é um timestamp UTC. O cálculo
da idade usa o mesmo referencial do PCF85063, inclusive após reinício sem NTP.
Só existe uma requisição; estado e resultado pendente são protegidos por mutex.

## Interface

`src/apps/weather_app.c` é registrado com `CHRONVS_REGISTER_APP`:

- ID `weather`, nome `Clima`, visível no launcher;
- retorno por arraste para a direita usando `ui/app_input.h`;
- título `CLIMA`, temperatura em Montserrat 48;
- condição, sensação, umidade e mínima/máxima em textos compactos;
- rodapé `Atualizado agora`, `Atualizado há 3 h` ou `Sem dados`;
- durante a consulta, conservar o cache e mostrar `Atualizando...`;
- em erro com cache, mostrar `Falha ao atualizar` sem esconder os dados;
- sem cache, mostrar `Sem dados` e `Sem Wi-Fi`, `Tempo esgotado` ou `Resposta inválida`.

Criar o conteúdo uma vez. `on_show` carrega o cache e inicia a consulta; um timer aplica resultados no contexto LVGL. `on_hide` pausa o timer. Com a tela apagada, não ler RTC nem redesenhar; a idade pode ser atualizada somente quando a tela estiver ativa.

SSID/senha vêm de `chronvs_secrets.h`, com armazenamento Wi-Fi somente em RAM.
HTTPS verifica o servidor pelo bundle de CAs do ESP-IDF (`esp_crt_bundle_attach`),
sem redirecionamentos. Limites: conexão 20 s com até três tentativas; orçamento
HTTP de 15 s compartilhado por até três tentativas de transporte/HTTP 5xx.
HTTP 4xx, JSON inválido e resposta excessiva encerram a consulta. O buffer de
resposta tem 16 KiB e é alocado na PSRAM só durante a consulta; o parser rejeita
aninhamento superior a oito níveis para limitar o uso da pilha da tarefa.

A validação exige campos numéricos finitos, temperaturas de −90 a 70 °C,
umidade inteira de 0 a 100%, código WMO conhecido, mínima ≤ máxima, unidades
Celsius, timezone/offset de São Paulo e um único dia correspondente à medição.
Cache inválido na NVS é ignorado. Erros adicionais locais são `Falha ao salvar`
e `Memória insuficiente`; com cache, a UI continua mostrando `Falha ao atualizar`.
Reabrir durante a consulta acompanha a operação existente; não agenda outra.

### Indicador nos acessos rápidos

O círculo à direita de `APPS` consome exclusivamente o cache persistido pelo
serviço. Antes da primeira consulta salva, ou com cache inválido, mostra apenas
`CLIMA`, sem temperatura ou condição inventadas. Com dados, mostra o ícone da
condição e a temperatura arredondada em Celsius (`23°`). Após reiniciar, pode
mostrar a última leitura salva sem abrir o app novamente.

Tocar no atalho (círculo, ícone ou temperatura) abre Clima e fecha os acessos
rápidos, inclusive quando mostra apenas `CLIMA`. A consulta ocorre pelo
`on_show` normal do app; exibir o painel continua consumindo somente cache.
O arraste para fechar não abre o app, e o primeiro toque com tela apagada
somente acorda o relógio.

O painel chama `chronvs_weather_init` para restaurar NVS uma vez e
`chronvs_weather_get_snapshot` para ler a cópia protegida por mutex. Não chama
`chronvs_weather_request_update` nem `chronvs_weather_take_result`: não inicia
rede e não retira resultados pendentes do app. Sem nova consulta bem-sucedida,
conserva a leitura salva. Os ícones vetoriais são compartilhados com o app e o
launcher por `ui/weather_icon.c`, com escala para o círculo de 70 px.

O teste integrado `tests/run_aion_ui.ps1 -System` cobre ausência/cache válido,
atualização do indicador, falha preservando dados, ausência de consultas e
consumo de resultados, pausa oculto/apagado, abertura pelo texto/ícone/temperatura,
primeiro toque reservado ao despertar, gesto de fechamento sem abrir o app
e os sete símbolos com temperaturas extremas dentro do círculo. Build e testes
no host passaram. O firmware foi gravado na COM3, com hashes de bootloader,
partições e aplicação verificados. Após a gravação da versão com abertura por
toque, o usuário aprovou o resultado e solicitou seu registro e publicação no Git.

## Testes

- cache válido aparece com idade correta;
- resposta válida persiste após reinício;
- falhas de Wi-Fi, timeout, HTTP não-200 e JSON incompleto preservam o cache;
- duas chamadas durante uma consulta não criam outra tarefa;
- sair durante a consulta não altera outro app;
- Wi-Fi é liberado em sucesso e erro;
- NTP e clima não executam sessões simultâneas;
- layout e gesto de retorno funcionam no recorte circular.

## Dificuldades, soluções e prevenção

| Dificuldade | Evidência e solução adotada | Regra para futuras mudanças |
| --- | --- | --- |
| NTP e Clima precisam usar o mesmo rádio | Sessão exclusiva com mutex e espera por `STA_STOP`; teste com duas threads confirmou a exclusão mútua. | Centralizar start/stop e handlers no serviço de sessão; tarefa de rede nunca toca LVGL. |
| Listras imediatamente ao abrir Clima | Erros de transmissão SPI precederam o início da rede; havia cerca de 24,5 KiB internos livres. Mover o pool de objetos LVGL de 128 KiB para PSRAM resolveu o fluxo no relógio. | Manter esse pool em PSRAM e distingui-lo dos dois buffers de pixels `1/20`. Não aumentar buffers nem alterar QSPI para compensar falta de RAM. |
| HTTPS falhava mesmo conectado ao Wi-Fi | `mbedtls_ssl_setup: -0x7F00` identificou falha de alocação. Após liberar a RAM interna, TLS conectou sem alterar sua estratégia de memória. | Medir RAM interna e maior bloco DMA por etapa; 8 MB de PSRAM e um build com pouco uso percentual não garantem RAM interna disponível. |
| Sintoma parecido com a falha anterior do som | O áudio foi corrigido pela espera síncrona QSPI; no Clima, o atraso de 2 s provou que o SPI falhava antes da rede. | Não generalizar a causa a partir das listras. Isolar fases, preservar correções anteriores e testar uma variável por vez. |
| Build e simulação não reproduziam as listras | Mocks passaram antes da falha física. Logs, teste no dispositivo e confirmação visual foram necessários. | Validar o fluxo original no hardware, incluindo a versão final sem atraso artificial. |
| Recuperação e artefatos locais | Firmware e sdkconfig foram preservados em `.pio/recovery/`; números e conclusões foram transcritos neste documento. | Guardar um firmware de recuperação antes do teste. Logs, imagens, binários, vendor e credenciais ficam fora do Git. |

O pool LVGL reserva memória uma única vez, após a inicialização da PSRAM;
reinicializar TLSF reutiliza a reserva. Não substitua esse mecanismo por um novo
array estático interno nem por alocação repetida. PSRAM não é substituta universal
para RAM interna: DMA, interrupções e acesso com cache desligado têm requisitos
próprios. O ajuste validado move somente o pool de objetos; TLS e pilhas continuam
na RAM interna.

Em uma regressão, primeiro registre em que etapa surgem os erros, memória
interna livre e maior bloco DMA, usando `heap_caps_get_free_size` e
`heap_caps_get_largest_free_block`. Compare antes de abrir, durante a rede/TLS
e depois de sair. Repita entradas/saídas para detectar perda de memória. Os
valores abaixo são observações desta unidade, não um orçamento mínimo seguro.
O total de RAM reportado no build não mede o pico dinâmico nem a fragmentação.

### Listras ao abrir — teste de isolamento

O usuário relatou listras imediatamente ao tocar em Clima. Inicialmente, a causa
era desconhecida; uma leitura de 8 s da COM3, sem solicitar reset, não recebeu
linhas de diagnóstico relevantes. No problema anterior do áudio, a correção
validada foi a espera síncrona QSPI, que continua aplicada.

O primeiro firmware de diagnóstico esperava **2 segundos na tarefa de clima antes de adquirir Wi-Fi**.
A tela e `Atualizando...` eram desenhados com o loop LVGL ativo.
O atraso não bloqueava a interface, não cancelava a consulta ao sair e não mudava
os prazos de conexão/HTTP. Ele separava o desenho inicial do início da operação
de rede do Clima; não constituía correção nem exclusão mútua entre rede e LVGL.
Uma sessão NTP já ativa poderia manter o rádio ligado nesse intervalo.

Os logs do diagnóstico marcavam a espera inicial e as etapas de rede. Os logs
finais `weather` mantêm aquisição da sessão, início de HTTPS, TLS conectado,
fim de HTTPS e rádio desligado. Cada marca inclui memória interna livre,
maior bloco DMA e PSRAM livre. mbedTLS usa RAM interna; o SPI também pode
precisar de buffers DMA temporários nesse heap. A pressão de memória foi a
hipótese examinada e depois corrigida, conforme os resultados abaixo.
A opção de TLS em PSRAM foi considerada, mas não integrou o primeiro teste:
buffers, QSPI, CPU, caches e estratégia de memória foram preservados nessa etapa.

No teste de isolamento, o procedimento foi aguardar a tentativa NTP do boot,
abrir Clima e comparar o início das listras com os 2 segundos de espera e as
marcas de rede no monitor serial. Esse atraso não faz parte da versão final.
Se começarem antes da rede, investigar criação da tela/tarefa e memória; se
começarem depois, usar as marcas para separar Wi-Fi, TLS e persistência.
O firmware anterior e o sdkconfig estão em
`.pio/recovery/before-weather-tls-psram-20260910-090525/`.

Build e testes do serviço/sessão Wi-Fi passaram, incluindo a espera antes da
aquisição da rede. O teste do patch Waveshare confirmou idempotência, restauração
e limites do display. A versão de diagnóstico foi gravada na COM3 com os hashes
de bootloader, partições e aplicação verificados. O `sdkconfig.h` efetivo mantém
`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`, CPU a 240 MHz e caches 16/32 KiB.

### Evidência no dispositivo e correção de memória

A captura `.pio/host-tests/weather-device-diagnostic.log` mostrou o primeiro
erro `spi transmit (queue) color failed` em 50.225 ms de uptime, apenas 10 ms
depois da entrada na tarefa de clima. A aquisição do Wi-Fi só começou em
52.215 ms. Portanto, a falha inicial de transmissão antecede a rede do Clima.
Nesse instante havia 24.503 bytes de RAM interna livre e maior bloco DMA de
8.704 bytes. Depois da conexão, a RAM interna caiu para 21.915 bytes e
`mbedtls_ssl_setup` falhou três vezes com `-0x7F00` (`SSL_ALLOC_FAILED`).

O pool TLSF de **objetos LVGL**, antes um array estático de 128 KiB na RAM
interna, passa a ser alocado uma única vez em PSRAM por
`platform/lvgl_memory.c`, usando o hook `LV_MEM_POOL_ALLOC` do LVGL 8.3.11.
O tamanho e o alocador TLSF permanecem iguais; reinicializar LVGL reutiliza o
mesmo pool. Isso libera 128 KiB internos para tarefas, rede e alocações DMA.
Os dois buffers de pixels continuam em PSRAM com `1/20` da tela, QSPI de 2 KiB,
refresh de 20 ms e espera síncrona. TLS continua na RAM interna. O atraso de
2 s e os logs foram mantidos na primeira comparação no dispositivo.

O teste integrado `tests/run_aion_ui.ps1 -System` usa esse mesmo hook com
alocação simulada: confirma uma única reserva de 128 KiB, reutilização do pool,
todos os apps, navegação e proteção de despertar.

**Validação física confirmada pelo usuário: sem listras e com dados.** No log
`.pio/host-tests/weather-device-psram.log`, a RAM interna livre ao abrir subiu
de 24.503 para 155.675 bytes; durante TLS havia 117.079 bytes livres. O HTTPS
terminou e o rádio foi desligado sem erros SPI nessa captura. A falta de memória
do caminho anterior foi resolvida mantendo TLS na RAM interna.
Após essa confirmação, o atraso de diagnóstico foi retirado: a consulta volta
a iniciar normalmente ao abrir, com o pool LVGL em PSRAM e os logs de memória.
**Versão final sem atraso também confirmada pelo usuário: funcionando
normalmente, sem listras e carregando dados.** A gravação foi verificada por
hash. Em `.pio/host-tests/weather-device-final.log`, a abertura registrou
155.491 bytes internos livres; TLS conectou com 117.047 bytes livres e a sessão
encerrou em cerca de 4,8 s, sem erros SPI/TLS nessa captura. Esse tempo corresponde
a uma consulta observada, não a uma garantia de latência da rede.

### Testes no host

`tests/run_weather_tests.ps1` compila o parser cJSON do ESP-IDF e o serviço real
com mocks de HTTP/NVS/FreeRTOS: cache e reinício, JSON incompleto, datas/unidades,
HTTP não-200, timeout, limite inclusive com resposta chunked, falhas de memória,
gravação e criação da tarefa, pedido duplicado e liberação do Wi-Fi. Também
executa o gerenciador real de sessão com duas threads no host, confirmando
exclusão mútua, limite de tentativas, timeout e desligamento.

Com `-UI`, compila LVGL 8 e a tela real, verifica o recorte circular, idade de
3 h, estados sem dados/cache/erro, retorno por conteúdo e ícone, contatos
prolongados, ausência de consultas periódicas e pausa ao sair ou apagar a tela.
As capturas ficam em `.pio/host-tests/weather-*.bmp`.
`tests/run_aion_ui.ps1 -System` cobre o registro no launcher, a proteção do
primeiro contato após despertar e memória LVGL com todos os apps criados.

O build PlatformIO passou (aproximadamente 1,51 MB de aplicação, 36,1% da partição).
Uma consulta HTTPS pública pelo host retornou HTTP 200 com os campos e unidades
esperados; isso não valida a conexão TLS/Wi-Fi do ESP32.
No relógio foram confirmados dados e ausência de listras após a correção de
memória; os logs confirmaram TLS e encerramento da sessão em duas consultas.
Em 2026-09-10, o usuário confirmou que os testes funcionais complementares
foram realizados no relógio e estavam corretos:

- carregar os dados, reiniciar sem rede e manter o cache com idade após a falha;
- restaurar o Wi-Fi e atualizar normalmente ao reabrir;
- abrir durante a sincronização NTP;
- sair durante a consulta;
- apagar e acordar a tela.

Essa confirmação conclui a validação funcional proposta. Medições quantitativas
de consumo, pico de memória/pilha e fluidez permanecem avaliações separadas;
não foram fornecidas novas capturas ou medições para esses testes complementares.
Não houve alteração de buffers LVGL, transferência QSPI, caches ou driver.

Referências de protocolo: [Open-Meteo](https://open-meteo.com/en/docs) e
[ESP HTTP Client 5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/protocols/esp_http_client.html).
