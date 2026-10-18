# SRT nativo — implementação e passagem para outra IA

Atualizado em 16/10/2026. Branch `code-veronica`, autorizada pelo usuário.
Sem commit nem push. Implementação própria em C++; o scanner/interface C# do
projeto de referência não foi incorporado nem usado como dependência.

## Como usar

Abra pelo iniciador normal, entre na nova aba **SRT** e ligue o painel.
Também é possível mostrar/esconder durante a partida com **F8** ou **L3+R1+○**.
As escolhas são gravadas em `[srt]` no `cv_launcher.ini` ao lado dos executáveis.
O padrão para uma instalação sem configurações é desligado; nesta sessão foi
ligado no launcher para o teste visual do usuário.

A página permite selecionar nome, vida, tempo de jogo, contadores de saves /
tentativas / sprays, inventário e limite de inimigos (desligado, 1–5 ou todos).
Há quatro cantos, tamanho de 50% a 200% e idioma automático, inglês ou PT-BR.
Tamanho e opacidade usam **barras deslizantes**, com ajuste de 1 em 1% ao
arrastar ou usar a roda do mouse. A porcentagem atual fica visível à direita.
A **Opacidade do fundo** vai de 0% a 100% (padrão 70%). Ela afeta os fundos, sem apagar os textos
e as barras. Os contadores ficam ligados por padrão em novas configurações.
Nesta sessão, `contadores=1` foi ativado na configuração atual a pedido do usuário;
as outras preferências foram preservadas.
A escala acompanha a altura da imagem e diminui automaticamente se o conteúdo
completo não couber, mantendo margem de 16 pixels.

## Design original

Painel estreito, preto translúcido, linhas discretas em vermelho escuro,
texto creme e detalhes dourados no equipamento. Nome serifado, vida atual /
máxima, porcentagem, barra proporcional e traçado de ECG decorativo animado
(não é medição cardíaca real; fica em linha reta no estado de morte). As fontes Segoe UI/Semibold (nomes, títulos, estado e informações pequenas)
e Georgia (números em destaque)
são rasterizadas na resolução final da imagem, sem ampliar pixels de uma
fonte pequena. O tempo usa o contador do próprio jogo.
Os blocos são independentes para não ocupar a tela com informação indesejada.

As cores seguem os dados de vida: verde a partir de 60%, amarelo a partir de
30%, laranja a partir de 15%, vermelho abaixo disso. O rótulo dos dois estados
intermediários é Caution / Atenção. Veneno é roxo; gás e morte têm indicação
própria. Detecta Claire Redfield, Chris Redfield, Steve Burnside e Wesker
(para modos em que esteja presente). Não há retratos.

O inventário fica numa coluna à direita da vida/tempo/inimigos, com o topo
alinhado ao painel principal. O item equipado fica destacado e os itens de dois espaços
ocupam uma linha. Quantidade e munição infinita aparecem quando aplicáveis.
**Esta versão usa nomes dos itens**, em inglês ou português. Os ícones
originais ainda não foram identificados/recortados; não foram inventados nem
incluídas imagens do jogo na distribuição. Outros idiomas traduzem os rótulos
do painel, mantendo nomes de itens/inimigos em inglês.

## Arquitetura e limites

- `cv_srt_data.cpp`: leitor independente, somente leitura da VM do port.
  Usa `vm_read8/16/32` para conversão big-endian. Valida páginas, personagem e
  assinatura RDX de sala carregada; confere novamente sala/personagem ao final
  para descartar uma troca de sala ocorrida durante a amostra.
- `cv_version.h`: endereços específicos de EU NPEB00553, JP NPJB00135 e
  US NPUB30467. JP usa 200 HP (400 em Very Easy); EU/US usam 160 (320).
