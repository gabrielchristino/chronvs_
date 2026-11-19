# A-GNSS por Wi-Fi — pesquisa de viabilidade

Pesquisa em 12/11/2026 para o **ATGM336H-5N-31 / AT6558** instalado no Chronvs.
Resultado da pesquisa: há suporte documentado e uma fonte online verificada.
Implementação iniciada/autorizada em 12/11/2026, descrita abaixo. O ganho de
aquisição ainda exige teste físico; a falta de fix relatada permanece sem
causa demonstrada.
Na integração previsão/IGS, download, conversão e persistência foram confirmados
no relógio. O ensaio posterior do lote completo recebeu ACK para 21 PRNs
diferentes, entre as duas fontes. Há aceitação parcial demonstrada; validade
interna, fix real e ganho de aquisição ainda exigem validação.

## Firmware integrado aos apps GPS e Corrida — 12/11/2026

`services/agnss_service.c` atende os dois apps. GPS solicita ao abrir; Corrida
solicita ao preparar uma nova atividade. Tentativas automáticas têm intervalo
mínimo de dez minutos. No GPS, subir abre **Assistência GPS**, com fonte, fase,
progresso, ACK/rejeições, efemérides válidas informadas pelo receptor e memória
amostrada na operação. **Atualizar** permite repetir manualmente quando não
houver operação nem corrida iniciada/pausada. Direita/descer retorna aos dados.

