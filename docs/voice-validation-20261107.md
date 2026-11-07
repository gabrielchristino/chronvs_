# Validações automáticas de voz — 07/11/2026

## Resultado e alcance

Esta rodada usou somente os áudios existentes, no PC, sem abrir nova coleta,
alterar o firmware, corrigir rótulos ou enviar gravações à rede. O download
se limitou a bibliotecas, pesos públicos e metadados públicos.

As 205 gravações continuam com formato e hashes válidos. A referência grande
de português acertou exatamente **89/100 gravações com texto esperado**.
Com uma detecção de fala separada, os textos espúrios em silêncio caíram de
19/55 para 0/55 neste conjunto, mantendo os 89 acertos. Esses resultados não
demonstram reconhecimento no ESP32 nem aprovação das transcrições IPA.

## Auditoria e testes

- Conferidos formatos WAV, PCM, duplicatas entre raízes, estatísticas e planos
  congelados das três sessões. Não há áudio pendente.
- Snapshot agregado dos PCM permaneceu
  `214f4b5f0f2ab4ab8de22ab7ad0d78f9e9a90b5306db84c923768a288a6530a3`.
- Passaram 32 casos de teste Python, cobrindo dados, políticas de log,
  reconexão, coleta sem Enter, alinhamento, CTC, treinamento/exportação,
  auditoria de fluxo, métricas de referência e detecção de fala.
- Passaram os dois executáveis C de host do Vox e da captura.
- Não houve mudança em C, configuração do firmware ou scripts de build nesta
  rodada; não foi necessário recompilar/gravar o relógio.

Snapshots privados em `.pio/voice/workflow-validation-20261107-01` e
`.pio/voice/workflow-validation-20261107-02`. O segundo incorpora a evidência
completa da referência e os metadados públicos, sem marcar outros marcos como
concluídos. O auditor recusa relatórios parciais ou de outro snapshot de áudio.

## Referência local de português