- `cv_srt.cpp`: layout, cores, tradução recebida do launcher e cache de desenho.
  Amostragem limitada a aproximadamente 30 Hz; desenho reconstruído somente
  quando o estado exibido, configurações ou resolução mudam. O ECG acrescenta
  um lote pequeno de segmentos animados sobre esse cache, sem reconstruir
  fontes por causa da animação. Runs iguais são unidos horizontal e
  verticalmente para reduzir a geometria. Com SRT desligado
  não lê memória do jogo nem emite geometria.
- `launcher.cpp` / `launcher_i18n.h`: página SRT, preferências, F8 e L3+R1+○,
  traduções nas 13 línguas do launcher. As opções anteriores foram preservadas.
- `main.cpp`: registra o callback do painel.
- `rsx_d3d12_backend.c`: chama o callback depois da imagem final, com lote de
  vértices separado dos outros overlays. Brilho, contraste e saturação do jogo
  não mudam as cores/legibilidade do painel. O overlay de desempenho continua
  por cima; se houver sobreposição, escolha outro canto para o SRT.

Não há injeção em processo externo, alteração de HP, inventário ou saves.
Não muda recompilador de shaders, FXC/DXC, patches ou menu PC SETTINGS.
O leitor limita a lista a 64 estruturas de inimigos e ignora tipos desconhecidos.
Nos chefes com máximo conhecido usa o valor conhecido. Nos outros inimigos,
o máximo é o maior HP observado naquela entrada/sala: continua sendo uma
**estimativa**, embora o prefixo `~` tenha sido removido do desenho a pedido
do usuário. Pode ser menor que o máximo real se o inimigo já estava
ferido quando o painel foi ativado. Ao recarregar sala/entrada, a estimativa
é reiniciada. Não é uma base universal de máximos por dificuldade.

A dificuldade US foi colocada em `0xBB35A4`, acompanhando a estrutura da EU
com deslocamento de `-0x10300` e os campos vizinhos. O plano original/provider
indicava `0xBB36A4`. **A variante Very Easy da US ainda precisa de comparação
no jogo/RPCS3**; os testes sintéticos não comprovam o endereço de um executável.

## Validação realizada

Build pelo único ponto autorizado, `build-release.cmd`, com `CV_BUILD_JP=1`,
`CV_BUILD_US=1` e `CV_SRT_TESTS=1`: os três executáveis e os três testes passaram.
Log em `build-release/srt-build.log` (diagnóstico local, não distribuir).

`tests/test_srt.cpp` usa memória sintética e verifica endianness, limites de
estados/veneno/gás/morte, dificuldades, Steve, equipamento, quantidade/infinito,
Tyrant e máximo estimado, assinatura inválida e ausência de escrita na VM.
Verifica também que só os segmentos do ECG mudam durante a animação e que
a linha de morte fica estática. Também verifica cache e geometria nos 4 cantos, escalas 50/100/200%, resoluções
720p/1080p/4K e lista de 64 inimigos. O pior caso testado ficou em cerca de
118 mil quads, abaixo do limite de 131072. Isso não é uma medição de FPS real.

As prévias locais em `codex_diag_srt/srt-*-state-*.png` usam dados de teste,
não representam uma captura de gameplay. A página SRT foi conferida visualmente
no launcher. O usuário testou uma partida EU e confirmou que o painel e o inventário
funcionaram. Foi observado Claire com 40/160 HP, 25%, Atenção em laranja,
tempo 00:01:41, três zumbis e equipamentos/itens reais, com a janela indicando
30 FPS. Isso é observação da primeira versão; a revisão de fontes/coluna
lateral/ECG passou nos testes isolados e aguarda teste visual do usuário.
O microbenchmark mais recente de 10000 chamadas de layout já cacheado + ECG ficou em
aproximadamente 0,005 ms/chamada nesta máquina; não inclui reconstrução GDI,
leitura do jogo, envio de vértices ou GPU e não garante o custo total.
**Ainda pendentes:** veneno real, troca para Chris/Steve, chefes, inventário
completo, JP/US em gameplay e comparar os números com o SRT do RPCS3.
Não afirmar que esses testes de gameplay já foram concluídos.

