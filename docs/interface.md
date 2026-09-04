# Interface, gestos e renderização

Este documento descreve o comportamento observável da interface e os limites
que devem ser preservados no display SPD2010 de 412 × 412 pixels.

## Camadas da interface

```text
lv_scr_act()
├── content_layer                 apps ativos e prévias de navegação
│   ├── watch                     mostrador
│   ├── apps                      lista de aplicativos
│   └── aion                      cronômetro
└── settings_panel                painel global de acessos rápidos
```

`content_layer` é administrada por `core/app_manager.c`. Cada app é criado
preguiçosamente na primeira abertura e depois apenas ocultado ou exibido. O
`settings_panel` é global e fica acima dessa camada; por isso ele continua
disponível sobre o mostrador sem pertencer a um app.

## Navegação por gesto

| Tela atual | Gesto | Resultado |
| --- | --- | --- |
| Mostrador | Arrastar da borda superior para baixo | Abre os acessos rápidos acompanhando o dedo. |
| Mostrador | Arrastar para a esquerda | Revela a lista de apps acompanhando o dedo. |
| Acessos rápidos | Arrastar para cima | Fecha o painel. O limiar é 5 px e vale também sobre o arco de brilho. |
| Lista de apps | Tocar uma linha | Abre o app selecionado. |
| Lista de apps | Arrastar 70 px para a direita | Volta ao mostrador. |
| Aion | Arrastar 80 px para a direita | Volta à lista de apps. |

Os movimentos visuais são limitados a uma atualização a cada 20 ms. Isso evita
invalidar a árvore LVGL em cada amostra do touch e mantém o painel próximo ao
dedo sem formar uma fila de quadros antigos.

## Painel de acessos rápidos

O painel usa fundo retangular opaco `#26302B`, botões `#748173`, texto claro e
acento amarelo `#F2B84B`, a mesma linguagem visual da lista de apps.

- O arco externo seleciona brilho de 10% a 100%.
- O botão superior esquerdo alterna os perfis `15s`, `30s` e `ON`.
- O botão superior direito mostra a bateria e alterna o modo `ECO`.
- O botão central abre a lista de apps.
- Os demais círculos são espaços reservados para futuros atalhos; não recebem
  toque.

Brilho, perfil de tempo e modo econômico são gravados na NVS. Quando o modo
ECO limita o brilho a 35%, o valor do arco também reflete esse teto.

## Política de energia

| Perfil | Reduz brilho | Apaga backlight |
| --- | ---: | ---: |
| `15s` | 15 s | 45 s |
| `30s` | 30 s | 2 min |
| `ON` | nunca | nunca |
| `ECO` | 5 s | 15 s |

Ao apagar, o PWM do backlight é configurado para 0%, o timer de atualização do
mostrador é pausado e o loop principal deixa de consultar RTC e bateria. LVGL e
touch continuam ativos somente para receber o toque que acorda a tela. Esse
primeiro toque apenas acorda o relógio; não aciona controles.

## Renderização e desempenho

O projeto usa **LVGL 8.3.11**, distribuído na referência oficial da Waveshare.
Embora muitos guias recentes tratem de LVGL 9, os princípios de reduzir áreas
invalidadas e evitar trabalho por quadro continuam válidos, mas os detalhes do
driver não podem ser copiados diretamente.

- `LV_DISP_DEF_REFR_PERIOD` é 20 ms, com alvo de até 50 FPS durante um gesto.
  O mostrador normal continua atualizado uma vez por segundo.
- Há dois buffers LVGL em PSRAM. O driver SPD2010 deve permanecer no tamanho
  original de `1/20` da tela e em transferências QSPI de 2 KiB.
- O script `scripts/add_waveshare_drivers.py` reaplica essas escolhas ao
  exemplo da Waveshare antes de cada build. Não edite apenas a cópia em
  `.vendor-reference/`, pois o script é a fonte persistente da configuração.
- Wi-Fi só é usado na sincronização NTP e é desligado após a tentativa. Apps
  devem pausar timers em `on_hide` quando não forem necessários; `Aion` já faz
  isso para seu timer de interface.

### Configurações que não devem ser reintroduzidas

Foram testadas no hardware e apresentaram falhas:

| Alteração | Resultado no SPD2010 |
| --- | --- |
| Buffer de `1/10` ou maior | Linhas pretas grossas e tela listrada. |
| Dois buffers de tela inteira | O relógio inicia o desenho e trava. |
| Transferência DMA de 8 KiB | Instável quando combinada aos buffers grandes. |
| Chamar `lv_disp_flush_ready()` somente no callback assíncrono do QSPI | A tela deixa de atualizar após o primeiro quadro. |

Para melhorar a fluidez daqui em diante, prefira simplificar o conteúdo
redesenhado durante transições: menos objetos vetoriais ativos, menos texto ou
ticks e nenhuma tarefa de rede concorrendo com o gesto. Qualquer mudança no
driver deve ser testada isoladamente, com um firmware de recuperação pronto.

## Touch espúrio

O touch SPD2010 pode reportar um ponto isolado quando a tela não está sendo
tocada. O script de build aplica debounce no limite LVGL: um contato precisa
aparecer em duas amostras consecutivas, permanecer próximo e ter intensidade
válida. A liberação é imediata. Isso evita acordar a tela, alternar brilho ou
abrir painéis sem interação real.
