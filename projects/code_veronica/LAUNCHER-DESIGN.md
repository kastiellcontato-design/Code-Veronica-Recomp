# Launcher: layout e biblioteca regional — 22/10/2026

## Escopo

Implementação do visual aprovado pelo usuário, exclusivamente no launcher. Fontes padrão do Windows: Impact para a marca RESIDENT EVIL, Georgia/alternativas por idioma para títulos, Segoe UI/alternativas para campos. Sidebar com ícones vetoriais; paleta carvão, vinho e dourado; fundo estático de castelo. A página inicial passa a se chamar Gráficos, com cartões Tela / Qualidade / Desempenho. As configurações existentes, suas restrições em execução e avisos de FPS são preservados.

Nenhum código de gameplay, interpolação, FSR, personagens ou gravação/restauração de saves foi alterado nesta tarefa. Não foram iniciadas partidas nem alterados arquivos de jogos/licenças/saves durante a validação visual.

## Biblioteca de jogos

A aba Jogos mostra Japão (NPJB00135), América (NPUB30467) e Europa (NPEB00553), nessa ordem. Adicionar versão / Procurar lê TITLE_ID e APP_VER do PARAM.SFO e reaproveita a validação existente: o nome da pasta não precisa conter o ID. O launcher cadastra pastas já instaladas; não baixa, instala ou redistribui arquivos do jogo.

A imagem principal de cada cartão é o ICON0.PNG da pasta cadastrada, mostrada inteira e com proporção preservada. PIC1.PNG, quando existe, preenche o fundo do cartão. Não são geradas novas capas nem copiadas imagens dos jogos para o repositório. Sem imagens, há uma indicação traduzida.

Cada versão guarda sua própria pasta e licença RAP. A japonesa continua dispensando RAP. A descoberta automática de licença procura o ID da região no nome; não herda silenciosamente uma licença manual de outra região. Arquivos de licença renomeados continuam podendo ser escolhidos manualmente, como antes.

Selecionar um cartão atualiza o perfil ativo. Jogar reaproveita exatamente o caminho anterior de validação/descriptografia e abertura do executável regional. Os executáveis normais devem ficar juntos. A troca de região e os botões de pasta/licença ficam bloqueados com uma partida aberta. O botão Voltar ao jogo e todas as páginas de controles/patches/mods/SRT/cheats/performance/avançado permanecem disponíveis.

## Persistência e compatibilidade

No mesmo cv_launcher.ini, seções biblioteca_NPEB00553 / biblioteca_NPUB30467 / biblioteca_NPJB00135, com chaves pasta e rap; biblioteca/selecionada guarda o TITLE_ID ativo. pastas/jogo e pastas/rap continuam sendo escritos para compatibilidade com a abertura regional existente. O perfil antigo é migrado em memória pelo TITLE_ID, preservando caminhos já cadastrados. Licenças antigas cujo nome contém uma das regiões são reaproveitadas na região correspondente. A migração é persistida pelo fluxo normal de salvar preferências, sem editar automaticamente o INI durante a inspeção.

## Idiomas e tamanho da janela

launcher_design_i18n.h fornece todos os textos novos nos 13 idiomas do launcher, com checagem de completude em compilação. Os nomes de região reaproveitam as traduções existentes de launcher_i18n.h. Textos em árabe e fontes asiáticas usam o suporte já existente.

A tela lógica é 1280×820 e se ajusta à área útil do monitor. O desenho e as coordenadas de mouse usam a mesma transformação. As páginas longas mantêm a rolagem e o footer fixo. Os controles de pasta de saves e demais opções antigas da aba Jogo foram preservados abaixo da biblioteca, acessíveis rolando a página.

## Arquivos

- launcher.cpp: layout, desenho dos ícones e biblioteca/perfis regionais.
- launcher_design_i18n.h: textos novos.
- launcher_art/launcher_castle.png: fundo gerado para o visual aprovado; incorporado pelo glob de recursos já existente no CMake.

