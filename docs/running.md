# Corrida — GPS e cartão SD

Primeira implementação em 11/11/2026. Registra corrida offline usando o GNSS
UART existente e o cartão microSD de 16 GB informado pelo usuário. Cadência e
contagem de passos pelo QMI8658 ficam para a próxima etapa.

## Uso

1. Com o relógio desligado, mantenha o cartão FAT32 inserido. O firmware nunca
   formata o cartão e não exclui registros para liberar espaço. Outros sistemas
   de arquivos podem falhar na montagem; não formate um cartão com dados sem backup.
2. Abra **Corrida** em local aberto e aguarde `GPS pronto para iniciar`.
   A preparação agora solicita A-GNSS por Wi-Fi automaticamente e mostra a fase
   no rodapé. Usa a mesma assistência do app GPS; neste, subir abre detalhes e
   a ação Atualizar. Falha na assistência não impede a busca independente.
   É necessário fix com data/hora UTC de RMC recente; o horário local do RTC
   não é usado para inventar timestamps do percurso.
3. Toque **Iniciar**. A tela mostra tempo ativo, distância, velocidade GPS,
   ritmo instantâneo e tempo total. **Pausar** interrompe a gravação de pontos;
   **Retomar** cria outro segmento, sem somar o deslocamento durante a pausa.
4. Pode sair por gesto e usar outros apps. A tela apaga normalmente em AUTO/ECO,
   e o primeiro toque apenas a acorda. Gravação e pausa mantêm a UART recebendo;
   light sleep fica impedido até finalizar, pois essa versão não implementa
   recepção UART durante o sono. A preparação também mantém GPS ativo enquanto
   o app está aberto; sair antes de iniciar libera esse pedido.
5. **Finalizar** abre uma confirmação; **Salvar** fecha os arquivos e mostra
   o resumo, incluindo velocidade/ritmo médios pelo tempo ativo. Direita cancela
   a confirmação. **Nova** prepara outra atividade. Reiniciar/desligar pelo
   menu Energia também tenta finalizar uma gravação, mesmo fora de Corrida.
6. Subir abre os nomes dos cinco CSV mais recentes; descer ou direita retorna.
   Essa lista também pode conter gravações interrompidas. Ela não é um navegador
   de detalhes, não apaga arquivos e não retoma atividades de um boot anterior.

## Arquivos e exportação

Pasta no cartão: `chronvs/runs/`. O nome usa o início em UTC, por exemplo
`20261111T143000Z-00`. Sufixos evitam sobrescrever arquivos existentes.

- `.csv`: UTC Unix, tempo total, tempo ativo, segmento, latitude, longitude,
  velocidade em km/h, altitude em metros e distância acumulada em metros.
  Valores ausentes ficam vazios. Uma linha `#finish` guarda o resumo final.
- `.gpx`: GPX 1.1, com latitude/longitude WGS84, UTC por ponto e altitude quando
  disponível. Pausas, lacunas e saltos rejeitados separam segmentos.
  Velocidade original permanece no CSV; GPX não declara cadência.
- `.gpx.part`: arquivo em gravação ou interrompido. Só recebe a extensão final
  depois de fechar e sincronizar os arquivos com sucesso.

Após finalizar, desligue o relógio antes de retirar o SD. Copie o `.gpx` pelo
leitor de cartão e importe pelo site do Strava em **Enviar atividade → Arquivo**,
selecionando Corrida se necessário. Não há sincronização automática nem serviço
de download USB/Wi-Fi nesta etapa. O Strava pode recalcular distância, velocidade,
tempo em movimento e pausas de modo diferente do relógio. A importação real de
um arquivo produzido pelo relógio ainda precisa ser validada.

Os arquivos são sincronizados a cada cinco segundos, na pausa e na finalização.
Uma queda de energia pode perder as últimas escritas e também danificar o FAT;
`fsync` não garante a sobrevivência a toda falha elétrica do cartão. O firmware
preserva arquivos parciais. Para recuperar os pontos completos de um CSV:

```powershell
python scripts/export_run.py 'E:\chronvs\runs\20261111T143000Z-00.csv' 'corrida-recuperada.gpx'
```

O conversor lê em fluxo, preserva os segmentos, ignora a última linha sem newline
e recusa sobrescrever o destino. Um erro em outra linha interrompe a exportação;
um destino parcial pode permanecer e deve ser inspecionado antes de usar.
Não é prometida retomada da corrida após reinício nem recuperação de FAT corrompido.

## Aquisição, distância e energia

Grava no máximo um ponto por segundo UTC novo de RMC; não repete o último fix
em cada passagem do loop. Exige RMC com menos de dois segundos; fix ausente e
HDOP conhecido acima de 5 criam lacunas. Altitude/velocidade seguem a expiração
do parser. Distância usa Haversine, sem ponte após pausa, lacuna maior que cinco
segundos ou salto acima de 10 m/s. Deslocamentos menores que 2 m acumulam contra
a última âncora até ultrapassar o limiar; velocidade conhecida abaixo de 1,5 km/h
não soma distância. Esses filtros são iniciais, não uma calibração; devem ser
avaliados em percurso conhecido, curvas e com o usuário parado. UTC repetido ou
regressivo não cria outro ponto. Tempo ativo/total usam relógio monotônico.

