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


## F7: Carregar save (2026-10-17)

Novo botão **Carregar save** abre a lista nativa dos 15 slots durante uma
partida controlável. A leitura/validação do DATA0, escolha, confirmação e cópia
do estado completo são feitas pelo próprio jogo. Nenhum arquivo de save é
reescrito pela nova ação. Não adiciona slots nem muda o formato.

Implementação isolada em `cv_test_tools.cpp`:

- `load_request` é consumido no dispatcher do jogo, com as mesmas condições de
  partida viva das ferramentas existentes. Pedidos concorrentes não executam
  carregamento junto de warp/save.
- Primeiro pede o fluxo da máquina de escrever (`state+74 |= 0x00200000`) para
  preparar mensagens e recursos da interface. **Não** ligar `state+50 & 0x200`
  antes disso: o inicializador pularia a leitura desses recursos.
- No construtor da tela de salvar, somente para a solicitação F7, liga esse bit,
  chama o construtor nativo de carregar com o armazenamento próprio e publica
  seu ponteiro no global usado pelo dispatcher. Não chama o serializador.
- O fluxo nativo de confirmação (`EU 001C2D60`) liga `state+58 & 0x10`.
  O update (`EU 001C1F78`) copia o slot inteiro de 0x838 bytes para `state+8C`.
  O encerramento então reinicializa a partida/sala pelo caminho nativo.
- Ao cancelar, o wrapper do encerramento limpa **só** o bit 0x200, antes do
  original: usa o retorno da máquina de escrever para continuar na sala atual,
  em vez do caminho de cancelamento da tela inicial. As demais flags ficam.
- Sem solicitação F7, ambos os wrappers delegam diretamente aos originais.

| Função/dado | EU | JP | US |
| --- | --- | --- | --- |
| Construtor salvar (wrapper) | 001D952C | 001D788C | 001D770C |
| Construtor carregar | 001C1668 | 001BFCE0 | 001BFA48 |
| Encerramento modo save/load (wrapper) | 00033FD0 | 00033B78 | 00033BD0 |
| Armazenamento UI carregar | 00CB72C8 | 00CA7048 | 00CA6FC8 |
| Global ponteiro UI carregar | 00CB7304 | 00CA7084 | 00CA7004 |

Botão e mensagem traduzidos nos 13 idiomas do launcher. Enter/confirmação
acionam o botão com foco; lista de mapas mantém Enter/duplo clique.
Testes em memória sintética cobrem os novos gates, construtor, cancelamento,
confirmação e preservação dos caminhos anteriores nas três regiões.
Compilação oficial concluída nas três regiões com `CV_MAP_TESTS=1`: todos os
testes passaram (NPEB00553, NPJB00135 e NPUB30467), incluindo mapas e salvar
aqui. Saves reais não foram usados nos testes automatizados.
Validação interativa pendente: abrir F7 numa partida, cancelar o carregamento
uma vez, depois carregar um slot e conferir sala, inventário e personagem.


### Retorno preto após F7 carregar/cancelar (17/10/2026)

Teste real de Kastiel na versão JP falhou nos dois casos. Evidência nos logs
`run-log-release.txt` e `.previous-1.txt` às 15:48–15:49:

- AutoLoad leu DATA0.DAT com sucesso; confirmar chegou a carregar `pl00.arc` e
  `rm_1000.arc`. Porém o retorno ficou com `state+70=08000040` e
  `state+74=00200000`, tarefas 00300050. O encerramento de load da tela inicial
  não limpa os bits da máquina de escrever, porque normalmente não os usa.
  O bit 08000000 bloqueia a atualização da partida em EU func_001AF2FC.
- Cancelar terminou a devolução da sala, mas continuou preto. O fluxo da UI de
  load usa o escurecimento do menu; devolver só o seletor de modo não devolve
  o estado visual anterior. Além disso, a saída nativa de salvar tem várias
  fases: não se deve perder a identificação F7 na primeira fase.

