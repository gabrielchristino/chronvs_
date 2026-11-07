# Próximas melhorias de desempenho

Atualizado em 08/10/2026. O firmware de referência é o padrão release/O2,
com submostradores fixos e proteção do arco de brilho. Procedimentos de build,
diagnóstico, geração dos caches e recuperação estão em [performance.md](performance.md).

## Situação atual

- O padrão usa LVGL e aplicação em O2, sem logs, consoles ou profiling.
- O mostrador permanece estático em repouso; atualiza ao acordar/voltar e,
  durante pressão de 600 ms, uma vez por segundo até soltar ou mover o dedo.
- Os anéis externos usam RLE na flash e uma linha de 824 bytes em PSRAM.
  Horas, semana e temperatura têm centros fixos; segundos, contornos internos
  e traços intermediários de minutos foram removidos.
- Gestos visuais são coalescidos em 20 ms. O pool LVGL permanece com 128 KiB
  em PSRAM, buffers duplos de 1/20 e transferências QSPI síncronas de 2 KiB.
- A proteção do brilho passou no build e no teste integrado; o firmware
  padrão foi gravado com hashes verificados. Confirmação física ainda pendente.

O usuário percebeu melhora com o cache dos anéis, com a retirada de logs e
com a simplificação do mostrador. Ainda há faixas do menu em posições
diferentes durante o movimento; a imagem parada está correta. Os relatos
são qualitativos e não identificam uma causa exclusiva nem medem autonomia.

## Resultados que orientam a próxima etapa

| Experimento | Resultado e limite |
| --- | --- |
| Fundo plano | Melhora percebida; indica contribuição do custo do mostrador |
| Menus sem conteúdo | Gelatina pareceu menor; ícones e textos foram omitidos juntos |
| Textos restaurados, sem ícones | Percepção semelhante ao teste sem conteúdo; custo dos textos não quantificado |
| Código dos painéis em O2 | Sem melhora perceptível; hoje o padrão já recebe O2 globalmente |
| Cache da face principal | Redução localizada medida em composição anterior; ganho físico não confirmado |
| Cache dos ícones do launcher | Comparação de pixels e fallback aprovados no host; ganho físico pendente |
| Painéis sem invalidação final | Resposta satisfatória percebida, mas gelatina persiste; ganho isolado não quantificado |

Esses candidatos continuam experimentais. Não promover caches ou omissões
sem comparar com o padrão atual. Tempos da composição orbital anterior não
representam o mostrador simplificado. TE/sincronismo permanece hipótese.

## Fila de trabalho

1. Confirmar no relógio a proteção do brilho: iniciar no arco, sair do
   contorno e soltar sem fechar; depois fechar com um novo gesto fora dele.
2. Validar o padrão release nos fluxos de menus, AUTO/ECO, despertar,
   alarmes, Clima e Vox, antes de atribuir ganhos a outro experimento.
3. Comparar o cache de ícones com menus completos, separando launcher e
   acessos rápidos. Verificar fade, falha de reserva e memória interna/DMA.
4. Medir o custo atual de horas, números de minutos/data, semana e
   temperatura; priorizar pelo custo observado, preservando a composição.
5. Avaliar o cache da face principal contra o vetor atual, com cenas e
   áreas equivalentes e com a mesma instrumentação nos dois builds.
6. Se o efeito residual persistir após reduzir o desenho, isolar a sequência
   de faixas e a varredura do painel. Alterações no driver exigem teste físico
   isolado e firmware restaurável; não iniciar por uma hipótese de TE.

## Critérios de cada experimento

Preservar uma referência verificável antes de gravar, alterar uma variável
por vez e comparar no mesmo brilho/perfil/ECO. Capturar despertar, pressão
prolongada e abertura/fechamento/cancelamento de cada painel separadamente,
com arrastes lentos e rápidos. Separar medições instrumentadas da avaliação
visual final sem diagnóstico.

Repetir os testes de função e interface afetados; conferir retorno ao vetor,
memória, gestos, áudio, Wi-Fi e suspensão quando aplicável. Build e mocks não
substituem avaliação no painel. Não adotar mudanças com ganho não repetível,
regressão visual, falha ao acordar ou perda de folga interna/DMA.

O registro extenso de tentativas foi retirado na limpeza do repositório.
O histórico versionado permanece no Git; novas
capturas devem registrar ambiente, cenário e resultado antes da consolidação.
