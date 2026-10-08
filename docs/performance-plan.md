# Plano de continuidade de desempenho

Atualizado em 08/10/2026. Base do firmware atual: `060e714`, em `develop`,
com a proteção do arco de brilho e os experimentos pendentes deste workspace.
Este documento reúne o trabalho restante da frente de fluidez do mostrador
e dos painéis circulares. É a fila atual; as seções antigas de “próximo passo”
em [performance-audit.md](performance-audit.md) são histórico, não uma lista
de tarefas ainda abertas. Não inclui funcionalidades novas dos aplicativos.

## 1. Resultado esperado e situação atual

Preservar o mostrador atual com submostradores fixos e melhorar a abertura, o fechamento
e o cancelamento dos painéis, com prioridade para os acessos rápidos.
O acompanhamento do dedo continua sendo o comportamento vigente.
Não existe compromisso de atingir 50/60 FPS nem de eliminar completamente
a diferença visível de posição entre regiões da tela.

Já concluído nesta frente:

- Firmware padrão release/O2; build de uso sem logs/consoles ou profiling.
- Submostradores fixos de horas, semana e temperatura; retirada do mostrador
  de segundos, dos contornos internos e dos traços intermediários de minutos.
- Proteção do arraste de brilho: contato iniciado no arco não fecha o painel.
  Build, teste integrado e upload aprovados; confirmação física pendente.
- Mostrador atualizado ao acordar/voltar e sob pressão prolongada, evitando
  rotação periódica desnecessária quando a imagem está em repouso.
- Coalescência visual dos gestos em 20 ms e descarte conservador do desenho
  oculto pelos painéis circulares.
- Oito entradas no cache de máscaras circulares e descarte de nomes do
  launcher com opacidade zero.
- Diagnóstico com fundo liso para isolar o custo do mostrador.
- Cache RLE dos dois anéis externos: 24.666 bytes constantes na flash e
  uma linha de 824 bytes no pool LVGL em PSRAM, sem framebuffer completo.
- Comparação de 60 cenários e 22 capturas circulares sem mudança de pixels
  para esse cache; testes de retorno ao vetor com máscara externa e sem
  memória temporária; builds normal e `display_profile_o2` aprovados.

O usuário confirmou melhora com o cache. Depois confirmou que o build sem
logs melhora bastante, mas ainda apresenta um efeito leve de “gelatina”:
durante o arraste, uma parte parece mais avançada que outra.
Isso é compatível com atualização parcial visível, mas ainda não identifica
se a causa residual é a varredura do painel, a sequência de faixas transmitidas,
a cadência de desenho ou uma combinação. Não foi comprovado defeito geométrico.

## 2. Referência física disponível

Em 08/10/2026, foi gravado o firmware padrão release com a proteção do arco,
hashes verificados e reset automático. A simplificação do mostrador foi
percebida como mais fluida pelo usuário; a nova interação do brilho ainda
aguarda retorno. As comparações abaixo são históricas, anteriores à
composição simplificada. Novas otimizações devem usar o padrão atual como
referência e não atribuir a ele os tempos da versão orbital.

Teste silencioso em 07/10/2026: `panels_quiet` gravado com sucesso e hashes
verificados. O usuário confirmou abertura/fechamento rápidos e boa resposta,
mas linhas e gelatina persistem sem profiling/logs. Não há comparação pareada
que quantifique ganho ou custo da instrumentação. Caracterizar as linhas em
movimento/repouso e continuar P6; não promover o candidato como solução dos
artefatos nem atribuir a causa a TE sem isolamento.

Esclarecimento posterior: as faixas são partes do menu em posições diferentes
somente durante abrir/fechar; a imagem parada está correta. O próximo candidato
isolado é `panels_code_o2`, que otimiza os dois arquivos dos painéis sem mudar
LVGL, driver ou desenho. A regressão integrada passou nas duas variantes e
148 capturas BMP tiveram hashes idênticos. Ganho físico permanece pendente;
comparar com `panels_quiet` antes de promover qualquer alteração ao padrão.