## Continuação

1. Testar visualmente a revisão lateral/ECG no jogo e comparar HP, tempo e
   inventário da partida real com o SRT do RPCS3.
2. Confirmar a dificuldade US (especialmente Very Easy) e registrar evidência.
3. Conferir os ícones no jogo e completar os IDs ainda sem recorte. As duas
   folhas originais agora são carregadas da cópia selecionada em runtime;
   nomes continuam sendo o fallback. Ver a última seção deste documento.
4. Medir o custo do painel ligado/desligado na mesma cena; a geometria é
   cacheada, mas o lote ainda é enviado em cada quadro apresentado.
5. Somente após validação do usuário, considerar commit/push (não autorizados
   nesta sessão).

## Créditos

Endereços e regras de memória estudados no **re-cvx-srt-provider, de Kapdap
(MIT)**: https://github.com/kapdap/re-cvx-srt-provider . O design e a
implementação nativa deste port são independentes. Nenhum arquivo do jogo,
executável, DLL ou fonte gerada deve ser incluído no Git por causa desta função.

## Referências visuais e decisão do usuário

Depois de testar a primeira versão, o usuário confirmou o funcionamento e
pediu ECG animado, texto mais nítido e inventário ao lado da vida. Também
autorizou deixar os ícones para depois de identificar as imagens originais,
possivelmente por dump. As três melhorias visuais foram implementadas.

Duas imagens de conceito foram criadas com a ferramenta integrada de imagens,
somente para estudar o visual; não são capturas do jogo nem texturas usadas
pelo código. Arquivos e prompts exatos em
[codex_diag_srt/design/REFERENCIAS.md](codex_diag_srt/design/REFERENCIAS.md).
A prévia determinística do desenho real está em
`codex_diag_srt/srt-NPEB00553-state-0.png`, com dados sintéticos.

## Refinamento de legibilidade e opacidade — 16/10/2026

Pedido do usuário após ver a revisão em jogo: acentos em maiúsculas, rodapé
legível, VIDA sem artefato, ECG mais lento/suave, barra de vida mais grossa,
menos área ocupada, opacidade ajustável e espera menor.

- A fonte não era a única causa: `std::towupper` com locale C deixava ç/ã em
  minúsculas no meio das palavras. `cv_srt_text.h` agora usa conversão Unicode
  do Windows, testada com ATENÇÃO / INVENTÁRIO / MUNIÇÃO / GÁS e demais acentos.
- Georgia e Segoe UI substituem Palatino/Bahnschrift. Rótulos pequenos e
  rodapé usam tamanhos maiores, com antialiasing na resolução final.
- Foi removido o pequeno canto decorativo desenhado sobre o V de VIDA.
- Vida/máximo/porcentagem ficam numa linha. Barra aumenta de 7 para 11 unidades,
  módulos de tempo/inimigos ficam mais compactos e os saves aparecem nos
  contadores. Inventário continua na coluna direita.
- ECG à esquerda do estado, com cobertura fracionária nas bordas dos segmentos
  (sem os degraus de arredondamento) e ciclo de aproximadamente 2,2 s, antes
  0,9 s. Continua separado do cache de texto; morte mantém linha reta.
- `[srt] opacidade=70` / `SRT_OPACITY=70`, limitado a 0–100. Preferência salva
  pelo launcher e alterada ao vivo. Rótulo traduzido nas 13 línguas.
- A espera exibe somente a mensagem, sem título e rodapé microscópico.
  Em 720p/100% ocupa aproximadamente 208×36 px, em vez de 244×104 px.
- `build-release/srt-refinement-build.log`: builds EU/JP/US e testes aprovados,
  incluindo Unicode, transparência real do fundo, limites da espera, ECG e
  posicionamento. Prévia local em `codex_diag_srt/srt-*-state-*.png`,
  `srt-*-opacity-*.png` e `srt-*-waiting.png` (dados sintéticos).
