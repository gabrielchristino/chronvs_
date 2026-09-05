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
| Mostrador | Arrastar da borda superior para baixo | Abre os acessos rápidos sobre o mostrador, acompanhando o dedo com ganho visual 2×; menos de 120 px cancela a abertura. |
| Mostrador | Arrastar para cima | Revela a lista de apps sobre o mostrador, acompanhando o dedo desde a borda inferior. |
| Acessos rápidos | Arrastar para cima | Fecha o painel. O limiar é 5 px e vale também sobre o arco de brilho. |
| Lista de apps | Tocar uma linha | Abre o app selecionado. |
| Lista de apps | Arrastar para baixo a partir do topo | Fecha a lista acompanhando o dedo; o gesto precisa avançar 120 px. |
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
- O desenho vetorial do mostrador recorta as regiões cobertas por superfícies
  retangulares opacas acima dele (painel rápido e lista de apps). O recorte
  acompanha as coordenadas atuais dessas superfícies durante o arraste e a
  animação; ao recuarem, a região exposta volta a ser desenhada normalmente.
- Submostradores inteiramente fora da faixa de desenho são descartados antes
  de calcular seus textos, escalas e ponteiros. O horário usado na renderização
  só avança na atualização do relógio, não entre as faixas de um mesmo quadro.
  Essas otimizações preservam o visual, os gestos e os buffers parciais; o
  ganho de fluidez e a ausência de rastros ainda precisam ser medidos no painel.

Para validar no dispositivo, abra e feche os acessos rápidos lentamente e
rapidamente (também pelo arco), revele a lista e cancele o gesto, e retorne da
lista ao relógio. Observe especialmente a borda móvel e os submostradores que
reaparecem. Repita após apagar e acordar a tela, verificando que o primeiro
toque apenas acorda e que a hora é atualizada.

### CPU e biblioteca gráfica

A configuração em teste usa apenas a CPU a 240 MHz. Os caches permanecem nos
valores validados de 16 KiB para instruções e 32 KiB para dados; flash e PSRAM
continuam a 80 MHz, com linhas de cache de 32 bytes. LVGL permanece no fluxo
original de compilação, com `-O0` e `LV_MEMCPY_MEMSET_STD=0` (padrão da
biblioteca). Antes de testar outro ajuste, é necessário confirmar no relógio
que display, toque, gestos, serial e sincronização NTP continuam funcionando.

O teste anterior conjunto de CPU a 240 MHz, caches 32/64 KiB, LVGL com `-O2` e
rotinas de memória da plataforma compilou, mas foi seguido de tela apagada e
ausência de saída serial no dispositivo. Esses ajustes foram revertidos em
conjunto para recuperação; a causa individual ainda não foi isolada. Não
reaplique o conjunto. Futuras experiências devem alterar uma opção por vez,
verificando boot, display e serial antes de prosseguir. A otimização de recorte
vetorial anterior, confirmada pelo usuário, foi preservada.

As escolhas de CPU e cache estão em `sdkconfig.defaults`. Em um checkout já
configurado, atualize também as mesmas opções no `sdkconfig.waveshare_esp32_s3_touch_lcd_146`
local: defaults não substituem opções já salvas. Confira os valores efetivos
em `.pio/build/waveshare_esp32_s3_touch_lcd_146/config/sdkconfig.h` após compilar.
Para recuperar o dispositivo, use `pio run -t upload`: ele grava bootloader,
partições e aplicação com o mesmo build, nos endereços corretos. Não grave
apenas a aplicação ao trocar configurações de cache. Se o reset automático
não funcionar, entre no bootloader com BOOT pressionado ao acionar RESET,
solte BOOT e confira a porta USB antes de repetir o upload.

O alvo de 50 FPS não é uma medição: compare os mesmos arrastes no hardware.
O monitor opcional `LV_USE_PERF_MONITOR` do LVGL mede atividade do renderizador,
não a utilização total dos dois núcleos, e sua sobreposição também gera desenho.
Deixe-o desativado no firmware normal para preservar a política de tela apagada.

Referência: [otimização de velocidade do ESP-IDF 5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/performance/speed.html).

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
