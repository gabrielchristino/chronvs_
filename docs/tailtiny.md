# TailTiny no Chronvs

O ambiente opcional `tailtiny` conecta a placa ao Tailscale com o nome
`chronvs`. É um runtime dedicado à montagem sem tela: não inicializa
LCD, touch, LVGL, GPS, áudio, SD nem os apps. Mantém o Wi-Fi ligado e não
entra no light sleep manual do mostrador. Autonomia ainda não medida.
O firmware normal e suas sessões NTP/Clima permanecem independentes.

## Escopo e decisão de uso

Esta integração foi feita em **firmware de teste separado**, selecionado por
`pio run -e tailtiny`, e gravada no relógio para validar cadastro e conexão.
O ambiente padrão continua sendo `waveshare_esp32_s3_touch_lcd_146`: o runtime
normal dos apps não inicia a VPN nem depende de baixar o componente TailTiny.
Há pontos condicionais de build/entrada no repositório para selecionar o teste;
isso não habilita a função no firmware principal. Após o experimento, o padrão
foi compilado para regressão, sem ser gravado novamente no dispositivo.

O resultado atual é uma possibilidade técnica validada, ainda sem uma função
de produto que justifique VPN permanente no relógio. Não há decisão de
incorporá-la ao firmware principal nem medição de seu custo em bateria.

### O que funciona e o que ainda seria desenvolvimento

O firmware instalado cadastra o nó, mantém a conexão, recupera sua identidade
após reiniciar e abre um serviço de eco UDP na porta 9000 do túnel. O serviço
recebe bytes e devolve `ECHO: ` seguido dos mesmos bytes. Não executa comandos.
O eco está pronto no firmware; sua troca de dados com outro aparelho ainda
não foi confirmada neste ensaio.

Painel web, leitura remota de bateria/sensores, tocar som para localizar o
relógio, configuração de alarmes, sincronização de notas, download de GPX/CSV,
consulta remota de GPS e atualização de firmware pela rede são possibilidades
futuras. Não foram implementadas nem ficam disponíveis apenas por instalar
Tailscale. Este perfil não inicia os periféricos necessários a essas funções.
Também não há shell, SSH, compartilhamento automático de arquivos ou ponte
para a internet do celular.

### Conectividade e utilidade prática

Tailscale fornece o caminho privado entre aparelhos que já têm acesso à rede;
não fornece conexão à internet ao relógio. O Chronvs depende de Wi-Fi com
internet. Fora do alcance de uma rede configurada, dependeria de outra rede
ou do hotspot do celular. Manter o hotspot ligado o tempo todo foi considerado
inviável pelo usuário; a VPN não resolve essa dependência.

Um servidor doméstico, por exemplo Home Assistant, daria alvos úteis para
controle de luzes, cenas e avisos em casa, onde o relógio já teria Wi-Fi.
Home Assistant não é requisito do TailTiny e não precisa necessariamente
rodar em Raspberry Pi; poderia estar em outro computador adequado. Sem
servidor/serviço ou função concreta, conectar o relógio à mesma rede tem
utilidade limitada por si só.

Para o Chronvs, os usos com justificativa mais clara seriam sincronização
ocasional ao chegar em casa (por exemplo, enviar uma corrida), avisos
específicos e ações rápidas para serviços existentes. Esses fluxos poderiam
conectar somente quando necessário, mas o perfil atual mantém a conexão
permanente: operação sob demanda ainda não foi desenvolvida.

Para comunicação durante o dia junto do celular, uma alternativa discutida
é Bluetooth com um app Android que faça a ponte de dados com a internet.
Isso exige protocolo, firmware e app próprios; o diagnóstico BLE existente
não oferece essa ponte, notificações ou sincronização.

No PDA baseado em LilyGO T-Deck, teclado e tela tornam mais plausível o uso
direto de serviços remotos: terminal para computadores, mensagens via servidor,
consulta de arquivos e controle de serviços. Trata-se de uma direção futura,
sem implementação ou validação de TailTiny no T-Deck nesta sessão. O PDA
também precisaria de internet por Wi-Fi, hotspot ou modem compatível com sua
montagem. A discussão não autorizou nem iniciou migração para esse aparelho.

## Dependência e credenciais

TailTiny 3.3.0, commit `68351ae2908e1e77c46022c143582ddc6af5e76a`,
é uma dependência local ignorada pelo Git:

```powershell
git clone --depth 1 --branch v3.3.0 https://github.com/Asimmimx/tailtiny.git .vendor-tailtiny
```

O perfil reutiliza o Wi-Fi de `src/chronvs_secrets.h`. A chave de cadastro
fica em `src/chronvs_tailtiny_secrets.h`, também ignorado:

```c
#pragma once
#define CHRONVS_TAILSCALE_AUTH_KEY "sua-chave-de-cadastro"
```

