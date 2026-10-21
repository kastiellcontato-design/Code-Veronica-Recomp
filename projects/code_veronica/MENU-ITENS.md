# Menu de itens no jogo (F6)

Aba **Inventário** do launcher, entre SRT e Desempenho: uma opção (desligada
por padrão, `cv_launcher.ini` seção `inventario`, chave `menu_itens`), o
atalho F6 e como usar, os avisos, e (a pedido do usuário) as opções de vida e
munição infinitas, que saíram da aba Patches (mesmas chaves do ini; CHEATS.md).
Desligada, F6 e o combo do controle não fazem nada. Textos nas 13 línguas
(launcher_i18n.h, S_NAV_ITEMS e S_ITEMS_*; o menu em cv_items_i18n.h).

## No jogo

F6, ou R1+R2+L1+L2+△ no controle, abre uma janela própria sobre o jogo (como
a dos mapas F7); a mesma tecla/combo ou Esc fecha. Teclado, controle
(direcional, ✕/A, ○/B, L1/R1) e mouse. **O jogo fica pausado** enquanto o menu
está aberto (`cv_focus_pause_hold`, mesma pausa da perda de foco: relógio do
jogo, áudio e quadros parados, sem o aviso "Pausado").

- Categorias: armas, munição, cura, itens-chave, arquivos e outros.
- Cada item com o ícone original (lido da cópia do jogo, cv_srt_icons) e o
  nome traduzido (cv_srt_i18n). IDs desconhecidos (8D, 92, 93, 94, 97) ficam
  de fora.
- À direita, o inventário atual: espaço fixo (isqueiro/gazua), os espaços em
  duas colunas como na tela do jogo, a arma equipada marcada, e os dois espaços
  da Bolsa lateral trancados enquanto ela não foi usada.
- Painel de colocação: quantidade (quando o item tem contagem), espaço
  (primeiro livre ou um item para substituir) e o botão Colocar.
- Remover: R (ou □/X) escolhe um espaço do inventário; Enter/✕ tira o item,
  como o jogo faz ao gastar um: os de baixo sobem e o número do equipado
  acompanha. Nunca o espaço fixo nem a arma equipada.

## Atalhos no jogo (todos os do port)

No controle, sempre R1+R2+L1+L2 segurados mais um botão
(cv_launcher_controls.h); as janelas do port ignoram os próprios botões
enquanto os dois gatilhos estão apertados.

| Tecla | Controle | Função |
|---|---|---|
| F5 (ou Insert) | + L3 | menu de opções (launcher) |
| F6 | + △ | menu de itens |
| F7 | + ○ | mapas |
| F8 | + ✕ | SRT |
| F9 | + □ | texturas |
| F10 | + R3 | screenshot (antes F11) |

F12 continua sendo a captura rsxcap (desenvolvimento). Esc não fecha mais o
jogo: só o X da janela ou Alt+F4.

## Formato e regras (cv_items.cpp)

`CV_SRT_INVENTORY + personagem * 0x40`: palavra 0 = entrada equipada + 1; depois
11 entradas `FLAGS ID QTD(16)`. A entrada 0 é o espaço fixo; 1..10 são uma
lista compacta. Capacidade: 8, ou 10 com o bit `0x08000000` de `estado+0x6C`
(estado = `CV_SRT_CHARACTER - 0x94`). Confirmado no código do jogo nas três
regiões: a tela de inventário (EU func_0019FA5C) e o script de equipar
(EU func_00136FD4) usam 8 ou 0xA conforme esse bit. Cada item ocupa uma
entrada; as armas largas só são desenhadas mais largas (saves reais confirmam).

- O menu nunca escreve no espaço fixo nem na entrada da arma equipada, e não
  muda o índice do equipado. Flags da nova entrada ficam zeradas.
- Escolher um espaço vazio depois do fim da lista vai para o primeiro livre
  (a lista continua compacta, como o próprio jogo a mantém).
- Itens sem contagem gravam 1 (faca: 0), como nos saves. Lança-granadas fica
  abaixo de 64 (os 2 bits altos são o tipo de granada).

## Momento seguro

