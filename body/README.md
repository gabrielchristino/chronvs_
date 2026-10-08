# Carcaça e arquivos de impressão

Inventário do workspace em 08/10/2026:

| Arquivo | Conteúdo | Estado |
| --- | --- | --- |
| `chronvs.stl` | Malha STL atual da carcaça, 40.084 bytes | Modelo atualizado; encaixe e impressão não validados nesta etapa |
| `chronus.3mf` | Projeto 3MF com modelo, configurações e metadados de impressão, 33.719 bytes | Preservado como projeto editável; parâmetros não revisados nesta etapa |
| `EI_DIY_SmartWatch.zip` | Referência local contendo `Watch Case - Watch Body.stl` e `Watch Case - Watch Lid.stl`, além de metadados macOS | Preservada separadamente do modelo atual; origem não registrada no workspace |

O STL é uma malha e não registra unidade de medida. O 3MF mantém informações
do projeto de impressão; não foi demonstrado que sua geometria corresponde
exatamente ao STL atual. A referência ZIP não é firmware nem driver da placa.

Antes de considerar a carcaça pronta, conferir no fatiador a escala, orientação,
paredes, suportes e material, e testar fisicamente a placa, display, bateria,
botões, conectores e fechamento. Não há confirmação de impressão ou montagem
destes arquivos registrada nesta atualização. Builds e testes do firmware não
validam dimensões ou tolerâncias mecânicas.
