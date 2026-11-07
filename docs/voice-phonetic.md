# Voz por unidades fonéticas — estudo e protótipo

Para o próximo ciclo, siga o [fluxo de evolução](voice-workflow.md), que coloca
uma base acústica em português antes da adaptação pessoal e define critérios
de recusa, teste reservado e validação no relógio.

## Objetivo e situação

Este estudo investiga reconhecer sequências de sons do português brasileiro,
permitindo cadastrar palavras por pronúncia sem uma classe neural exclusiva
para cada palavra. A coleta do piloto de palavras continua independente.
Nenhum destes comandos inicia ou substitui o coletor que já está aberto.

Implementado em 06/11/2026:

- Conversão **local** do texto das 200 palavras para um rascunho em IPA.
- Tratamento de unidades com nasalização e africadas, em vez de contar letras.
- Seleção de um roteiro por cobertura de unidades e pares adjacentes.
- Roteiro reservado com palavras ausentes do roteiro de treino.
- Coletor futuro para os contextos fonéticos, em outra raiz de dados.
- Rede acústica causal pequena com CTC e comparação de palavras por pronúncia.
- Testes de matemática, causalidade e integridade das transcrições congeladas.

**É um protótipo de pesquisa no PC.** Não há inferência fonética no firmware,
pronúncias pessoais revisadas, calibração de aceitação fonética ou medida de
generalização em palavras novas. O treinador CTC exporta pesos float32 e um
relatório de ajuste ao treino; não produz firmware nem um reconhecedor pronto.

## O alfabeto fonético

