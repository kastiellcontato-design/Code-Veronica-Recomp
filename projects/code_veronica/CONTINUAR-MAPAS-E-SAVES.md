# Janela de mapas e salvar em qualquer lugar (ferramentas de teste)

Pedido de Kastiel (16/10/2026): uma janela separada, aberta por atalho durante o
jogo, para ir a qualquer sala e salvar em qualquer lugar, para testar e otimizar.
Opção desligada por padrão (launcher, aba Jogo: "Ativar mapas de teste (F7)").

## Como usar

- F7, ou L3 + R3 + Triângulo no controle, abre a janela (só a janela; o launcher
  não aparece). Esc, "Voltar ao jogo" ou F7 fecham.
- Lista das salas lida dos arquivos do jogo (`biocv_disc/<idioma>/rdx_lnk/rm_*.arc`),
  com área, número e "versão de evento" (último dígito diferente de 0).
- Entrada: o ponto da sala onde a personagem aparece (porta ou passagem); 0 é o
  primeiro. Versões de evento podem começar no meio de uma cena.
- "Salvar aqui": abre a tela de salvar do próprio jogo em qualquer sala, sem
  máquina de escrever e sem fita. A fita e o contador de saves não mudam.
- Textos da janela e dos botões do launcher nos 13 idiomas (`cv_test_tools_i18n.h`).

## Como funciona (EU; JP/US: mesma lógica, endereços na tabela abaixo)

- Base do estado = CV_SRT_CHARACTER - 0x94 (= *(0xA89BD0)). Sala: +95 área,
  +96 número, +97 variante, +98 entrada. Tarefas +64, bloqueios +68, flags +70/+74.
- Partida disponível: personagem válido, vida (32 bits) > 0, tarefa 0x80 ativa,
  nenhuma de 0x1803F ativa, +74 & 7 == 0, +70 & 0x1C040008 == 0.
- Troca de sala: no dispatcher por quadro (func_000333A0, gancho TEST_FRAME),
  chama func_00190700 (o mesmo que o opcode de porta dos scripts) com r3=0,
  r4 área, r5 sala, r6 entrada, r7=255, e grava a variante em +97.
- Salvar: a máquina de escrever, ao aceitar a fita, liga +74 bit 0x00200000;
  func_001AEED4 vê o bit e abre a tela de salvar. O botão liga o mesmo bit.
- O serializador (func_001D9370, gancho TEST_SERIALIZE) chama func_001D90A8, que
  soma um save e gasta uma fita depois de copiar os dados: para saves do botão,
  inventário e contador são devolvidos (o arquivo é o mesmo).
- Em sala sem máquina de escrever não há nome de local ("NO_DATA" no slot), e o
  limite de exibição de "Saving content" da lista de slots (+1C) fica com lixo:
  a espera mínima (func_001D8D70) nunca acaba. Gravado o arquivo (gerente
  *(0xAF97C8)+4E8 == 0, objeto *(0xABF0D4) em estado 2/4), a ferramenta encerra
  a espera como o próprio jogo faria (+20=0, +0=0, +14=0xFFFA).

## JP e US (mapeado em 16/10/2026)

Funções casadas pelo corpo levantado normalizado (endereços, rótulos e números
mascarados; similaridade 0,83 a 1,0). func_001AEED4 tem no EU um teste a
mais (5 linhas) que JP/US não têm; a leitura de +74 e o teste do bit
0x00200000 são iguais.
As diferenças são só endereços globais (lis/addi e leituras pelo TOC); nenhum
deslocamento de estrutura muda nas funções da tela de salvar (EU 0x1D7000 a
0x1DA000) nem do gerente do savedata (0x271000 a 0x275000).