- Esta revisão ainda aguarda conferência visual do usuário no jogo. Nenhum
  leitor de memória, patch, controle ou compilador de shader foi alterado
  neste refinamento. Sem commit nem push.

## Sombras, suavização e crédito — revisão seguinte de 16/10/2026

O usuário pediu barras de inimigos mais grossas, menos serrilhado em nome /
inventário / estado, retirada do símbolo ~, crédito no SRT e sombras nos
elementos em primeiro plano quando o fundo for transparente. Implementado:

- Barras dos inimigos com 11 unidades de altura, igual à barra da personagem;
  espaçamento das linhas ajustado para não sobrepor o próximo inimigo.
- Segoe UI Semibold em nomes/títulos/estados, sem o traço serifado pequeno.
  Cada texto é renderizado em 2x e reduzido para um tile com resolução física
  exata. A posição é alinhada aos pixels físicos, incluindo a coluna lateral,
  para não interpolar a imagem uma segunda vez. Isso NÃO muda o AA do jogo,
  a resolução nem os textos do PC SETTINGS.
- Cache de textos limitado a 96 entradas e 12 MiB de bitmaps finais. O layout
  e a animação reaproveitam esses tiles; não há renderização 2x do jogo/painel.
- Sombra preta discreta e contorno suave nos textos; sombra/moldura escura nas
  barras; sombra no traçado animado. Nada disso desaparece com SRT_OPACITY=0.
  Os fundos/caixas do painel não ganharam uma sombra grande.
- Valores dos inimigos ficam apenas `atual / máximo`, sem ~. O leitor e a
  estimativa de máximo observado continuam iguais (não se tornou máximo
  comprovado por remover o marcador).
- Rodapé `SRT · CODE:VERONICA` com `por Kastiel` / `by Kastiel` à direita.
  Demais idiomas usam o crédito já traduzido do launcher via `SRT_CREDIT`.
  O atalho F8 continua funcionando, mas cedeu o espaço no rodapé ao crédito.
- Em escalas/resoluções grandes, se as muitas gradações de AA/sombra excederem
  o orçamento de 120 mil quads, os tons intermediários são agrupados com o
  menor passo necessário. Os HUDs de tamanho normal preservam todos os tons.
  Isso evita cortar o fim do painel no limite do backend de 131072 quads.

`build-release/srt-shadow-build.log`: build e testes EU/JP/US aprovados,
incluindo nome com sombra mesmo a 0% de fundo, barras grossas, 4K/tamanho 200%,
cinco inimigos com nomes longos e fundo 0/100%. Pior lote testado ~118 mil.
Microbenchmark local: ~0,005 ms/chamada com layout cacheado + ECG; ~2,7 ms
para reconstruir o layout/tempo em 720p com os tiles de texto reaproveitados.
São medidas isoladas de CPU, sem o custo de upload/desenho da GPU, sem promessa
de FPS. Novas prévias determinísticas em `codex_diag_srt/`, incluindo
`srt-NPEB00553-shadow-bright.png` sobre fundo de teste claro, opacidade zero.

Mantidas as preferências escolhidas pelo usuário: tamanho 90% e fundo 10%.
Revisão pronta para teste visual no jogo; sem commit/push.

## Sliders, crédito completo e novos atalhos — 16/10/2026

Pedido mais recente: trocar +/− por barras de arrastar, escrever Feito por
Kastiel (traduzido) e exigir três botões para evitar acionamentos acidentais.

- Tamanho (50–200%) e fundo (0–100%) agora têm trilho, preenchimento, puxador
  e valor numérico. Clique ou arraste em qualquer ponto do trilho; passos de
  1%, também pela roda do mouse.
- Captura do mouse mantém o arrasto quando o cursor sai da barra/janela;
  extremos são limitados. Alterações aparecem ao vivo, mas o INI é gravado
  ao soltar o mouse ou perder a captura/foco, evitando escrita por movimento.
- `cv_launcher_controls.h` centraliza a conversão posição/porcentagem e as
  máscaras dos atalhos, usadas pelo launcher e pelos testes.
