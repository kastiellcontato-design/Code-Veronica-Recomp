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

## Como funciona (EU; JP/US têm os mesmos dados, endereços de código próprios)

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

## Pendente

- JP/US: mapear SAVE_OBJECT e SAVE_MANAGER (dados de func_001D952C e do gerente
  do savedata); até lá o "Salvar aqui" fica desativado nessas versões. A troca de
  sala já usa os endereços de cada versão.
- Nome do local nos saves feitos fora das salas de save (hoje "NO_DATA").

Testes: `CV_MAP_TESTS=1 build-release.cmd` (memória sintética, sem jogo).
