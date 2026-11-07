# Voz pausada — 07/11/2026

Pausa solicitada pelo usuário para testar GPS. Não iniciar novas coletas,
downloads ou treinamentos até a retomada explícita da frente de voz.
As execuções de treinamento desta rodada já terminaram; não há treinamento
de voz que precise continuar durante o teste de GPS.

## Ponto preservado

- 205 gravações pessoais verificadas: 80 de treino, 80 de calibração e
  45 de contextos fonéticos. As originais permanecem locais e imutáveis.
- Referência grande de português executada offline no PC: 89/100 textos
  conhecidos exatos; não é um modelo embarcado. Com detecção de fala,
  as 19 transcrições em silêncio foram eliminadas nos 55 casos disponíveis,
  mantendo os 89 acertos. Isso não garante o mesmo resultado em áudio novo.
- Base pública FLEURS PT-BR: 128 frases completas para treino, 32 para
  validação, separação oficial preservada. Ensaio compacto de 100 épocas
  encerrado, sem qualidade para uso: checkpoint escolhido pela perda na
  época 38, CER de validação 82,30%, 0/32 frases exatas. O menor CER observado
  durante o desenvolvimento foi 66,29%, ainda insuficiente.
- 36 testes Python passaram, incluindo duração variável, comprimentos CTC,
  duplicatas/divisões e vínculo dos relatórios ao manifest. A retomada de
  checkpoint completo reproduziu previsões e métricas.
- Nenhum modelo próprio foi integrado ao reconhecimento no relógio. O
  firmware padrão conserva MultiNet; `vox_capture` é somente coleta de áudio.

## O que fica local, fora do commit

| Caminho | Conteúdo |
| --- | --- |
| `data/voice-local/` | Treino e calibração pessoais |
| `data/voice-phonetic-local/` | 45 contextos fonéticos |
| `.pio/voice/reference-audit-20261107-01/` | Transcrições e métricas da referência |
| `.pio/voice/vad-validation-20261107-02.json` | Diagnóstico da detecção de fala |
| `.pio/voice/public-ptbr-02/` | Áudio público convertido e manifest fixado |
| `.pio/voice/public-character-01/` | Ensaio compacto de 30 épocas |
| `.pio/voice/public-character-02/` | Ensaio compacto de 100 épocas, best/latest |
| `.pio/voice/workflow-public-final-02/` | Snapshot do fluxo e evidências finais |
| `.pio/voice-reference-model/` | Modelo grande para referência no PC |
| `logs/firmware-references/` | Referências locais restauráveis do firmware |

O push preserva código e documentação; **não é backup dos áudios, pesos ou
firmwares locais**. Preserve essas pastas ao trabalhar no GPS. Nenhuma limpeza
de `.pio`, dados ou referências de firmware faz parte desta pausa.

## Retomada

1. Ler o [fluxo](voice-workflow.md), a [validação da referência](voice-validation-20261107.md)
   e o [ensaio público](voice-public-20261107.md).
2. Reauditar os áudios antes de modificar o treinamento, usando uma saída nova:
   `workflow_status.py`, com os argumentos de referência, relatório público
   e manifest indicados no ensaio público. O snapshot PCM pessoal esperado é
   `214f4b5f0f2ab4ab8de22ab7ad0d78f9e9a90b5306db84c923768a288a6530a3`.
3. Ampliar a base acústica e estudar supervisão pela referência forte.
   Não pedir nova coleta pessoal apenas para compensar a qualidade ruim
   da rede treinada do zero com o subconjunto pequeno.
4. Somente depois adaptar à voz pessoal, calibrar recusa, congelar candidato
   e reservar um teste novo. Quantização, paridade Python/C, memória e latência
   no dispositivo continuam pendentes.

O limite atual de captura no relógio é de dois segundos. Os áudios públicos
de duração variável não alteram esse limite nem habilitam ditado de frases
no firmware. Testar GPS não exige iniciar ou retomar os scripts de voz.

## Verificação antes do commit da pausa

Em 07/11/2026, passaram novamente o build padrão `platformio run` no worktree,
os dois executáveis C de voz/captura e os 36 testes Python de voz. Não houve
upload ao relógio. Alterações locais anteriores de energia/desempenho foram
preservadas fora do commit desta frente. O commit não inclui dados privados,
modelos, `.pio`, referências de firmware, credenciais ou drivers de terceiros.