- SRT: F8 ou **L3+R1+○** (bolinha/B, posição leste do controle).
- Launcher dentro do jogo: Insert ou **L2+R3+R1**. Os três botões precisam
  estar pressionados no mesmo controle; manter o combo não fica alternando.
  As dicas na interface foram atualizadas nas 13 línguas.
- Crédito SRT: **Feito por Kastiel** em português, **by Kastiel** em inglês
  e a tradução de autoria correspondente nos outros 11 idiomas, por meio
  de `srt_credit_text()` / `SRT_CREDIT`. Polonês e russo agora usam a frase
  de criação completa também. As traduções longas ocupam duas linhas no
  rodapé, com altura reservada, sem reduzir a fonte ou cortar o nome.

Log desta revisão: `build-release/srt-sliders-build.log`. Os testes verificam
cada percentual inteiro dos dois intervalos, limites fora do trilho, cada
botão ausente dos novos combos e crédito nas 13 línguas; geram prévias locais
`srt-*-credit-*.png` para inspecionar as traduções e quebra de linha.

Resultado desta revisão: builds e testes EU/JP/US aprovados. Prévias PT-BR
e turco conferidas: Feito por Kastiel completo e tradução longa em duas
linhas. Launcher conferido visualmente com os dois sliders, os valores ao
lado e a dica L3+R1+○. Foi observada opacidade 49% durante a interação do
usuário; a automação de arrasto foi interrompida para não disputar o mouse.
Mapeamento de todos os percentuais e máscaras dos atalhos testados de forma
automatizada; verificação física dos combos nos controles fica com o usuário.
As preferências atuais não foram redefinidas. Sem commit nem push.


## Ícones originais e posições espelhadas — 16/10/2026

Pedido do usuário: manter vida/estado/inimigos contra a borda escolhida e o
inventário do lado de dentro, nos quatro cantos. O desenho agora desloca as
colunas separadamente; o ECG acompanha a coluna principal. No canto direito,
o inventário fica à esquerda da vida. No esquerdo, continua à direita.

As duas folhas fornecidas pelo usuário foram identificadas na própria cópia:
`USRDIR/BHCV/nativePS3/biocv_disc/<idioma>/item1/subtx_tex.arc`, texturas
`tex0004_BM` (primeira folha, armas/itens iniciais) e `tex0003_BM` (segunda).
Os TEX descomprimidos de EU/JP/US têm hashes SHA-256 iguais; as pastas locais
são respectivamente `eng`, `jpn` e `usa`. **Não é necessário copiar os PNGs
do dump para o launcher. Nenhuma imagem do jogo foi adicionada ao código.**

`cv_srt_icons.cpp/.h` contém o leitor próprio de SFH/ARC/TEX e o mapa de
recortes, usando apenas o descompressor zlib já existente no runtime. Limites:
arquivo de até 8 MiB; tabela/offsets/tamanhos conferidos antes de ler; somente
os dois TEX DXT1 de 768×768 esperados são aceitos. Sem alteração do arquivo,
checksums, memória do jogo, inventário ou save. Não depende de DLL nova.

O fundo azul escuro comprimido em DXT1 é convertido em alpha antes de
recortar as margens. Ciano das ervas e azul das joias são preservados. Os
ícones têm sombra própria independente da opacidade do painel, proporção
preservada e filtro bicúbico. Área máxima de 28×25 unidades na grade e 34×32
no equipado, sem aumentar células ou painel. Nomes sem quantidade podem
ocupar duas linhas dentro da mesma célula; nomes longos continuam usando
reticências quando não couberem. Quantidades continuam visíveis.