O fundo foi criado com a ferramenta integrada image_gen a partir do mockup aprovado. Prompt: extrair apenas a atmosfera de fundo, removendo toda a interface, textos, capas e personagens; reconstruir uma fortaleza/prisão gótica em ilha numa noite tempestuosa, em formato 16:9, tons azul acinzentado e preto, janelas âmbar discretas, castelo nos dois terços direitos e névoa escura no terço esquerdo. A origem desse fundo é geração de imagem, não um arquivo extraído do jogo.

## Validação e entrega

Compilação dos três launchers normais JP/US/EU: Clang, compilação dos recursos RC e link dos executáveis com os comandos dos respectivos builds CMake. Após a inclusão da nova imagem, o cache do Python foi corrigido para o interpretador disponível; nenhuma opção de desempenho foi alterada. A etapa Ninja de verificação de globs travou; a compilação final usou diretamente os comandos de compilação/link do próprio CMake e RC, sem modificar a lógica do jogo.

Inspeção visual real das páginas Gráficos e Jogos do launcher japonês, sem pressionar Jogar. A imagem oficial japonesa, o cadastro antigo, os títulos, ícones e campos foram verificados. Os cartões das regiões ainda não cadastradas mostraram sua indicação de adicionar pasta. Não houve teste de início de partida ou abertura regional com as três instalações; isso fica para o usuário. Também não foi feito teste visual de cada um dos 13 idiomas.

Backups e logs no workspace do Codex: work/launcher-redesign/before, compile-final-{jp,us,eu}.log, resource-{jp,us,eu}.log e link-final-{jp,us,eu}.log. Nenhum commit/push foi solicitado para esta alteração.


## Logo aprovada no canto esquerdo — 22/10/2026

A marca desenhada por fontes no topo esquerdo foi substituída pelo PNG aprovado pelo usuário: RESIDENT EVIL e CODE: VERONICA X em marfim, com a mesma textura, e somente PORT em vermelho. Sem ornamento e com alfa transparente. Arquivo launcher_art/launcher_logo.png, preservado byte a byte da arte aprovada; recurso LAUNCHER_LOGO incorporado ao executável pelo mecanismo existente.

Apenas o espaço transparente de exportação é recortado no bitmap de exibição, mantendo o PNG original intacto. A imagem é ajustada proporcionalmente em uma caixa de 96 px de altura no cabeçalho da sidebar e usa o cache de desenho já existente. A marca de texto antiga é fallback se o recurso não puder ser lido. Não muda títulos, imagens das capas, abas, configurações ou comportamento do jogo. Backups/diff/logs desta mudança: work/launcher-logo no workspace do Codex.

Validação desta logo: compilação e instalação dos executáveis JP/US/EU, com os recursos atualizados. O lld-link travou na etapa de link; os mesmos objetos e argumentos do CMake foram finalizados com o LINK do MSVC instalado, sem alterar fontes do jogo. Inspeção visual do launcher JP confirmou transparência, proporção, cores e espaço acima da navegação. Nenhuma partida foi iniciada. Hash do PNG idêntico à arte aprovada; manifesto em work/launcher-logo/installed.json.

## Redes sociais no rodapé — 22/10/2026

Ícones vetoriais do GitHub e YouTube em marfim neutro, sem moldura ou fundo próprio, abaixo da navegação. As áreas de clique têm 44 × 44 px; o cursor e o nome da rede no hover indicam a ação. O clique abre o navegador padrão, sem iniciar o jogo.

Os links são lidos de `cv_launcher.ini`, ao lado dos executáveis regionais. A seção é criada no primeiro uso, sem substituir valores existentes. É possível mudar os endereços sem recompilar; deixar um valor vazio desativa seu link. Somente endereços http/https são aceitos.

```ini
[social]
github=https://github.com/kastiellcontato-design/re-cvx-ps3recomp
youtube=https://www.youtube.com/@kastieloficial
```