Retorno posterior de `panels_code_o2`: nenhuma melhora perceptível, gelatina
persiste. Próximo isolamento P6: `panels_no_content`, omitindo apenas ícones e
textos e mantendo superfícies, máscaras e interação. Comparar com
`panels_quiet`; detalhes e restauração em `performance.md`. Persistência nesse
teste não autoriza atribuir a causa a TE sem isolar o trabalho remanescente.

Retorno físico de `panels_no_content`: gelatina pareceu menor. O usuário
também confirmou melhora no teste anterior com fundo plano. Priorizar redução
do custo de desenho preservando o visual; antes de escolher caches ou alterar
conteúdo, separar ícones de textos (próximo teste: textos restaurados, ícones
omitidos). As duas melhorias são qualitativas, não quantificam ganhos nem
demonstram causa exclusiva. O padrão mantém mostrador e menus completos.

Preparado `panels_no_icons`: textos restaurados e ícones omitidos, conforme
autorização do usuário. Comparar separadamente launcher e acessos rápidos
com as referências completas e sem conteúdo; procedimento em `performance.md`.

Retorno dos textos sem ícones: percepção igual ao teste anterior, interpretada
como ausência de piora ao restaurar textos. Próximo candidato autorizado:
`launcher_icon_cache`, menus completos e snapshots pequenos em PSRAM apenas
nas linhas opacas. Vetor no fade/falhas. Testes e 148 capturas preservaram os
pixels, inclusive com reserva falhando; ganho físico permanece pendente.

Captura enviada após `1067585`: arquivo anexado de identificador
`117860ef-60ed-441b-a258-682f862a93a4`, trecho 08:02:10–08:03:51,
ambiente `display_profile_o2`. O relato posterior sobre o build normal é
qualitativo; não há medição numérica do custo removido ao desativar diagnóstico.

| Medida | Antes do cache | Com cache | Limite da comparação |
| --- | --- | --- | --- |
| Refresh completo, 169.744 pixels | 236–242 ms | aproximadamente 166 ms | Capturas distintas; não é ensaio pareado de gestos |
| Dois anéis externos | cerca de 60 ms/quadro | aproximadamente 5,9 ms/quadro | Comparar quadros completos, normalizando os acumuladores |
| Flush de quadro completo | cerca de 17 ms | cerca de 17 ms | Está incluído no tempo total, não somar novamente |
| RAM interna livre, captura atual | — | 142.695–142.783 bytes | Observação, não orçamento mínimo seguro |
| Maior bloco DMA, captura atual | — | 38.912 bytes | Não comprova folga durante TLS/áudio |

A redução observada do refresh completo é de aproximadamente 30%; não
equivale a aumento de FPS de toda a navegação. Há janelas sem desenho do
mostrador com médias de 76–81 ms e máximos perto de 90 ms: os painéis também
têm custo próprio. Janelas mistas têm áreas e quantidades de quadros diferentes.

Os campos `watch_*_us` acumulam tempo na janela. Por exemplo, às 08:02:50,
`watch_frames=2` e `watch_rings_us=11776` correspondem a 5.888 µs por
quadro nessa janela de dois quadros completos. Não interpretar o acumulado
como custo de um único quadro nem dividir pelos frames de outros apps.

| Grupo do perfil | Desenho abrangido no código | Custo aproximado por quadro completo |
| --- | --- | --- |
| `MOTHER` | Disco principal, ponteiro longo de minutos e centro | 28 ms |
| `HOURS` | Submostrador de horas, incluindo face, escala e ponteiro | 26 ms |
| `MINUTES` | Escala fixa de minutos: números e traços | 15 ms |
| `WEEKDAY` | Submostrador semanal e seus indicadores | 14 ms |
| `TEMPERATURE` | Submostrador de temperatura | 13 ms |
| `DATES` | Os 31 números do anel de datas | 12 ms |
| `RINGS` | Dois anéis externos já armazenados em RLE | 5,9 ms |
| `SECONDS` | Submostrador de segundos | 4 ms |
| `MARKER` | Marcador fixo | 2,4 ms |
| `GEOMETRY` | Cálculos instrumentados de posições/ângulos | 1 ms |