O usuário autorizou a referência regional pelo CEP **04686-001**, informando
que os testes ficarão nessa área. O logradouro fica em Vila Isa/São Paulo,
corroborado por [cadastro municipal público](https://links.prodam.sp.gov.br/wp-content/uploads/2022/05/TR_ARP_SD_WAN_v7-1.pdf).
O ponto de auxílio foi arredondado para **−23,668°, −46,690°**, com incerteza
de **5 km**, tomando a região aproximada do
[cadastro do CEP](https://cepbrasil.org/sao-paulo/sao-paulo/vila-isa/04686001).
Não representa endereço nem coordenada medida. A referência fica no firmware;
não consulta CEP, localização por IP ou geolocalização remota a cada uso e não
envia coordenadas ao servidor de órbitas. Se o relógio mudar de região, ajustar
ou retirar essa referência antes de usar assistência posicional.

Fluxo implementado:

1. Aguarda até 90 s por UTC confiável, de correção NTP/GPS deste boot com no
   máximo 12 h. Não usa data de compilação nem RTC não validado. NTP conserva
   o fluxo existente; não cria outro cliente SNTP concorrente.
2. Reutiliza o conjunto atual em PSRAM se houver pelo menos quatro efemérides
   GPS saudáveis a no máximo ±2 h de sua referência. Um worker temporário de
   8 KiB procura primeiro a previsão no SD, depois o pequeno cache IGS no SD.
   Sem dados utilizáveis, obtém a sessão Wi-Fi exclusiva de NTP/Clima.
3. Baixa primeiro a previsão pronta de sete dias do projeto
   [open-extended-ephemeris](https://github.com/badriram/open-extended-ephemeris).
   Se estiver ausente, corrompida ou sem quatro registros para agora, baixa o
   RINEX de transmissões reais do IGS/BKG e converte no próprio relógio. Tenta
   hoje e ontem em UTC, inclusive na virada do dia. HTTPS usa o bundle de
   certificados, sem redirecionamento nem relaxamento da validação TLS; cada
   previsão tem prazo de 60 s e cada IGS de 180 s, com operações de
   abertura/cabeçalhos de 15 s.
   A previsão tem limite de 384 KiB; o gzip IGS, 2 MiB comprimidos/16 MiB de
   saída. O gzip é descompactado pela ROM em anel de 32 KiB, com CRC32 e ISIZE;
   o parser RINEX 3 processa linhas sem reservar todo o arquivo descompactado.
   Os buffers grandes e o parser ficam em PSRAM. Seleciona somente um registro
   saudável por PRN, o mais próximo da hora atual, sempre dentro de ±2 h. A
   previsão de sete dias contém muitos períodos; não amplia o limite de idade
   de um registro individual. Confere ambos os checksums CASIC e só publica
   GPS-EPH; não executa comandos de configuração vindos da rede. O conjunto
   final tem no máximo 2.624 bytes. Libera HTTP e transientes antes de publicar.
4. Pela tarefa UI, envia AID-INI e os frames válidos por RX44/TX43. TX tem ring
   de 512 bytes; envia um frame por vez e consulta conclusão com zero ticks,
   sem esperar os segundos da transmissão no loop LVGL. Há pelo menos 20 ms
   entre envios; aguarda ACK correspondente antes do próximo pacote, com prazo
   de 1,5 s por confirmação e 180 s para a fase de transmissão. ACK de outro
   tipo não libera o próximo envio. Sem confirmação, interrompe e informa o
   timeout, sem afirmar que o receptor não suporta assistência. Aguarda UART com
   NMEA recente e pelo menos um segundo de sessão; a espera por serial termina
   após 120 s da solicitação. Perfis com console UART0 preservam GPIO43 e não
   iniciam assistência, evitando conflito elétrico/lógico com o RX do módulo.
5. Se a atividade começar durante o download, guarda o resultado sem injetar.
   Iniciar/pausar uma corrida impede novos envios. Não reseta o GNSS, não altera
   constelações, baud ou saída NMEA e não grava configuração persistente.
6. Após AID-INI, suprime posição/velocidade/altitude até um GGA novo confirmar
   fix com pelo menos quatro satélites em uso. A proteção sobrevive ao fechamento
   da UART, pois o receptor pode continuar alimentado. A referência fornecida
   nunca é exportada como ponto só por ter sido enviada.
7. Separa CASIC binário de NMEA com buffer de 512 bytes e timeout de 2 s para
   fragmentos. Consulta NAV-STATUS após o envio; mostra as efemérides válidas
   observadas sem afirmar que vieram do download. Cada NACK orbital permite
   no máximo duas repetições idênticas, com 200 ms entre tentativas. Depois de
   três rejeições, marca somente esse PRN como recusado e segue os demais.
   Ao terminar consulta NAV-STATUS, inclusive após lote parcial/todo rejeitado.
   Lote de previsão com registros rejeitados permite uma única tentativa IGS
   após a consulta de estado, respeitando o
   bloqueio durante corrida. Ausência de ACK/status não confirma rejeição nem
   aceitação. Enviada também
   não significa fix; GSV/C/N0 e posição real continuam necessários.

Operação ativa mantém recepção mesmo fora do app/com tela apagada e impede
light sleep até concluir/falhar. Não mantém backlight aceso, não renderiza nem
lê RTC/bateria com tela apagada. Erros de Wi-Fi, UTC ou arquivo são mostrados
sem desligar a recepção independente de satélites. A previsão completa fica em
`/sdcard/chronvs/gps-pred-a.bin` ou `gps-pred-b.bin`; o conjunto IGS fica em
`gps-igs.bin`. Escrita temporária, flush/fsync e dois slots de previsão conservam
o slot anterior quando a atualização falha. FAT é montado na UI antes do worker,
sem formatação; arquivos da corrida são independentes e FatFs é reentrante.
Sem SD continua usando PSRAM, informando a ausência/falha de persistência. Não
grava em NVS. Uma previsão utilizável no SD evita nova conexão até deixar de
cobrir a hora atual; não há atualização diária forçada nesta versão.

Reutilização sem rede ainda exige hora confiável deste boot (NTP/GPS). Após
reinício completamente offline, RTC não validado continua insuficiente; possuir
o arquivo no SD sozinho não resolve a referência temporal da assistência.

As previsões são experimentais: o autor publica comparações numéricas, mas
ainda não relata injeção de registros previstos em receptor físico. Não há
garantia de aquisição dentro de casa ou da precisão da primeira posição. O
receptor continua recebendo as efemérides reais diretamente dos satélites.

### Verificação da previsão e IGS — 12/11/2026

Amostra pública de previsão: 213.528 bytes, 2.604 frames com checksums válidos,
cobrindo 12/11 13:59:42 até 19/11 11:59:42 UTC. Seleção às 18:45:54 UTC:
31 GPS. Amostra IGS/BKG: gzip de 912.885 bytes, RINEX de 5.815.904 bytes;
conversão de 32 GPS, 2.624 bytes, idênticos byte a byte à implementação de
referência do projeto l76k-at6558-agnss para a mesma época. Nenhum BeiDou é
injetado nesta etapa. O relógio não depende desse conversor Python externo.

`run_agnss_tests.ps1` cobre seleção, expiração, checksum, leitura HTTP parcial,
chunked/tamanho excessivo, fallback previsão→IGS, dois dias IGS, persistência,
reutilização sem rede, publicação entre tarefas, um retry após NACK e bloqueio
durante corrida. `agnss_orbits_test.exe` aceita RINEX, previsão, UTC e saída
opcionais para repetir com amostras reais. `agnss_gzip_test.c` verifica a mesma
conversão usando miniz de referência no host, CRC/tamanho e truncamento/corrupção.
Esse teste opcional usa [miniz v1.14](https://github.com/richgel999/miniz/tree/v114),
baixado para `.pio/host-tests/agnss-research/miniz-host.c`; o firmware usa a ROM
do ESP32-S3, sem incorporar esse fonte. Aquisição e consumo continuam exigindo
observação física. Os ensaios abaixo distinguem transporte de aceitação pelo GNSS.

### Ensaio físico das novas fontes — 12/11/2026

O perfil USB baixou e persistiu a previsão; após reinício, reutilizou o SD
antes de abrir Wi-Fi. ACKs e NACK da previsão provocaram fallback IGS. O HTTPS
BKG foi aceito pelo bundle do ESP32. O prazo inicial de 60 s interrompeu o
download em 888.832 de 925.969 bytes, com progresso de aproximadamente
15 KiB/s. Com prazo IGS de 180 s, completou os 925.969 bytes; o ESP32
descompactou/converteu e mostrou `IGS salvo no SD`, com 32 registros selecionados.
Menores valores amostrados na operação: RAM interna 105.695 B, maior bloco DMA
31.744 B. Não são mínimos globais nem margem garantida para outros fluxos.

Na sequência inicial, que esperava apenas fim do TX, houve rejeição também do
IGS. O teste seguinte aguardou confirmação individual: AID-INI 0B/01 recebeu
ACK; o primeiro GPSEPH 08/07 recebeu NACK, tanto na previsão quanto no IGS.
O fallback usou `IGS do SD` sem repetir o download. Um teste isolado com
incertezas lineares em metros/segundos (convenção de outra implementação
URANUS5) deu o mesmo resultado; essa variação foi retirada, conservando as
variâncias da especificação V4.2. Não houve reset nem configuração permanente
do receptor, alteração de constelação ou desativação de NMEA.

Naquele teste limitado ao primeiro GPSEPH, a aceitação orbital permaneceu
inconclusiva; o lote completo descrito abaixo posteriormente demonstrou ACKs.
NACK não identifica sozinho se o motivo é época,
conteúdo, estado interno ou variante do protocolo. Não concluir falta de
suporte definitivo nem prometer ganho de fix. No último ensaio dentro de casa
continuou sem fix, zero satélites em uso e maior C/N0 observado de 19 dB-Hz.

Logs locais: `gnss-usb-20261112T190135Z.log` (prazo),
`gnss-usb-20261112T190540Z.log` (IGS completo/conversão/SD),
`gnss-usb-20261112T191027Z.log` (ACK por pacote),
`gnss-usb-20261112T191248Z.log` (incertezas lineares, sem melhora).
Firmware normal recompilado e gravado após esses ensaios, com hashes das quatro
imagens verificados e reset automático. Sem console ou abertura automática do
GPS no padrão. O cache no SD permanece disponível.

### Lote completo por PRN — 12/11/2026

O serviço agora aguarda resposta por pacote e repete somente após NACK
explícito: envio original mais duas repetições idênticas, com 200 ms de
intervalo. Após três NACKs, registra a recusa do PRN e continua os demais.
Timeout não é NACK: interrompe o lote e consulta estado, pois o ACK contém
classe/ID, sem PRN; avançar após silêncio poderia atribuir resposta tardia ao
satélite seguinte. AID-INI rejeitado também interrompe. Prazo por ACK de 1,5 s,
por lote de 180 s; UART e LVGL permanecem na tarefa UI, sem espera bloqueante.

Resultado físico, perfil USB, fonte de previsão e IGS reutilizadas do SD:

| Fonte | GPS no lote | PRNs com ACK | PRNs recusados após 3 NACKs | Sem confirmação | Repetições |
| --- | --- | --- | --- | --- | --- |
| Previsão | 31 | 20 | 11 | 0 | 22 |
| IGS | 32 | 1 | 31 | 0 | 62 |

Na previsão foram confirmados PRNs 13–32; PRNs 1–11 foram recusados e PRN 12
não estava no conjunto selecionado. IGS confirmou PRN 12. Portanto, houve ACK
para **21 PRNs diferentes**. A tela conserva esse total entre fontes em
`ACK GPS no pedido`; as demais contagens são por registros únicos da fonte
atual. Repetições não aumentam o total de satélites. ACK/NACK brutos também
incluem AID-INI e todas as tentativas, portanto diferem dessas contagens.

O lote de previsão terminou em cerca de 20 s, depois houve fallback IGS e
consulta de estado ao final de ambos. As duas consultas NAV-STATUS receberam
NACK de classe 01/00: não há contagem interna válida de efemérides a mostrar.
Isso não significa zero efemérides. As confirmações de GPSEPH demonstram
recepção de registros, não posição nem validade orbital interna comprovada.

Não houve reset GNSS, troca de constelações/baud/NMEA ou gravação de configuração.
NMEA continuou sem erros; dentro de casa permaneceu sem fix, zero satélites
em uso e C/N0 máximo observado de 25 dB-Hz. Não há medição de melhora de TTFF.
O padrão dos NACKs pode envolver estado já carregado, mas isso é hipótese,
não um significado confirmado do protocolo. Teste futuro pode comparar ordem
de fontes e épocas antes de qualquer alteração de RF/configuração.

Log: `logs/gnss-usb-20261112T192918Z.log`. Testes A-GNSS cobrem primeiro PRN
recusado com os demais aceitos, recuperação em repetição, lote todo recusado,
contagem única entre fontes, consulta antes do fallback, ACK não correspondente,
timeout sem avanço e corrida iniciada durante repetição. GPS, gravador e UI
integrada passaram; capturas incluem envio, recusa e envio parcial.
Firmware normal final gravado após o ensaio, com hashes das quatro imagens
verificados e reset automático. O padrão volta ao mostrador, sem console nem
abertura automática do GPS; mantém os dados no SD e os novos contadores.

### Pesquisa de NACK e próximos ensaios — 12/11/2026

Há relatos primários semelhantes; nenhum demonstra sozinho a causa nesta unidade:

- [wardriver, issue 175](https://github.com/JosephHewitt/wardriver_rev3/issues/175):
  ATGM336H recebendo NACK de GPSEPH e ACK de GPSUTC/GPSION. O autor não confirmou
  ganho de aquisição nem publicou correção; informou em janeiro de 2026 que
  não avançou. Comentários sugerindo comprimento de um byte são hipóteses,
  não uma correção demonstrada. Não alterar framing por essa sugestão: nossos
  ACKs válidos e os arquivos reais usam comprimento de dois bytes.
- [Espruino, discussão 7026](https://github.com/orgs/espruino/discussions/7026):
  registros específicos falhavam enquanto outros eram aceitos; um participante
  observou primeiro registro rejeitado e segundo aceito. Outro relatou melhora
  de TTFF mesmo com algumas mensagens falhando. Há relato de respostas mais
  confiáveis ao suspender NMEA durante os comandos. São experiências no
  Bangle/AT6558, com variantes de firmware; não provam a causa no 5N-31.
- A nota **Quectel L76K AGNSS Application Note V1.1**, de 23/05/2024, descreve
  aguardar ACK de AID-INI e retransmitir o pacote orbital quando houver NACK.
  [Cópia do documento do fabricante](https://www.scribd.com/document/888884540/Quectel-L76K-AGNSS-Application-Note-V1-1-1).
  Essa orientação é do L76K, não garantia para todo módulo AT6558.
- [Suporte oficial Quectel, sem fix com 15 satélites listados](https://forums.quectel.com/t/gps-l76k-no-fix-but-15-satellites-in-view/21219):
  azimute/elevação sem SNR podem vir de informações já guardadas; não demonstram
  recepção atual. [Outro esclarecimento do suporte](https://forums.quectel.com/t/l76k-cold-start-issues/29858/2)
  afirma que assistência não resolve sinal RF insuficiente.

**Limite do ensaio anterior:** o firmware então interrompia a fonte no primeiro NACK.
Os ensaios sequenciados confirmaram rejeição do primeiro GPSEPH de cada fonte;
não demonstraram rejeição dos 31/32 registros. A política de abortar o lote é
uma escolha nossa e pode esconder aceitação parcial. Essa é uma hipótese de
melhoria baseada nos relatos; os itens 1 e 2 abaixo foram implementados no
diagnóstico por PRN, com os resultados físicos da seção anterior. Redução de
NMEA e comparação com pacotes originais continuam como ensaios separados.

Próximo diagnóstico proposto, uma variável por ensaio:

1. Registrar PRN, época, tentativa e resposta por pacote. Repetir o mesmo
   registro no máximo duas vezes, com intervalo limitado, antes de seguir aos
   demais. AID-INI rejeitado ainda deve interromper. Contar aceitos/rejeitados/
   sem resposta separadamente, sem tratar ACK como prova de órbita válida/fix.
   Sem resposta, a implementação interrompe e consulta estado, em vez de
   avançar: ACK só informa classe/ID, portanto resposta atrasada poderia ser
   atribuída ao próximo PRN. Somente NACK explícito permite repetir/seguir.
2. Consultar NAV-STATUS também após lote parcial; a consulta atual só acontece
   ao completar o envio, portanto o primeiro NACK impede observar o estado.
   Ausência de resposta continua inconclusiva, pois a variante pode não suportar
   essa consulta. Não declarar que os satélites estão sem efemérides sem resposta.
3. Comparar envio com NMEA reduzido temporariamente, conservando/restaurando
   a configuração conhecida e a recepção necessária ao runtime. Sem PCAS00,
   reset, alteração simultânea de baud/constelações ou corrida em andamento.
4. Comparar um pacote CASIC original e atual com os convertidos, preservando
   bytes reservados e verificando época/checksums. Não injetar arquivo expirado
   nem inventar valores reservados. Capturar identidade real de firmware/chip;
   a transcrição de versão ainda não prova equivalência com o L76K dos relatos.

Como alternativas, o [suporte Quectel](https://forums.quectel.com/t/l76k-how-to-use-agnss-fetaure-with-l76k/6947)
oferece dados oficiais com conta para L76K; isso não autoriza assumir acesso ou
compatibilidade desse serviço no ATGM336H. Para o nosso módulo, a via equivalente
é o fabricante Zhongke/fornecedor. Preservar alimentação de backup também pode
reduzir partidas frias, dependendo dos pinos/circuito efetivamente expostos:
[exemplo de placa L76K que mantém RTC/estado quente](https://wiki.seeedstudio.com/get_start_l76k_gnss/).
Trocar o módulo não é a primeira conclusão desses resultados.

Para AID-INI foram adotadas as unidades da especificação CASIC V4.2: variância
de posição em m² e variância temporal escalada por c², com incerteza de 2 s e
altura marcada como desconhecida. Isso ainda requer validação do firmware da
unidade; a divergência entre versões da documentação é descrita na pesquisa.
UTC→GPS usa o offset de 18 s confirmado pelo
[IERS Bulletin C72](https://datacenter.iers.org/data/16/bulletinc-072.txt), sem
leap second ao final de dezembro de 2026. A tabela recusa datas a partir de
01/07/2027 até revisão, em vez de aplicar silenciosamente uma correção futura
desconhecida.

O diagnóstico guarda o menor valor **amostrado** de RAM interna livre e maior
bloco DMA antes/depois da rede e após abrir TLS. Não é rastreamento de todo pico
instantâneo do handshake, nem prova de margem segura. Buffers grandes ficam em
PSRAM; pilha e buffers UART/DMA continuam em RAM apropriada, e os parâmetros
do driver de tela foram preservados.
Os campos de leitura da nova página são desenhados no próprio objeto para manter
a reserva de objetos LVGL com o editor de lembretes e o aviso abertos.

Verificação: `tests/run_agnss_tests.ps1` cobre CASIC, época, referências inválidas,
checksums, whitelist, duplicados, expiração, gate de hora, HTTPS simulado,
tamanho declarado/chunked excessivo, publicação do worker, cache, fase UART,
adiamento durante corrida e erros. O parser C também validou a amostra real
baixada na pesquisa (32 efemérides e um GPS-ION), usando a hora daquela coleta.
`tests/run_gps_tests.ps1` cobre mistura de binário/NMEA, bloqueio da posição
injetada até GGA, sondagem TX sem espera e os fluxos anteriores. As suítes
NTP, gravador e LVGL verificam integração. Mocks/build não substituem aquisição,
consumo, desempenho do TLS/SD nem inspeção do painel no relógio.

Firmware anterior preservado com hashes em
`logs/firmware-references/20261112T171338245952Z-waveshare_esp32_s3_touch_lcd_146/`.

Entrega em 12/11/2026: build padrão, testes A-GNSS/GPS, NTP, gravador/exportação
e integração LVGL padrão/`-GPSStandby` passaram. As capturas de espera, envio e
conclusão simulada foram inspecionadas. A variante experimental reteve mais de
8 KiB de maior bloco livre LVGL com todos os apps e editor, e mais de 4 KiB
com o aviso adicional, sem ampliar o pool. Esses números são do host e não
medem a RAM interna/DMA do ESP32.
O firmware padrão foi gravado na COM3, com quatro hashes verificados e reset
por RTS. Download/TLS no ESP32, aceitação AID-INI, status/ganho de aquisição,
consumo e comportamento do painel permanecem pendentes de teste físico.

As seções seguintes conservam as fontes e resultados da pesquisa de viabilidade.

## Ensaio físico por USB dentro de casa — 12/11/2026

Com o relógio conectado e autorização do usuário para análise, foi usado
temporariamente `gps_assist_diagnostic`, com a mesma otimização/buffers do
padrão e logs apenas pelo USB Serial/JTAG. UART0 de console ficou desativada,
preservando TX43 para o GNSS. Esse perfil abre GPS uma vez no boot e registra
resumos a cada 5 s, sem coordenadas, sem reiniciar o receptor nem gravar sua
configuração. Logs não contam como toque e não mantêm o backlight aceso.
Não é o perfil `display_profile`, cujo console UART0 ocupa GPIO43 e impede A-GNSS.

O novo modo `custom_serial_diagnostics = usb` foi verificado por teste de
configuração, incluindo preservação das políticas anteriores `no/yes/capture`.
Compilaram padrão e diagnóstico, e passaram os testes A-GNSS/GPS. O padrão foi
preservado antes do ensaio em
`logs/firmware-references/20261112T175548713758Z-waveshare_esp32_s3_touch_lcd_146/`.

Captura local: `logs/gnss-usb-20261112T180512Z.log`, aproximadamente 15:05 BRT.

| Etapa observada no relógio | Resultado |
| --- | --- |
| UART | RX44 a 9600 baud, NMEA aumentando e zero erros |
| Aquisição no interior | Sem fix, zero em uso, HDOP 25,5 |
| GSV | GP lista 11, BD lista zero; C/N0 ausente |
| Hora | RTC sincronizado por NTP às 15:05:18 BRT |
| TLS e HTTP | Abertura HTTPS retornou ESP_OK; HTTP 200; comprimento declarado de 3.660 bytes |
| Memória no ponto amostrado após abrir TLS | 103.867 bytes internos livres; maior bloco DMA de 31.744 bytes |
| Assistência | Recusada na validação antes de enviar; 0 frames, sem ACK/NACK ou consulta NAV-STATUS |

A comparação independente com o espelho às 15:07 BRT encontrou o mesmo SHA-256
do arquivo de 14:05 BRT, embora Last-Modified tivesse avançado de 16:10 para
17:10 UTC. O arquivo continuava íntegro, mas 30 das 32 efemérides tinham toe
mais de duas horas atrás (aproximadamente 2,13 h); somente duas estavam na
janela de ±2 h. A regra exige pelo menos quatro. Isso é suficiente para explicar
a recusa por idade, sem atribuí-la à memória, à antena ou a truncamento HTTP.
Last-Modified sozinho realmente não comprova atualização orbital.

Uma consulta somente pelo computador à fonte alternativa
`https://download.openluat.com/9501-xingli/CASIC_data.dat` falhou na negociação
TLS. Não foi adotada nem enviado seu conteúdo ao módulo. Os limites de validade
foram mantidos; não houve relaxamento para fazer o teste passar.

O teste confirmou rede, NTP, TLS e recepção UART no hardware e identificou a
limitação temporal da fonte naquele momento. Não validou envio/aceitação de
AID-INI, ganho de aquisição ou rastreamento dentro de casa. GSV listar satélites
com C/N0 ausente não comprova que seus sinais estejam sendo rastreados.

O perfil temporário é reproduzível com `pio run -e gps_assist_diagnostic -t upload`.
Voltar ao padrão usa `pio run -t upload`; a captura USB deve estar fechada antes.
Ao terminar o ensaio, o padrão foi restaurado na COM3, com quatro hashes
verificados e reset por RTS; o diagnóstico USB não ficou instalado.
Os logs completos são locais/ignorados pelo Git e podem conter dados de rede
dos logs do SDK; não devem ser publicados sem revisão.

## Suporte e protocolo

O [manual do fabricante ATGM336H-5N, seção 1.6, página 5](https://www.tinytronics.nl/product_files/002176_ATGM336H.pdf)
declara A-GNSS para toda a família 5N, com informação orbital, hora e posição
aproximada. A tabela de modelos inclui o 5N-31. O suporte não é a restrição
à família 5L encontrada no experimento anterior de standby por CAS12.

A [especificação CASIC V4.2.0.3](https://www.espruino.com/forumattachments/68b597874e0a617692ebebc6a878032f76b34272.pdf)
descreve `AID-INI` (classe 0x0B, ID 0x01, payload de 56 bytes) para inicializar
posição e tempo, e mensagens orbitais `MSG-GPSEPH` (0x08/0x07, 72 bytes).
É protocolo binário, diferente das sentenças `$PCAS...` usadas até agora.

É necessário verificar as convenções da versão do firmware: o documento CASIC
e a implementação de outro fabricante da plataforma divergem nas unidades de
incerteza de AID-INI. Há também uma divergência publicada na fórmula do checksum;
os pacotes reais baixados permitem verificar a ordem correta de Class/ID sem
depender de tradução ou copiar cegamente uma biblioteca. A implementação deve
ter vetores independentes para checksum, tempo GPS/UTC e validade dos dados.

O manual exige ACK para CFG, sem garantir a mesma resposta para todas as classes.
Portanto, silêncio após envio AID não prova rejeição; ACK, quando disponível,
também não comprova posição. É preciso observar status/efemérides no receptor e
medir tempo até fix real. Uma posição aproximada fornecida não é ponto de corrida.

## Fontes de assistência

A apresentação [Zhongke Micro AGNSS Solution](https://www.espruino.com/forumattachments/b589417f21931e2a2182c6785b846ac64ef64b40.pdf)
descreve serviço TCP em `www.gnss-aide.com:2621`, com usuário/senha e acesso
autorizado. Essa documentação é antiga; a disponibilidade do servidor e as
condições atuais de acesso não foram confirmadas. Nenhuma conta foi usada.

O projeto do relógio Bangle.js 2, também com AT6558, publica uma implementação
de envio pela UART e um espelho HTTPS dos dados CASIC:

- [Código primário de Assisted GPS](https://github.com/espruino/BangleApps/blob/master/apps/assistedgps/custom.html).
- [Arquivo CASIC em Base64](https://www.espruino.com/agps/casic.base64).

Esse espelho é operado pelo Espruino, não um serviço contratado do fabricante.
A cópia observada contém GPS; não traz efemérides BeiDou. Isso não exige
desabilitar BeiDou no receptor. A atualização dos dados deve ser verificada
antes de cada uso, sem tratar um download bem-sucedido como prova de validade.

## Verificação executada no computador

Em 12/11/2026, por volta de 14:05 BRT / 17:05 UTC:

| Verificação | Resultado |
| --- | --- |
| HTTPS do espelho | HTTP 200, certificado verificado pelo cliente padrão |
| Base64 recebido | 3.660 bytes |
| Arquivo decodificado | 2.708 bytes |
| Cabeçalho textual | 58 bytes; conteúdo binário declarado de 2.650 bytes |
| Mensagens orbitais GPS | 32 × MSG-GPSEPH, satélites distintos |
| Mensagem ionosférica | 1 × MSG-GPSION, payload de 16 bytes |
| Checksums externos | 33 válidos; sem bytes restantes após os frames |
| Verificações internas das efemérides | 32 válidas |
| Flags no arquivo | Saúde 0 e validade 3 nos 32 registros |
| Referência temporal orbital (toe) | Aproximadamente 0,38–1,13 h antes da inspeção |

O servidor informou Last-Modified de 12/11/2026 às 16:10:02 UTC. A inspeção
temporal também usou week/toe dos registros, pois Last-Modified sozinho não
comprova que as órbitas estejam atuais. A conversão dessa inspeção assumiu
GPS−UTC de 18 s, com reconstrução da semana módulo 1024; não é uma política
de leap seconds pronta para embarcar.

Arquivos locais de pesquisa: `.pio/host-tests/agnss-research/`, incluindo
`casic.base64`, `casic.bin`, o código consultado, PDF e resumo das efemérides.
Nenhum código baixado foi executado. Apenas o script local de inspeção leu os
dados; nada foi enviado ao relógio e nenhum firmware foi alterado nesta pesquisa.

## Evidência experimental em outro receptor

O [experimento publicado pelo autor de l76k-at6558-agnss](https://github.com/badriram/l76k-at6558-agnss)
mediu um Quectel L76K/AT6558R com URANUS5 V5.3.0.0: cerca de 41 s sem ajuda e
6 s com posição, hora e dados orbitais, em três rodadas ao ar livre. Ele também
observou que hora/posição ou órbitas isoladamente ajudaram menos.

É evidência de funcionamento na plataforma CASIC, não um teste do nosso 5N-31.
A versão informada pelo usuário em `gps.md` é semelhante, mas foi transcrita
sem captura bruta; não prova igualdade do firmware nem de todos os comandos.

## Integração proposta no Chronvs

1. Obter hora confiável por NTP, reutilizando a sessão Wi-Fi exclusiva existente.
2. Obter uma posição aproximada local: último fix válido salvo com idade e
   margem de erro, ou uma referência explicitamente configurada pelo usuário.
   A apresentação do fabricante recomenda erro abaixo de 15 km. Se não houver
   referência confiável, omitir a posição; não inventar coordenadas nem usar
   automaticamente a cidade fixa do Clima. Estar conectado ao Wi-Fi não fornece
   por si só latitude e longitude. Geolocalização por IP pode errar muito.
3. Baixar o pequeno arquivo por HTTPS, impor limite de tamanho e validar
   estrutura, checksums, tipos permitidos e época das efemérides. Cache de dados
   pode ficar em PSRAM/SD; tamanho pequeno do arquivo não elimina o custo de
   RAM interna da conexão TLS. Medir RAM interna e maior bloco DMA durante a operação.
4. Liberar a rede e injetar somente os frames de assistência na UART1 TX43 → RX
   do GNSS, fio já instalado e testado com consulta de versão. O firmware padrão
   atualmente configura apenas RX44; será preciso habilitar TX e preservar a
   exclusão de console no GPIO43.
5. Adicionar recepção binária CASIC ao lado do parser NMEA para inspecionar
   respostas/status, sem misturar os bytes com o parser de linhas. Enviar de
   forma parcelada para não bloquear a tarefa LVGL por toda a transferência.
6. Mostrar estados distintos para download, validação, envio e fix real. Evitar
   reconfiguração persistente, troca de firmware do módulo, reinício a frio
   automático ou interrupção de uma corrida em andamento.

Primeiro validar em um ambiente experimental: referência do firmware preservada,
controle sem ajuda versus com ajuda no mesmo local, tempo até fix e evolução
de GSV/C/N0, aceitação/status do módulo e continuidade após apagar/acordar.
O download e os checksums já foram verificados; aceitação e ganho de aquisição
na unidade do usuário continuam pendentes. A-GNSS não substitui recepção de rádio
nem demonstra que a ausência de fix atual seja causada por falta de efemérides.