Há 127 IDs mapeados. Entradas vazias, placeholders numéricos e recortes ainda
não identificados continuam mostrando **o nome sem imagem**, sem inventar um
ícone. Os IDs ainda sem recorte são:
`0A 26 31 3A 4A 4D 66 6D 6E 7F 84 85 86 87 89 8A 8B 8C 8D 8F 92 93 94 97 98 99`.
Também há fallback para nomes se o arquivo estiver ausente ou inválido. A
associação dos itens ainda deve ser conferida pelo usuário durante a campanha;
o teste de renderização não comprova cada associação no jogo real.

A leitura acontece somente quando o inventário aparece pela primeira vez,
usando `PS3_VFS_ROOT` da cópia selecionada. Pixels e bitmaps ficam em cache;
não há leitura de arquivos/descompressão a cada frame. A mudança da pasta do
jogo invalida esse cache. Nenhuma preferência do usuário foi alterada.

Validação: `build-release.cmd`, `CV_BUILD_JP=1 CV_BUILD_US=1 CV_SRT_TESTS=1`.
A opção local `CV_SRT_TEST_GAME_ROOT=<pasta que contém os três títulos>`
ativa os testes de leitura dos arquivos reais de cada região, somente leitura;
sem essa opção os testes continuam independentes dos arquivos do jogo.
Foram verificados recortes, transparência e preservação de azul/ciano com
fixture sintética, leitura de ícones das duas folhas em cada região, fallback,
metadados inválidos e limites. O teste das quatro posições verifica a barra
de vida e o ECG na coluna externa, em 720p/1080p/4K e 50/100/200%.
Também há inventário cheio de ícones em 4K/200%, opacidade zero e limite de
geometria. Log final em `build-release/srt-icons-build.log`.

Prévias locais `codex_diag_srt/srt-*-corner-{tl,tr,bl,br}.png`,
`srt-*-full-inventory-4k.png` e `srt-*-shadow-bright.png` usam estado sintético
com ícones lidos da instalação; **não são capturas de gameplay**. A revisão
está compilada para as três regiões e aguarda o teste visual do usuário no jogo.

Resultado da revisão: builds e testes EU/JP/US aprovados. Pico entre os casos
testados: 117625 quads (limite do backend 131072), incluindo 11 ícones e
sombras em 4K/200%. A conferência visual das prévias confirmou o espelhamento
e ausência da placa azul nos itens exibidos. Custo medido somente do layout
já cacheado + ECG: cerca de 0,006–0,007 ms/chamada; reconstrução do layout
sintético ao trocar o timer: cerca de 4,2 ms. Essas medidas não incluem o
custo total da GPU e não são uma promessa de FPS no jogo.


## Nomes completos e prévia de dois slots — 16/10/2026

O usuário confirmou os ícones no jogo e pediu que nomes de itens com quantidade
não fossem cortados com reticências. Todos os nomes da grade, com ou sem
quantidade e com ou sem ícone disponível, agora são quebrados por palavras
usando as métricas da própria Segoe UI de 13 unidades. A fonte não é reduzida.
Palavras maiores que uma célula são divididas por caracteres, preservando
pares UTF-16. Cada fileira usa a altura necessária para o maior dos seus dois
itens; a quantidade ocupa uma linha separada abaixo do nome. As demais
fileiras continuam compactas. O nome do equipado também pode quebrar e sua
caixa só cresce se precisar. O ajuste à tela considera a altura calculada.

Itens de dois slots continuam ocupando uma única célula de largura dupla,
alinhada ao começo da fileira; não são duplicados em duas células. O ícone
pode ocupar 56×25 unidades na grade (58×32 no equipado), mantendo proporção,
transparência e sombra. Nomes completos continuam ao lado e quantidades abaixo.

Prévias do **renderer de produção**, com inventário sintético e ícones reais,
sem alterar inventário ou save do usuário:
- `codex_diag_srt/srt-NPEB00553-two-slots-pt-BR.png`
- `codex_diag_srt/srt-NPEB00553-two-slots-en.png`
- `codex_diag_srt/srt-NPEB00553-wrapped-ammo-pt-BR.png`
- `codex_diag_srt/srt-NPEB00553-wrapped-ammo-en.png`

