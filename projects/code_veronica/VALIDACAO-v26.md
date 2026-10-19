# Validação v26 — otimizações de 30 FPS com controle ON/OFF

- Compilação JP concluída em 2026-10-19, target MyGameRecomp no build-release-jp.
- Log: build-release/fps30-v26-jp-build.log. Código de saída 0, sem erros.
- Executáveis EU e US mantiveram os hashes do registro v12.
- git diff --check passou (apenas avisos existentes sobre LF/CRLF).
- Política ON/OFF/default, valores inválidos, compatibilidade com 60 FPS e
  overrides passaram no teste C check-opt30.cmd.
- 15 variantes de shaders capturados (incluindo cinco OFF) compilaram em
  DXC e FXC. Destino do uso neste teste: DXC.
- D3D12 WARP: 2.949.376 resultados bit a bit de endereçamento de textura.
- WARP: gerador de sombras capturado, 512 desenhos consecutivos vs um grupo,
  cor e profundidade idênticas, inclusive triângulos sobrepostos.
- Helpers reais de preparação e agrupamento: invalidação por quadro,
  colisões, VS/PSO pendente/evicção/geração, cada byte de estado e constantes,
  20.000 decisões de agrupamento e 10.000 casos de endereçamento passaram.

## Escopo e próximo teste

A opção OFF desliga o novo pacote de comparação, não faz rollback de
correções ou de todas as otimizações históricas. Native30 não ativa a busca
nem o blend de malhas. Atalhos anteriores preservados.

Abrir executar-code-veronica-30fps-comparacao.cmd, selecionar 1 (ON), usar
pasta JP e 1080p. Aguardar shaders, testar isqueiro ligado/desligado,
movimento, menus e cortes de câmera. Fechar jogo/launcher e repetir com 2
(OFF), mantendo sala, câmera e resolução. Limite nativo de 30 FPS e escolha
salva de FPS preservados. Arquivos de log ON/OFF separados, com [OPT30]
confirmando configuração. Ainda não medido em gameplay: não há alegação de
ganho de FPS ou de consumo neste hardware.

Sem commit ou push. Fonte compartilhada preparada; somente binário JP atualizado.

SHA256 JP: 1256012fb2292c8da1ebb20d22acda68152ae0744809b7ef23e754ee6e1a6a18