São prioridades de investigação, não uma decomposição por primitiva.
`MOTHER` não mede apenas um círculo; `MINUTES` não mede uma “órbita de minutos”.
Não somar `CASE` com `RINGS` e `DATES`: `CASE` já é a soma desses dois grupos.

## 3. Ordem de execução

| ID | Prioridade | Entrega | Estado |
| --- | --- | --- | --- |
| P0 | Imediata | Referência reproduzível e preservação do firmware atual | Artefatos locais preservados e restauração verificada no host; ensaio físico pendente |
| P1 | Alta | Separar o custo interno de MOTHER e HOURS | Capturas de vetor/cache disponíveis; interferência da instrumentação ainda não isolada |
| P2 | Alta | Otimizar a parte fixa do disco principal | Redução localizada medida; percepção de fluidez não confirmada; permanece experimental |
| P3 | Alta | Otimizar o submostrador de horas | Pendente, depende de P1/P2 |
| P4 | Média | Escala de minutos e números da data | Traços intermediários removidos; custo dos números ainda a medir na composição atual |
| P5 | Média | Submostradores semanal e de temperatura | Pendente |
| P6 | Alta, em execução | Custo próprio dos painéis e invalidações | Isolamentos de conteúdo concluídos; cache de ícones pendente de teste físico; brilho protegido no padrão |
| P7 | Condicional | Isolar a atualização parcial residual e investigar TE | Sem implementação definida |
| P8 | Alternativa de interação | Transição curta após reconhecimento do gesto | Somente se necessária e escolhida pelo usuário |
| P9 | Fechamento | Regressão integrada, memória e documentação final | A cada etapa e ao concluir |

Executar uma mudança de comportamento ou desenho por experimento. Reordenar
P4/P5/P6 se as novas medições mostrarem outro gargalo. Uma hipótese descartada
com evidência encerra a investigação correspondente; não é obrigatório
implementar um cache para cada elemento.

## 4. P0 — referência, diagnóstico e recuperação

Em 01/10/2026, os artefatos locais padrão e `display_profile_o2` foram
preservados em `logs/firmware-references/`, fora de `.pio/build`, com SHA-256,
ELF, configurações locais/geradas e modelos de voz. O manifesto identifica
o commit do workspace, sem afirmar que ele prova a origem do binário.
O procedimento e a seleção da cópia pelo uploader estão em `performance.md`.
Ainda não houve restauração física nem nova captura por cenário.

- [x] Preservar, antes de novo experimento, bootloader, partições, firmware,
  ELF, ambiente, commit e hashes da versão de referência. Guardar fora do
  caminho que o próximo build sobrescreve, em diretório local ignorado.
- [x] Registrar também configurações efetivas relevantes; o `sdkconfig`
  local usado como referência dos diagnósticos não deve ficar implícito.
  Não copiar credenciais para material compartilhável.
- [ ] Confirmar que existe procedimento de restauração usando o uploader
  customizado e os endereços corretos; não depender apenas de reconstruir
  um commit antigo com configurações locais diferentes.
- [ ] Separar capturas por cenário: despertar, pressão prolongada, launcher,
  acessos rápidos e cancelamento. Usar o mesmo brilho, perfil de energia,
  estado do ECO e, quando possível, o mesmo horário exibido.
- [ ] Repetir cada arraste ao menos cinco vezes, incluindo lento e rápido;
  registrar observação visual separada dos tempos internos.
- [ ] Manter O2 e a mesma política de diagnóstico nos dois lados de uma
  comparação de código; repetir no build normal para aceitação de uso.
- [ ] Se for necessário quantificar o custo dos logs, preparar diagnóstico
  experimental que acumule contadores e só os descarregue após a interação.
  Separar custo de instrumentação do custo de saída serial, sem alocar por
  quadro e sem logs por faixa. Esse experimento ainda não existe.

Saída: referência identificável, resultados por cenário e restauração pronta.
O relato sem logs já é válido como percepção de uso; não pedir novamente
o mesmo teste sem uma pergunta técnica nova.