A prévia contém Lugers douradas, submetralhadoras e lança-foguetes em três
fileiras largas; munição de pistola/faca e isqueiro/manual em células comuns.
As imagens são recortadas diretamente da saída do SRT, sem ampliar ou
redesenhar o painel depois. São dados de demonstração, não captura de gameplay.

Build autorizado `build-release.cmd` com EU/JP/US e `CV_SRT_TESTS=1`, log
`build-release/srt-wrap-build.log`: três builds e testes aprovados. Testes de
quebra com nomes PT/EN, acentos e palavra maior que a coluna verificam
preservação de todos os caracteres e largura de cada linha. A prévia verifica
que não há divisão central nas três células largas, mas há nas células comuns.
Também passaram novamente cantos, escalas, resoluções, opacidade, inventário
cheio, limite de geometria e animação. Pico com arquivos reais: 117554 quads; teste adicional sem arquivos/ícones
aprovado, pico 119657 (limite 131072). Preferências
do usuário preservadas; revisão aguarda a conferência visual no jogo.

## Wesker, árabe e teste de traduções (Claude) — 16/10/2026

Continuação do trabalho do GPT (fotos, munição do equipado, 13 idiomas).

- Munição no bloco **Equipado** e fotos de Claire/Chris/Steve já estavam no
  código; conferidas nas prévias.
- Foto do **Albert Wesker** (modo Battle, personagem 3): `launcher_art/srt_wesker.png`,
  96×96, recortada da imagem enviada pelo usuário. Entra no exe como o recurso
  `SRT_WESKER`, como as outras.
- O último build do GPT falhava no teste de traduções: o teste comparava os
  5 primeiros caracteres com "Objeto desconocido" e reprovava itens
  espanhóis legítimos que começam por "Objeto". Agora procura o termo inteiro.
- Árabe: com RTL, o GDI+ invertia o alinhamento e jogava o texto para a
  direita, colando nos números. Agora o alinhamento é trocado no RTL e o árabe
  ocupa as mesmas posições dos outros idiomas, escrito da direita para a esquerda.
- Build `build-release/srt-wesker-build.log`: EU/JP/US e testes aprovados,
  pico 114297 quads. Prévias em `codex_diag_srt/srt-*-character-3-*.png`
  e `srt-*-locale-*.png`. Falta o teste visual do usuário no jogo. Sem commit.

## Chefes, efeito e fotos dos inimigos — 16/10/2026

- Chefes reconhecidos pelo tipo: Gulp Worm (2), Alexia I/II/III (12/13/14),
  Nosferatu (15), Steve mutante (17), Tyrant (19), Albinoid adulto (22),
  aranha gigante (23). Vão para o topo da lista dentro do limite de inimigos,
  num bloco com moldura dourada, CHEFE, nome grande, barra em quartos e
  losangos de fase na Alexia. Textos "Chefe"/"Fase" nos 13 idiomas.
- Efeito escolhido pelo usuário: a barra e a moldura pulsam (1,4 s) e um
  brilho lento cruza só a parte com vida, depois descansa 1,2 s. Desenhado
  por cima do cache, como o ECG (`animate_bosses`).
- Fotos geradas pelo usuário (`launcher_art/srt_boss_*.png`, `srt_enemy_*.png`,
  96×96, recortadas do zip dele). O usuário autorizou publicá-las no GitHub.
  Sem foto do zumbi anatomista (26); sweeper, formiga e aranha filhote
  vieram no zip mas não têm tipo reconhecido no leitor.
- Cada pixel distinto vira um quad: acima de 44 px na tela a foto é resolvida
  em 44×44 e ampliada em blocos. Pico testado 119402 quads (4K, 200%).

## Barra de chefe desenhada e barra no centro da tela — 16/10/2026

