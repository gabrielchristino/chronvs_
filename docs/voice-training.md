# Voz pessoal em português: coleta, treinamento e avaliação

O [fluxo de evolução](voice-workflow.md) organiza as próximas etapas, critérios
de avanço e aproveitamento dos áudios já coletados. Este documento mantém os
comandos de coleta e o histórico dos pilotos.

## Objetivo e estado atual

O objetivo é reconhecer offline as **200 palavras portuguesas já cadastradas
no Vox**, inicialmente com a voz de uma pessoa, e depois permitir compor notas.
Cada pessoa coleta suas amostras e treina seu próprio candidato no PC. O áudio
e o treinamento permanecem locais; não há serviço de transcrição em nuvem.

Em paralelo ao piloto, o [estudo de reconhecimento por unidades fonéticas](voice-phonetic.md)
investiga aprender sons compartilhados entre palavras, com roteiro de cobertura
e protótipo CTC no PC. A coleta atual permanece independente desse experimento.

Implementado nesta etapa:

- Firmware opcional `vox_capture`: captura PCM do microfone do próprio relógio.
- Coletor guiado com repetição, reprodução opcional e retomada de sessão.
- Treinamento neural em CPU, exportação de pesos int8 e calibração de recusa.
- Avaliação separada, hashes de integridade e detecção de áudio duplicado.

**Ainda não há modelo treinado com a voz do usuário, nem carregamento do novo
formato no relógio.** O firmware padrão continua usando o MultiNet6 original.
O candidato exportado precisa de implementação e comparação do processamento
de áudio/inferência em C, medição no S3 e teste físico antes de substituir esse
backend. A primeira sessão real de coleta foi concluída, conforme o registro
abaixo. A inteligibilidade é revisada por quem ouve e aceita cada gravação;
as estatísticas e hashes não medem sozinhos a qualidade de reconhecimento.

Esta primeira rede reconhece **uma palavra isolada por janela de dois segundos**.
Não é um transcritor de frases. Reconhecer as palavras separadamente não prova
que reconhecerá uma sequência como “comprar pão amanhã”. Segmentação, pausas,
palavras repetidas e edição da nota são etapas futuras. O vocabulário também
não contém todos os conectivos, flexões ou nomes necessários para ditado livre.

## Por que um novo modelo