Para distribuir outros links como padrão, alterar `k_social_defaults` em `launcher.cpp`; para a instalação atual, editar apenas a seção acima. Não houve mudança na lógica do jogo.

## Caminho do cache e alinhamento do overlay — 22/10/2026

O launcher resolve a raiz do port pelo executável, sem depender da pasta herdada do editor/atalho. Nos builds de desenvolvimento em build-release[-jp/-us]/bin, reconhece a raiz usada pelo iniciador normal; na distribuição, usa a pasta do executável. A opção Arquivos aponta para shader_cache nessa raiz. Ao iniciar a partida, o launcher usa essa mesma pasta de trabalho para o backend existente, sem alterar o compilador de shaders ou sua estrutura de cache. Nenhum shader existente é movido ou apagado pela correção.

A contagem e o botão de limpeza consideram os arquivos de cache nas subpastas de compilador (.cso, pipelines.bin/list e temporários), ignorando junctions/links e arquivos de outros tipos. A limpeza não foi executada no cache real durante a validação.

Os campos Posição/Tamanho e Disposição/Fundo agora compartilham a largura dos rótulos em cada coluna. As bordas esquerdas e direitas dos dropdowns coincidem entre as duas linhas, com medidas adaptadas ao idioma. Não muda valores salvos ou funcionamento do overlay.

## Reversão da otimização de fluidez — 22/10/2026

A pedido do usuário, removida integralmente a última otimização de verificação/fluidez após relato de lentidão e stutters. Restaurados launcher.cpp, helper de cache, testes e os três executáveis exatamente do estado anterior a essa otimização. Mantidos o caminho do cache, a contagem recursiva, o alinhamento Tamanho/Fundo e o atualizador. Nenhum cache real, save ou configuração foi apagado. O processo de jogo já aberto deve ser reiniciado pelo usuário para carregar os executáveis restaurados. Não houve teste de gameplay nem confirmação da causa da lentidão nesta reversão.

## Avançado sem varredura automática — 22/10/2026

Depois da reversão da otimização anterior, foi aplicada uma correção limitada ao desenho da aba Avançado: removida a contagem automática de arquivos/tamanho do cache. Exibe shader_cache, o caminho existente e os botões Abrir pasta/Limpar. Não cria threads nem tarefas de verificação. A enumeração do cache só acontece na ação explícita de limpeza, que permanece desabilitada durante a partida.

O bloco de log também deixa de consultar o tamanho/forçar flush de stdout e stderr a cada pintura. Mostra o caminho do log; ver, copiar e salvar continuam como ações explícitas. Nenhuma mudança em start_game, pasta de trabalho, validação das versões, configurações salvas, FPS, FSR, renderizador ou runtime. O cache real não foi movido, apagado ou recontado durante esta correção.

## SRT: posição livre e inventário independente — 22/10/2026

A aba SRT ganhou as opções **Mover livremente** e **Separar inventário**. Separar ativa o modo livre e a exibição do inventário. Na prévia do launcher, cada painel pode ser arrastado para uma posição diferente; vida/nome/tempo/estatísticas/inimigos continuam no painel principal. O inventário tem seu próprio controle de tamanho de 50 a 200%, sem alterar o tamanho do painel principal. Restaurar posições retorna vida ao canto superior esquerdo e inventário ao superior direito. Os campos antigos de canto e posição ao lado/embaixo ficam desabilitados enquanto são substituídos pelo modo livre.

As opções vêm desligadas. Desligar Mover livremente também desliga a separação e restaura o comportamento anterior, preservando as coordenadas e o tamanho independente para uso posterior. O teste de regressão compara os quads do layout padrão byte a byte após desligar o modo livre.

