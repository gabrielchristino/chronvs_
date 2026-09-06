# Chronvs — instruções para agentes

Estas instruções se aplicam a toda alteração neste repositório. Leia também
[`README.md`](README.md), [`docs/interface.md`](docs/interface.md) e
[`docs/apps.md`](docs/apps.md) antes de alterar arquitetura, interface ou
driver de tela.

## Objetivo e plataforma

Chronvs é um relógio pessoal para a Waveshare ESP32-S3-Touch-LCD-1.46:

- ESP32-S3R8, 16 MB de flash e 8 MB de PSRAM OPI.
- Display redondo SPD2010 de 412 × 412 por QSPI.
- Touch SPD2010 por I2C, RTC PCF85063, IMU QMI8658 e bateria no ADC.
- ESP-IDF 5.3.1, PlatformIO `espressif32 @ 6.9.0` e LVGL 8.3.11.

O projeto prioriza uma interface fluida, discreta e consistente: fundo verde
escuro, superfícies cinza-esverdeadas, texto claro e amarelo como acento.

## Arquitetura

```text
src/
├── apps/       telas e estado próprio de cada app
├── core/       catálogo e ciclo de vida de apps
├── platform/   inicialização do hardware da placa
├── services/   RTC, bateria, NTP e futuros dados compartilhados
├── ui/         painel rápido e política global de energia/toque
└── main.c      composição e loop principal LVGL
```

- Todo app é um `chronvs_app_t`, registrado com `CHRONVS_REGISTER_APP`.
  Não mantenha um array central de apps manualmente.
- Apps criam seus objetos LVGL como filhos do `parent` recebido. Hardware deve
  ser acessado por `services/`, nunca diretamente no app.
- Use `on_show` e `on_hide` para iniciar/pausar timers ou trabalho visual.
- `watch` e `apps` são internos e devem manter `launcher_visible = false`.
- Ícones de app são criados por `create_icon` dentro do contêiner fornecido;
  não adicione assets bitmap sem uma necessidade clara.

## Navegação e interação

Preserve estes contratos, salvo pedido explícito do usuário:

- Mostrador: arrastar da borda superior para baixo abre acessos rápidos;
  arrastar para a esquerda abre a lista de apps.
- Painel rápido: arrastar para cima fecha, inclusive quando iniciado no arco
  de brilho.
- Lista de apps: tocar abre um item; arrastar para a direita volta ao relógio.
- Apps devem permitir retorno coerente à lista ou ao relógio.
- O primeiro toque com backlight apagado somente acorda a tela.

Os limites de gesto, layout e paleta estão documentados em
`docs/interface.md`. Atualize esse documento quando alterar comportamento
visível ou parâmetros de interface.

- Para conjuntos com sete opções circulares que caibam na tela, use sempre
  a disposição hexagonal **2–3–2**, como nos acessos rápidos, preservando a
  identidade visual. Ações complementares, como `Criar` e `Parar`, ficam abaixo.
- Opções e atalhos usam círculos de 70 px; ações textuais (`Criar`, `Excluir`,
  `Parar`, `Voltar`, `Iniciar` etc.) usam pílulas de 54 px de altura. Use os
  estilos compartilhados de `ui/control_style.h` e os construtores do Aion;
  não escolha proporções diferentes para a mesma categoria de controle.

## Display e desempenho — regras críticas

O driver da Waveshare tem limitações observadas em hardware. Não altere estes
parâmetros sem um teste isolado no relógio e uma forma rápida de restaurar o
firmware:

- LVGL deve ficar com `LV_DISP_DEF_REFR_PERIOD = 20` ms.
- O buffer LVGL é duplo, em PSRAM, e deve ter `1/20` da tela.
- A transferência QSPI deve permanecer em 2 KiB.
- Preserve a espera síncrona da fila QSPI antes de `draw_bitmap` retornar,
  aplicada por `scripts/add_waveshare_drivers.py`. Ela eliminou as listras
  no boot e nos avisos de timer, conforme teste confirmado no relógio.
- Não use buffers de tela inteira, buffers `1/10` ou maiores, transferências
  de 8 KiB, nem mova `lv_disp_flush_ready()` para callback assíncrono de QSPI.
  Essas experiências causaram, respectivamente, listras pretas ou travamento.
- Os gestos visuais devem ser coalescidos a cada 20 ms; não invalide a árvore
  LVGL a cada amostra do touch.
- O mostrador só deve atualizar uma vez por segundo em repouso. Com a tela
  apagada, não faça renderização, leitura de RTC nem leitura de bateria.

Os drivers da Waveshare ficam em `.vendor-reference/`, que não é versionado.
As correções persistentes são aplicadas por
`scripts/add_waveshare_drivers.py`; edite esse script, não apenas a cópia do
driver. O script também preserva a limpeza inicial preta do GRAM e o debounce
contra toques espúrios.

## Build e verificação

Antes de entregar mudança de código C, scripts de build ou configuração:

```powershell
& 'C:\Users\gabri\.platformio\penv\Scripts\platformio.exe' run
```

- Execute o build a partir da raiz do repositório.
- Se `firmware.elf` estiver bloqueado, aguarde ou encerre a tarefa de upload/
  monitor que o mantém aberto; não limpe a pasta `.pio` sem necessidade.
- `pio run -t upload` usa o uploader customizado e grava bootloader, partições
  e firmware nos endereços corretos.
- Não inclua `src/chronvs_secrets.h`, credenciais Wi-Fi ou `.vendor-reference/`
  em commits.

## Qualidade e documentação

- Use `apply_patch` para alterar arquivos.
- Preserve mudanças não relacionadas do usuário no worktree.
- Para mudanças visuais ou de gesto, registre o comportamento em
  `docs/interface.md`.
- Para mudanças no runtime de apps, atualize `docs/apps.md`.
- Mantenha o README como visão geral e links de entrada, sem duplicar detalhes
  extensos dos documentos especializados.
- Ao informar resultado, diga o que mudou, como foi validado e qualquer limite
  que ainda exija teste no dispositivo.

## Commits

Use commits pequenos, em português ou inglês claro, descrevendo o resultado.
Não faça `reset --hard`, checkout destrutivo, limpeza ampla ou exclusão de
artefatos sem autorização explícita. Antes de commit/push, confira
`git status`, revise o diff e confirme que segredos e arquivos gerados ficaram
de fora.
