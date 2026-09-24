# Teste de palavras do Vox

O Vox reconhece palavras isoladas com o MultiNet6 em inglês. Cada palavra
portuguesa tem uma grafia inglesa aproximada em
`src/services/voice_lab_service.c`. O visor e a linha `VOX:` da serial mostram
diretamente o campo `string` retornado pelo modelo, sem convertê-lo para a
palavra portuguesa. O ID detectado continua associado a um comando cadastrado,
mas esse campo pode variar entre tentativas do mesmo comando. Ele não equivale
a uma transcrição livre em português nem expõe os fonemas captados. Sem
detecção, nenhuma linha `VOX:` é emitida.

## Coleta no relógio

1. Grave `pio run -e display_profile_o2 -t upload` e abra o monitor serial
   a 115200 baud. O firmware padrão mantém o texto no visor, mas não emite logs.
2. Abra Vox, toque no microfone e diga **uma** palavra cadastrada por vez.
   A escuta continua até tocar no microfone novamente ou sair do app.
3. Para cada palavra, faça pelo menos cinco tentativas com a mesma pronúncia,
   mantendo distância e ambiente semelhantes. Anote a palavra falada, o texto
   `VOX:` obtido e as tentativas sem reconhecimento.
4. Repita em outro ambiente e, se possível, com outra pessoa. Separe os erros
   de captura (voz baixa, corte, ruído) das confusões entre palavras.
5. Altere uma grafia aproximada por vez e repita exatamente as mesmas tentativas.
   Compare acertos, confusões e ativações indevidas antes de manter a mudança.

Exemplo de registro:

| Palavra falada | Grafia testada | Tentativas | Saída `VOX:` esperada | Sem resultado | Outras saídas |
| --- | --- | ---: | ---: | ---: | --- |
| Comprar | cone prah | 5 |  |  |  |

O log não contém áudio nem fonemas reconhecidos. Para investigar se o microfone
está cortando sílabas, é preciso uma captura PCM/WAV separada.