Use Auth key, sem Reusable/Ephemeral e pré-aprovada quando possível. Não
publique arquivos de credenciais, logs privados, ELF ou imagens de firmware.
O servidor HTTP de configuração e o suporte a modem ficam desativados.
A verificação TLS dos relays DERP permanece habilitada. SNTP acerta somente
o horário UTC do sistema; o RTC PCF85063 não é modificado por este perfil.

## Build, gravação e restauração

Antes do primeiro ensaio, preserve os artefatos existentes:

```powershell
python scripts/firmware_reference.py --save waveshare_esp32_s3_touch_lcd_146
pio run -e tailtiny
pio run -e tailtiny -t upload
```

Usa o uploader e o mapa de partições do Chronvs, sem apagar a NVS. As chaves
privadas do nó são criadas na placa e salvas pelo TailTiny no namespace
`microlink`; reiniciar ou restaurar o firmware normal preserva a identidade.
Não execute erase-flash para resolver autenticação sem avaliar essa perda.
O script `configure_tailtiny.py` habilita criptografia e sockets apenas nesse
perfil; não altera buffers/QSPI, pool LVGL ou política de RAM do padrão.
Grandes buffers de protocolo usam a política PSRAM do componente.

Restaure a interface com `pio run -t upload`, ou use a referência preservada
via `CHRONVS_FIRMWARE_REFERENCE`, conforme `scripts/upload_waveshare.py`.
O upload padrão encerra a conectividade VPN; TailTiny não está integrado
simultaneamente ao runtime dos apps.

## Verificação

A console USB a 115200 registra `Connected! VPN IP: 100.x.y.z`, a abertura
do eco UDP 9000, estado, número de peers e RAM interna/maior bloco DMA/PSRAM.
Não registra a chave de cadastro, senha Wi-Fi nem conteúdo dos datagramas.
A conexão Wi-Fi pertence exclusivamente ao loop de rede; esse loop tenta
reconectar se perder a associação. Falha inicial tenta novamente após 30 s.

Capture sem bloquear um monitor interativo:

```powershell
python scripts/capture_tailtiny.py --seconds 45 --output logs/tailtiny-device.log
```

Acrescente `--reset` somente quando quiser reiniciar a placa. Cada arquivo
de captura deve ser novo; o capturador não sobrescreve logs existentes.

Em outro dispositivo conectado à mesma conta Tailscale, confirme `chronvs`
na lista e teste ping. Num PC da tailnet, teste tráfego WireGuard com:

```powershell
python .vendor-tailtiny/tools/udp_echo.py 100.x.y.z
```

O retorno esperado é `ECHO: hello`. Estar cadastrado no controle não comprova
tráfego entre peers: registre separadamente cadastro, DERP, ping/eco e reboot.
Desabilitar expiração da chave do nó é uma opção administrativa do usuário;
não é feito automaticamente por este firmware.

## Validação desta integração

Em 21/11/2026, o perfil compilou com 1.771.370 bytes de programa e 58.888
bytes estáticos. Upload pela COM3 verificou os hashes das quatro imagens.
O relógio cadastrou `chronvs`, recebeu `100.109.156.67` e viu o celular online.
O usuário confirmou no app Android o nó `chronvs` como **Connected**, com
o mesmo IPv4 e nome MagicDNS `chronvs.tuna-vernier.ts.net`. Essa confirmação
valida a presença do nó na conta, sem substituir o teste de tráfego pelo túnel.
Após reset USB real, carregou as chaves da NVS e reconectou ao mesmo IP em
cerca de 26 s, com DERP/TLS validado e eco UDP pronto. Não houve erase-flash.
O firmware dedicado permanece instalado, conforme solicitado.

A primeira captura usou somente RTS e não reiniciou a placa: não conta como
teste de persistência. A captura corrigida aplica o workaround DTR do driver
Windows usado pelo esptool e registra boot novo, chaves carregadas e reconexão.
Logs privados: `logs/tailtiny-upload.log`, `logs/tailtiny-device-20261121.log`
e `logs/tailtiny-reboot-confirmed-20261121.log`.

Antes do túnel, foram observados 155.003 bytes internos livres e maior bloco
DMA de 61.440 bytes. Após conectar, 64.119 bytes internos e maior bloco DMA
de 26.624 bytes; PSRAM livre de 8.358.240 bytes. São amostras deste perfil
sem UI, não limites seguros para adicionar VPN ao runtime completo.

O build padrão também passou (1.694.998 bytes de programa e 55.400 estáticos),
sem upload do padrão após o cadastro. Os testes de configuração verificam
USB, isolamento da política e repetição sem reescrever a configuração.
Não há ainda confirmação de ping/eco de dados a partir de outro dispositivo,
coexistência com interface, consumo em bateria ou serviços remotos de produto.
