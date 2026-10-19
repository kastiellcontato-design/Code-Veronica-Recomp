# Vida infinita e municao infinita

Duas opcoes no launcher, ambas desligadas por padrao. Desde o menu de itens
(MENU-ITENS.md) ficam na aba Inventario, a pedido do usuario, e nao mais em
Patches; mesmas chaves e mesmo comportamento.
Chaves em cv_launcher.ini, secao patches: cv_infinite_health e cv_infinite_ammo.
Mudancas sao aplicadas imediatamente por flags atomicos; nao dependem do SRT.
Textos nas 13 linguas do launcher. Nenhuma aba nova.

## Implementacao

- cv_cheats.cpp: valida personagem/assinatura de gameplay com os mesmos
  enderecos por regiao usados pelo SRT. Nao altera inimigos.
- cv_cheats_lift.h: preserva os stores coerentes originais da recompilacao;
  permite filtrar apenas diminuicoes de vida e municao de armas do personagem.
  Escritas de itens diferentes, slots vazios, chaves e consumiveis passam.
- tools/cv_native_menu_hooks.py: aplica o include apenas nas copias locais de
  build das seis unidades PPU, depois dos helpers inline originais. Acrescenta
  um tick na funcao original de pacing ja identificada em cada regiao.
  Nao altera src/recomp*, EBOOT ou arquivos originais do jogo.
- Tick executado pela PPU, em todos os modos de FPS: completa vida positiva
  ate o maximo da dificuldade/regiao e abastece armas e reservas com pelo menos 99
  unidades (100 para as que usam porcentagem). Vida negativa nao e ressuscitada.
- Desligar volta a permitir consumo/dano; nao restaura a vida ou quantidade
  anterior a ativacao. O abastecimento faz parte do estado atual da partida e
  pode ser gravado por um salvamento normal do proprio jogo.
- Nao grava o bit nativo de item infinito, evitando deixa-lo preso depois
  de desativar. Armas que ja tinham esse bit originalmente o preservam.
- Nao remove veneno/gas, animacoes de dano, requisitos de itens ou game-overs
  de roteiro. A protecao atua no campo de vida, nao na logica desses eventos.

## Validacao

Compilar somente por build-release.cmd com CV_BUILD_JP=1, CV_BUILD_US=1 e
CV_CHEAT_TESTS=1. Tambem usar CV_SRT_TESTS=1 e CV_FPS_TESTS=1 para regressao.
Testes isolados em tests/test_cheats.cpp usam memoria ficticia, incluindo
as tres regioes, quatro personagens, armas e reservas por unidade/porcentagem,
ativacao/desativacao, stores de byte/halfword/word, inimigos e itens inalterados,
movimentacao/descarte, game-over/carregamento e todas as traducoes.

EU/JP/US compiladas e atualizadas. CV_CHEAT_TESTS, CV_SRT_TESTS e
CV_FPS_TESTS passaram. Checagem estrutural: seis unidades instrumentadas e
um unico tick de pacing em cada regiao. Logs locais em build-release/:
cheats-build-final.log (compilacao e testes) e cheats-refresh.log (atualizacao
incremental final dos executaveis com o mesmo codigo validado pelos testes).
Validacao manual em 2026-10-17: usuario confirmou funcionamento das opcoes
no jogo JP e informou que nao percebeu perda de FPS. Esse relato nao equivale
a uma medicao comparativa de desempenho; impacto quantitativo nao medido.
Nenhum commit autorizado ou feito nesta tarefa.