## 5. P1 — localizar as primitivas caras

O ambiente `display_profile_primitives` acrescenta sete temporizadores
aninhados, sem alterar pixels ou os contadores de quadros/faixas. O ambiente
`display_profile_o2` mantém os marcadores anteriores para comparar o custo
da instrumentação. O padrão não executa esses temporizadores. Detalhes e
limites em `performance.md`. P2/P3 continuam dependentes das medidas físicas.

Arquivo principal: `src/apps/watch_app.c`. Diagnóstico:
`src/platform/display_profile.c` e respectivo cabeçalho.

- [x] Separar temporariamente o círculo/borda, ponteiro e centro em MOTHER.
- [x] Separar face/borda, numerais/traços e ponteiro em HOURS.
- [ ] Medir quadros completos e trechos parcialmente encobertos, sem comparar
  diretamente médias de áreas diferentes.
- [ ] Verificar se a instrumentação adicional altera perceptivelmente o
  custo; manter os marcadores apenas no ambiente de diagnóstico.
- [ ] Registrar custo, quantidade de primitivas e memória antes de escolher
  a representação. Cálculos geométricos de cerca de 1 ms não são o primeiro alvo.

Saída: decisão fundamentada sobre o primeiro elemento a otimizar. Não assumir
que todo o tempo de HOURS seja rasterização de círculo ou de texto.

## 6. P2 — parte fixa do disco principal

Comparação atualizada após a captura vetorial: quatro janelas, sete quadros
completos sem movimento detectado, deram 163,71 ms de refresh, MOTHER de
30,04 ms e face de 23,44 ms. No candidato, os valores eram 142,33 / 9,87 /
2,81 ms. A face fixa tem redução consistente nas amostras; a diferença de
refresh observada é cerca de 13%, sem equivaler a ganho de fluidez geral.
As capturas ocorreram em horários/fases diferentes e não são trajetórias
pareadas. O usuário não percebeu melhora no candidato. Manter experimental
e priorizar P6, pois o vetor também mostra 76 ms em quadros sem mostrador.
O arquivo bruto vetorial foi analisado sem reparos; detalhes na auditoria.

Primeiro retorno físico em 01/10/2026: usuário não percebeu melhora. Na
captura do candidato, seis quadros de área completa sem movimento detectado
tiveram refresh médio de 142,33 ms, MOTHER de 9,87 ms e face de 2,81 ms.
Há também 50 quadros sem desenho do mostrador com refresh médio de 74,68 ms.
A comparação com a referência histórica não é pareada. O candidato permanece
experimental, aguardando log equivalente de `display_profile_primitives`.
A auditoria registra seleção das janelas, reparos de espaçamento no texto
colado, evidências e limites; não há ganho de autonomia medido.

Experimento preparado em 01/10/2026: `display_profile_mother` acrescenta
`CHRONVS_WATCH_MOTHER_CACHE` ao diagnóstico de primitivas. O firmware padrão
mantém o caminho vetorial. O cache da face ocupa 9.982 bytes constantes na
flash, reutiliza a linha dos anéis e preserva ponteiro/centro dinâmicos.
O gerador usa o LVGL de referência e grava somente os trechos que a face
substitui sobre o fundo dos anéis, sem pintar os cantos do retângulo.
P1 ainda precisa fundamentar sua adoção; esta implementação antecipada é
um candidato para comparação, não uma otimização aceita no dispositivo.

No host, passaram 142 comparações de pixels, recortes/fallbacks e a suíte
integrada com o experimento. A reserva de memória permaneceu a mesma.
O gerador reproduziu exatamente os dados versionados. A falha de build
padrão foi contornada excluindo somente o módulo RGB paralelo sem uso,
preservando o transporte SPI/QSPI; o build padrão passou. Consulte a entrada
datada da auditoria para evidências e os limites dessa etapa.

`draw_mother_disk()` desenha círculo de raio 154, ponteiro orientado pelos
minutos e centro. O preenchimento circular tem geometria fixa enquanto o
mostrador está estacionário, mas a composição completa inclui estado dinâmico.