Modelo publicado:
[`jonatasgrosman/wav2vec2-large-xlsr-53-portuguese`](https://huggingface.co/jonatasgrosman/wav2vec2-large-xlsr-53-portuguese).
Foi usada a conversão pública safetensors de
[`refs/pr/4`](https://huggingface.co/jonatasgrosman/wav2vec2-large-xlsr-53-portuguese/discussions/4),
fixada no commit `66153c537449086b14c100226b65a7cfa32b9bd0`. O hash dos pesos
baixados foi comparado ao SHA-256 dos metadados LFS. Foram verificados 424
tensores e aproximadamente 1,262 GB de arquivos locais.

Inferência em CPU, float32, dois threads, um áudio por lote, decodificação
CTC gulosa sem modelo de linguagem. O carregamento usa apenas arquivos locais,
`use_safetensors=True`, `local_files_only=True` e modo offline das bibliotecas.
Foram processados 12 casos e depois retomados os 193 restantes, preservando
as primeiras predições. Cada resultado é salvo após o respectivo áudio.

| Sessão | Texto conhecido exato | Erro de palavras (WER) |
| --- | ---: | ---: |
| Piloto de treino | 28/30 | 6,67% |
| Calibração | 28/30 | 10,00% |
| Contextos fonéticos | 33/40 | 19,51% |
| Total | 89/100 | 12,87% |

O total compara 101 palavras esperadas, pois há um contexto com duas palavras.
O erro de caracteres (CER) foi 4,14%. Comparação por gravação e WER são medidas
diferentes: uma gravação pode conter mais de um erro de palavra. Normalização
ignora caixa e pontuação, mas conserva acentos; dobrar os acentos não mudou
o número de correspondências exatas desta execução.

Os 50 exemplos `_unknown` não têm transcrição humana e não entram na acurácia:
um transcritor produzir texto para essa fala é comportamento esperado.
Nos 55 silêncios, a referência sem filtro produziu texto 19 vezes.

As divergências com texto conhecido se concentraram em nove grupos e 11
gravações: Bluetooth, Creality, Fazer, Helena, Notebook, Projeto, Pão,
Rodoviária e campo. Há possíveis diferenças de ortografia e nomes, além de
confusões acústicas; não foram transformadas em correções automáticas.
A hipótese `projecto` para `Projeto`, por exemplo, não prova um erro de fala.

Relatórios privados em `.pio/voice/reference-audit-20261107-01/`:

- `run.json`: modelo, entrada e código congelados para retomada;
- `predictions.jsonl`: hipóteses separadas dos textos originais;
- `report.json`: métricas totais e por sessão;
- `review.json`: divergências, incluindo texto produzido em silêncio.

A mediana registrada foi de cerca de 733 ms por áudio no PC, incluindo
inferência/decodificação e operações auxiliares do trecho cronometrado.
Não é uma medida de desempenho no S3. O tamanho deste modelo é incompatível
com o orçamento atual do relógio; ele serve como referência de validação.

## Detecção de fala antes do reconhecimento

Foi avaliado o
[detector WebRTC](https://github.com/daanzu/py-webrtcvad-wheels), versão
`webrtcvad-wheels==2.0.14`, com PCM16/16 kHz em quadros de 20 ms. O estado
adaptativo é reiniciado a cada gravação. A seleção compara quatro modos e
seis limites de sequência de quadros ativos, usando apenas as sessões
marcadas como treino. A calibração não escolhe os parâmetros.

Configuração selecionada: modo 3 e pelo menos 15 quadros consecutivos ativos
(300 ms). No treino, passaram 95/95 falas e 1/30 silêncios. Na calibração,
passaram 54/55 falas e 0/25 silêncios. A fala recusada era um exemplo `_unknown`;
nenhuma das 100 gravações com texto esperado foi bloqueada.

| Referência no conjunto completo | Sem detecção de fala | Com detecção de fala |
| --- | ---: | ---: |
| Textos conhecidos exatos | 89/100 | 89/100 |
| Silêncios que produziram texto | 19/55 | 0/55 |
| Falas `_unknown` com texto produzido | 50/50 | 49/50 |

O filtro é uma detecção de atividade, não uma recusa de vocabulário: ele deve
deixar passar fala válida mesmo que a palavra não esteja no léxico. A etapa
que distingue palavras cadastradas de fala fora do léxico ainda falta.
Zero ocorrências nestes 55 silêncios não garante taxa zero em uso.

O relatório final é `.pio/voice/vad-validation-20261107-02.json`. Ele registra
o modelo de referência, o hash do script, todos os parâmetros comparados e
uma fila após o filtro, com os nove grupos/11 áudios discrepantes de fala.
O relatório sem metadados adicionais `...-01.json` foi preservado.

## Metadados da base pública

Foi consultado o repositório oficial
[FLEURS](https://huggingface.co/datasets/google/fleurs), commit
`70bb2e84b976b7e960aa89f1c648e09c59f894dd`. A configuração `pt_br` declara
16 kHz, transcrições, licença CC-BY-4.0 e as divisões:

- treino: 2.793 registros;
- validação: 386 registros;
- teste: 919 registros.

Esses são metadados declarados pela fonte. Nenhum áudio público foi baixado,
nenhum modelo foi treinado com essa base e duplicatas entre seus splits ainda
precisam de auditoria na exportação. O relatório é
`.pio/voice/fleurs-metadata-validation-20261107.json`.

## Reproduzir

Use Python 3.11 e o ambiente de treinamento já descrito no projeto:

```powershell
& '.\.venv-voice\Scripts\python.exe' -m pip install -r tools/voice/requirements-reference.txt
& '.\.venv-voice\Scripts\python.exe' tools/voice/download_reference.py
& '.\.venv-voice\Scripts\python.exe' tools/voice/validate_reference.py --output .pio/voice/reference-audit-next
& '.\.venv-voice\Scripts\python.exe' tools/voice/validate_vad.py --reference .pio/voice/reference-audit-next --output .pio/voice/vad-validation-next.json
```

O download precisa de acesso à rede e fica separado da inferência. Para
retomar o mesmo modelo, use `--revision` com o commit registrado acima.
O comando de inferência não usa o ID remoto do modelo. Para retomar após
interrupção, repita com `--resume`; código, modelo e entradas devem coincidir
com `run.json`. Saídas novas não sobrescrevem experimentos anteriores.

```powershell
./tests/run_voice_tests.ps1
& '.\.venv-voice\Scripts\python.exe' tests/voice_workflow_test.py
& '.\.venv-voice\Scripts\python.exe' tests/voice_alignment_test.py
& '.\.venv-voice\Scripts\python.exe' tests/voice_phonetic_test.py
& '.\.venv-voice\Scripts\python.exe' tests/voice_training_test.py
& '.\.venv-voice\Scripts\python.exe' tests/voice_reference_test.py
& '.\.venv-voice\Scripts\python.exe' tests/voice_vad_test.py
```

## Próximo marco

As evidências favorecem usar uma base acústica maior: uma referência já
treinada reconhece grande parte dos mesmos áudios que os protótipos pequenos
confundiram. Isso não isola uma causa única nem valida automaticamente os
alvos fonéticos. O trabalho seguinte é preparar um subconjunto público
auditável, suportar comprimentos variáveis e treinar/validar uma base compacta,
comparando fonemas com caracteres e preservando os testes reservados.

Continuam pendentes revisão das divergências, recusa de fala fora do léxico,
teste independente e validação no relógio. Os arquivos originais das sessões
e o firmware permanecem preservados.
