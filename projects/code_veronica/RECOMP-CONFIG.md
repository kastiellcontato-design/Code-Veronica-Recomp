# Recomp Config: ajustes gráficos nativos

- Anti-serrilhado: OFF, FXAA e SMAA (opção existente preservada).
- Nitidez FSR 1 RCAS: OFF e 10% a 100%, em passos de 10%.
- Filtro anisotrópico: OFF, 2x, 4x, 8x e 16x.
- Anti-serrilhado e nitidez são aplicados durante o jogo; o filtro anisotrópico é salvo para a próxima execução, com aviso no menu.
- O menu usa os mesmos valores e chaves de configuração do launcher, incluindo o caminho sem a janela do launcher.
- As linhas novas usam a fila de desenho nativa, com o restante do menu, sem uma janela adicional.
- Reset Colors continua restrito a brilho, contraste e saturação.
- A leitura das opções de efeitos tem mensagem própria, sem conflito com F9 (texturas), F8 (SRT) ou os menus de mapas e itens.

Validação: teste de navegação real com entrada sintética para AA/RCAS/anisotrópico, limites, reabertura, reset de cores e limites dos desenhos na tela. Executáveis EU, JP e US recompilados; teste do menu passou nas três configurações. Não houve teste visual interativo dentro de uma partida nesta alteração.