- [ ] Avaliar reaproveitar preenchimento e borda, mantendo ponteiro e centro
  na ordem original. Usar geração pelo renderizador de referência se houver
  cache de pixels ou de sequências.
- [ ] Definir exatamente os pixels substituídos. Um retângulo opaco contendo
  um círculo pode apagar anéis ou datas fora dele; preservar transparência,
  cobertura e antialiasing nas bordas.
- [ ] Comparar cache isolado com eventual extensão do cache estático existente;
  estimar flash, memória temporária e custo de composição antes de implementar.
- [ ] Evitar imagem completa descomprimida e reservas por frame. Manter retorno
  ao vetor para posição, máscara ou configuração não suportada e falta de memória.
- [ ] Testar deslocamentos, recortes em todas as bordas, sobreposição de painel,
  mudanças de minuto e atualizações sucessivas enquanto segura.
- [ ] Comparar pixels e tempos físicos com P0; descartar a alternativa se
  leitura/descompressão/composição custar mais que o desenho original.

Aceitação: aparência preservada, redução repetível no grupo ou na transição,
sem regressão relevante de memória, estabilidade ou latência ao acordar.

## 7. P3 — submostrador de horas

O centro agora é fixo; a tipografia permanece vertical e o ponteiro tem
ângulo próprio. Reavaliar a representação com a geometria simplificada antes
de reutilizar conclusões sobre posições orbitais.

- [ ] Identificar partes locais invariantes: face/borda, escala e numerais.
- [ ] Comparar reaproveitamento dessas partes com otimização das primitivas;
  manter o ponteiro dinâmico separado quando isso for mais barato.
- [ ] Verificar arredondamento das coordenadas: a referência usa posições
  fracionárias antes de arredondar limites e elementos. Simples translação
  de uma imagem pode produzir diferenças de um pixel entre fases orbitais.
- [ ] Preservar a composição com o disco principal, textos verticais e recorte
  parcial pelos painéis. Não gravar no cache um fundo que muda atrás da borda.
- [ ] Definir limites de memória e retorno ao vetor antes de reservar caches.
  Se houver geração em runtime, medir também o custo de reconstrução ao acordar.
- [ ] Testar todas as horas, várias fases de minuto/segundo, centros próximos
  dos limites de arredondamento e transições em ambos os sentidos.

Aceitação: mesmos pixels nos casos de referência, sem saltos de posição e
ganho físico líquido, incluindo reconstrução quando houver.

## 8. P4 — textos e marcas dos anéis

### Escala fixa de minutos

- [ ] Medir o custo dos numerais em `draw_minute_chapter()`; os traços
  intermediários foram retirados do firmware atual.
- [ ] Avaliar reaproveitamento da escala fixa sem apagar o disco/ponteiro.
  Sua ordem após MOTHER é intencional e deve ser preservada.
- [ ] Preservar a posição reservada ao marcador e o descarte por recorte.
- [ ] Evitar cache de uma área retangular opaca onde a composição exige
  cobertura apenas de glifos/traços; medir o custo de transparência.

### Números da data

- [ ] Medir o custo de rótulos versus posicionamento em `draw_case_dates()`.
- [ ] Aproveitar que as posições dependem do dia exibido, sem criar
  automaticamente 31 imagens completas em flash.
- [ ] Definir chave de validade com dia e coordenadas relevantes. Atualizar
  ao exibir nova data; não deixar a imagem anterior após despertar/NTP.
- [ ] Testar dias 1, 28, 29, 30 e 31, virada de mês/ano e números parcialmente
  encobertos. O calendário real continua responsabilidade do serviço de tempo.

Aceitação de cada subetapa: redução de custo com o mesmo texto, fonte,
posição e ordem de composição. Não reduzir qualidade tipográfica como
efeito colateral de uma otimização interna.

## 9. P5 — semana, temperatura e elementos menores

- [ ] Separar face/borda, escala/textos, arcos e indicadores em WEEKDAY e
  TEMPERATURE; aplicar primeiro a técnica que já mostrou ganho em P2/P3.
- [ ] Para semana, incluir dia destacado, AM/PM e ângulo na análise de
  validade; testar mudança de dia e meio-dia/meia-noite.
