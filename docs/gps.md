# GPS UART

A-GNSS por Wi-Fi implementado em 12/11/2026 para GPS e Corrida: espelho HTTPS
CASIC, validação de órbitas, referência regional autorizada pelo CEP e envio
UART parcelado. Aceitação e ganho no módulo continuam pendentes de teste físico.
Uso, fontes e limites em [`agnss.md`](agnss.md).

Atualização em 11/11/2026: o novo app Corrida grava no SD e o serviço de hora
usa UTC de RMC como fallback de NTP. Esses clientes podem manter recepção com
tela apagada e fora de GPS; o diagnóstico sozinho mantém o comportamento
descrito a seguir. O experimento de standby é recusado enquanto Corrida usa
o receptor. Uso, exportação e limites em [`running.md`](running.md).

Módulo GNSS GPS+BD, o mesmo usado no pda-os do LilyGo T-Deck.
Ligação informada pelo usuário em 07/11/2026:

| Módulo | Chronvs |
| --- | --- |
| VCC | 3V3 |
| GND | G |
| TX | RXD / GPIO44 |
| RX | TXD / GPIO43 (fio adicional confirmado em 07/11/2026) |
| PPS | sem uso |

RXD/GPIO44 e TXD/GPIO43 constam na
[documentação oficial da placa](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46).
O firmware atual recebe UART1 em RX44 e envia assistência em TX43. Não grava
configuração persistente do GNSS nem usa PPS. A versão anterior RX-only não
exigia o fio adicional; ele agora é necessário para a assistência.

Abra GPS no launcher. A detecção de baud é automática, com as mesmas taxas
do pda-os. Primeiro deve aparecer `Buscando satélites`, confirmando NMEA
válido; somente depois do fix aparece `Posição encontrada`. Faça o teste
em local aberto, com antena instalada e visão do céu. Para aguardar sem
interrupção por inatividade, selecione ON e desative ECO no painel rápido.
Sem serial, confira alimentação, terra comum e TX do módulo no RXD da placa.

São exibidos latitude/longitude WGS84 em graus decimais, velocidade RMC,
altitude GGA em metros, satélites usados e HDOP. Campos ausentes não são
substituídos por zero. Checksum inválido não atualiza dados. Sentenças sem fix
invalidam a posição, e dados com cinco segundos ou mais deixam de ser atuais.
GSV agora fornece diagnóstico de satélites listados por talker e intensidade
de sinal, separado dos satélites em uso no GGA; veja a seção abaixo.

## Diagnóstico de aquisição — 11/11/2026

Após instalar Corrida, o usuário informou NMEA aumentando, estado `Buscando
satélites` e contador de satélites em uso zerado durante alguns minutos ao ar
livre. Isso confirma recepção de sentenças válidas, mas não identifica a causa
da ausência de fix. Os valores de GSV e C/N0 não estavam disponíveis na tela
da versão testada. Não foi demonstrada uma regressão do parser ou falha da antena.

