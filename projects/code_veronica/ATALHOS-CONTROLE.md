# Atalhos do controle sem acoes no jogo

2026-10-18. Solicitacao: consumir combinacoes do launcher antes de aplicar
mapeamento para PS3; exibir nomes Xbox quando conectado, nos 13 idiomas.

- cellPad: protecao optativa habilitada pelo launcher Code Veronica. Ao
  segurar os quatro ombros fisicos, neutraliza botoes, pressoes, gatilhos e
  analogicos deste controle para o jogo. O leitor fisico permanece intacto
  para o launcher reconhecer F5-F10. Nao altera mapeamentos personalizados.
- Mantem a protecao ate soltar todos os ombros/botoes seletores da combinacao,
  evitando tiro ou confirmacao residual. Estado separado por controle,
  reiniciado ao desconectar. Teclado continua independente.
- Pressione os quatro ombros primeiro e depois o seletor. Botoes apertados
  isoladamente ANTES de completar o prefixo ainda sao comandos normais: nao
  adicionamos atraso a todos os botoes para adivinhar um futuro atalho.
- Xbox ja era suportado pelo leitor fisico. Indicacoes do launcher agora
  traduzem apenas as combinacoes para RB/RT/LB/LT, LS/RS e Y/B/A/X conforme
  o modelo conectado, sem mudar os textos localizados restantes. F7 tem
  indicacao ao lado das opcoes de mapas. Outros controles mantem nomes PS.
- Testes adicionados ao conjunto SRT: seis seletores, prefixo, cauda de
  liberacao, entrada normal e isolamento de estado por controle.
- Compilacao/testes ainda pendentes. Nao houve commit/push.

Validacao: build-release.cmd com CV_BUILD_JP=1, CV_BUILD_US=1 e CV_SRT_TESTS=1. EU/JP/US compilados; testes SRT PASS nas tres regioes, incluindo os seis casos novos do gate. diff --check sem erros. Teste fisico pelo usuario ainda pendente; nao foram enviados comandos sinteticos ao jogo.
