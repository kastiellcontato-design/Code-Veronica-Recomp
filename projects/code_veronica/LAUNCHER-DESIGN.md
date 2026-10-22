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