Correção atual, somente para o load solicitado por F7:

1. Captura antes da abertura os 24 words do fader em `state+2A5CC..2A628` e o
   bit de fade ativo `state+74 & 2`.
2. Confirmar limpa exclusivamente os bits de transição da máquina de escrever
   `+70 & 08000000` e `+74 & 00200000`, antes do encerramento de load nativo.
   Mantém o resultado de confirmação e a restauração nativa do slot completo.
3. Cancelar mantém a identificação da operação até o final da saída nativa.
   Só quando `+5C=0`, tarefa 8000 bloqueada e pedido de salvar limpo devolve o
   fader capturado. Não força a transparência/cor em quadros seguintes e não
   restaura inventário, posição ou eventos de um snapshot próprio.
4. Após confirmar, registra por 20 segundos progresso de tarefas/flags/sala e
   fader (uma linha a cada 60 quadros) para conferir o próximo teste real.

Os testes anteriores tinham um encerramento substituído por uma função vazia,
portanto não revelavam o estado que o código nativo deixava. Agora
`tests/native_save_finish_fixture.h` executa o corpo original EU 00033FD0 com
serviços externos isolados (mesmo fluxo normalizado em JP/US). O teste reproduz
os bits que sobravam na saída original, verifica a liberação pelo wrapper e
executa as duas fases do cancelamento, verificando o fader completo restaurado.
Apenas o nome da função e as chamadas a serviços foram adaptados na fixture;
as operações/branches do estado são as do lift fornecido.

Compilação oficial e testes passaram nas três regiões. Ainda precisa repetir
os dois testes dentro do jogo; não declarar resolução visual confirmada antes
do retorno do usuário. Atalho normal: `executar-code-veronica-otimizado.cmd`.


### Segundo teste real ainda preto: fader da interface (17/10, 15:58)

Os dois testes continuaram pretos. O novo log confirma que os bits da máquina
foram limpos e que o cancelamento restaurou o fader da sala. Portanto essas
mudanças eram insuficientes; não repetir a afirmação de que os testes
anteriores já demonstravam a resolução visual.

Achado na rotina original EU 001C1F78, ao fechar a lista de carregar (vale para
confirmar e cancelar): além de destruir a UI/copiar o slot, ela chama
`00219BA0(*(00AC0968), 0x40, 0, -1)` e liga byte `controller+4C=1`.
Este é um **segundo fader**, da biblioteca de UI, com sprite em `controller+30`
e cor/alpha em `sprite+300`. Não fica no bloco +2A5CC do estado do jogo.
A tela de salvar EU 001D9EC8 não faz essa configuração.

O retorno nativo EU 001D7E14 chama `00219BA0(controller,0,255,1)` e limpa
`controller+4C`. O F7 pulava esse retorno ao mudar para a saída da máquina de
escrever. Agora `TEST_SAVE_FINISH` o chama uma vez por operação, antes da
seleção do caminho de cancelar/confirmar. Mantém o retorno normal da sala e a
leitura/restauração nativa completa do slot. Não há mudança no renderer.

| Dado/função | EU | JP | US |
| --- | --- | --- | --- |
| Retorno visual nativo | 001D7E14 | 001D6174 | 001D5FF4 |
| Ponteiro controlador UI fade | 00AC0968 | 00AB06D8 | 00AB061C |

`tests/native_load_visual_fixture.h` executa as duas rotinas nativas EU de
configurar/devolver esse fader, além da fixture de encerramento já existente.
Os testes agora reproduzem o sprite com valor 0/modo 1 deixado pela UI e
verificam o retorno para 255/modo 0 nos dois caminhos, uma vez por operação.
Compilação oficial e testes PASS em EU/JP/US. Log de build:
`build-release/f7-load-visual-return-all.log`.

