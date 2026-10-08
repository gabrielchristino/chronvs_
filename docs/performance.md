# Diagnóstico de fluidez

## Estado consolidado e última gravação (08/10/2026)

O firmware de uso é `waveshare_esp32_s3_touch_lcd_146`, em release/O2,
sem logs, consoles ou profiling. O mostrador tem três submostradores fixos
(horas, semana e temperatura), sem submostrador de segundos nem contornos
internos; a escala de minutos contém apenas os 12 números. Mantém atualização
ao acordar/voltar e a cada segundo somente sob pressão prolongada.

Os caches da face principal e dos ícones do launcher, a omissão de conteúdo
e a retirada do redraw final continuam restritos aos ambientes experimentais.
O cache dos anéis externos faz parte do padrão. Os relatos e métricas de
experimentos anteriores abaixo não medem o desempenho desta composição atual.

O usuário relatou fechamento acidental ao deslizar no brilho. O painel agora
ignora, para navegação, contatos cujo alvo inicial é o arco, mantendo o ajuste
e o registro de atividade até soltar. Um novo arraste fora do arco fecha.
Contrato e roteiro de teste em [interface.md](interface.md#acessos-rápidos).

Validação desta correção: build padrão aprovado; suíte integrada
`tests/run_Relogio_ui.ps1 -System -Panels` aprovada, incluindo mudança de
valor sem movimento do painel, saída do contorno e fechamento posterior fora
do arco. Upload padrão pela COM3 concluído em 75,86 s, com verificação de
hashes de bootloader, partições, firmware e modelos de voz, e reset automático.
O build enviado reportou 1.586.154 bytes de programa e 52.056 bytes estáticos.
A interação corrigida, a fluidez residual e a autonomia aguardam teste físico;
gravação verificada não equivale a validação funcional.

## Firmware padrão release (08/10/2026)

Por pedido do usuário, o padrão passa de `debug` para `release`, com `-O2`
na aplicação e nos drivers compilados, além de `CONFIG_COMPILER_OPTIMIZATION_PERF`
para os componentes ESP-IDF. LVGL e watchface já usavam `-O2`; anteriormente
o restante usava principalmente `-Og`. As bibliotecas binárias do fabricante
mantêm a otimização com que foram distribuídas. Não foi habilitado fast-math.

Logs da aplicação e bootloader, consoles e profiling ficam desativados.
Também foram desligados os logs da ROM de boot e a integração de depuração
OCD do sistema/FreeRTOS. As verificações de runtime permanecem com assertions
silenciosas; uma falha fatal reinicia sem imprimir diagnóstico. Símbolos no
ELF do computador não são execução de debug nem são gravados como código de
diagnóstico no firmware. Os ambientes `display_profile*` mantêm modo debug
e instrumentação exclusivamente quando selecionados explicitamente.

`scripts/configure_logging.py` aplica essa política mesmo com sdkconfig
existente, preservando as escolhas de hardware e memória. Buffers 1/20,
PSRAM/TLSF, período de 20 ms, QSPI de 2 KiB e espera síncrona permanecem.
O firmware anterior foi preservado em `.pio/diagnostics/before-release-o2/`.
A redução do mostrador foi percebida como mais fluida pelo usuário; o ganho
adicional do release e a regressão funcional ainda requerem teste físico,
incluindo menus, AUTO/ECO, despertar, alarmes, Clima e Vox.

Validação local: build release aprovado e três testes de política de
configuração aprovados, incluindo alternância release/debug e preservação de
memória/hardware. As flags geradas para a aplicação e QSPI são `-O2`, sem
macro de profiling. A auditoria do ELF não encontrou instrumentação Chronvs,
scanner I2C de diagnóstico ou callback de log do Clima. O firmware ficou com
1.586.122 bytes de programa e 52.056 bytes estáticos, contra 1.687.676 e
54.628 no build anterior. Esses números não medem RAM interna livre/DMA em uso.

## P6 — cache experimental dos ícones do launcher (07/10/2026)

Retorno de `panels_no_icons`: somente os textos pareceu igual ao teste sem
conteúdo. Interpretado como ausência de piora perceptível ao restaurar textos;
não é medição individual de custo. Próximo candidato: `launcher_icon_cache`,
derivado de `panels_quiet`, com menus completos e cache somente do launcher.

`CHRONVS_LAUNCHER_ICON_CACHE` habilita snapshot LVGL apenas no experimento.
Na criação do launcher, gera cada ícone de 44 × 44 em RGB565 sobre a cor fixa
do badge. Pixels dessa cor viram chroma key; as bordas antialiasadas permanecem
precompostas. Reserva 3.872 bytes por ícone diretamente em PSRAM, além de
descritores/objetos no pool existente. Não é framebuffer, buffer DMA ou asset
bitmap versionado. Não há reconstrução ao acordar nem reserva por quadro.

O cache só substitui o vetor em linhas totalmente opacas; o fade conserva
os objetos originais. Snapshot inválido, tamanho diferente, colisão com chroma
key ou falha na reserva conserva o vetor. Geometria/paleta fixas são requisito;
essa composição não serve como imagem transparente genérica. Menus completos,
arco, gestos, driver e buffers permanecem. Comparar com `panels_quiet`,
observando launcher separado dos acessos rápidos (estes não usam o cache).

Host: `tests/run_Relogio_ui.ps1 -System -Panels -LauncherIconCache`, com
comparação de capturas contra a referência. Ganho físico ainda pendente.

## P6 — textos restaurados, somente ícones omitidos (07/10/2026)

`panels_no_icons` herda `panels_quiet` e define apenas
`CHRONVS_PANEL_NO_ICONS`. Restaura nomes do launcher e textos dos acessos
rápidos, mantendo omitidos os ícones dos apps e os símbolos Clima/volume.
Círculos, arco, layout e interação permanecem. O símbolo de volume é um
glifo LVGL, classificado pela função visual de ícone; seu número permanece.
A omissão vale em repouso e movimento, sem alternância. Comparar com
`panels_no_content` para avaliar a volta dos textos e com `panels_quiet`
para avaliar os ícones, no mesmo brilho/ECO e com arrastes lentos/rápidos.
Registrar separadamente launcher e acessos rápidos. Buffers/QSPI não mudam.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e panels_no_icons -t upload
```

Host: `tests/run_Relogio_ui.ps1 -System -Panels -PanelNoIcons`. Restaurar
`panels_quiet` para retornar aos menus completos. Ganho físico ainda pendente.

## P6 — isolar ícones e textos (07/10/2026)

O usuário não percebeu diferença com `panels_code_o2`: gelatina persiste.
Não promover essa otimização ao padrão com base no teste perceptivo.

`panels_no_content` herda `panels_quiet`, sem a otimização adicional O2,
e acrescenta somente `CHRONVS_PANEL_NO_CONTENT`. Omite o desenho de nomes
e ícones do launcher e de todos os rótulos e símbolo Clima do painel rápido,
inclusive em repouso. Mantém círculos dos slots/badges, arco, máscara circular,
geometria, layout, áreas de toque, callbacks e acompanhamento do dedo.
A ausência permanece durante todo o teste para evitar que esconder/restaurar
conteúdo introduza invalidações nas fronteiras do movimento. É um diagnóstico,
não a interface proposta para uso; controles continuam ativos sem seus textos.

Comparar abertura/fechamento dos dois menus com `panels_quiet`, no mesmo
brilho/ECO e com arrastes lentos/rápidos. Melhora indicaria contribuição do
conteúdo, sem separar ícones de textos nem provar a causa exclusiva. Persistência
também não prova problema de TE: superfícies, máscaras, mostrador exposto,
transmissão e varredura continuam presentes. Buffers e QSPI não mudam.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e panels_no_content -t upload
```

Para restaurar a interface completa, gravar `panels_quiet`. Teste do host:
`tests/run_Relogio_ui.ps1 -System -Panels -PanelNoContent`.

Retorno físico: o usuário percebeu gelatina menor e identificou melhora em
dois isolamentos: mostrador substituído por fundo plano e menus sem conteúdo.
O resultado é qualitativo, sem tempos/corrente e sem discriminar os dois menus.
O teste atual remove ícones e textos juntos, portanto não atribuir o ganho só
aos ícones. Os relatos indicam contribuição do trabalho de desenho, sem
excluir varredura/transmissão como parte do efeito residual. Próximo isolamento:
restaurar os textos e omitir apenas os ícones, com a mesma referência silenciosa.

## P6 — otimização isolada do código dos painéis (07/10/2026)

O usuário esclareceu que as linhas são partes do próprio menu em posições
diferentes somente durante abrir/fechar; a imagem parada fica correta.
Isso caracteriza atualização parcial visível, sem identificar sua causa.

`panels_code_o2` herda `panels_quiet` e acrescenta somente
`CHRONVS_PANEL_CODE_O2`: `app_list_app.c` e `system_ui.c` usam uma diretiva
GCC local `O2`, como o mostrador. Não habilita fast-math, logs ou profiling.
Inclui a política de energia existente no mesmo arquivo do painel rápido;
por isso a regressão integrada cobre AUTO/ECO, despertar e controles.
Drivers, buffers, QSPI, máscara circular e limites de gesto permanecem iguais.
O padrão e `panels_quiet` conservam a referência sem essa diretiva.

Essa comparação foi preparada antes da adoção de release/O2 no padrão.
Atualmente, os arquivos dos painéis já recebem O2 pela configuração global;
`panels_code_o2` não representa uma nova otimização isolada nesse contexto.

Comparar fisicamente com `panels_quiet`, com mesmo brilho/ECO e arrastes
lentos/rápidos, incluindo cancelamento, ajuste pelo arco e fechamento fora dele. Não há ganho
medido nem promessa de eliminar a gelatina: o custo dominante pode estar no
LVGL já otimizado ou na apresentação das faixas. Para gravar o candidato:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e panels_code_o2 -t upload
```

Regressão do host: `tests/run_Relogio_ui.ps1 -System -Panels`, repetida com
`-PanelCodeO2`, comparando também hashes das capturas BMP. A referência para
restaurar no relógio é `panels_quiet`.

## P6 — repetição sem instrumentação (02/10/2026)

O usuário relatou que ainda percebe efeito gelatina na segunda captura P6
e levantou a hipótese de custo dos logs. As capturas têm refreshes de
78–82 ms mesmo sem desenho do mostrador, mas não isolam o custo dos logs.
Desligar apenas o monitor não remove a instrumentação compilada no firmware.

`panels_quiet` mantém o candidato que omite a invalidação final do painel
rápido, MOTHER vetorial, anéis em cache e LVGL `-O2`. Herda a configuração
do padrão, sem `CHRONVS_DISPLAY_PROFILE`, sem `CHRONVS_PANEL_PROFILE` e com
logs/consoles desativados. Não modifica driver, buffers, gestos ou animação.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e panels_quiet -t upload
```

Não é necessário abrir o monitor. Repetir com o mesmo brilho: abrir/fechar
launcher e acessos rápidos, inverter o arraste, cancelar abertura curta e
ajustar brilho pelo arco e fechar fora dele. Registrar sensação de acompanhamento do dedo, gelatina,
rastros e funcionamento dos gestos. Comparar com `display_profile_panels`
isola conjuntamente profiling/logs; comparar com o padrão silencioso
isola a invalidação final (somente nos acessos rápidos):

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e waveshare_esp32_s3_touch_lcd_146 -t upload
```

Em 07/10/2026, o upload de `panels_quiet` passou, com hashes verificados e
reinício automático. O usuário confirmou menus abrindo e fechando bem
rápido e interface responsiva, mas ainda observa linhas e efeito gelatina.
Portanto, profiling/logs não são condição necessária para esses artefatos;
o relato não quantifica sua contribuição nem o ganho da invalidação final.
A aparência das linhas e sua persistência com o menu parado ainda precisam
ser caracterizadas, assim como o fechamento especificamente pelo arco.
Continuar a decomposição do desenho dos painéis em P6; não há comprovação
de problema de sincronismo/TE nem medição de corrente/autonomia.

## P6 — painéis e fechamento (01/10/2026)

`display_profile_panels_reference` mantém o redraw completo ao terminar de
fechar/cancelar os acessos rápidos. `display_profile_panels` remove somente
essa invalidação explícita, usando as regiões expostas já invalidadas pelo
movimento/ocultação LVGL. Ambos usam mostrador vetorial para MOTHER, anéis
em cache e a mesma instrumentação. O padrão mantém o redraw de referência
até a aceitação física. Não muda duração, coalescência, formato ou gestos.
Os dois ambientes incluem a correção de propagação dos eventos do arco ao
painel, também presente no padrão; assim ela não varia na comparação A/B.

Cada linha `display_perf` acrescenta dois conjuntos, `quick_*` e `launcher_*`:

| Sufixo | Significado |
| --- | --- |
| `frames` | Refreshes com ao menos uma medição completa dessa raiz |
| `slices` | Inícios de desenho da raiz na janela |
| `clip_px` | Soma das áreas retangulares de clip desses inícios |
| `draw_us` | Tempo entre DRAW_MAIN_BEGIN e DRAW_POST_END, incluindo filhos |
| `root_us` | Parte até DRAW_MAIN_END, incluindo desenho da raiz e preparação de máscara |
| `post_only` | Fins de desenho de ancestral cujo início o LVGL omitiu |

`root_us` já está em `draw_us`; não some os dois. A diferença inclui filhos,
recorte, travessia e pós-desenho, não mede exclusivamente ícones ou textos.
As medidas incluem preempções e custo dos marcadores. O flush fica fora do
escopo. `clip_px` não é contagem de pixels coloridos nem união de áreas:
inclui cantos transparentes e pode repetir regiões. Não compare médias de
áreas diferentes como se fossem cenas iguais.

O LVGL pode iniciar o refresh em um filho opaco e enviar somente eventos
POST aos ancestrais. Esses trechos não têm tempo atribuível ao painel e
aparecem em `post_only`; não se inventa uma duração para eles. Os dois
painéis podem participar do mesmo quadro; não some seus frames como se
fossem disjuntos. O analisador usa o denominador de cada painel e informa
cobertura, somas e médias por frame; logs antigos retornam `null`.

Há três callbacks por raiz, vinculados uma vez na criação, e contadores
estáticos para dois painéis. Não há alocação por quadro, nova tarefa ou
timer. O build normal não registra esses callbacks. Ao apagar a tela, as
amostras são descartadas pela política existente de profiling.

Na captura, separe acessos rápidos e launcher. Para os acessos rápidos,
inclua abrir, cancelar uma abertura curta, fechar por botão e fechar pelo
arco de brilho. Repita lento/rápido cinco vezes com o mesmo brilho/ECO.
Use `-f log2file`, sem pipe, como no procedimento de P2 abaixo. Compare
primeiro referência e candidato, ambos instrumentados. Ainda não há ganho
de fluidez/autonomia medido para essa mudança.

```powershell
.\tests\run_Relogio_ui.ps1 -System -Panels -KeepCloseRedraw
.\tests\run_Relogio_ui.ps1 -System -Panels
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_panels_reference
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_panels
```

## P0/P1 e candidato P2 (01/10/2026)

### P2 experimental: face fixa do disco principal

`display_profile_mother` herda `display_profile_primitives` e habilita somente
o cache da face de MOTHER. Compare `watch_mother_face_us` e o total da transição
nos dois ambientes, com cenas e áreas iguais. Não está habilitado no padrão;
P1 e a aceitação física permanecem pendentes. A face vetorial continua como
referência independente nos testes e como fallback no runtime.

O gerador desenha os anéis, guarda a linha anterior e desenha o círculo de
raio 154 pelo LVGL 8.3.11. Em cada linha, encontra os limites dos pixels
modificados e codifica somente esse trecho em RLE RGB565 com fim exclusivo.
São 413 offsets, 412 inícios e 2.083 pares fim/cor: **9.982 bytes** na flash.
A composição reutiliza os 824 bytes já reservados pelos anéis. Não há
reconstrução ao acordar. Ponteiro e centro são desenhados depois, na ordem
original; a escala de minutos continua posterior ao disco.

As bordas antialiasadas incorporam o fundo fixo dos anéis. Isso só é válido
na geometria e paleta atuais, em que as datas ficam fora do disco. Não é um
bitmap transparente genérico. Centro deslocado, máscara externa, outro
renderizador ou ausência da linha temporária mantêm o desenho vetorial.
Trechos totalmente ocultos pelo painel circular são descartados; a área
fora dos trechos não é sobrescrita. Alterações em geometria, cores, formato
RGB565 ou fundo exigem regeneração e nova comparação.

```powershell
.\tests\generate_watch_ring_cache.ps1 -Mother
.\tests\run_watch_optimization_tests.ps1 -MotherCache
.\tests\run_Relogio_ui.ps1 -System -MotherCache
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_mother
```

O gerador escreve em `.pio/host-tests/watch_mother_cache.h` e confere o arquivo
versionado. Para atualizar dados, revise esse arquivo gerado e aplique o diff.
Os testes comparam 142 cenários (60 anteriores, 60 fases estacionárias e 22
posições de painel), além de recortes parciais, deslocamentos, máscara e falta
de memória. O total de pixels deve coincidir exatamente com a referência.

Para a comparação física, use primeiro `display_profile_primitives` e depois
`display_profile_mother`, conservando brilho, perfil e ECO. Em cada um, capture
separadamente: despertar; segurar para atualizar; abrir/fechar/cancelar acessos
rápidos; abrir/fechar/cancelar launcher. Repita cada arraste cinco vezes, lento
e rápido, e registre horário exibido e observação visual. Inclua fechamento
fora do arco e ajuste exclusivo do brilho no contorno. Exemplo para preparar e capturar um cenário:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_primitives -t nobuild -t upload
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' device monitor -e display_profile_primitives -p COM3 -b 115200 -f log2file
```

O filtro `log2file` salva em `logs/device-monitor-YYMMDD-HHMMSS.log` e informa
o caminho ao iniciar, mantendo o terminal interativo. Não use pipe com
`Tee-Object` nesse monitor. Identifique qual arquivo pertence a cada ambiente.
Encerre o monitor antes do próximo upload. Para a segunda execução, troque
o ambiente por `display_profile_mother` nos comandos de upload e monitor.
Passe os dois arquivos ao analisador. Compare `watch_mother_face_us` por
quadro apenas nas janelas de áreas equivalentes; refresh, flush e observação
visual devem ser avaliados separadamente. A instrumentação tem a mesma
política nos dois ambientes. Não adote o cache se o ganho não for repetível
ou houver regressão visual, de memória ou ao acordar. O firmware padrão
preservado continua disponível pelo procedimento abaixo.

### Recuperação e diagnóstico de primitivas

Nesta máquina, o shell herdava `IDF_PATH` de outra instalação. Para os
comandos abaixo, a validação usou o ESP-IDF 5.3.1 do PlatformIO, configurando
somente a sessão de PowerShell:

```powershell
$env:IDF_PATH = Join-Path $env:USERPROFILE '.platformio/packages/framework-espidf'
```

Antes de compilar um experimento, preserve os artefatos existentes:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' scripts/firmware_reference.py --save waveshare_esp32_s3_touch_lcd_146
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' scripts/firmware_reference.py --save display_profile_o2
```

Cada execução cria uma pasta nova em `logs/firmware-references/`, ignorada
pelo Git, com manifesto SHA-256, bootloader, partições, firmware, ELF,
`srmodels.bin`, sdkconfig local e cabeçalho gerado, INI e uploader.
O commit e o status registrados são do workspace no momento da cópia;
não comprovam qual fonte produziu um binário anterior. Não execute enquanto
houver build ativo. Os artefatos binários podem incorporar credenciais:
essas cópias são locais e não devem ser compartilhadas.

Para verificar uma cópia e restaurá-la pelo uploader customizado, use o
caminho exato impresso ao salvar, no lugar de `CAMINHO_DA_REFERENCIA`:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' scripts/firmware_reference.py --verify 'CAMINHO_DA_REFERENCIA'
$env:CHRONVS_FIRMWARE_REFERENCE = 'CAMINHO_DA_REFERENCIA'
try {
    & 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e waveshare_esp32_s3_touch_lcd_146 -t nobuild -t upload
} finally {
    Remove-Item Env:CHRONVS_FIRMWARE_REFERENCE
}
```

A variável seleciona as imagens arquivadas; não substitui arquivos do build.
O uploader verifica o mapa e todos os hashes antes de aceitar a referência.
Preserva os endereços `0x0`, `0x10000`, `0x20000` e `0x420000` para os modelos
de voz. O teste no host confere os argumentos e rejeita cópia corrompida;
uma restauração real ainda precisa ser verificada no relógio.

`display_profile_primitives` herda O2 e a política de logs de
`display_profile_o2`. Acrescenta estes acumuladores em microssegundos:

| Campo `watch_*_us` | Primitivas abrangidas por invocação |
| --- | --- |
| `mother_face` | Círculo de raio 154, preenchimento e borda |
| `mother_hand` | Ponteiro de minutos |
| `mother_center` | Círculo central de raio 4 |
| `hours_face` | Círculo externo de raio 75 |
| `hours_scale` | Seis numerais, letra P e cinco traços; inclui posicionamento |
| `hours_inner` | Campo histórico do círculo interno de raio 55; zero no desenho atual, que removeu esse contorno |
| `hours_hand` | Ponteiro de horas |

Recortes e cobertura podem eliminar primitivas. As contagens acima descrevem
o código, não quantas chegaram ao rasterizador. Os tempos incluem verificações
e preempções; não são tempos exclusivos da rasterização. Os grupos MOTHER/HOURS
continuam abrangendo seus detalhes e a instrumentação: **não some ambos**.
Os temporizadores aninhados não incrementam `watch_frames` ou `watch_slices`.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_primitives
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' scripts/analyze_display_profile.py logs/cenario.log
```

O analisador retorna `watch_primitive_detail_windows`, a quantidade de frames
cobertos e `watch_primitive_detail_avg_us`; logs antigos mantêm valores `null`.
Detalhes incompletos ou cuja soma exceda o grupo pai são rejeitados.
Compare cenas e áreas iguais, com cinco repetições por gesto, entre O2 com e
sem detalhes para avaliar a interferência dos marcadores. O build normal
não executa os novos temporizadores nem imprime esses campos. Não há tarefa,
timer LVGL ou alocação por quadro acrescentados. Esta etapa prepara a escolha
de P2/P3; ainda não reduz o trabalho de desenho do firmware normal.

Para avaliar a bateria de 150 mAh, registre separadamente firmware, brilho,
perfil, ECO, duração de tela ligada, consultas Clima/NTP e uso de áudio/Vox.
Compare ciclos equivalentes no build normal, fora do USB. A estimativa por
tensão mostrada na UI não mede corrente nem energia: tempo de render menor
não permite afirmar um percentual de autonomia. Ainda faltam os ensaios
físicos de desempenho e consumo desta etapa.

A fila atual de trabalho, prioridades, critérios de aceitação e testes
pendentes está no [plano de continuidade](performance-plan.md). A auditoria
registra o histórico; seus antigos “próximos passos” podem já estar concluídos.

## Configuração e histórico da otimização

As adoções e medidas históricas abaixo antecedem a simplificação do mostrador
e o release de 08/10/2026. O estado vigente está no início deste documento.

O ambiente padrão compila o LVGL com `-O2` e não instala callbacks de diagnóstico.
O desenho em `apps/watch_app.c` também usa `-O2`, por uma diretiva GCC
local após os includes. Isso evita que o modo debug do PlatformIO o rebaixe
para `-Og`; não habilita fast-math nem altera opções dos drivers ou outros apps.
`tests/run_watch_optimization_tests.ps1` compara 60 cenários em `-Og`/`-O2`
mantendo o LVGL em `-O2`, exigindo o mesmo SHA-256 dos pixels. O ganho de tempo
desse ajuste na composição atual ainda precisa ser medido no dispositivo.
Os dois anéis externos usam um cache RGB565 comprimido por sequências de cores
na flash (24.666 bytes). Uma linha de 824 bytes no pool LVGL em PSRAM é
descomprimida por vez; não é um framebuffer de tela inteira nem um buffer DMA.
O desenho vetorial é usado se faltar essa alocação, se o mostrador estiver
deslocado ou se houver máscara externa. As cores/medidas do cache são fixas:
mudanças exigem regeneração com `tests/generate_watch_ring_cache.ps1` e revisão
do arquivo produzido em `.pio/host-tests/`. A comparação
`tests/run_watch_optimization_tests.ps1 -RingCache` verifica os pixels contra
o desenho vetorial. Na captura após `1067585`, os anéis caíram para cerca de
5,9 ms e o refresh completo para 166 ms. O usuário confirmou melhora adicional
sem logs, com leve diferença de posição entre regiões durante o arraste ainda
visível. Limites da comparação e trabalho restante estão no plano de continuidade.
Também desativa logs da aplicação/ESP-IDF e do bootloader de software em
compilação (`LOG_DEFAULT_LEVEL=0`, `LOG_MAXIMUM_LEVEL=0`) e os consoles UART
e USB (`ESP_CONSOLE_NONE`, `ESP_CONSOLE_SECONDARY_NONE`). Os `printf` diretos
dos drivers usados são eliminados pela política de compilação aplicada em
`scripts/add_waveshare_drivers.py`, sem avaliar seus argumentos.
Não há REPL da aplicação registrado. As mensagens iniciais da ROM do chip
podem permanecer; não alteramos eFuses, strapping ou o mecanismo de upload.
As bibliotecas pré-compiladas podem conservar código de formatação interno;
desabilitar o console não recompila esses arquivos.
Os ambientes opcionais `display_profile` e `display_profile_o2` permitem
comparar o mesmo fluxo no relógio. O segundo altera somente a compilação dos
fontes LVGL para `-O2`; o primeiro preserva explicitamente o LVGL em debug
(`custom_lvgl_optimization = -O0`, resultando em `-Og` no PlatformIO atual).
Aplicação, ESP-IDF e drivers mantêm as opções anteriores em todos os ambientes.
O workaround histórico do compilador não é removido globalmente.
Embora o INI declare `-O0`, o PlatformIO em modo debug aplica `-Og` nos objetos
inspecionados. Portanto a comparação real é baseline debug versus LVGL `-O2`,
e não se deve inferir ausência total de otimização apenas pelo INI.
Antes da configuração CMake, os dois diagnósticos copiam o `sdkconfig` do
ambiente padrão local, incluindo seus ajustes de pilhas, Wi-Fi e PSRAM.
Depois, `scripts/configure_logging.py` reativa logs INFO e os consoles UART/USB
somente nos diagnósticos. Assim, a comparação entre os dois diagnósticos tem
a mesma política de logs; o padrão não paga esse custo de saída. O script
aplica a política também a `sdkconfig` já existente, sem alterar opções de hardware.
Compile primeiro o ambiente padrão; alterações manuais nos `sdkconfig` dos
diagnósticos são substituídas na próxima compilação.

Ambos preservam atualização de 20 ms, buffers duplos de 1/20 em PSRAM,
pool TLSF de 128 KiB em PSRAM, transferências de 2 KiB, espera síncrona QSPI
e debounce do touch. Não há alteração visual ou de gesto.
O scanner de endereços I2C do boot é executado somente nos diagnósticos;
o padrão inicializa normalmente os dispositivos, sem sondagens extras.

O cache de máscaras circulares do LVGL usa oito entradas (`LV_CIRCLE_CACHE_SIZE`),
para reutilizar raios entre as faixas de um mesmo refresh. Os dados de
antialiasing são alocados no pool TLSF existente em PSRAM e liberados pelo
LVGL ao terminar o refresh. Não há cache de framebuffer. A comparação com
quatro entradas está em `tests/run_watch_render_tests.ps1`: verifica SHA-256
dos pixels, recálculos de máscaras e memória amostrada durante o desenho.
O ganho de tempo no dispositivo ainda aguarda medição; veja a
[análise do cache](performance-audit.md#cache-de-máscaras-circulares).

O recorte experimental do interior coberto dos círculos foi retirado após
a captura física mostrar aumento de custo. Permanecem as duas primitivas
originais com o cache de oito raios. A comparação de pixels continua nos
testes; o [histórico do experimento](performance-audit.md#recorte-do-interior-coberto-dos-círculos)
registra por que reduzir preenchimento não garantiu menor tempo de desenho.
Após a retirada, o usuário confirmou a fluidez e os logs mostraram retorno
dos círculos a cerca de 57 ms e refresh de 227–230 ms em repouso. Veja a
[confirmação física](performance-audit.md#recuperação-confirmada-após-retirar-o-recorte).

O launcher interrompe o desenho dos nomes com opacidade zero,
preservando as linhas no layout, os ícones e o fade original quando visíveis.
A implementação e os limites da validação estão na
[análise do launcher](performance-audit.md#launcher-interromper-desenho-de-nomes-totalmente-transparentes).

## Comparação no dispositivo

### Isolamento do fundo durante arrastes circulares

O ambiente `display_profile_flat` herda o diagnóstico O2 e troca somente o
desenho do mostrador por fundo liso escuro. O mostrador fica sem hora visível neste
experimento. Mantém eventos, invalidações, painéis circulares, gestos, buffers,
transferência QSPI e logs. O serviço de horário e os alarmes continuam ativos.
O boot identifica `watch_background=flat`; o diagnóstico normal identifica
`watch_background=orbital`, nome histórico mantido no protocolo mesmo com
submostradores fixos. O padrão de uso diário mantém o mostrador completo e silencioso.

Compare `display_profile_o2` e `display_profile_flat`, ambos com ON e ECO
desligado, após terminar o NTP. Em cada versão, repita abertura/fechamento dos
atalhos e launcher, lentamente e rapidamente, incluindo cancelar a abertura.
Separe cada sequência por 3 s sem tocar. Registre se a borda deforma e compare
`refresh_avg_ms`, `render_max_us`, `flush_avg_us` e `px_avg` em gestos equivalentes.
No fundo liso, as seções de órbitas ficam zeradas; setup e background permanecem.

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_flat -t upload
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' device monitor -e display_profile_flat -p COM3 -b 115200
```

Para voltar à comparação com o mostrador completo, use `run -e display_profile_o2 -t upload`.
Para uso diário sem logs, use `run -t upload`. Encerre o monitor com Ctrl+C
antes de trocar o firmware. Melhora com fundo liso indica contribuição do
desenho do mostrador; persistência do efeito não comprova sozinha falha de sincronismo,
pois o próprio painel circular ainda exige renderização.

### Procedimento geral

O mostrador agora fica estático em repouso. Segure por 600 ms para atualizar
imediatamente e depois a cada segundo até soltar. Para comparar o custo de
desenho com capturas antigas, mantenha essa pressão; ausência de resumos
`display_perf` quando não há frames é esperada. A mudança reduz a frequência
de desenho, não demonstra redução do custo individual de cada quadro.

Use o PlatformIO instalado em `C:\Users\gabri\.platformio\penv\Scripts\platformio.exe`
se `pio` não estiver no PATH. Execute na raiz:

```powershell
pio run -e display_profile -t upload
pio device monitor -p COM3 -b 115200
```

Após terminar NTP, repita durante cerca de 15 segundos cada etapa:
mostrador parado; abrir/fechar painel rápido; abrir/rolar/fechar launcher;
abrir/voltar de apps. Identifique as etapas junto aos logs. Feche o monitor
antes de outro upload. Repita os mesmos gestos, brilho e condições com:

```powershell
pio run -e display_profile_o2 -t upload
pio device monitor -p COM3 -b 115200
```

Valide também Clima com rede, aviso de timer com som e apagar/acordar.
Se surgirem listras, travamentos ou regressão de toque, registre a etapa e
retorne à referência debug instrumentada para isolar a otimização:

```powershell
pio run -e display_profile -t upload
```

Para uso normal, com LVGL otimizado e sem os logs de diagnóstico:

```powershell
pio run -e waveshare_esp32_s3_touch_lcd_146 -t upload
```

Cada ambiente possui sua própria pasta `.pio/build/<ambiente>`; os builds
experimentais não substituem os binários do ambiente padrão. Não execute
builds simultâneos: o script de preparação compartilha a referência Waveshare.

## Leitura dos logs

### Captura e resumo por cenário

Com o firmware `display_profile_o2` gravado, aguarde terminar o NTP e abra
o monitor abaixo. O filtro instalado `log2file` salva a saída em
`logs/device-monitor-<data-hora>.log` e imprime o caminho ao iniciar.
Essa pasta é ignorada pelo Git. O padrão sem diagnóstico não produz essas métricas.

```powershell
pio device monitor -e display_profile_o2 -p COM3 -b 115200 -f log2file
```

Faça uma captura separada de cerca de 15 s para cada cenário: mostrador
parado, abrir/fechar painel continuamente, rolar launcher continuamente.
Encerre com Ctrl+C entre capturas e anote qual arquivo corresponde a cada
cenário. Remova do arquivo de análise as linhas de transição entre cenários;
um resumo representa a janela inteira anterior, de aproximadamente 2 s.
Mantenha brilho, perfil de energia e ritmo dos gestos comparáveis. Feche o
monitor antes de trocar firmware. A coleta requer interação física no relógio.

Para manter a captura USB contínua, selecione temporariamente o perfil `ON`
nos acessos rápidos e desligue `ECO` (ele prevalece sobre ON). Restaure o
perfil anterior ao terminar e use a mesma configuração nas comparações.
Ao apagar a tela, o firmware entra em light sleep explícito; o USB Serial/JTAG
usado pelo diagnóstico pode perder a conexão e não retornar ao despertar,
limitação descrita no Kconfig do ESP-IDF instalado. A opção de bloquear
light sleep automático via USB não bloqueia essa chamada explícita.
A queda da COM3 sozinha não comprova travamento ou reset. Testes de suspensão
devem ser separados dessa coleta de desempenho com tela ligada.

Analise os arquivos (substituindo os nomes pelos caminhos salvos):

```powershell
python scripts/analyze_display_profile.py logs/mostrador.log logs/painel.log logs/launcher.log
```

Se Python não estiver no PATH, use
`C:\Users\gabri\.platformio\penv\Scripts\python.exe` com `&` no PowerShell.
O relatório JSON separa arquivos e a otimização informada no banner, quando
presente; `unknown` significa que o banner não foi capturado. Não deduz o
build a partir da velocidade observada. Use arquivos distintos também para
builds ou sessões diferentes.

- As médias de refresh, flush e pixels são ponderadas por quadros; o sufixo
  `_approx` lembra que o firmware já arredondou cada média para baixo.
- `refreshes_per_observed_second` considera apenas as janelas presentes no
  arquivo. Não é FPS máximo nem média de toda a sessão: intervalos apagados,
  sem redraw e logs ausentes não entram no denominador.
- Tempos máximos preservam o maior valor; memória usa o menor valor amostrado.
  Os mínimos não estabelecem um limite seguro de RAM/DMA.
- `touch_windows` informa quantas janelas contêm as métricas novas de touch.
  Logs antigos sem esses campos produzem `null`, não uma falsa medição zero.
- Linhas incompletas/inconsistentes geram aviso e saída com código 1; as
  janelas válidas ainda são resumidas. Um arquivo sem dados também falha.
  Perdas de linhas inteiras ou truncamento que preserve números válidos não
  são detectáveis sem numeração/checksum no protocolo.

A ferramenta não conecta à serial nem altera firmware. Validação:
`python tests/test_display_profile_analysis.py`, usando dados sintéticos para
ponderação, extremos, arquivos UTF-8/UTF-16, logs antigos e entradas inválidas.

### Campos emitidos pelo firmware

Os diagnósticos também separam o desenho vetorial do mostrador. Estes campos
ficam na mesma linha `display_perf` e são **totais em microssegundos por
janela**, não médias por faixa:

| Campo | Trecho medido |
| --- | --- |
| `watch_setup_us` | Contexto, recorte de superfícies opacas e atualização do cache |
| `watch_background_us` | Limpeza opaca do fundo |
| `watch_geometry_us` | Ângulos e preparação geométrica; centros atualmente fixos |
| `watch_case_us` | Aros externos e números da data |
| `watch_rings_us` | Somente os dois círculos externos, com preenchimento e borda |
| `watch_dates_us` | Somente os 31 números da data |
| `watch_mother_us` | Disco central, ponteiro de minutos e centro |
| `watch_minutes_us` | Escala de minutos; atualmente somente os números |
| `watch_hours_us` | Submostrador de horas |
| `watch_weekday_us` | Submostrador de dia da semana |
| `watch_temperature_us` | Submostrador de temperatura |
| `watch_seconds_us` | Campo histórico; zero após retirada do submostrador de segundos |
| `watch_marker_us` | Marcador fixo da data |

`watch_case_us` é a soma de `watch_rings_us` e `watch_dates_us`, preservada
para comparação com logs anteriores. Esses dois detalhes **não devem ser
somados novamente ao total**. O analisador os apresenta separadamente em
`watch_case_detail_avg_us`, com cobertura em `watch_case_detail_windows` e
`watch_case_detail_frames`. Só janelas com ambos os detalhes entram nessas
médias; logs antigos retornam `null`. Detalhes incompletos ou cuja soma difere
de `watch_case_us` tornam a linha inválida. O marcador triangular da data
continua em `watch_marker_us`, fora dessa soma.

`watch_slices` conta entradas no callback customizado, inclusive as que saem
por estarem cobertas. `watch_frames` conta atualizações concluídas em que ele
foi chamado, no máximo uma vez por refresh, mesmo com várias faixas.
Não é uma contagem de telas inteiras nem prova visibilidade no painel.
O analisador soma cada grupo e divide por `watch_frames`, produzindo
`watch_section_avg_us`. Janelas sem mostrador não diluem a média; logs antigos
ou capturas sem quadros do mostrador retornam `null`. `watch_windows` informa
a cobertura desses campos na captura.

Os tempos incluem preempções e pequeno custo da instrumentação. Os grupos
principais não se sobrepõem (os detalhes de `case` já estão incluídos nele),
mas sua soma não inclui todo o refresh LVGL nem o flush QSPI.
Não some esses valores novamente ao tempo de refresh. Os marcadores não
criam tarefa, alocação ou saída serial por faixa; o resumo continua a cada 2 s.
No build padrão, macros eliminam os marcadores e seus acessos ao relógio.

Para localizar o custo principal, repita o upload de `display_profile_o2` e
capture primeiro pelo menos 15 s do mostrador parado, depois painel/launcher
em arquivos separados. Envie também o resultado do upload, pois o parâmetro
`-e` do monitor não comprova o firmware gravado. Não compare pequenos ganhos
com uma captura sem essa instrumentação adicional como se o custo fosse igual.

O prefixo `display_perf` identifica resumos emitidos no máximo uma vez a cada
2 segundos, sem logging por frame ou por bloco QSPI:

- `window_ms`, `frames`: duração da janela e atualizações efetivas. Poucos
  frames em repouso são esperados; isso não mede a capacidade máxima do painel.
- `refresh_avg_ms`, `refresh_max_ms`: tempo LVGL de atualização, incluindo
  layout, desenho e flush síncrono, com resolução do tick LVGL.
- `over20`: atualizações cujo tempo excedeu 20 ms; não é contagem de frames perdidos.
- `flush_avg_us`: soma dos tempos dos flushes dividida pelas atualizações,
  incluindo chamadas do driver, envio e espera QSPI.
- `px_avg`: pixels processados por atualização, útil para comparar a carga.
- `internal_free`, `dma_largest`: RAM interna livre de 8 bits e maior bloco
  interno apto a DMA, em bytes, amostrados ao emitir o resumo.
- `touch_reads`, `touch_read_max_us`: chamadas ao leitor do touch e maior
  duração de uma chamada, incluindo o backend I2C quando ele é executado.
- `touch_gap_max_us`: maior intervalo entre leituras consecutivas enquanto
  o contato permanece pressionado.
- `interaction_frames`, `frame_gap_max_us`: atualizações concluídas durante
  contato e maior intervalo entre elas no mesmo contato. Um dedo parado
  pode não exigir novos quadros; intervalos longos não provam travamento.
- `input_refresh_max_us`: maior espera da primeira mudança de posição/estado
  ainda pendente até a próxima atualização concluída. Essa atualização pode
  ter outra causa; não mede latência física nem prova resposta ao gesto.
- `render_max_us`: maior duração entre `render_start_cb` e `monitor_cb`,
  incluindo desenho e flush síncrono; exclui layout anterior ao início do
  desenho. O callback anterior do driver é preservado.
- `render_idle_max_us`: maior intervalo entre a conclusão de um desenho e
  o início do seguinte na janela. Inclui tempo sem invalidações, limpeza,
  outros timers, tarefas e espera; não significa sozinho bloqueio da UI.
- `motion_reads`: leituras com coordenadas diferentes da leitura anterior
  durante o mesmo contato pressionado. Pressionar/soltar e contato parado
  não incrementam esse contador; ruído de coordenadas também pode contar.
- `motion_frames`: quadros concluídos cujo início encontrou movimento lido
  desde o desenho anterior. Várias leituras são agrupadas num quadro; pode
  contar um quadro após soltar e não prova que o movimento o causou.
- `motion_age_max_us`: maior idade da **última** mudança de coordenadas ao
  começar o desenho, entre os quadros acima. Não mede desde o primeiro
  movimento nem desde a invalidação e pode incluir espera sem novo desenho.

Esses cinco campos aparecem juntos nos novos logs. O analisador retorna
`render_windows` como cobertura, soma as contagens e preserva os máximos;
capturas antigas retornam `null`. Campos incompletos ou contagens de quadros
de movimento superiores a quadros totais/leituras de movimento são rejeitados.
Os máximos podem vir de quadros diferentes: não some `render_idle_max_us`,
`motion_age_max_us` e `render_max_us` para deduzir uma latência individual.

A diferença entre refresh e flush é apenas uma estimativa do trabalho fora
do driver; os relógios têm resoluções diferentes e incluem preempções.
Não mede latência completa do toque, tempo dos demais timers ou abertura
do app fora do refresh. O logging também tem custo: compare ambos os builds
instrumentados e confirme o resultado final sem instrumentação.
Com tela apagada, as amostras são descartadas e não há resumo nem consulta
de heap. Nenhum timer ou tarefa extra é criado.
As métricas de touch usam o callback original, sem alterar sua cadência de
30 ms nem o debounce. Contadores e intervalos são reiniciados por janela;
janelas sem atualizações não emitem resumo. Compare os intervalos durante
arrastes contínuos equivalentes, separando repouso, painel e launcher.

Compilação bem-sucedida, isoladamente, não confirma ganho de fluidez ou
estabilidade física. Novas alterações continuam exigindo comparação no painel.

## Validação de preparação

Os três ambientes compilaram com sucesso. As opções ativas dos `sdkconfig`
dos diagnósticos foram comparadas com o padrão local, sem diferenças.
A informação de compilação dos objetos confirmou `-Og` no LVGL de referência
e `-O2` no LVGL experimental; os objetos inspecionados de `main.c` e do driver
LVGL mantiveram `-Og` em ambos.

## Validação física e adoção no padrão

O usuário relatou que `display_profile_o2` ficou perceptivelmente mais fluido
e leve. Depois confirmou funcionamento nos fluxos propostos: Clima com rede,
timer com som, apagar/acordar repetidamente e abrir, rolar e fechar aplicativos.
Com essa validação, `-O2` foi adotado somente no LVGL do ambiente padrão,
sem habilitar `CHRONVS_DISPLAY_PROFILE`. Os diagnósticos continuam disponíveis
para comparação com a referência debug.

Após o upload do firmware padrão com LVGL em `-O2`, sem instrumentação e com
logs/consoles desativados, o usuário confirmou em 24/09/2026 que o relógio
está "extremamente fluido". Essa configuração passa a ser a base validada
para as próximas mudanças. Não houve alteração dos parâmetros do painel.

O ganho é qualitativo, relatado no dispositivo; não foram fornecidos logs
para quantificar FPS, latência ou autonomia, nem isolar o ganho adicional da
retirada de logs. O texto do Vox e demais informações da interface continuam
disponíveis na tela.

Validação da política de logs: os três builds passaram, e os dois testes de
`tests/test_logging_config.py` verificaram alternância, preservação das opções
de hardware, herança e reaplicação sem reescrita desnecessária. O cabeçalho de
configuração gerado confirmou níveis zero e consoles desativados no padrão.
Mensagens representativas de boot, navegação, Vox, drivers e profiling ficaram
ausentes do binário padrão e presentes nos diagnósticos (onde aplicável).
O binário padrão passou de 1.775.120 para 1.659.376 bytes, cerca de 113 KiB a
menos; essa redução não é uma medição de fluidez ou autonomia.

## Validação das correções de runtime

Em 24/09/2026, o usuário confirmou "tudo certo nos meus testes" após as
correções da espera do loop, contagem do cronômetro durante suspensão,
redraws em pausa e ciclo de vida/captura do Vox. Os defaults também foram
alinhados à configuração local validada. Essa revisão é a nova base física,
sem mudança nos parâmetros críticos do display e sem medição quantitativa
de desempenho. A análise, os testes automatizados e a pendência de memória
no teste integrado do host estão em [`performance-audit.md`](performance-audit.md).

Na revisão seguinte, estilos compartilhados e callbacks restritos a alvos
clicáveis resolveram a falha de memória da suíte integrada, mantendo o pool
de 128 KiB. Build e suítes de interface passaram. O usuário confirmou no
relógio: "tudo perfeito nos meus testes aqui". Essa é a nova base validada;
os números de memória do host estão na análise e não representam uma medição
equivalente no ESP32.

Também foram validadas no relógio a espera do áudio por notificações, em
vez de polling ocioso, e a consulta da agenda civil uma vez por segundo.
Timer e soneca preservam seus prazos monotônicos em cada passagem do loop.
Build e testes de serviço passaram; a redução de conversões da agenda foi
medida no host, sem quantificar ganho de autonomia no dispositivo.

O NTP passou a usar uma tarefa temporária por sessão, liberando a pilha
entre sincronizações. Build e testes de NTP/Clima/Wi-Fi passaram, e o usuário
confirmou "tudo certo nos testes" no relógio. O relato não discrimina cada
cenário nem mede memória recuperada; os limites estão na análise de desempenho.

A consolidação das atualizações do mostrador, o cache de estilos do launcher,
a consulta RTC por minuto e a retirada do scanner I2C do padrão também foram
testados no relógio pelo usuário: "tudo funcionando perfeito". Os três builds
e os testes de RTC/interface passaram. A próxima etapa é a captura das métricas
de touch e renderização descritas acima; ainda não há medidas físicas para
justificar alterações na cadência do touch ou quantificar o ganho desta revisão.