AFI, ou IPA em inglês, é o **Alfabeto Fonético Internacional**. A Associação
Fonética Internacional disponibiliza a
[tabela traduzida para português brasileiro](https://www.internationalphoneticassociation.org/IPAcharts/common_files/pdfs/pdfs_IPA_charts_T/IPA_Kiel_por-br.pdf).
Essa tabela representa sons de muitas línguas; não é uma lista dos fonemas
exclusivos do português, nem um modelo acústico já treinado.

Neste projeto, a pessoa lê palavras e pequenos contextos naturalmente. Não
precisa aprender a ler IPA nem pronunciar o nome das letras. Gravamos sons
em palavras, transições, nasalização e variações naturais de pronúncia.
Soletrar “pê, a, tê, o” é diferente de pronunciar “pato”.

Uma unidade acústica precisa ser reconhecida com durações diferentes e junto
a sons vizinhos. Por isso, uma amostra isolada por fonema não substitui um
corpus de fala. O número de rótulos pode diminuir; a quantidade e a diversidade
de áudio necessárias não diminuem automaticamente na mesma proporção.

## Arquitetura investigada

```text
PCM 16 kHz
  → log-mel preservando os quadros de 10 ms
  → rede causal: probabilidades de unidades + blank do CTC
  → busca de sequências compatíveis com as pronúncias do léxico
  → candidatos a palavra, ainda sujeitos a recusa e revisão
```

CTC aprende o alinhamento temporal a partir de uma sequência esperada de
unidades, sem exigir que a pessoa marque o início e fim de cada fonema.
O `blank` do CTC significa ausência de emissão naquele quadro; não equivale
semanticamente a silêncio físico nem a “palavra desconhecida”.

O artigo
[Small-Footprint Open-Vocabulary Keyword Spotting with Quantized LSTM Networks](https://arxiv.org/abs/2002.10851)
investiga vocabulário configurável com CTC e redes quantizadas menores que
500 kB. Ele fundamenta a direção de pesquisa; não valida nosso modelo no
ESP32-S3. Nossa primeira implementação usa convoluções causais e não reproduz
a arquitetura ou os resultados do artigo.

Adicionar uma palavra ao léxico pode dispensar novo treinamento acústico
**quando os sons e os contextos necessários já foram aprendidos**. Acrescentar
um fonema ainda não observado não cria essa capacidade. Homófonos também
permanecem ambíguos: sons iguais podem corresponder a grafias diferentes.
Contexto linguístico e revisão do texto são necessários para notas livres.

## Gerar o léxico e o roteiro

Use o ambiente descrito em [`voice-training.md`](voice-training.md). O helper
opcional fica em uma pasta isolada, sem instalar nada no relógio ou trocar
pacotes do coletor em execução:

```powershell
& '.\.venv-voice\Scripts\python.exe' -m pip install --target .pio/voice-phonetic-tools -r tools/voice/requirements-phonetic-tools.txt
& '.\.venv-voice\Scripts\python.exe' tools/voice/prepare_phonetic.py --output .pio/voice/phonetic-draft-02
```

Use uma pasta de saída nova. O roteiro existente `phonetic-draft-02` foi gerado
com Piper 1.4.2 e voz `pt-br`, usando o eSpeak NG incluído no pacote. Essa é
apenas uma ferramenta texto→pronúncia, não um reconhecedor de fala. Não foi
gerado áudio sintético para treinamento. Referência de implementação:
[Piper/eSpeak](https://github.com/OHF-Voice/piper1-gpl).

Arquivos gerados:

| Arquivo | Função |
| --- | --- |
| `lexicon.json` | Pronúncia provisória das 200 palavras, unidades e proveniência |
| `recording-plan.json` | Contextos selecionados para coletar treino e calibração |
| `generalization-plan.json` | Palavras reservadas para testar composição de sons |
| `coverage.json` | Cobertura textual e lacunas de pares adjacentes |

O algoritmo guloso prioriza unidades ainda não cobertas e depois pares
adjacentes, com preferência relativa por enunciados curtos. O material auxiliar
original está em `tools/voice/phonetic-prompts.txt`. O limite padrão é 40 itens.
Não existe otimização baseada em áudio nem garantia de mínimo global.

Na geração inicial, as 200 palavras produziram **41 unidades acústicas
distintas e 347 pares adjacentes**. O roteiro selecionado de 40 itens cobre as
41 unidades e 198 pares. As seis palavras do piloto cobrem 17 unidades e
20 pares. Esses números são derivados da transcrição automática, não um
inventário linguístico oficial ou uma medida de cobertura acústica real.

O roteiro reservado contém 20 palavras: dez sem novos pares adjacentes e dez
com novos pares. Nenhuma delas aparece como palavra no roteiro de treino,
nem repete integralmente a mesma sequência fonética de um item selecionado.
A verificação inclui palavras dentro dos pequenos contextos. Isso permite
separar recombinação de sons de mera repetição de palavras já gravadas.

## Revisar as pronúncias

Todos os itens gerados começam com `reviewed: false`. O eSpeak é uma hipótese,
não uma transcrição comprovada da voz do usuário. Pode errar em empréstimos,
nomes, reduções, vogais e consoantes regionais. Por exemplo, o rascunho pode
incluir um R final que a pessoa omite ao falar um infinitivo naturalmente.
Treinar esse alvo sem revisão pode ensinar o sistema a prever sons ausentes.

A especificação experimental em `phonetic_units.py`:

- Normaliza Unicode em NFD e mantém diacríticos nas unidades-base: a nasalização
  não vira um fonema independente nem desaparece.
- Agrupa `tʃ`, `dʒ`, `ts`, `dz`; não colapsa automaticamente sotaques.
- Preserva modificadores de palatalização/labialização na unidade-base.
- Descarta acento de intensidade, comprimento e pontuação nesta primeira versão.
- Usa `|` como separador textual entre palavras e `<blank>` como saída CTC.

É uma convenção de modelagem que poderá precisar de revisão. Não representa
todos os contrastes possíveis do português. Alterá-la muda a preparação dos
rótulos, o inventário de saídas, os dados e os pesos compatíveis.

Antes de uma coleta definitiva, revisar pronúncias e alternativas naturais,
validar que os enunciados cabem confortavelmente em dois segundos e congelar
léxico e roteiro. Não pedir à pessoa que distorça a própria pronúncia para
imitar a saída automática. Variantes devem ser descritas explicitamente;
não trocar alvos de uma sessão depois de olhar a avaliação.

O corpus lexical
[BIPA](https://aclanthology.org/2026.propor-1.47/) oferece transcrições de
português brasileiro com variantes dialetais e é outra fonte a investigar.
Não foi importado neste experimento e não fornece por si só o áudio pessoal.

## Coleta de contextos

O coletor independente usa o mesmo firmware `vox_capture` já validado no
transporte. **Encerre o outro coletor antes de executar este comando**, pois
a COM3 precisa de acesso exclusivo. Os comandos abaixo são para a etapa
posterior ao piloto atual; não são executados automaticamente.

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/collect_phonetic.py --plan .pio/voice/phonetic-draft-02/recording-plan.json --port COM3 --speaker gabri --session fonetica-treino01 --split train --playback
```

O padrão é três repetições de cada um dos 40 contextos e 15 silêncios: 135
gravações, sem precisar gravar todas as 200 palavras como classes. Esse número
é um ponto de partida experimental, não uma afirmação de que bastará. Serão
necessárias outras sessões e maior diversidade conforme os erros observados.

O coletor de contextos também aceita `--hands-free` no Windows: basta tocar no
relógio para gravar e avançar, `r` repete o pendente e `q` salva e pausa, sem
Enter. A última gravação tem cinco segundos para revisão antes da confirmação
automática. Veja o [fluxo completo](voice-training.md#coleta-sem-enter-windows).

Após pausa e mudança de condições, use o mesmo roteiro numa sessão nova de
calibração. Para a avaliação de palavras inéditas, use outro nome de sessão,
`--split evaluation` e `--plan .../generalization-plan.json`. O coletor recusa
usar esse roteiro reservado como treino ou calibração.

O áudio é salvo em `data/voice-phonetic-local/`, ignorado pelo Git. Cada sessão
congela uma cópia de `plan.json`, hash do plano, pessoa, conjunto e sequência
de coleta. O manifest registra texto, índice do contexto e hash do PCM; a
pronúncia esperada vem da cópia congelada do plano. A retomada verifica os WAVs
aceitos e não sobrescreve áudio sem registro. A operação física deste novo
coletor ainda requer uma primeira sessão; ele não foi aberto durante o piloto.

### Primeira passagem exploratória — 07/11/2026

Após o piloto de palavras, o usuário solicitou testar a coleta fonética.
Foi preparada a sessão `gabri/20261107-fonetica-exploratoria01` como treino,
com uma passagem pelos 40 contextos e cinco silêncios (45 amostras), semente 97,
reprodução e modo sem Enter. A passagem curta serve para conferir a coleta e
as pronúncias antes de ampliar as repetições; não substitui o roteiro completo
de múltiplas sessões nem demonstra generalização. Os alvos continuam como
`reviewed: false` até revisão, sem marcar automaticamente a voz como validada.

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/collect_phonetic.py --plan .pio/voice/phonetic-draft-02/recording-plan.json --port COM3 --speaker gabri --session 20261107-fonetica-exploratoria01 --split train --repetitions 1 --silence 5 --seed 97 --playback --hands-free
```

As sessões anteriores de palavras e calibração ficam preservadas. O próximo
toque no gravador confirma o áudio anterior e grava o contexto seguinte;
`r` repete o pendente e `q` salva e pausa. O comando é retomável mantendo
esses mesmos parâmetros.

### Resultado da primeira passagem — 07/11/2026

A sessão terminou com 45 amostras válidas: cada um dos 40 contextos foi
gravado uma vez, além de cinco silêncios. Todos os hashes e o plano congelado
conferiram; não houve saturação, duplicatas ou WAV pendente. O RMS ficou
entre 0,003093 e 0,047739. A integridade do áudio não valida automaticamente
as transcrições IPA, que continuam provisórias.

Foi acrescentada a opção `--phonetic-only`, para excluir as sessões antigas
de palavras do treino. Assim, Comprar, Fazer, Hoje, Casa, Pão e Leite não
aparecem, nem como palavras dentro de expressões, nos áudios usados pelos
novos modelos. As 30 gravações dessas palavras na calibração anterior foram
usadas como desafio de recombinação. Elas já haviam orientado experimentos
anteriores: esse diagnóstico não é uma avaliação cega independente.

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/train_phonetic.py --phonetic-only --phonetic-dataset data/voice-phonetic-local --lexicon .pio/voice/phonetic-draft-02/lexicon.json --allow-unreviewed --epochs 300 --output .pio/voice/phonetic-context-01
& '.\.venv-voice\Scripts\python.exe' tools/voice/diagnose_pilot.py --word-model .pio/voice/piloto-01 --phonetic-model .pio/voice/phonetic-context-01 --lexicon .pio/voice/phonetic-draft-02/lexicon.json --all-words --output .pio/voice/phonetic-context-01-calibration-diagnostic.json
```

Use outras pastas de saída caso os artefatos já existam. O primeiro modelo
ajustou os alvos do próprio treino com erro de unidades 0%. As 41 unidades
derivadas do léxico receberam exemplos, além do separador de palavras.
Uma unidade auxiliar do inventário gerado, `ɪʲ`, ficou sem exemplos. Isso é
cobertura de rótulos esperados, não prova de sons efetivamente aprendidos.

Também foi testada uma única alteração: remover a média do log-mel de cada
quadro (`--gain-normalize`), antes da normalização por banda do treino. Essa
representação versão 2 reduz dependência de intensidade uniforme; a invariância
sem saturação é verificada em teste. Gravações, arquitetura, semente e 300
épocas foram mantidas. O resultado ficou pior, portanto a versão 1 permanece
o padrão. Os dois candidatos continuam somente como arquivos de pesquisa.

| Diagnóstico em 30 gravações de palavras ausentes do treino | Sem normalização de ganho | Com normalização de ganho |
| --- | ---: | ---: |
| Palavra correta, escolha forçada entre seis | 12/30 | 5/30 |
| Palavra correta, primeiro lugar no léxico de 200 | 2/30 | 0/30 |
| Palavra correta entre os cinco primeiros do léxico | 5/30 | 0/30 |
| Silêncios com alguma emissão fonética na saída gulosa | 11/25 | 11/25 |

Os arquivos são `.pio/voice/phonetic-context-01` e
`.pio/voice/phonetic-context-02-gain`, com diagnósticos correspondentes. Para
reproduzir a segunda variante, use a opção `--gain-normalize` no treinamento,
uma pasta nova, e o mesmo diagnóstico apontando para ela. O diagnóstico lê
automaticamente a especificação de processamento gravada no relatório e
recusa especificações desconhecidas.

As saídas ainda não têm recusa calibrada; esses números de escolha forçada
não equivalem a um reconhecedor utilizável. O erro guloso de unidades foi
135,56% na primeira variante e 110,37% na segunda, incluindo inserções, por
isso pode exceder 100%. Ambas ajustaram o próprio treino com erro zero,
demonstrando novamente que memorizar os exemplos não resolve generalização.

Sete unidades esperadas apareceram em apenas um ou dois contextos distintos:
`dʒ`, `tʃ`, `ʃ`, `ʊ̃`, `ʒ`, `ts` e `ũ`. A coleta cobre o inventário, mas não
oferece variedade suficiente para concluir que o modelo aprendeu cada som.
Também continuam em aberto os alvos automáticos, o processamento e a
adequação da arquitetura. Não se atribui a falha a uma única causa.

Os sete testes fonéticos passaram, incluindo a exclusão explícita das sessões
antigas, a invariância da nova representação e os testes de CTC anteriores.
Nenhum candidato foi instalado no relógio. Antes de nova coleta extensa,
revisar a base acústica e os alvos, e projetar variações/avaliações capazes de
medir generalização real, preservando estes resultados como regressão conhecida.

## Protótipo de treinamento e resultado observado

`train_phonetic.py` lê somente sessões de **treino**. As sessões em calibração
ou avaliação não são usadas para selecionar parâmetros ou relatar qualidade.
As amostras `_unknown` do primeiro coletor não têm transcrição e são excluídas:
não se pode rotular fala desconhecida como silêncio para treinar CTC.

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/train_phonetic.py --dataset data/voice-local --lexicon .pio/voice/phonetic-draft-02/lexicon.json --allow-unreviewed --epochs 300 --output .pio/voice/phonetic-fitcheck-next
```

`--allow-unreviewed` autoriza explicitamente um **experimento diagnóstico** com
as pronúncias automáticas. Sem isso, o treinador exige revisão dos alvos de
fala usados. Não se trata de uma aprovação do modelo para uso. Para incluir
as futuras sessões fonéticas, acrescente
`--phonetic-dataset data/voice-phonetic-local`. Um som fora do inventário fixado
faz o treinamento recusar a combinação de arquivos.

O protótipo preserva 198 quadros de log-mel por gravação, em vez de condensá-los
num único vetor temporal. Usa 32 bandas, uma convolução causal e dois blocos
com convoluções por canal e projeções pontuais, com dilatações 2 e 4. O campo
receptivo cobre 29 quadros (305 ms de áudio incluindo a janela inicial).
A normalização por banda vem exclusivamente do treino.

Para 64 canais ocultos e o inventário gerado de 44 saídas (incluindo blank,
fronteira e unidades auxiliares), a rede tem **22.252 parâmetros**, ou 89.008
bytes de parâmetros float32. Isso não inclui buffers, FFT, pilha, runtime ou
arena de inferência; não é medição de RAM/flash no relógio.

Foram feitos dois diagnósticos usando somente a primeira sessão concluída:
30 áudios das seis palavras e 25 silêncios, excluindo 25 falas não transcritas.
Com 60 épocas, a decodificação gulosa ficou vazia e teve erro de unidades de
100% no próprio treino. Com 300 épocas, o erro no próprio treino caiu a 0%.
Os artefatos locais estão em `.pio/voice/phonetic-fitcheck-01` e
`.pio/voice/phonetic-fitcheck-02`.

**Zero no treino demonstra capacidade de ajuste, não generalização.** As
pronúncias eram provisórias; apenas 17 unidades receberam exemplos de fala.
O sistema pode memorizar associações de palavras inteiras e prever sons que
não foram realmente pronunciados. O experimento não prova reconhecimento
fonético de palavras novas, acurácia em uso ou funcionamento no ESP32.

Em 07/11/2026, o candidato de 300 épocas foi comparado com a calibração
concluída do piloto, mantendo os pesos congelados: acertou 5/30 palavras
quando obrigado a escolher entre as seis, e teve erro de unidades de 97,78%
na decodificação gulosa contra os alvos automáticos. Logo, o ajuste perfeito
ao treino não se transferiu a essa sessão. Esse resultado é diagnóstico de
calibração, sem teste de palavras inéditas ou política de recusa pronta.
Artefatos e comando em [primeiro candidato](voice-training.md#calibração-concluída-e-primeiro-candidato--07112026).

Um controle posterior por MFCC e alinhamento temporal obteve 20/30 palavras
corretas na mesma calibração (24/30 quando forçado entre seis palavras).
Esse método depende de exemplos por palavra e não substitui este objetivo
fonético; serve como referência para revisar o processamento e medir o ganho
real de uma rede de unidades acústicas. Os detalhes e as falhas com negativas
estão no [guia do piloto](voice-training.md#controle-de-representação-e-tempo--07112026).

Os relatórios novos incluem hashes dos scripts, do léxico, dos pesos e dos
áudios usados. O formato NPZ é um arquivo de pesquisa, não um modelo ESP-SR.
Ainda faltam exportação e comparação de inferência em C, quantização,
calibração com negativas, avaliação independente e medidas de CPU/memória.

## Decodificação, testes e próximos critérios

`phonetic_ctc.py` implementa a soma dos caminhos CTC em log-espaço para uma
sequência de unidades. O ranqueamento consulta palavras e variantes do léxico,
mantendo homófonos empatados. Ele **sempre encontra um melhor candidato**, mesmo
para ruído: isso não é uma decisão de aceitação e nunca deve salvar notas.

```powershell
& '.\.venv-voice\Scripts\python.exe' tests/voice_phonetic_test.py
```

Os seis testes aprovados em 06/11/2026 verificam nasalização/africadas,
cobertura textual, CTC contra enumeração de todos os caminhos e PyTorch,
unidades repetidas/homófonos, causalidade/gradientes e integridade de dados.
Não medem reconhecimento real nem substituem uma sessão física do novo coletor.

Antes de adotar esta alternativa:

1. Revisar a unidade de saída e as pronúncias, registrar contextos variados.
2. Aprender os sons e medir erro de unidades em sessões independentes.
3. Congelar modelo e testar palavras ausentes dos áudios de treino; separar
   combinações novas de pares adjacentes já observados.
4. Calibrar recusa com silêncio, ruído e fala fora do léxico; medir falsos
   positivos, palavras recusadas e ambiguidade, além de acertos.
5. Portar ao S3 sem alterar os parâmetros críticos de tela, medir memória
   interna/DMA, PSRAM, tempo por quadro e impacto na bateria/interface.
6. Só depois investigar fala contínua e composição revisável no Notas.