O diagnóstico acrescenta uma linha `GSV GP:8 BD:4`, por exemplo, mostrando
separadamente os satélites listados por sistema. Não soma GP/BD/GN nem saídas
de sinais diferentes, que podem se sobrepor. Guarda até quatro talkers e a
última sequência por talker. `C/N0 34 dBHz` mostra o maior valor recebido nos
ciclos atuais; um ciclo parcial contém somente os fragmentos já recebidos.
`--` significa informação ausente ou expirada, não zero. GSV pode listar um
satélite sem C/N0: estar listado não garante rastreamento nem participação no fix.
Os campos e unidades seguem a
[descrição NMEA GSV do fabricante u-blox](https://content.u-blox.com/sites/default/files/ZED-F9T_InterfaceDescription_%28UBX-18053584%29.pdf).
Essa referência descreve o formato NMEA, não comprova suporte específico do nosso módulo.

O parser verifica checksum, índices e sequência dos fragmentos, aceita signal
ID opcional e descarta valores após cinco segundos. GSV não cria nem invalida
posição, hora ou velocidade e não envia comandos ao receptor. Corrida agora
distingue `Sem NMEA recente`, `Buscando satélites` e `Posição OK; aguardando hora`.

Para o próximo teste, manter ON e ECO desativado, abrir GPS e anotar estado,
satélites em uso, linha GSV, C/N0 e erros, com o relógio parado em local aberto.
NMEA sem GSV recente não basta para concluir ausência de recepção de satélites.
A mudança melhora a observabilidade; não é uma correção comprovada da aquisição.

O firmware anterior foi preservado antes do build em
`logs/firmware-references/20261111T221037797138Z-waveshare_esp32_s3_touch_lcd_146/`,
com hashes verificados pelo utilitário de referências. Essa referência é a
versão Corrida gravada e reportada sem fix, não uma validação física do GPS.

O build padrão e os testes de parser/UART (RX-only e experimental), gravador,
exportação, NTP e interface integrada passaram. A captura de GPS com GSV/C/N0
simulados foi inspecionada. O firmware padrão com esse diagnóstico foi gravado
na COM3 em 11/11/2026, com quatro hashes verificados e reset por RTS. A causa
da ausência de fix continua pendente das leituras físicas de GSV e C/N0.

Ao sair ou apagar o backlight, o driver UART é liberado; voltar/acordar
reinicia a sessão e exige dados novos. Isso permite manter light sleep,
mas não desliga fisicamente o GPS ligado ao 3V3: seu consumo continua.
O app GPS não grava trilhas nem tem mapa; Corrida grava no SD e o serviço de
hora usa UTC do GPS como fallback. A-GNSS é um cliente temporário adicional,
capaz de manter recepção com tela apagada até concluir a operação.

## Verificação

- `tests/run_gps_tests.ps1`: checksum, hemisférios, limites geográficos,
  estados sem fix, campos ausentes, expiração independente e UART simulada
  com fragmentação, detecção de baud, interrupção da serial, suspensão e
  recuperação após falha de inicialização.
- `tests/run_Relogio_ui.ps1 -System`: app e ícone no launcher, renderização,
  retorno por gesto, contato prolongado, AUTO/ECO e primeiro toque ao despertar.
- Build padrão PlatformIO obrigatório antes da entrega.

Em 07/11/2026, passaram o build padrão, os testes de parser/UART e a suíte
integrada LVGL. As capturas `.pio/host-tests/gps-searching.bmp` e
`gps-position.bmp` foram inspecionadas. Os dados da captura são simulados;
não representam aquisição do módulo físico.
O firmware padrão também foi gravado na COM3 nessa data, com hashes das quatro
imagens verificados e reset automático.

### Validação física pelo usuário — 07/11/2026

O usuário testou o primeiro firmware ao ar livre e enviou duas fotos da tela:

| Etapa | Estado | Satélites em uso | HDOP | NMEA / erros |
| --- | --- | --- | --- | --- |
| Após ligar, antes do fix | Buscando satélites | 0 | 25,5 | 382 / 0 |
| Após aquisição | Posição encontrada | 11 | 1,1 | 1343 / 0 |

Ambas mostram RX44 a 9600 baud. A primeira mantém coordenadas, velocidade e
altitude sem valores; a segunda exibe coordenadas, velocidade de 0,0 km/h
e altitude de 751 m. Não houve erro de checksum registrado nessas capturas.
As fotos confirmam a aquisição física e a exibição dos dados; as coordenadas
não foram comparadas com uma referência independente.

O usuário relatou aproximadamente 5 km/h ao caminhar com passo acelerado e
2 km/h ao andar mais devagar, considerando boa a resposta da velocidade.
Esse relato confirma resposta ao movimento, sem constituir calibração da
precisão de velocidade. Não foi medido o tempo até o primeiro fix.

Continuam pendentes: coordenadas/altitude contra referência conhecida,
desconexão/perda de fix, retorno ao launcher, apagar/acordar e observação de
artefatos durante esses fluxos. Também faltam medições de
corrente e memória interna/bloco DMA com outros serviços em uso.
Os parâmetros do driver de tela, buffers, QSPI e pool LVGL são preservados.

## Pesquisa de standby por comando — 07/11/2026

O módulo identificado no pda-os é ATGM336H-5N-31. A especificação CASIC,
seção 1.6.9, página impressa 32, documenta `$PCAS12,60*28\r\n` para entrar em
standby por 60 segundos e depois retornar automaticamente. Porém, a mesma
seção restringe o suporte documentado aos módulos de baixo consumo **5L**.
Não assumir suporte no nosso **5N31** nem exibir estado de baixo consumo com
base apenas no envio do comando ou no silêncio da UART.

O datasheet do chip AT6558, seção 10.6.1, descreve standby por UART como
capacidade do chip, mas não especifica ali o comando nem garante sua presença
no firmware de todos os módulos. Portanto, há capacidade no silício e uma
restrição no protocolo dos módulos; suporte na unidade instalada segue sem teste.

Um teste futuro exige TXD/GPIO43 do relógio no RX do módulo, além da ligação
existente. Primeiro consultar a versão com `$PCAS06,0*1B\r\n`; só depois testar
o standby temporário de 60 s, observar retorno de NMEA e medir a corrente no
VCC do GPS. Não salvar configuração em flash, trocar firmware do GNSS ou
habilitar suspensão automática antes de validar entrada, retorno e consumo.
Na versão de 07/11/2026, o firmware padrão apenas recebia; o experimento opcional
habilitava TX. A versão de 12/11/2026 também usa TX para A-GNSS, sem habilitar
standby automático.

O manual ATGM336H-5N também documenta **ON/OFF no pino 5 do módulo metálico**,
ativo em nível baixo. Esse sinal não aparece entre os cinco terminais do
breakout mostrado (VCC, GND, TX, RX e PPS). Usá-lo exige inspecionar a placa e
suas conexões antes de soldar; não confundir ON/OFF com RX ou PPS. A economia
do conjunto, incluindo regulador, LED e antena, precisa de medição.

Fontes primárias do fabricante, em cópias públicas:

- [Especificação CASIC, seção CAS12, p. 32](https://docs.ptwsmart.com/casic%E5%A4%9A%E6%A8%A1%E5%8D%AB%E6%98%9F%E5%AF%BC%E8%88%AA%E6%8E%A5%E6%94%B6%E6%9C%BA%E5%8D%8F%E8%AE%AE%E8%A7%84%E8%8C%83.pdf).
- [Datasheet AT6558, seção 10.6, pp. 35–36](https://14374449.s21i.faiusr.com/61/ABUIABA9GAAg16-50AUowNe_pAM.pdf).
- [Manual ATGM336H-5N, pinagem na p. 10](https://www.tinytronics.nl/product_files/002176_ATGM336H.pdf).

## Firmware experimental `gps_standby_test`

Preparado em 07/11/2026 para testar a unidade instalada. Requer o fio adicional
**TXD/GPIO43 do Chronvs → RX do GPS**, mantendo TX do GPS no RXD/GPIO44.
A ligação deve ser feita com a placa desligada. Esse ambiente usa UART1 RX44/TX43,
com console UART desabilitado, sem Wi-Fi adicional, tarefas ou configuração
persistente do receptor. O build rejeita console UART concorrente no GPIO43.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e gps_standby_test
```

Com ON e ECO desativado, abra GPS, aguarde a serial receber NMEA e arraste
para cima. A segunda página tem duas pílulas de 120 × 54 px a y=314:

1. **Versão** envia `$PCAS06,0*1B\r\n`. A tela mostra o texto TXT recebido,
   limitado a 95 bytes, ou informa ausência de resposta após cinco segundos.
2. **Testar 60 s** envia uma única vez `$PCAS12,60*28\r\n`, sem salvar em flash.
   Invalida os dados anteriores e mostra contagem regressiva. Controles ficam
   desabilitados enquanto observa. Um segundo de saída em trânsito é tolerado.

Resultados descrevem somente observações:

- **NMEA continuou**: chegou NMEA válido depois da tolerância inicial e antes
  do retorno esperado; comando não demonstrou o standby de 60 s.
- **Serial em silêncio**: não houve novo NMEA após três segundos. Isso também
  pode ocorrer por falha de conexão; não confirma redução de corrente.
- **Serial retornou**: após silêncio observado, NMEA voltou a partir de 55 s.
  É compatível com o temporizador de 60 s, mas ainda exige medir consumo.
- **Sem retorno em 70 s**: o prazo passou sem NMEA válido. Não envia comandos
  adicionais automaticamente; o módulo pode exigir reinício de alimentação.
- **Teste interrompido**: sair do app ou apagar a tela encerrou a observação.
  O comando já enviado não é desfeito; aguarde o prazo do módulo.
- **Falha ao enviar**: a UART não aceitou todos os bytes do comando.

Arrastar para baixo ou para a direita na segunda página retorna aos dados,
sem interromper a observação; da página de dados, direita volta ao launcher
e interrompe o teste. Apagar a tela libera a UART antes do light sleep, como
no padrão, sem renderização. Nenhum timer do teste registra atividade de toque.
Não há envio automático de standby no boot, ao sair ou ao apagar. A automação
de economia depende de confirmar suporte, despertar e redução de corrente.

A referência do firmware GPS validado foi preservada e verificada em
`logs/firmware-references/20261107T201025397415Z-waveshare_esp32_s3_touch_lcd_146/`.
O ambiente padrão permite restaurar a versão sem comandos; arquivos locais de
referência contêm as imagens exatas e o mapa de gravação validado.

Testes: `tests/run_gps_tests.ps1` inclui as variantes RX-only e standby;
`tests/run_Relogio_ui.ps1 -System -GPSStandby` simula a segunda página, ações,
estados, botão desabilitado e retorno por gesto, além dos fluxos globais.
Esses testes não confirmam suporte de standby ou economia no hardware.

Preparação validada: em 07/11/2026 passaram os builds padrão e
`gps_standby_test`, os testes de parser/UART nas duas variantes e a suíte
integrada com `-System -GPSStandby`. As capturas da página de teste foram
inspecionadas, incluindo consulta, observação, silêncio e retorno simulados.
Após o usuário confirmar a solda do fio adicional, o experimento foi gravado
na COM3 em 07/11/2026, com hashes das quatro imagens verificados e reset
automático.

### Resultado físico do comando — 07/11/2026

O usuário informou a resposta `sw=unarus5v5.3.0.0` à consulta de versão
(transcrição fornecida pelo usuário, sem captura bruta). A resposta confirma
comunicação no sentido Chronvs → GPS após a solda do fio adicional.

Ao executar Testar 60 s, o resultado exibido foi **NMEA continuou**. Pela lógica
do teste, chegou NMEA válido depois da tolerância inicial de um segundo,
antes do retorno esperado. Portanto, CAS12/60 s não demonstrou standby nesta
unidade com esse firmware. O resultado é compatível com a restrição documentada
do comando à família 5L; não prova ausência de todo recurso de baixo consumo
no silício. Não houve medição de corrente nem confirmação de economia.

Não habilitar standby automático com esse comando. O próximo caminho de
hardware é inspecionar a disponibilidade do ON/OFF do módulo ou usar corte
de alimentação controlado. Naquela etapa o firmware experimental permaneceu
instalado; foi substituído pelo padrão com Corrida/diagnóstico em 11/11/2026 e
pelo padrão com A-GNSS em 12/11/2026. CAS12 continua restrito ao experimento
e ao toque explícito em Testar 60 s.
