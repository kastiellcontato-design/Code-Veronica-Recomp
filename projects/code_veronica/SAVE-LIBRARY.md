# Biblioteca de saves — 22/10/2026

O F7 agora abre **Arquivos**, uma janela única com as abas **Mapas / Salvar / Carregar**, no visual escuro, vermelho e dourado aprovado pelo usuário. A biblioteca mantém o formato e a captura/restauração nativa já existentes. O usuário confirmou que criou um save com a versão anterior; esta atualização é de interface, controles e tradução.

## Uso e controles

Ativar as ferramentas F7 no launcher e abrir F7 durante uma partida. A aba Mapas lista as salas dos arquivos da versão ativa, tem filtro por código, sala atual e ajuste da entrada de 0 a 255. O pedido de viagem continua sendo revalidado e executado no dispatcher do jogo. Uma imagem de mapa só aparece quando existe um save com miniatura daquela mesma sala/variante; não são usadas imagens ilustrativas geradas.

Aba Salvar: **Novo save** cria um arquivo independente. Selecionar um save existente permite ver sua foto e dados; **Sobrescrever** abre uma confirmação própria, operável com mouse, teclado e controle, inicialmente em Cancelar. Slots originais ficam somente para leitura. Na aba Carregar, selecionar uma entrada e confirmar carrega o progresso.

| Ação | Teclado e mouse | PlayStation | Xbox |
|---|---|---|---|
| Trocar aba | Tab / Shift+Tab, Q / E, clicar na aba | L1 / R1 | LB / RB |
| Selecionar | ↑ / ↓, clicar na linha, roda do mouse | Direcional ou analógico esquerdo | Direcional ou analógico esquerdo |
| Rolar um bloco da lista | Page Up / Page Down | L2 / R2 | LT / RT |
| Primeira / última entrada | Home / End | Segurar navegação | Segurar navegação |
| Ação principal (novo save na aba Salvar) | Enter, botão da tela; N também cria save | ✕ | A |
| Sobrescrever selecionado | O, botão da tela, duplo clique na linha de save | □ | X |
| Novo save (alternativa) | N | △ | Y |
| Ajustar entrada do mapa | ← / →, botões − / + | ← / → | ← / → |
| Filtrar mapas | F e digitar, ou clicar no campo | Filtro opcional; navegação sem teclado | Filtro opcional; navegação sem teclado |
| Voltar / cancelar | Esc, F7, botão Voltar | ○ | B |

Segurar a navegação do controle acelera depois de 1,1 s. Os gatilhos também repetem a rolagem por blocos, em todas as abas. Na confirmação, ←/→ ou Tab escolhem Cancelar/Confirmar e Enter/✕/A executa. Os atalhos da tela acompanham o último dispositivo usado. O menu desfaz o remapeamento de ombros do layout Xbox 360 apenas para sua própria navegação, portanto os nomes LB/RB/LT/RT correspondem aos botões físicos. O mapeamento da gameplay é preservado.

## Idiomas

Todos os rótulos, dicas, confirmações, erros e avisos da nova janela usam o idioma do launcher: português, inglês, espanhol, japonês, francês, alemão, italiano, polonês, turco, árabe, chinês simplificado, coreano e russo. `cv_archives_i18n.h` tem uma checagem de compilação que exige todos os textos nos 13 idiomas. Fonte por idioma e leitura da direita para a esquerda usam as funções já existentes em `cv_srt_i18n`.

Os nomes descritivos completos das salas continuam em inglês apenas no idioma inglês. Nos demais idiomas a localização aparece como área/sala traduzidas e o código `rm_XXXX`, sem apresentar um nome em inglês como se estivesse traduzido. Nomes próprios dos personagens são mantidos.

O F6 (menu de inventário) já possui interface nos 13 idiomas em `cv_items_i18n.h` e nomes dos itens em `cv_srt_i18n`; `cv_item_menu_toggle(...,g_lang)` recebe o idioma atual do launcher. O inventário do painel SRT também tem traduções; a opção de idioma do SRT pode acompanhar o launcher (padrão) ou usar a preferência explícita do jogador, que foi preservada.

Sem limite de 15 slots: cada entrada fica em `<pasta de saves>/CodeVeronicaLibrary/<TITLE_ID>/*.cvsav`. Limitado pelo espaço de armazenamento disponível. A pasta segue `PS3_SAVE_ROOT`, a mesma configurada no launcher. JP/US/EU ficam separados e um arquivo de outra região é recusado. Os DATA0.DAT originais são lidos somente para exibir os 15 slots antigos; não são alterados. Para eles não há uma foto retroativa: aparece Slot original, e a data é a do arquivo DATA0, não a data individual que o jogo não guarda.

## Persistência e formato

`cv_save_library.cpp/.h`; locais em `cv_save_rooms.h`; integração no dispatcher e abertura unificada em `cv_test_tools.cpp`. A captura GPU reaproveita o caminho de screenshot limpo em `rsx_d3d12_backend.c`, somente sob pedido. Não muda FPS, pose, replay, FSR ou lógica de renderização. O hook de captura faz readback de um quadro quando solicitado, então não medir performance durante a gravação.