Próximo teste real **ainda pendente**. Solicitar primeiro cancelar. Caso volte
a ficar preto, pedir para manter o jogo aberto e capturar o estado em memória,
em vez de continuar ajustando só com testes isolados. Os logs novos mostram
controller/modo/opacidade antes e depois do retorno nativo; o progresso do load
também mostra contador, duração e atraso do fade da sala.


### Terceiro teste real (17/10, 16:12): máscara de tarefas bloqueadas

Cancelar já volta certo. Confirmar ainda ficava preto, mas o log mostrou que
o load em si funciona: DATA0 lido, `pl00.arc`/`rm_1000.arc` carregados e
tarefas de volta a 0073DFC0. O problema era `state+68` (tarefas bloqueadas):

- Load da tela inicial: o encerramento entra nas tarefas 00300050 com
  bloqueadas=**00000080**, e o reinício monta a máscara de partida (0003DE00).
- Load pelo F7: entrava com 00035E80 (máscara herdada da partida e da máquina
  de escrever) e terminava em **0003DF80**: 0x80 e 0x100 continuavam
  bloqueadas, a sala não atualizava e o fade da sala ficava em 255 (preto).

Correção: no load confirmado pelo F7, antes do encerramento nativo,
`state+68 = 0x80`, igual ao load da tela inicial. Teste sintético cobre o
valor. Compilação oficial + testes PASS em EU/JP/US
(`build-release/f7-load-blocked-mask.log`). Teste real pendente.

### Quarto teste real (17/10): sons da sala antiga seguravam a entrada

Com a máscara 0x80 o load entrou igual ao da tela inicial, mas o reinício
voltou a 0003DF80 e a tela continuou preta. Lendo a memória do jogo aberto: a
entrada da sala (EU func_001B0CB0, passo `+1B110` = 4, subpasso 0x0A) chama
EU func_001E2314, que espera até nenhuma voz do banco de som 9 tocar. Duas
vozes do ambiente da sala anterior (água, em loop) continuavam tocando; o
usuário confirmou que ouvia a água. Na tela inicial não há sons de sala, então
o load nativo nunca precisou pará-los. Pular o subpasso à mão (diagnóstico) fez
a sala aparecer, mas com som antigo, lentidão e lixo visual — não usar isso.

Correção: no load confirmado pelo F7, antes do encerramento nativo, chamar o
reset de som do jogo (o mesmo que a tarefa de início de partida roda via
EU func_00194E5C): EU 001DF7B0, JP 001DDB10, US 001DD990 (mesmo corpo, gerente
de som em EU 00AC61F4 / JP 00AB5F64 / US 00AB5EA8). Cancelar não chama.
Build + testes PASS EU/JP/US (`build-release/f7-load-sound-reset.log`).
Teste real pendente.

Atualização: o reset geral (001DF7B0) **não** bastou — só para o banco 3; as
duas vozes do banco 9 continuaram e a tela seguiu preta (teste real 16:45).
Trocado por `func_0025B32C(gerente, 9)` (EU; JP 00259C4C, US 0025935C), que
percorre as vozes do banco 9 e para cada uma (método +A4 do banco), chamado só
no load confirmado. Build + testes PASS EU/JP/US
(`build-release/f7-load-stop-bank9.log`). Teste real pendente.

**Teste real 17/10 (JP): OK.** Escolher um save pelo F7 carrega o jogo daquele
save, sem o som da sala anterior e sem lentidão. Cancelar já voltava certo.
Ainda não testado em EU/US dentro do jogo (só testes sintéticos).

## 22/10/2026 — biblioteca de saves
Os botões Salvar aqui e Carregar save do F7 agora abrem a biblioteca PC com miniaturas e arquivos independentes, sem a tela de slots original. Documentação atual: SAVE-LIBRARY.md. Slots antigos continuam disponíveis para leitura. Save/load na gameplay desta integração ainda aguardam confirmação do usuário.