- [ ] Para temperatura, revisar a entrada `chronvs_set_ambient_temperature()`
  e os limites usados pelo desenho; testar mudanças de valor e extremos
  suportados sem adicionar acesso a hardware dentro do app.
- [ ] Considerar os centros fixos e os recortes/composição da geometria atual.
- [ ] Reavaliar MARKER após os maiores grupos; SECONDS foi removido.
  Os tempos históricos não medem a composição atual.
- [ ] Medir memória e custo combinados dos caches aceitos; economias isoladas
  não se somam necessariamente no resultado final.

## 10. P6 — painéis, invalidação e área redesenhada

Em 01/10/2026 foram acrescentados tempos de raiz/subárvore, frames, faixas,
área de clip e cobertura de medição para acessos rápidos e launcher. O
ambiente `display_profile_panels_reference` mantém a invalidação completa
do mostrador ao concluir o fechamento; `display_profile_panels` omite
somente essa invalidação. Ambos mantêm MOTHER vetorial e a mesma política
de profiling. A mudança permanece experimental, sem adoção no padrão.

No host, cancelamento, fechamento por botão e fechamento pelo arco tiveram
pixels finais idênticos ao redraw completo, com 166–167 mil pixels a menos
enviados por sequência. Não representa tempo/corrente medidos no ESP32.
Os testes revelaram ausência de propagação do contato no arco: corrigida
em ambos os diagnósticos e no padrão para restaurar o contrato documentado.
Não se alteraram limites de gesto ou parâmetros do display. Detalhes,
limitações dos novos contadores e comandos em `performance.md`.

Esse fechamento pelo arco descreve o contrato antigo. Em 08/10/2026, após
teste do usuário, contatos iniciados no arco passaram a ajustar somente
brilho. A suíte atual confirma que o painel permanece aberto nesse contato
e que um novo arraste fora do arco continua fechando.

As duas capturas físicas seguintes contêm 29 e 16 janelas válidas. Nos
trechos com acessos rápidos e sem launcher, o refresh médio foi 73,870 e
63,082 ms, mas a área média também mudou (66.005 e 54.636 pixels). Os
gestos não foram pareados; isso não comprova um ganho de 14,6% da alteração.
No launcher sem mostrador, foram 78 e 82 ms por refresh; sua própria
subárvore consumiu 45,541 e 48,954 ms. Priorizar a decomposição desse
desenho antes de novas otimizações do mostrador. O candidato continua
experimental; confirmação visual e consumo não constam dos logs.

Em 02/10/2026, o usuário confirmou efeito gelatina residual e sugeriu
interferência dos logs. Preparado `panels_quiet`, com o mesmo candidato
P6 sem profiling nem logs/consoles, para isolar essa influência antes de
nova mudança no desenho. O padrão continua como referência silenciosa.
Procedimento em `performance.md`; teste físico e autonomia pendentes.

Alvos: `src/ui/system_ui.c`, launcher e sua integração com o mostrador.
O fundo liso não tornou todas as janelas baratas; há trabalho independente
das órbitas. O estado estático do horário não impede redesenhos de regiões
expostas pelo movimento do painel.

- [ ] Capturar launcher e acessos rápidos separadamente, sobre fundo orbital
  e sobre o diagnóstico liso, mantendo o mesmo gesto e política de logs.
- [ ] Medir máscaras circulares, fundo, ícones, textos e arco de brilho quando
  necessário; não atribuir todo o custo restante ao mostrador.
- [ ] Revisar áreas invalidadas e movimentos que não mudam coordenadas finais;
  preservar coalescência em 20 ms e aplicação da última posição ao soltar.
- [ ] Conferir descarte de conteúdo realmente oculto. Não remover desenho
  parcialmente visível nem repetir o recorte interno que piorou no hardware.
- [ ] Preservar opacidade efetiva dos ícones personalizados e a animação dos
  nomes; não generalizar o descarte de labels para todos os objetos.