Arquivo v1: cabeçalho de 128 bytes (magic, versão, região, timestamp UTC FILETIME, tamanhos, CRC32, personagem/sala/tempo), payload nativo de 0x838 bytes, configuração de 0x34 bytes e flag auxiliar guardados para referência, e miniatura BGRA 320×180. O carregamento restaura o progresso, mantendo as opções do perfil atual. CRC cobre o arquivo inteiro; tamanhos e região são conferidos antes de qualquer gravação no estado do jogo, assim como versão, personagem, sala e coordenadas finitas. Arquivos temporários `.pending` não entram na lista.

Gravação em worker: WriteFile + FlushFileBuffers, seguido de MoveFileEx com WRITE_THROUGH, na mesma pasta; substituição somente após arquivo completo. Novo save tem timestamp, PID e sequência no nome, sem limite fixo de número. Miniatura capturada antes dos overlays do port; espera máxima de 3 s. Se não houver um quadro disponível, o progresso é preservado com imagem vazia, sem inventar uma captura. A leitura de fotos é limitada a um cache pequeno, não carrega todas as imagens do catálogo na RAM.

## Dados nativos e carregamento

O snapshot espelha o código original PS3 (EU **00190BA8**, JP **0018F1A8**, US **0018F210**): versão 10, andar, status 0x78280000, posição, ângulo, arma e vida do ator atual. A cópia é feita só na memória local, no dispatcher PPU entre quadros; não altera o checkpoint vivo, inventário, fita nem contador de saves. Além dos campos do ator, conserva o bloco de progresso original sys+8C..+8C+838: inventário, flags de eventos, tempo etc.

O load faz a mesma cópia do slot que a rotina original faz (EU 001C1F78), e entra no caminho de reinício confirmado de EU 00033FD0, sem construir nem destruir objetos de UI. Chama a rotina original LOAD_BEGIN (EU **001D0680**, JP **001CE9E0**, US **001CE860**) e para as vozes do banco 9 antes da troca (mesmo conserto de F7 confirmado pelo usuário). Define tarefas 00300050, máscara bloqueada 80 e limpa os bits exclusivos de UI/typewriter. Não simula uma confirmação por tecla e não usa endereços PS2.

É um save de progresso nativo, não um dump completo da máquina: a sala é reconstruída pelo jogo ao carregar. Estado transitório de inimigos, projéteis ou animação não é uma promessa de savestate. Os campos de posição atuais são guardados; a entrada final também depende da lógica nativa da sala. Pedidos são revalidados e recusados em telas/transições sem partida controlável.

## Entrega e limites de validação

Compilar os três executáveis normais (CV_APPEARANCE=OFF / CV_CHARACTERS=ON). Logs desta entrega em `work/save-library/build-final-{jp,us,eu}.log`. Backup anterior: `projects/code_veronica/save-library-backups/pre-library-20261022/`. Não houve testes isolados nem controle de gameplay, conforme preferência do usuário. Compilação não comprova restauração visual em todas as salas; a primeira confirmação de salvar/carregar na partida será do usuário. Sem commit/push solicitado.

Os rótulos de locais são adaptados de recvx-enhanced-port (aikatore e contribuidores, MIT), usado só como referência de nomes. Licença e atribuição em SAVE-LIBRARY-NOTICES.txt. Não foram incluídos arquivos de jogo ou saves do usuário.

Nota do load: o bit sys+50 0x200 é mantido, como no caminho original de load confirmado. O reinício da sala usa esse bit para distinguir a restauração do save de uma partida nova.

Entrega concluída: build-final-jp/us/eu retornaram exit 0; executáveis normais recompilados, hashes registrados em work/save-library/production-installed.json. A licença dos rótulos acompanha os EXEs. Não foi executada a gameplay nem foram criados/alterados saves reais durante a implementação.

## Atualização Arquivos — 22/10/2026

Os três executáveis normais JP/US/EU foram recompilados. Logs da interface em `work/archives-menu/build-{jp,us,eu}.log` no workspace do Codex. Os arquivos anteriores a esta atualização estão em `work/archives-menu/before/`. Não houve mudança em 60 FPS, FSR, interpolação, personagens ou formato dos saves. Não foram executadas gameplay nem gravações/carregamentos de saves reais para validar esta interface; a navegação e o visual no jogo ficam para a avaliação do usuário. Compilação e revisão do código não equivalem a teste com os controles físicos.


## Correção das dicas por dispositivo — 22/10/2026

Corrigida a atualização dos atalhos do menu Arquivos. No modo Automático, qualquer tecla (inclusive digitação no filtro), clique, rolagem ou movimento real do mouse muda as dicas para teclado; botões e analógicos dos controles atualizam para Xbox ou PlayStation. A mudança solicita redesenho imediato, mesmo quando não houve alteração de seleção/aba. O modo de ícones escolhido no launcher (`CV_BUTTON_ICONS`: auto/ps/xbox/keyboard) também é respeitado, incluindo mudanças enquanto a janela está aberta. O dispositivo que abre F7 é enviado explicitamente pelo launcher: F7 do teclado, clique ou combinação de controle, evitando herdar o último dispositivo da gameplay pausada. A leitura inicial dos botões usa o mesmo remapeamento local usado na navegação para não produzir bordas falsas ao abrir.

Recompilados os três executáveis normais JP/US/EU. Logs: `work/archives-menu/build-input-{jp,us,eu}.log` no workspace do Codex. Sem teste de gameplay ou controles físicos; validação por compilação e revisão das rotas de entrada.