Persistência no mesmo cv_launcher.ini, seção [srt]: mover_livremente, separar_inventario, tamanho_inventario, painel_x, painel_y, inventario_x, inventario_y. As coordenadas vão de 0 a 1000 sobre o espaço disponível: a posição se adapta à resolução e ao tamanho do próprio painel, sem sair da área do jogo. O tamanho principal continua usando a chave tamanho existente. Novos textos em launcher_srt_layout_i18n.h cobrem os 13 idiomas do launcher.

No modo Em janela separada, cada painel separado usa uma janela própria, arrastável sem ativar a janela ou tirar o foco da partida. A prévia também define a posição inicial sobre a área do jogo. Arrastar diretamente uma janela guarda uma posição de desktop em janela_x/janela_y (painel principal) ou janela_inventario_x/janela_inventario_y (inventário). Um novo arrasto na prévia ou Restaurar posições limpa essas posições manuais, aplicando as coordenadas da prévia. Cada janela preserva seu tamanho e a alternância entre modos esconde as janelas que deixaram de ser usadas.

cv_srt.cpp rasteriza cada painel em sua própria resolução final, preservando a nitidez dos textos. A cache de layout continua sendo reconstruída somente quando há mudanças no estado/tamanho/configuração. Não foi alterado código de gameplay, interpolação de 60 FPS, FSR, compilação/cache de shaders, inventário real ou saves.

Validação: testes SRT passaram em JP/US/EU, incluindo escalas independentes, coordenadas fora da faixa, limites de tela em 480p/720p/4K e restauração exata do layout padrão. Teste de integração de janelas, executado em pasta isolada com memória sintética e viewport oculta, validou duas janelas, tamanhos independentes, aplicação/reset das posições da prévia e alternância entre separado/junto/sobre o jogo/desligado. Inspeção visual real da aba SRT confirmou ativação, arrasto do inventário sem mover a vida, ajuste independente de tamanho e gravação das preferências em perfil isolado. Nenhuma partida ou save real foi aberto. Logs, backups e prévias em work/srt-free-layout no workspace do Codex.

## SRT: arrasto direto sobre a tela do jogo — 22/10/2026

Com Mover livremente ativo e o SRT exibido sobre o jogo, agora é possível clicar e segurar o botão esquerdo em qualquer parte de um painel para arrastá-lo diretamente. Com Separar inventário ativo, cada painel é movido de forma independente; em sobreposição, o inventário, que é desenhado por último, recebe o clique. Sem separação, o bloco inteiro é movido. Desligar o modo livre mantém o posicionamento antigo por cantos.

A leitura do mouse ocorre apenas no caminho do SRT em modo livre e com a janela do jogo em primeiro plano. As coordenadas do cliente são convertidas para os pixels do back buffer. O arrasto fica limitado à área disponível; perder o foco encerra o movimento sem pular para a posição do mouse em outro aplicativo. Painéis ocultos/desligados não recebem arrasto. Quando o modo sem bordas esconde o cursor do Windows, o próprio SRT desenha uma pequena seta enquanto a movimentação livre está ativa.

O movimento apenas translada os quads/imagens/efeitos já armazenados, incluindo o ECG: não rasteriza textos nem grava arquivos a cada movimento. Ao soltar o botão, o SRT atualiza as coordenadas normalizadas, notifica a thread do launcher e esta salva as quatro chaves de posição existentes no cv_launcher.ini. A prévia passa a refletir a posição utilizada no jogo. As instruções foram atualizadas nos 13 idiomas.

Arquivos desta continuação: cv_srt.cpp, cv_srt.h, launcher.cpp, launcher_srt_layout_i18n.h e tests/test_srt.cpp. Nenhuma alteração no backend D3D12, FPS/interpolação, FSR, shaders, gameplay ou saves. Testes de mouse por entrada sintética e regressões gerais passaram em JP/US/EU: arrasto independente, notificação ao soltar, preservação das posições, perda de foco, sobreposição, fundos totalmente transparentes, limites de tela, todos os painéis desligados e modos normal/janela separada. Os três executáveis normais foram recompilados. Não foi aberta partida real nem movido o mouse real para testar o jogo. Backups, diff e logs desta continuação: work/srt-game-drag no workspace do Codex.