- [ ] Testar primeiro/último trecho do movimento, reversão, cancelamento,
  toque sobre ícone, ajuste exclusivo no arco e fechamento fora dele.

Aceitação: ganho observado também na transição relevante, sem rastro,
clique ao soltar um arraste ou perda de gestos e sem alterar o formato circular.

## 11. P7 — investigar a diferença de posição entre regiões

Esta etapa é condicional ao efeito residual após reduzir desenho. Ainda
não há prova de sincronismo inadequado nem de que TE resolveria o problema.

- [ ] Registrar o efeito visual em arrastes repetidos nos dois sentidos;
  se necessário, usar vídeo em câmera lenta, considerando artefatos da câmera.
- [ ] Relacionar a observação com duração do render/flush, quantidade de faixas
  e cadência de touch. Tempos internos não medem a varredura física do LCD.
- [ ] Ler o driver realmente compilado, o patch persistente e a documentação
  oficial do SPD2010/placa antes de propor mudança de sincronismo.
- [ ] Verificar conexão e configuração efetivas do TE (GPIO 18 consta no mapa
  da placa), sinal disponível e semântica; a presença do pino não significa
  que o firmware o use nem que um quadro de 166 ms caiba no intervalo disponível.
- [ ] Se houver experimento justificável, definir escopo mínimo, timeout,
  recuperação e firmware de restauração antes de modificar qualquer driver.
- [ ] Avaliar possível aumento de latência: esperar um sinal de sincronismo
  pode introduzir pausas sem eliminar a atualização parcial.

Saída possível: manter o driver atual e documentar o limite. Esta investigação
não autoriza restaurar buffers grandes, flush assíncrono ou ajustes de QSPI
que já produziram listras/travamentos.

## 12. P8 — alternativa de interação, se o limite continuar perceptível

Uma transição curta após reconhecer o gesto pode reduzir o tempo durante
o qual o usuário compara o círculo à posição do dedo. Não é correção comprovada
de sincronismo e não está incluída nas otimizações internas acima.

- [ ] Apresentar e escolher com o usuário se mantém acompanhamento direto
  ou experimenta transição curta, antes de alterar o comportamento vigente.
- [ ] Definir reconhecimento, cancelamento, reversão, distância, duração e
  tratamento de novo contato durante a animação; evitar filas de gestos.
- [ ] Preservar os sentidos de abrir/fechar, hierarquia, toque para selecionar
  e consumo do contato que reconheceu o arraste.
- [ ] Comparar sensação de resposta e deformação nos dois painéis.
- [ ] Atualizar `interface.md` apenas se a alternativa for efetivamente adotada.

## 13. P9 — matriz de validação e encerramento

### Verificação no host e build

- [ ] Para cada mudança de C/build/configuração, executar o build padrão
  exigido pelo repositório; compilar o diagnóstico usado no teste físico.
- [ ] Usar os testes de pixels existentes e ampliá-los apenas para riscos
  introduzidos: recortes, arredondamento, invalidação de cache e fallback.
- [ ] Preservar o caminho vetorial de referência para comparação independente.
- [ ] Reproduzir dados gerados; documentar formato, paleta, geometria e
  condições que exigem regeneração. Não aceitar diferenças de pixels sem revisão.
- [ ] Rodar a suíte integrada de interface quando a alteração afetar composição,
  navegação ou memória retida. Build/host não substituem teste do painel.