Com o jogo pausado a PPU não roda, então cada escolha entra numa fila (até 10,
protegida por mutex) e o menu já mostra o inventário como vai ficar (o vivo
com a fila aplicada). Ao fechar, o jogo volta e o primeiro quadro grava a fila;
um aviso no canto do jogo diz o que entrou (ou que nada entrou). A gravação
acontece no quadro do jogo, na thread da PPU: `cv_items::frame()` no início de `TEST_FRAME`
(cv_test_tools.cpp, a função de quadro já usada pelos mapas F7). Antes de
gravar confere de novo: personagem vivo com a sala carregada (assinatura RDX
por região, como o SRT), tarefa da sala ativa (0x80) ou a tela de inventário
do jogo, nenhuma outra tela (tarefas 0x1803F; `+70` 0x10000000 e 0x08000000
máquina de escrever), mesmo
personagem do pedido, a entrada igual à que o menu mostrou (vazia: ainda a
primeira vazia da lista), dentro da capacidade e não a equipada. Qualquer
diferença: aquele item não é gravado e o aviso diz. Uma palavra de 4 bytes por
item, mais nada.

**Colocar em qualquer lugar.** Antes valia para colocar a máscara inteira que
o jogo usa para decidir se pode abrir um menu (EU func_001AEED4:
`+70 & 0x1C040008`, `+74 & 7`). Os bits de evento, ponto de checagem e fade
(`+70` 0x04040008, `+74` 1/2/4) são os únicos da regra antiga que não são
tela do jogo; perto de máquinas de escrever e em outros pontos do mapa o menu
abria mas recusava colocar (qual bit exato ainda não foi visto no log). Colocar ou
substituir grava uma palavra entre dois quadros, na thread da PPU, sem mover as
outras entradas, então agora só espera as telas acima. Se um evento do jogo já
tinha escolhido o espaço (por exemplo, pegar um item do chão com a pergunta na
tela), no máximo ele grava por cima daquele espaço. Remover move as entradas de
baixo e continua exigindo a máscara inteira (como `active_game()` do F7),
exceto com a tela de inventário do jogo aberta.

**Com o inventário do jogo aberto** (a pedido do usuário). Visto no log JP com
a tela ITEM aberta: tarefas `0073DFC0`, bloqueios `0003DC80` (a tarefa da sala
0x80 pausada, a 0x200 rodando), `+70` `00015408` (bit 0x8), `+74` 0x40. A
regra: tarefa 0x200 ativa, 0x80 parada e `+70 & 8`. (A tarefa 0x10000 com
`+70` 0x10000000 é outra tela e continua bloqueada.) Abrir o inventário do
jogo, depois o F6, permite colocar e tirar itens; a tela do jogo desenha as
entradas vivas. Aí remover não espera os bits de evento/fade. O cursor da tela nativa pode ficar
num espaço que mudou ou esvaziou; fechar e abrir o inventário atualiza tudo.
Save e as demais telas continuam bloqueados. `read().playing` (usado
pela aparência) não mudou: continua falso com qualquer tela aberta.
Quando o menu ou o quadro recusa, o log ganha uma linha
`[ITEMS <id>] item refused: tasks=… blocked=… flags=+70/+74 health=… room=…`
para identificar o bit se aparecer outro lugar que recusa.

## Testes

`CV_ITEMS_TESTS=1` em build-release.cmd (EU/JP/US com CV_BUILD_JP/US):
tests/test_items.cpp com memória fictícia nos endereços de cada região.
Catálogo e nomes nas 13 línguas, codificação das entradas, leitura (quatro
personagens, 8/10 espaços, mapa/arquivo/opções/save/morte recusam; bits
de evento, ponto de checagem e fade aceitam colocar mas não remover; tela de
inventário do jogo aceita os dois), regras de espaço
(cheio, equipado, além da capacidade), gravação de uma só palavra, opção
desligada (fila descartada), personagem trocado, entrada mudada, vários itens
na fila num quadro, fila cheia, resumo do aviso e navegação.
Com `CV_ITEMS_PREVIEW=<pasta>` a execução EU também desenha o menu em PNGs.