### RECOMP SETTINGS: áudio e Battle Game (23/10/2026)

- Abas Gráficos, Áudio e Jogabilidade no menu OPTION → RECOMP SETTINGS. A aba Gráficos preserva a aparência/preview e todos os controles existentes.
- Q/E no teclado, L1/R1 no PlayStation, LB/RB no Xbox e L/R no Switch mudam as abas. Direcional ajusta os volumes em passos de 5%; o teclado Q/E só é consultado enquanto este menu está aberto e a janela do jogo tem foco.
- Música e vozes inclui áudio dos vídeos, como na referência Enhanced. Efeitos sonoros têm um volume separado. Ambos começam em 100%, vão até 0% e têm restauração dos volumes. As três entregas de PCM (cellAdec/FFmpeg, cellAtrac e MultiStream ATRAC3) aplicam ganho sem modificar estado ou caches dos decodificadores. 100% conserva as amostras originais.
- Battle Game começa desligado. A opção libera o modo e seus cinco personagens por filtros em cinco leituras verificadas por região (duas entradas sobrepostas da tela de título e a checagem de personagens). Não escreve flags em SYS_WORK, no save nem em EBOOT. Ao desligar, valem os desbloqueios reais da partida; conquistas obtidas normalmente durante o jogo continuam sendo do jogo.
- No launcher, Jogos mostra os dois volumes e Cheats mostra a opção Battle Game. Armazenamento em cv_launcher.ini: [audio] musica=100, efeitos=100; [extras] battle_game=0. As mudanças do menu do jogo e do launcher usam a mesma ponte de configuração. Os textos acompanham os 13 idiomas do launcher; fontes do sistema e rasterização da linha completa permitem ideogramas e árabe.
- CV_NO_LAUNCHER: scripts podem definir CV_MUSIC_VOLUME, CV_SFX_VOLUME e CV_BATTLE_UNLOCKED. Padrões: 100/100/0.
- Testes: code_veronica_menu_tests (normal e interp), code_veronica_enhanced_tests e code_veronica_audio_gain_tests. Este último executa os três caminhos reais de entrega com saída determinística, sem assets ou dispositivo de áudio, verificando silêncio, metade do volume, restauração e buffers sem ganho acumulado. Os testes não substituem uma audição no jogo com os controles físicos.

Não foram alterados os caminhos de saves/shaders, o motor de 60 FPS, a interpolação, o FSR ou os arquivos instalados do jogo para este recurso.

- Exclusividade de Q/E no RECOMP SETTINGS: o menu reserva essas duas teclas no estágio de teclado → pad, antes da mistura com o controle físico. Enquanto a tela estiver aberta elas só mudam a aba, mesmo quando o usuário as mapeou para voltar/confirmar ou outras ações. Fechar, resetar ou desabilitar o menu libera a reserva; os outros atalhos e os botões físicos não são filtrados.

- Abas no controle: a leitura usa as bordas dos botões físicos L1/R1 (LB/RB, L/R), pelo mesmo leitor físico já usado no menu de itens. Não usa máscaras do estado interno de menu do jogo. Compensa a troca L1/L2 e R1/R2 que o leitor físico aplica no layout Xbox 360 (auto/ligado), para as dicas fixas das abas continuarem correspondendo aos ombros físicos. L2/R2 não mudam a aba; segurar o botão não repete, e abrir o menu com um ombro já pressionado aguarda soltar. A leitura adicional só acontece com o RECOMP SETTINGS aberto. Q/E e o mapeamento da partida permanecem como estavam.

- As dicas das abas respeitam o tipo de ícones escolhido em Controles (PlayStation/Xbox/Switch/teclado); em automático seguem o dispositivo usado. Ao trocar abas com Q/E, aparecem as dicas do teclado.