- Arte do usuário (Kastiel), autorizada para o GitHub, em duas versões:
  `launcher_art/srt_bossbar_*` (larga, centro da tela) e
  `srt_bossbar_small_*` (compacta, painel). Cada uma tem três camadas: a
  moldura com o canal recortado, o canal cheio e o canal vazio. O texto
  pintado na arte original foi apagado; nome, % e vida são escritos pelo SRT,
  traduzidos e por chefe. O retângulo do canal está em `wide_art`/`small_art`.
- As camadas são imagens de verdade, não quads: o SRT as redimensiona com
  bicúbico para os pixels exatos da tela num atlas RGBA (`prepare_atlas`) e o
  backend as desenha como textura (`rsx_ui_sprite`, `g_rsx_srt_sprites_hook`,
  PSO próprio, SRV `SPRITE_SRV`), 1:1 e nítidas em qualquer resolução. Ordem:
  quads do cache → canal vazio e vida → pulso/brilho/borda quente → moldura.
- Opção no launcher "Barra de chefe" ([srt] barra_chefe, `SRT_BOSS_BAR`):
  painel (padrão) | centro em cima | centro embaixo. No centro, os chefes
  saem da lista do painel e o primeiro ganha a barra larga (860 unidades, nome
  espaçado em serifa em cima, % e vida embaixo, fase da Alexia no meio). Se o
  painel ocupa a mesma borda, a barra se centraliza no espaço que sobra.
- Com o SRT em janela separada, o painel vai para a janela e a barra do
  centro continua sobre o jogo (`design(...,panel,centre)`).

### Status sem corte (2026-10-18)
- Somente o nome da condicao de saude usa ajuste automatico de fonte ao
  espaco de 90 unidades. Mede com a mesma familia GDI+ do desenho e reduz
  apenas quando necessario, reservando margem para sombra/contorno.
- Desativa reticencias nesse campo: ENVENENADO aparece por inteiro.
  Nomes curtos mantem tamanho 14; vale para todos os idiomas/escalas.
- Medicao feita na criacao do tile de texto em cache, sem trabalho extra
  a cada quadro. Layout, ECG, cores e logica de vida permanecem iguais.
- Validacao visual com personagem envenenado ainda depende do usuario.
- Compilacao oficial concluida: EU, JP e US linkados com CV_BUILD_JP=1 e CV_BUILD_US=1; diff --check sem erros no ajuste.

### Correcao do icone da alabarda (2026-10-18)
- O item 0x51 estava mapeado para a celula da linha 1, coluna 3, que contem
  outro objeto. A alabarda aparece no atlas de item1, pagina 1, linha 2,
  coluna 3 (x=306, y=144, celula 102x72).
- Corrigido o mapeamento da biblioteca SRT e adicionada assercao de coordenada
  e cobertura de extracao de 0x51 na integracao com os arquivos regionais.
- O atlas consultado e o original da instalacao local JP; o mod nao e empacotado.

Validacao da alabarda: build-release.cmd concluiu EU/JP/US com CV_SRT_TESTS=1; testes PASS nas tres regioes. A integracao leu o atlas original JP (jpn) e extraiu o ID 0x51 com a celula corrigida. Executaveis atualizados para teste pelo usuario.

### Ordem da janela separada do SRT (2026-10-18)
- A janela separada passa a ter a janela do jogo do mesmo processo como proprietaria, mantendo-se acima dela ao alternar foco e modo de exibicao. Outra instancia/regiao nao pode ser escolhida como proprietaria.
- Ao atualizar o painel, verifica a ordem quando o jogo esta em primeiro plano e restaura TOPMOST somente se necessario, sem ativar o SRT nem tirar o foco dos controles.
- Mantidos o arraste, a posicao salva, APPWINDOW para captura no OBS e a renderizacao do painel. A janela acompanha a minimizacao do jogo.
- Validacao interativa: usuario confirmou que a sobreposicao do SRT funcionou no jogo.
- Validacao: script oficial concluido com EU/JP/US recompilados e CV_SRT_TESTS=1; testes do SRT aprovados nas tres regioes. Sobreposicao em gameplay confirmada pelo usuario.