Comandos existentes, a partir da raiz, selecionados conforme a mudança:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e display_profile_o2
.\tests\generate_watch_ring_cache.ps1
.\tests\run_watch_optimization_tests.ps1 -RingCache
.\tests\run_watch_geometry_tests.ps1
.\tests\run_Relogio_ui.ps1 -System
```

### Verificação no relógio

| Cenário | Resultado a conferir |
| --- | --- |
| Boot e primeiro horário válido | Sem listras, imagem antiga ou atraso novo relevante |
| Repouso visível | Sem giro/redesenho periódico desnecessário |
| Segurar por 600 ms | Atualização imediata ao reconhecer e a cada segundo enquanto mantém |
| Soltar ou mover mais de 12 px | Encerra atualização contínua e respeita o gesto |
| Launcher, abrir/fechar/cancelar | Círculo íntegro, ícones corretos, sem clique residual |
| Acessos rápidos, abrir/fechar/cancelar | Fechar fora do arco; no arco, ajustar brilho sem fechar, inclusive saindo do contorno |
| Apagar e acordar repetidamente | Primeiro contato apenas acorda; horário atualizado |
| Viradas de minuto/hora/dia e correção NTP | Sem cache visual desatualizado |
| Apps retidos, editor e alerta | Sem falha de alocação, fragmentação crescente ou perda de conteúdo |
| Clima/NTP e avisos com áudio | Sem retorno de falhas SPI/TLS, listras ou bloqueios |
| Uso sem diagnóstico | Ganho percebido preservado; registrar o efeito residual |

### Memória, energia e critérios de decisão

- [ ] Medir pico/retensão dos caches, pool LVGL livre e maior bloco, RAM interna
  livre e maior bloco DMA em fluxos relevantes. Os 27.952 bytes livres no
  host com editor/alerta são referência anterior, não autorização para consumi-los.
- [ ] Exercitar falha de alocação e repetição de abrir/fechar; nenhum cache pode
  ser condição obrigatória para mostrar a hora nem crescer sem limite.
- [ ] Confirmar ausência de desenho, leitura de RTC e bateria com tela apagada;
  não introduzir tarefa, polling ou timer ativo só para manter cache.
- [ ] Autonomia real segue não medida. Se houver alegação de ganho energético,
  fazer ensaio separado com condições de brilho/uso/bateria registradas;
  redução de tempo de desenho não é medição de consumo.
- [ ] Aceitar otimização quando houver ganho repetível no alvo e ausência de
  regressão visual/funcional/memória nos cenários afetados. Registrar variação
  entre repetições; um único máximo menor não basta.
- [ ] Reverter o experimento se aparecerem listras, travamentos, pixels
  incorretos ou custo maior; documentar o resultado e manter o último validado.
- [ ] Encerrar a frente quando a experiência no build normal for aceita pelo
  usuário ou quando o custo/risco das alternativas superar o ganho observado.
  Registrar explicitamente limitações restantes, sem declarar “gelatina eliminada”
  apenas porque um benchmark melhorou.

## 14. Restrições e manutenção documental

Permanecem: refresh de 20 ms; buffers duplos de 1/20 em PSRAM; pool TLSF de
128 KiB em PSRAM; transferências QSPI de 2 KiB; espera síncrona antes de
`draw_bitmap` retornar; LVGL somente na tarefa da interface. Não introduzir
framebuffers completos, buffers 1/10 ou maiores, transferências de 8 KiB ou
`lv_disp_flush_ready()` assíncrono. Não restaurar o recorte interno descartado.

O contrato solicitado pelo usuário para o mostrador é sob demanda, com pressão
prolongada para 1 Hz. `AGENTS.md` e `interface.md` refletem esse comportamento
e a exclusividade do arco para ajustar o brilho.

- [ ] A cada etapa, atualizar esta fila e acrescentar resultado datado à auditoria,
  distinguindo build, host, medição física e percepção do usuário.
- [ ] Atualizar `performance.md` se mudar o procedimento de medição;
  `apps.md` se mudar ciclo de vida/memória e `interface.md` se mudar interação.
- [x] Corrigir descrições antigas do README sobre painel retangular e atualização
  contínua, confrontando-as com o contrato atual; manter o README como visão geral.
- [ ] Revisar afirmações antigas de “medição pendente” nos documentos de entrada
  quando já existir resultado; preservar as entradas históricas da auditoria.
- [ ] Fazer commits pequenos após revisar o diff; excluir segredos, referência
  vendor e artefatos de build. Versionar alterações da carcaça separadamente
  quando solicitadas pelo usuário.

Comparações com projetos externos/CrowPanel permanecem secundárias. Caso
retomadas, comparar resolução, área atualizada, desenho, interface de display
e condições de captura; vídeo de outra placa não estabelece meta garantida
para este hardware. Não há pesquisa externa nova concluída nesta etapa.