O classificador do pda-os recebe texto e identifica intenções. O Vox atual usa
um modelo acústico inglês e grafias aproximadas: editar essas grafias não
aprende a pronúncia de quem fala. A documentação do
[ESP-SR](https://github.com/espressif/esp-sr) descreve MultiNet para comandos
em inglês/chinês. O
[exemplo micro_speech da Espressif](https://github.com/espressif/esp-tflite-micro/blob/master/examples/micro_speech/README.md)
demonstra reconhecimento compacto no ESP32; não é evidência de qualidade ou
compatibilidade do nosso candidato de 200 palavras com esta versão do ESP-IDF.

O primeiro candidato próprio usa uma rede pequena e um formato explícito,
sem depender de um treinamento proprietário do MultiNet. Sua adequação às
200 palavras deve ser medida: se houver confusões persistentes, revisar dados,
representação temporal e arquitetura antes de ampliar a integração.

## Preparar o PC

Execute na raiz do Chronvs, com Python 3.11. O exemplo usa o Python já instalado
pelo PlatformIO somente para criar um ambiente independente:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' -m venv .venv-voice
& '.\.venv-voice\Scripts\python.exe' -m pip install torch==2.5.1 --index-url https://download.pytorch.org/whl/cpu
& '.\.venv-voice\Scripts\python.exe' -m pip install -r tools/voice/requirements.txt
```

Em outra máquina, substitua o primeiro executável por seu Python 3.11.
As dependências de referência são NumPy 1.26.4, PyTorch 2.5.1 CPU e pyserial 3.5.
GPU não é necessária. O download das dependências exige internet; depois disso,
coleta, treinamento e avaliação não precisam de rede.

`.venv-voice/`, `data/voice-local/` e `.pio/` ficam fora do Git. As gravações são
pessoais: outras pessoas devem gerar sua própria base. Se escolher outra pasta
de saída, mantenha também essa pasta fora de commits. Não publique backups de
firmware, pois podem conter credenciais.

## Preparar o relógio para coleta

Antes de gravar um experimento, preserve um firmware restaurável:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run
& 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' scripts/firmware_reference.py --save waveshare_esp32_s3_touch_lcd_146
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e vox_capture
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e vox_capture -t upload
```

Selecione **ON** no painel e desligue **ECO** durante a coleta. Isso evita que a
inatividade encerre o envio e que light sleep desconecte o USB. Feche qualquer
monitor serial antes de abrir o coletor. Abra Vox no relógio.

Neste firmware, tocar no microfone grava **dois segundos**, sem carregar
MultiNet. Fale quando aparecer “Gravando”. Há um buffer temporário de 64.000
bytes em PSRAM; o microfone I2S é fechado antes de enviar a amostra. A conversão
usa o slot direito, 32 bits, deslocamento de 14 bits e saturação para PCM16.
Essa escala precisa ser conferida nas primeiras gravações; não presuma que um
áudio sem erro de transporte esteja livre de corte, distorção ou volume baixo.

O envio é posterior à captura e pode levar vários segundos. A serial usa
linhas hexadecimais `VOXPCM`, versão, sequência e checksum FNV-1a. O coletor
recusa perdas, ordem incorreta ou checksum divergente. O WAV recebe também
SHA-256. FNV-1a detecta erros de transporte; não é assinatura de autenticidade.
Não toque novamente até concluir o envio. Sair por gesto ou apagar a tela
cancela a operação; uma transmissão incompleta não vira amostra aceita.

No primeiro ensaio físico em 06/11/2026, o coletor recusou pacotes com
`display_perf` intercalado dentro do hexadecimal; o envio também disparou o
watchdog. A correção deixa a console ativa exclusivamente para a coleta,
desativa profiling/logs nesse ambiente e cede um tick ao sistema após cada
pacote. O coletor mantém `transport.log` local para diagnóstico e continua
recusando registros contaminados. Mocks verificam a liberação do processador
por pacote; confirmar a amostra completa e o áudio no relógio é indispensável.

## Começar por um piloto, preservando a meta de 200 palavras

Antes de gravar milhares de amostras, valide captura e confusões com um pequeno
subconjunto. Exemplo com seis palavras do vocabulário existente:

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/collect.py --port COM3 --speaker gabri --session dia1-manha --split train --words Comprar Fazer Hoje Casa Pão Leite --repetitions 5 --playback
```

O terminal apresenta a palavra; pressione Enter para preparar e então toque no
microfone do relógio. Ouça a gravação, aceite com Enter ou digite `r` para repetir.
`q` encerra entre amostras; Ctrl+C interrompe. Repetir exatamente o comando
retoma os registros já aceitos, verificando os hashes. Se houver WAV sem registro
devido a uma interrupção durante o salvamento, o coletor pede inspeção em vez de
sobrescrever o arquivo.

### Coleta sem Enter (Windows)

Acrescente `--hands-free` ao comando para controlar o avanço pelo relógio.
Essa opção pode ser ativada ao retomar uma sessão manual; não muda o plano,
os rótulos ou os registros já aceitos.

- O terminal apresenta a palavra. Basta tocar no gravador do Vox.
- Ao terminar o envio, o áudio é reproduzido com `--playback` e fica pendente.
  O terminal apresenta o próximo pedido.
- Outro toque confirma o áudio anterior e grava o pedido seguinte. Aguarde
  a reprodução e leia o próximo pedido antes de tocar novamente.
- `r`, sem Enter, repete o último áudio ouvido enquanto ele estiver pendente.
  Durante um envio, `r` recusa essa tentativa para repetir o pedido atual.
- `q`, sem Enter, salva o último áudio completo pendente e pausa. Ctrl+C deixa
  esse WAV pendente para ser recuperado e reproduzido ao retomar.
- O último áudio da sessão é confirmado automaticamente cinco segundos após
  a reprodução; `r` continua disponível nesse intervalo.

A porta reconecta automaticamente quando retorna, sem pedir Enter. Capturas
parciais são descartadas e o pedido interrompido é mantido. Os áudios já aceitos
nunca são apagados por `r`. A gravação física ainda exige o toque no relógio.

Em 07/11/2026, a calibração foi pausada com 44 amostras aceitas e a 45ª já
recebida em `pending.wav`. O modo sem Enter preserva e reapresenta essa gravação
ao retomar. Os testes simulam repetição, avanço pelo próximo toque, recuperação
do pendente, desconexão no meio do envio e confirmação final sem teclado.

Se o USB desconectar ao preparar ou receber uma amostra, o coletor fecha a
porta inválida, preserva os registros aceitos e retorna à mesma gravação.
Reconecte o relógio, acorde a tela, selecione ON, desligue ECO e abra Vox;
Enter prepara uma nova tentativa e reabre a porta. Dados parciais não viram
WAV aceito. Se o Windows trocar o número da COM, saia com `q` e repita o
comando com a nova `--port`, mantendo os parâmetros da sessão.

Em 07/11/2026, uma desconexão interrompeu a sessão de calibração na tentativa
17/80. Os 16 registros já aceitos passaram na verificação de formato e hash.
Foi acrescentado tratamento de erro serial nos coletores de palavras e de
contextos fonéticos, incluindo falhas ao abrir, limpar e ler a porta.

Também são solicitadas 25 amostras de cada classe de fundo por sessão:

- `_unknown`: fala real fora das 200 palavras, variando o que é dito; inclua
  palavras parecidas e pequenas frases que não devem produzir uma anotação.
- `_silence`: ausência de fala, com o ruído real de ambientes diferentes.

Não gere todo o silêncio artificialmente com zeros nem use apenas tons ou TTS
como substitutos da voz. O RMS e a fração de amostras saturadas aparecem no
terminal; ouça especialmente amostras muito baixas ou saturadas. Rejeite palavras
cortadas, fala errada ou mais de uma palavra. As estatísticas são indicadores,
sem limites de qualidade validados para este microfone.

O coletor usa a lista portuguesa diretamente de `voice_lab_service.c`, sem as
aproximações inglesas, e congela o vocabulário nos metadados de cada sessão.
Para coletar todas as 200 palavras, **omita `--words`**. Use
`--background-repetitions` para ampliar as negativas. Mudanças no vocabulário
exigem uma revisão explícita da base; não reutilize rótulos por posição.

## Separar sessões antes de treinar

Uma sessão representa uma ocasião real de gravação, identificada por pessoa e
nome. Faça pausas entre sessões, varie ligeiramente distância, posição do braço,
intensidade da voz e ambiente. Não separe gravações consecutivas da mesma sessão
entre treino e avaliação. Não altere o `split` de uma sessão para melhorar números.

Como ponto de partida experimental, colete três sessões de treino com cinco
repetições por palavra, uma sessão de calibração e outra de avaliação. São
25 gravações por palavra no total, além das negativas; isso **não é uma garantia
de suficiência**, especialmente para 200 classes. Comece pelo piloto e aumente
conforme as confusões observadas. Use os mesmos `--words` em todas as etapas.

Para o piloto, repita o comando da coleta com nomes novos e os destinos:

```text
dia1-manha    train
dia1-tarde    train
dia2-manha    train
dia2-tarde    calibration
dia3-manha    evaluation
```

`data/voice-local/<pessoa>/<sessao>/` contém `session.json`, `manifest.jsonl` e
WAVs mono PCM16 little-endian de 16 kHz e 32.000 amostras. A sessão define o
conjunto inteiro. O manifest registra palavra, instante UTC, hash do PCM e
estatísticas. Retomar não modifica o conjunto nem a ordem planejada.

## Treinar e congelar um candidato

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/train.py --dataset data/voice-local --words Comprar Fazer Hoje Casa Pão Leite --output .pio/voice/piloto-01
```

Para as 200 palavras, omita `--words` também aqui. O treinador exige exemplos
de todas as classes selecionadas e das duas classes de fundo em treino e
calibração. Confira a documentação do seu candidato em `report.json`.

O processamento versão 1 usa 16 kHz/2 s, janelas Hann simétricas de 400 amostras,
salto de 160, FFT de 512 e 32 filtros mel triangulares de 80 a 7.600 Hz. Calcula
potência `abs(rfft)^2/512`, log natural com piso `1e-10` e médias em 32 faixas
temporais: 1.024 entradas. Média/desvio por banda vêm **somente do treino**;
o desvio tem piso 0,1. Não há detecção de início/fim de palavra nesta etapa.

A rede padrão é 1.024 → 96 → ReLU → dropout 0,2 no treino → 202 saídas para o
vocabulário completo. Treina em CPU com AdamW, minibatches de 32, pesos de
classe e até 150 épocas, interrompendo após 25 sem melhora. Seleciona o checkpoint
pela perda na calibração. Os pesos de cada saída são quantizados simetricamente
para int8 com escala por linha; vieses, normalização e cálculos de referência
permanecem float32. **Não é uma rede com execução inteira int8.**

A calibração escolhe confiança e margem para recusar entradas, já usando os
pesos quantizados. Se nenhum limite atingir a precisão empírica alvo de 95%,
exporta limites que recusam tudo. O alvo não garante 95% de qualidade em uso.
Silêncio/desconhecido sempre representam recusa de palavra. O relatório mostra
acertos aceitos, aceitações erradas, falsas aceitações de fundo e cobertura.

Saídas em pasta nova: `voice.bin` e `report.json`, incluindo rótulos, parâmetros,
versões, semente, hashes dos scripts e das amostras efetivas. O treinador não
abre o conjunto `evaluation`. Mesmas bibliotecas e máquina podem reproduzir
pesos; outros ambientes precisam de comparação numérica. Não sobrescreva um
candidato já avaliado.

## Avaliar em gravações reservadas

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/evaluate.py --dataset data/voice-local --candidate .pio/voice/piloto-01 --output .pio/voice/piloto-01-evaluation.json
```

O avaliador mantém pesos e limites congelados. Exige todas as classes na
avaliação, verifica hashes e recusa áudio ou sessões usados no treino/calibração.
Gera métricas e matriz de confusão com esperado nas linhas e predição bruta
nas colunas. Examine por palavra, sobretudo pares parecidos, e conte negativas
aceitas como palavras. Não confunda acurácia bruta com precisão entre aceitos.

Uma avaliação usada para corrigir erros passa a ser regressão conhecida. Para
alegar generalização melhor, grave novas sessões reservadas antes dos ajustes.
Um candidato de outra pessoa deve usar outra raiz de dados; misturar vozes é um
experimento diferente. Renomear sessões não torna áudios independentes; o
verificador detecta cópias byte a byte, não repetições semanticamente relacionadas.

## Formato experimental e integração futura

`voice.bin` usa little-endian, nesta ordem:

| Campo | Conteúdo |
| --- | --- |
| Cabeçalho, 24 bytes | magic ASCII `CVVOX001`, versão u32=1, entrada u32=1024, ocultas u32, classes u32 |
| Normalização | 32 médias float32, depois 32 desvios float32 |
| Camada 1 | pesos int8 `[ocultas,1024]`, escalas float32 `[ocultas]`, vieses float32 `[ocultas]` |
| Camada 2 | pesos int8 `[classes,ocultas]`, escalas float32 `[classes]`, vieses float32 `[classes]` |

Cada linha de peso é reconstruída por `int8 * escala`. A primeira camada usa
ReLU e a segunda, softmax. O relatório associado fornece rótulos, confiança,
margem e SHA-256. Para 96 ocultas e 202 classes, o binário tem **120.360 bytes**.
Esse tamanho não mede arena de inferência, FFT, pilha, DMA ou latência.

Antes de rodar no relógio: portar o processamento e inferência sem PyTorch,
comparar vetores/probabilidades Python e C, medir RAM interna livre/maior bloco
DMA e PSRAM, tempo por janela e efeitos sobre interface, áudio e consumo. Manter
os parâmetros validados de QSPI/LVGL e alocar grandes buffers não DMA em PSRAM.
Não carregar este arquivo na partição ESP-SR como se fosse MultiNet.

Depois da aceitação de palavras: criar segmentação de fala contínua, testar
palavras consecutivas/repetidas e montar um rascunho revisável no Notas. A voz
reconhecida não deve inventar conteúdo ausente ou descartar um rascunho quando
falhar. Notas livres continuam fora do escopo deste classificador fechado.

## Verificação da infraestrutura

```powershell
./tests/run_voice_tests.ps1
& '.\.venv-voice\Scripts\python.exe' tests/voice_training_test.py
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run -e vox_capture
```

Os testes de host exercitam a tarefa C real de coleta, leituras parciais,
cancelamento, liberação, saturação e decodificação do protocolo pelo Python.
O teste opcional de treinamento usa **sinais sintéticos**, verifica repetibilidade
de pesos, exportação, avaliação separada e recusa de artefato adulterado. Ele não
mede reconhecimento de voz. A lista de testes aprovados e os limites físicos
devem acompanhar cada revisão; build e mocks não substituem gravações reais.

Em 06/11/2026, passaram os testes C/Python de captura e base de dados e o
teste de treino/exportação/avaliação com NumPy 1.26.4 e PyTorch 2.5.1+cpu.
O treinamento de teste reproduziu o binário byte a byte em duas execuções.
Os firmwares padrão e `vox_capture` compilaram. Esses testes sintéticos não
medem qualidade de reconhecimento pessoal.

### Primeira coleta pessoal — 06/11/2026

Após corrigir a mistura de logs nos pacotes e a falta de tempo para o idle
durante a transmissão, a sessão `gabri/20261106-piloto01` foi concluída com
80 amostras de treino: cinco de Comprar, Fazer, Hoje, Casa, Pão e Leite,
25 de silêncio e 25 de fala fora do vocabulário. Cada WAV tem dois segundos
(160 segundos no total). Todos os hashes, formatos e estatísticas recalculadas
conferiram; não há áudio duplicado nem amostras saturadas. O RMS global ficou
entre 0,002849 e 0,071881. Esses números descrevem esta coleta, sem estabelecer
um limiar universal de qualidade ou uma medição de acurácia.

As gravações ficam somente na pasta local ignorada pelo Git. Essa sessão
continua inteiramente em treino. O próximo passo do piloto é gravar sessões
independentes de calibração e avaliação com as mesmas seis palavras e classes
de fundo; não dividir as primeiras 80 entre os três destinos. O primeiro
candidato terá apenas seis palavras, antes de ampliar gradualmente até 200.

Para restaurar o firmware padrão, use `pio run -t upload` ou a referência local
verificada conforme [`performance.md`](performance.md). O novo processo de
coleta/treino não altera notas existentes nem publica áudios automaticamente.

### Calibração concluída e primeiro candidato — 07/11/2026

A sessão `gabri/20261106-piloto02-calibracao` terminou com 80 WAVs: cinco
por palavra do piloto, 25 silêncios e 25 falas fora do vocabulário. As duas
sessões somam 160 amostras, sem duplicatas ou hashes divergentes. Não restou
`pending.wav`. Quatro WAVs da calibração têm alguma saturação; o máximo por
arquivo é 14/32.000 amostras (0,04375%). Isso não demonstra sozinho nem ausência
nem presença de distorção audível.

O candidato `.pio/voice/piloto-01` foi treinado com o comando documentado,
semente 41, selecionando a época 8 pela calibração. A saída bruta acertou
43/80 classes (53,75%), incluindo os fundos; nas palavras, acertou 9/30 (30%).
Forçando uma escolha somente entre as seis palavras, foram 14/30. Nenhum dos
limites testados atingiu a precisão empírica alvo de 95%; por isso, confiança e
margem ficaram em 1,01, recusando todas as entradas. Esse candidato não foi
instalado no relógio e não está aprovado para uso.

O diagnóstico reprodutível fica em `tools/voice/diagnose_pilot.py`:

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/diagnose_pilot.py --word-model .pio/voice/piloto-01 --phonetic-model .pio/voice/phonetic-fitcheck-02 --lexicon .pio/voice/phonetic-draft-01/lexicon.json --output .pio/voice/piloto-01-calibration-diagnostic.json
```

Use outra saída se o arquivo já existir. Esse comando confere hashes dos
artefatos e compara os modelos congelados na **calibração**, sem treinar novos
pesos. A comparação CTC forçada entre seis palavras acertou 5/30; o erro de
unidades da decodificação gulosa foi 97,78%, comparado com os alvos fonéticos
automáticos. O zero no treino observado anteriormente não generalizou para
esta sessão. O CTC ainda não tem um limiar calibrado de aceitação, portanto
seu ranqueamento não é comparável à política de recusa do classificador.

A avaliação independente ainda não foi coletada. Antes de pedir nova rodada,
revisar a inteligibilidade/duração das palavras e investigar representação,
variação entre sessões e quantidade de dados. Esses resultados não justificam
promover o modelo ou declarar reconhecidas as 200 palavras.

### Controle de representação e tempo — 07/11/2026

O usuário confirmou voz clara e palavras inteiras nas reproduções. Isso
favorece investigar o processamento/modelo, sem atribuir automaticamente o
resultado ruim ao microfone. Foi criado um controle por exemplos acústicos
e alinhamento temporal (`diagnose_alignment.py`), usando os mesmos conjuntos:

```powershell
& '.\.venv-voice\Scripts\python.exe' tools/voice/diagnose_alignment.py --output .pio/voice/piloto-alignment-diagnostic-01.json
& '.\.venv-voice\Scripts\python.exe' tests/voice_alignment_test.py
```

O controle extrai MFCCs sem o coeficiente de energia global, inclui diferenças
temporais, recorta a atividade por energia com margem e usa DTW para acomodar
variações de duração. A escala dos coeficientes vem somente do treino. A classe
usa a média das três menores distâncias aos seus exemplos de treino. Não há
rede neural ou compartilhamento de fonemas neste controle.

Na calibração, acertou 20/30 palavras com as classes de fundo disponíveis,
contra 9/30 do primeiro MLP. Forçando uma escolha entre seis palavras, foram
24/30, contra 14/30 do MLP. Acertou todos os 25 silêncios, mas classificou apenas
2/25 falas desconhecidas como `_unknown`: 18 foram para `_silence` e cinco
viraram palavras indevidamente. O total de classes corretas foi 47/80.

Esses resultados sugerem que representação espectral, duração e posição da
fala merecem correção antes de ampliar a coleta. Várias escolhas mudaram
juntas nesse controle, portanto ele não isola qual componente causou o ganho.
Ele não tem política de recusa calibrada, não foi avaliado em um conjunto
independente e não demonstra reconhecimento fonético de palavras novas.
Os três testes verificam alinhamento com durações diferentes, invariância a
ganho sem saturação, silêncio finito e recusa de entradas inválidas. A próxima
revisão neural/fonética deve usar esse controle e estas limitações como referência.