`sd_service.c` monta SDMMC de 1 bit: CLK14, CMD17, D0/16, conforme o exemplo
Waveshare local. Não usa GPIO21, que pertence ao painel. A montagem é sob demanda,
sem copiar o exemplo que habilita formatação automática. O SD usa buffers do
driver que podem exigir RAM interna/DMA; o ambiente de diagnóstico registra RAM
interna livre e maior bloco DMA antes/depois de montar. Não mudou buffer LVGL,
pool de objetos, período de refresh ou espera síncrona QSPI.

Escritas/montagem são síncronas na tarefa principal, com poucos dados por ponto,
sem nova tarefa ou cache de percurso em RAM. Cartão lento/defeituoso pode bloquear
a interface até o timeout do driver. É necessário medir latência, memória e
interação com Clima/NTP no hardware antes de declarar o fluxo validado.

## Hora do GPS quando NTP não está disponível

NTP conserva prioridade enquanto está ativo ou teve sucesso nas últimas 12 h.
Sem credenciais ou após tentativa sem sucesso, o serviço tenta adquirir GPS por
até 120 s, inclusive com backlight apagado. Durante essa janela, light sleep
fica impedido. Sem fix/hora, libera seu pedido e aguarda uma hora para repetir;
um GPS já aberto por outro cliente também pode fornecer a hora nesse intervalo.
Uma correção GPS bem-sucedida vale por 12 h antes de nova busca.

RMC com checksum, status ativo, posição e calendário válidos fornece UTC ao
relógio do sistema. O PCF85063 e o serviço de hora recebem UTC−3, preservando
o fuso atual do Chronvs, com virada de dia calculada. Se a tela estiver apagada,
a gravação do RTC e atualização da hora compartilhada aguardam o despertar,
usando uma âncora monotônica para não gravar uma hora atrasada. Falha de escrita
no RTC é repetida após 60 s, somente com tela acesa. Não há leitura de RTC,
bateria ou renderização para fazer essa busca com backlight apagado.

GPS, Corrida e sincronização têm pedidos independentes da UART. Sair de GPS
não cancela a corrida. O comando experimental de standby/consulta é recusado
durante a preparação/gravação da corrida para proteger a recepção.

## Verificação e pendências

Revalidação local em 19/11/2026, antes de versionar a integração GPS/Corrida/
A-GNSS: build padrão aprovado (1.685.842 bytes de programa e 54.944 bytes
estáticos), suítes GPS (normal e standby), A-GNSS (dados, órbitas, fontes e
serviço), Corrida/exportação, NTP/fallback GPS, configuração de console e
integração `run_Relogio_ui.ps1 -System` aprovadas. A integração manteve
11.088 bytes como maior bloco LVGL livre com editor e aviso, no host.
Esses valores não medem RAM interna/DMA do dispositivo. Não houve upload nem
novo ensaio físico nesta revalidação; permanecem os limites registrados abaixo
e em `gps.md`/`agnss.md`. Segredos, logs, firmware e referência Waveshare
continuam ignorados e fora do commit.

- `tests/run_run_tests.ps1`: deslocamento conhecido, dados repetidos, pausas,
  tempo ativo/total, navegação em segundo plano, saltos, jitter parado, colisão
  de nomes, ausência/falha de sincronização do SD e recuperação CSV → GPX/XML.
- `tests/run_gps_tests.ps1`: parser/UART nas duas variantes, UTC/calendário,
  validade, expiração e propriedade independente dos pedidos de recepção.
- `tests/run_ntp_tests.ps1`: prioridade NTP, janela GPS limitada, repetição,
  ajuste do sistema e RTC adiado com UTC−3 e virada de dia.
- `tests/run_Relogio_ui.ps1 -System` e variante `-GPSStandby`: app real,
  gravador real em diretório de teste, capturas, ações, cancelamento, retorno
  sobre controles, recepção em segundo plano, AUTO/ECO e toque de despertar.
- Build PlatformIO padrão e variante `gps_standby_test`.

Os testes usam UART, cartão e relógio simulados; não confirmam montagem do cartão
instalado, corrente, autonomia, precisão do percurso ou suporte físico ao SD.
Em 11/11/2026 passaram o gravador/exportação (incluindo leitura XML dos GPX
gerados em C), parser/UART nas duas variantes, NTP/fallback GPS e integração
LVGL padrão e experimental. As capturas de gravação, resumo, confirmação e
registros foram inspecionadas. Os builds padrão e `gps_standby_test` também
passaram. Após autorização do usuário, o firmware padrão foi gravado pela
COM3 em 11/11/2026, com hashes das quatro imagens verificados e reset por RTS.
A gravação não confirma montagem do SD nem funcionamento da corrida em campo.
Antes de uma corrida longa: validar um percurso curto conhecido, desligamento,
pausa/retorno, perda de sinal, leitura dos arquivos e importação no Strava.
Cadência/passos, mapa, voltas, pausa automática e sincronização direta ficam fora
desta primeira versão.

A integração A-GNSS de 12/11/2026, referência aproximada do CEP autorizada pelo
usuário e proteção contra registrar a posição fornecida como fix estão em
[`agnss.md`](agnss.md). Durante gravação/pausa não injeta nova assistência.
