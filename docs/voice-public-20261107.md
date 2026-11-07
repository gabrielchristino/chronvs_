# Primeiro treinamento compacto com fala pública — 07/11/2026

## Escopo

Esta rodada prepara a base pública e verifica treinamento CTC com frases
completas de duração variável. Tudo roda no PC. Não altera o firmware,
não usa gravações pessoais no treinamento e não envia áudios pessoais.
O resultado de 89/100 da [referência grande](voice-validation-20261107.md)
continua separado das métricas deste modelo compacto.

## Dados e rastreabilidade

Fonte: [FLEURS, Google, configuração pt_br](https://huggingface.co/datasets/google/fleurs),
licença declarada CC-BY-4.0, revisão
`70bb2e84b976b7e960aa89f1c648e09c59f894dd`.

`prepare_public.py` consulta as três tabelas oficiais, mas baixa áudio somente
de treino e validação. Percorre os arquivos comprimidos em ordem, selecionando
as primeiras frases elegíveis entre 0,5 e 15 segundos. A seleção é determinística
nessa revisão, sem alegação de amostragem representativa. Arquivos longos são
ignorados por inteiro; nenhum áudio recebe a transcrição de uma frase cortada.

Exclui IDs de sentença e textos normalizados presentes em mais de uma divisão.
Nesta revisão, não encontrou essas sobreposições nas tabelas. Verifica também
duplicatas de PCM no subconjunto exportado. A checagem de áudio não abrange
o teste, que não foi baixado, e IDs de sentença não são IDs de locutor: esta
rodada não demonstra separação entre pessoas nas divisões.

| Divisão oficial | Áudios | Sentenças distintas | Duração total |
| --- | ---: | ---: | ---: |
| Treino | 128 | 124 | 1.404,12 s |
| Validação | 32 | 28 | 349,02 s |

Áudios selecionados duram entre 5,04 e 14,88 segundos. A duração inclui pausas.
Há versões da mesma sentença dentro de uma divisão, preservadas como exemplos
distintos. A primeira tentativa de exportação identificou WAV float na origem;
ela ficou incompleta em `public-ptbr-01`, sem manifest aprovado para treino.

A exportação completa está em `.pio/voice/public-ptbr-02`. O leitor converte
float para PCM16 por arredondamento e saturação, sem alterar taxa, ganho,
duração ou transcrição. Somente uma amostra numérica precisou de saturação
no conjunto inteiro. O manifest registra hash do WAV original, do WAV convertido
e do PCM, tabelas, fonte, licença e código de exportação. Não executa código
remoto do dataset nem extrai caminhos do tar no sistema de arquivos.

SHA-256 do manifest completo:
`42cf45384e10cd2b12caf6b1ffcdef4de7eb6bcd8c0cadb71a6eaaab499c88aa`.

## Modelo e treinamento

`public_ctc.py` extrai 32 bandas log-mel, janela de 25 ms e passo de 10 ms,
usando a frase completa. Média/desvio são calculados somente nos quadros reais
do treino. O preenchimento do lote não participa dessas estatísticas.

A rede causal tem canais internos de 64 elementos, seis blocos separáveis
com dilatações 1, 2, 4, 8, 16 e 32, e saída CTC por caracteres. Campo receptivo
de 257 quadros, aproximadamente 2,585 segundos de áudio. São **41.338 parâmetros**,
ou **165.352 bytes de pesos float32**. Isso não inclui ativações, buffers,
extração de características, pilhas ou runtime; ainda não mede viabilidade no S3.

O alfabeto combina símbolos declarados para português/nomes emprestados com
caracteres presentes no treino, sem aprender o inventário na validação.
O CTC usa comprimentos individuais de entrada/alvo e verifica o espaço
necessário para caracteres repetidos. Perdas não finitas interrompem o processo;
não se usa `zero_infinity` para ocultar alinhamentos impossíveis.

CPU, duas threads, lotes de quatro, AdamW 0,001, seed 41. Escolha do checkpoint
pela menor perda na validação; portanto, ela é de desenvolvimento. Não há
resultado de teste independente nem calibração de recusa nesta rodada.
Decodificação gulosa, sem modelo de linguagem ou busca forçada nas 200 palavras.
Ambiente: PyTorch CPU 2.5.1, NumPy 1.26.4 e SoundFile 0.13.1.

O estado mais recente e o melhor estado são salvos a cada época, com fingerprint
dos dados, código e configuração. `--resume` rejeita alterações nesses campos
e retoma da última época completa, não do meio de um lote. Diretórios incompletos
de exportação ficam preservados; nova exportação exige outro diretório.

## Reprodução

No ambiente de voz existente, instalar as dependências adicionais:

```powershell
& '.\.venv-voice\Scripts\python.exe' -m pip install -r tools/voice/requirements-public.txt
& '.\.venv-voice\Scripts\python.exe' tools/voice/prepare_public.py --output .pio/voice/public-ptbr-02
& '.\.venv-voice\Scripts\python.exe' tools/voice/train_public.py --corpus .pio/voice/public-ptbr-02 --output .pio/voice/public-character-01 --epochs 30
```

As pastas acima já existem. Use novos nomes para reproduzir; para retomar um
treinamento, repita exatamente seu comando com `--resume`. Áudio público e
checkpoints ficam em `.pio`, fora do versionamento.

## Validação do processo

Os testes verificam conversão float/PCM e duração de três segundos, exclusão
por ID/texto entre divisões, limites para repetições CTC e equivalência entre
perdas/previsões individuais e em lote com preenchimento. Evidência de treino
incompleto ou vinculada a outro manifest é recusada pelo auditor do fluxo.
Os 36 testes Python de voz passaram, incluindo os quatro testes desta etapa:

```powershell
& '.\.venv-voice\Scripts\python.exe' -m unittest discover -s tests -p 'voice_*test.py'
```

Também passaram `compileall` e `git diff --check`. Não houve mudança de C,
configuração de build ou firmware nesta etapa; não foi feito novo upload.
Uma nova auditoria confirmou os 205 áudios pessoais e o snapshot PCM original
`214f4b5f0f2ab4ab8de22ab7ad0d78f9e9a90b5306db84c923768a288a6530a3`.

Uma cópia do checkpoint de 30 épocas foi retomada em
`.pio/voice/public-character-resume-probe-01`: histórico, previsões e métricas
foram idênticos aos originais. Isso valida a leitura/retomada de um checkpoint
completo; não simula desligamento durante a gravação de um arquivo.

O auditor aceita a evidência de treino explicitamente, sempre com o manifest:

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/workflow_status.py --output .pio/voice/workflow-public-final-01 --reference-report .pio/voice/reference-audit-20261107-01/report.json --public-training-report .pio/voice/public-character-01/report.json --public-manifest .pio/voice/public-ptbr-02/manifest.json
```

O estado resultante é `public-development-run-complete-quality-not-approved`;
ter completado uma execução não aprova a qualidade ou a exportação ao relógio.

## Primeiro resultado: 30 épocas

Artefatos em `.pio/voice/public-character-01`. Checkpoint escolhido: época 30.

| Conjunto | Erros/caracteres | CER | Frases exatas |
| --- | ---: | ---: | ---: |
| Treino | 13.204/15.293 | 86,34% | 0/128 |
| Validação | 3.591/4.005 | 89,66% | 0/32 |

Esse ensaio valida a execução, mas não produz um reconhecedor utilizável.
A perda diminuiu e a saída começou a deixar de ser vazia; isso não equivale
a transcrever corretamente. Foi planejado um segundo controle com 100 épocas,
mantendo seed, dados e arquitetura, para medir a evolução do mesmo aprendizado.

## Controle de 100 épocas — concluído

Artefatos em `.pio/voice/public-character-02`. As primeiras 30 épocas têm
histórico idêntico ao ensaio anterior. Pelo critério fixado de menor perda
de validação, o checkpoint escolhido foi o da época 38:

| Conjunto | Erros/caracteres | CER | Frases exatas |
| --- | ---: | ---: | ---: |
| Treino | 11.565/15.293 | 75,62% | 0/128 |
| Validação | 3.296/4.005 | 82,30% | 0/32 |

A época final teve CER de validação de 67,89%. O menor CER observado no
histórico foi 66,29%, na época 75. São diagnósticos adicionais, não a seleção
original: perda CTC e CER guloso não escolheram o mesmo estado. Não trocamos
silenciosamente o critério para anunciar o melhor número retrospectivo.
Mesmo o menor CER continua inadequado para o objetivo.

A perda de treino continuou caindo, enquanto a melhor perda de validação ficou
na época 38. O ensaio não separa todos os efeitos de quantidade/diversidade
de dados, arquitetura e representação, mas descarta promover essa execução
apenas por treinar por mais tempo. Nenhum checkpoint desta rodada foi
adaptado à voz pessoal, quantizado ou enviado ao relógio.

Uma evidência atualizada do fluxo foi salva em
`.pio/voice/workflow-public-final-02`, vinculada ao relatório de 100 épocas
e ao manifest exato do subconjunto. Os próximos experimentos devem ampliar
a base e avaliar supervisão da referência forte, preservando os mesmos
cuidados com divisões e métricas.

## Critério e próximo passo

Um reconhecedor que sempre retorna texto vazio tem CER de 100% neste conjunto.
CER mede inserções, remoções e substituições de caracteres, e não é diretamente
comparável ao acerto exato de 89% em palavras da referência grande. Relatar
também frases exatas e os resultados de treino evita chamar memorização de
generalização.

Antes de adaptar à voz pessoal, precisamos de uma base que reconheça fala
pública reservada com qualidade útil. Ampliar os dados e avaliar supervisão
pela referência forte são caminhos a testar; treinar esta rede pequena do zero
com 23 minutos de fala não pressupõe reproduzir o modelo grande. A comparação
controlada com saída fonética, a quantização e medições no relógio continuam
pendentes.