| Papel | EU NPEB00553 | JP NPJB00135 | US NPUB30467 |
|---|---|---|---|
| Dispatcher por quadro (TEST_FRAME) | func_000333A0 | func_00032F80 | func_00032FA0 |
| Troca de sala (TEST_WARP) | func_00190700 | func_0018ED00 | func_0018ED68 |
| Serializador (TEST_SERIALIZE) | func_001D9370 | func_001D76D0 | func_001D7550 |
| Gasta fita / conta save | func_001D90A8 | func_001D7408 | func_001D7288 |
| Abre a tela pelo bit +74 0x00200000 | func_001AEED4 | func_001AD524 | func_001AD47C |
| Cria a tela de salvar | func_001D952C | func_001D788C | func_001D770C |
| Espera mínima da lista de slots | func_001D8D70 | func_001D70D0 | func_001D6F50 |
| Gerente +4E8 (leitor) | func_00272BEC | func_0027151C | func_00270C2C |
| Entrada da thread AutoSave | func_00273DE8 | func_00272718 | func_00271E28 |
| Ponteiro do estado (TOC) | 0xA89BD0 | 0xA79C34 | 0xA79BD0 |
| SAVE_OBJECT | 0x00ABF0D4 | 0x00AAEE44 | 0x00AAED88 |
| SAVE_MANAGER | 0x00AF97C8 | 0x00AE9548 | 0x00AE94C8 |

- Ponteiro do estado lido nos EBOOT.elf: aponta para CV_SRT_CHARACTER - 0x94 nas
  três (0xBC3808, 0xBB3588, 0xBB3508). Campos +50/+64/+68/+70/+74 lidos igual
  em func_001AEED4 e equivalentes.
- Única diferença de deslocamento vista no gerente: func_002738E4 (EU) lê +494
  onde JP/US leem +49C; não é usado pela ferramenta. +4E8 é igual nas três.
- Catálogo de salas: JP usa `biocv_disc/jpn`, US `biocv_disc/usa` (sem `eng`);
  os mesmos 205 `rm_*.arc`. O catálogo já lê qualquer pasta de idioma.
- "Salvar aqui" ligado no JP e no US. Não testado dentro do jogo nessas versões
  (Kastiel só tem a europeia); os testes sintéticos rodam por versão
  (`CV_MAP_TESTS=1` com `CV_BUILD_JP=1`/`CV_BUILD_US=1`).

## Rótulo "F7" na lista de slots (16/10/2026)

Pedido de Kastiel: o save feito pelo F7 aparecia como NO_DATA quando o jogo
ainda não tinha save nenhum. Causa: a linha do slot (func_001D8090) escreve
NO_DATA quando contador, modo, personagem e local são todos 0 (jogo novo,
Claire = 0, contador 0 e sala sem nome de local).

- Slot no DATA0 (*SAVE_DATA: EU 0xAC2D64, JP 0xAB2AD4, US 0xAB2A18): 15 slots
  de 0x838 a partir de +30. Ao confirmar, func_001DAB20 copia o estado a partir
  de +8C (+0 = 0xA, +4 contador, +8 personagem, +9/+A/+B área, sala, variante).
  O objeto da tela passa ao passo +54 == 5 logo após a cópia; o slot é +A0 -> +10.
- Linha da lista: entrada de 16 bytes (+0 número 1..15, +4 contador, +8 modo,
  +C personagem, +D mensagem do local). func_0003356C(100 * área + sala) dá a
  mensagem do local.
- Mensagens do sistema: tabela em estado +1B3F0 (texto = tabela + entrada[id+1]),
  16 bits, FFFF fim, FF01 espaço. EU/US: código = ASCII - 0x20 (sysmes.msb).
  JP: só maiúsculas (A = 0x00) e dígitos em 0xD8.
- Ferramenta: no passo 5 de um save do F7, guarda um hash dos 0x838 bytes do
  slot em `cv_f7_saves.txt` (ao lado do exe, com o ID da versão). Nada é
  escrito no save. Gancho TEST_ROW (EU func_001D8090, JP func_001D63F0, US
  func_001D6270): se o slot bate com um hash, a mensagem do local é desenhada
  de uma entrada emprestada da tabela, apontada para "F7: rm_XXXX" (JP
  "F7 RMXXXX") numa área de pilha, e restaurada logo depois. Save normal por
  cima muda o hash e volta o texto do jogo.

## Pendente

- Teste em jogo do rótulo F7 nas três versões.

Testes: `CV_MAP_TESTS=1 build-release.cmd` (memória sintética, sem jogo).
