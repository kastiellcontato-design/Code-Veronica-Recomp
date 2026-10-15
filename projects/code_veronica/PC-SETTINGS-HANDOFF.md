# PC Settings nativo: resumo para continuidade

## O que foi implementado

- A opção **PC SETTINGS** foi integrada ao menu OPTION original do jogo, mantendo o fluxo e a navegação nativos.
- A tela oferece resolução, anti-aliasing, modo de exibição, brilho, contraste e saturação. As opções são alteradas pelas setas esquerda/direita e ficam salvas em `cv_launcher.ini`.
- Os textos do menu usam a mesma fonte serifada; a rasterização é feita em 1,5x e seus fragmentos são desenhados em posições fracionárias, sem reduzir a geometria à grade de baixa resolução do jogo.
- O lote gráfico agora cresce conforme a tela precisa, evitando que o maior número de fragmentos da fonte suavizada descarte e deixe invisíveis as opções.
- Brilho varia de -80% a +80% e contraste de 50% a 180%, ambos em passos de 1%. Segurar esquerda/direita acelera o ajuste depois de meio segundo e acelera novamente após 1,2 segundo.
- Brilho e contraste são aplicados no renderizador D3D12. O ajuste combina com o passe de FXAA/SMAA quando um deles está ligado; com AA desligado, só cria um passe de cor quando os valores saem do padrão. Os valores padrão não adicionam esse passe.
- Saturação varia de 0% a 200%, com 100% como padrão sem alteração. O passe de cor envia o valor no registrador correto do bloco HLSL (`colorExtra.x`), combinando o ajuste com AA quando ativo e evitando uma passagem adicional nesse caso.
- A imagem é ajustada no jogo, sem alterar texturas ou arquivos do conteúdo do jogo.

## Arquivos relacionados a esta implementação

- `projects/code_veronica/native_pc_menu.cpp`: linhas e navegação do menu do jogo, controles de cor e rasterização do texto.
- `projects/code_veronica/launcher.cpp`: leitura/gravação dos valores e ligação das opções ao renderizador.
- `libs/video/rsx_d3d12_backend.c`: desenho do texto em lote e ajuste de brilho/contraste/saturação por shader.

O porte anterior do PC SETTINGS também usa `projects/code_veronica/CMakeLists.txt`, `libs/video/rsx_ui_batch.h` e `tools/cv_native_menu_hooks.py` para compilar e ligar o desenho ao menu original.

## Escopo e preservação

Esta tarefa alterou somente o menu PC SETTINGS e o ajuste de cor necessário para ele. As alterações avançadas de controles que já estavam na cópia foram preservadas. Não alterei arquivos do jogo, dumps, executáveis, banner ou ícone; não criei commit nem fiz push.

## Build e teste

- Branch de trabalho: `gpt-experimentos`.
- Compile exclusivamente com `projects/code_veronica/build-release.cmd`.
- Para abrir o jogo compilado, use `projects/code_veronica/executar-code-veronica-otimizado.cmd` dentro desta cópia. O atalho copiado “Abrir Code Veronica” ainda pode apontar para a pasta original.
- O build desta alteração foi validado pelo script acima. A verificação visual do texto e dos controles de cor deve ser feita no jogo; a pessoa usuária controla a navegação.

