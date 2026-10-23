# Icones nativos em alta resolucao

Arte dos oito botoes de face aprovada pelo usuario em 2026-10-18. Recurso
launcher_art/controller_buttons_hd.png, gerado por image_gen a partir das
texturas locais do jogo; sem arquivos originais de jogo novos no repositorio.

O desenho de teclado aprovado continua em cv_keycap_style.h. A substituicao
nativa passa RGBA diretamente ao D3D12; nao tenta gravar imagens grandes nos
buffers pequenos do PS3. Dimensoes logicas, UVs, mapeamento e modo automatico
sao preservados. O seletor de conjuntos existente continua sendo a autoridade.

O callback de arte HD roda apenas quando a textura e enviada/atualizada. O
backend conserva a textura original e usa um recurso separado, com descarte
adiado seguro para GPU. Texto, geometrias e texturas de cenario nao reconhecidas
permanecem originais. Mods de textura continuam com seu caminho existente; a
arte nativa reconhecida tem precedencia. CV_HD_BUTTONS=0 permite desativar a
substituicao nativa antes da execucao.

Resolucao: escala inteira arredondada para cima de RES_SCALE, limitada a 4x.
Prompts 32x32 usam no minimo 2x. Assim 720p/1080p/1440p usam fontes de pelo
menos 64px, 4K usa 96px e 2880p usa 128px, com a arte mestre de cerca de 344px.
Mapas, paginas e texto de tela preservam as medidas logicas e recebem os
icones sobrepostos em resolucao maior. As teclas do manual sao redesenhadas
pelas posicoes registradas no gerador ja existente. O desenho original de
controle no manual e preservado. O manual japones agora tem um layout proprio de teclado, derivado da pagina
PS japonesa, sem comparar seu texto com o da pagina Xbox.

Arquivos envolvidos: button_icons.cpp, rsx_tex_hook.h/.c, campo/recurso HD e
selecao de textura no backend D3D12, recurso PNG, teste isolado e fluxo opcional
no build-release.cmd. Preservar os demais trechos de renderer/FPS sem commit.

Validacao: EU, JP e US compiladas pelo script oficial; log local em
build-release/hd-buttons-build.log. Testes isolados passaram, com log em
build-release/hd-button-tests.log. Conferidas visualmente as imagens produzidas
pelo compositor real (prompts PS/Xbox/teclado, texto, mapa e manual).
CV_HD_BUTTON_TESTS_ONLY=1 usa somente build-release.cmd
para compilar/rodar o compositor real em ambiente isolado, sem controlar jogo.
Compilacao final requer CV_BUILD_JP=1 e CV_BUILD_US=1. O usuario confirmou
os icones da gameplay e o alinhamento do manual nas tres regioes. O ajuste
final das ilustracoes ainda aguarda teste no jogo. Nenhum commit realizado.


Refinamento do manual (2026-10-18): cada pagina PS/Xbox agora identifica seus
proprios botoes, incluindo os simbolos brancos e os botoes extras da versao JP.
Nao reutiliza coordenadas do Xbox no PlayStation. O compositor limpa o circulo
original com amostras do painel local antes de aplicar a arte nova; pixels fora
dos circulos permanecem identicos. Gameplay, teclado e backend HD preservados.

Teste isolado ampliado: paginas 00/01/04 dos seis idiomas eng/fra/ger/ita/spa/jpn,
ambos os controles, contagens completas de botoes, marcos conhecidos EU/JP,
ausencia de sobreposicao e edicoes confinadas aos circulos originais. Seis PASS
em build-release/hd-manual-tests.log; imagens exportadas e conferidas, incluindo
paginas de itens e tela de jogo. Build regional em hd-manual-build.log. Teste
visual dentro do jogo ainda depende do usuario. Nenhum commit realizado.


Manual japones completo (2026-10-18): habilitada a pagina de teclado no seletor
existente (incluindo automatico), com mapeamento atual, teclado central e texto
japones preservado. A pagina principal usa recortes medidos no PS japones;
nenhuma inferencia a partir da pagina Xbox. Paginas 01/04 tambem usam os icones
PS da propria imagem. O aviso de vibracao do rodape continua legivel.

Somente nos baloes do manual jpn: L1/R1, LT/RT e setas agora sao vetoriais;
as ilustracoes dos controles e o disco de movimento permanecem originais,
conforme ajuste pedido pelo usuario. Arte HD somente nos baloes. Limpeza dos
icones antigos acontece antes da ampliacao. Os desenhos novos herdam a escala
de renderizacao. Paginas dos demais idiomas conservam o desenho aprovado.

Validacao: seis idiomas passaram em hd-jp-manual-tests.log. Acrescentadas
verificacoes de pagina de teclado diferente do fallback PS, destino SET_KB,
teclas para todas as acoes, remapeamento para F9 sem alterar PS/Xbox e limpeza
restrita aos retangulos medidos. Previa exportada pelo compositor real em
hd-button-check-jp/manual_page_0_set_[0,1,2].png; conferida visualmente. Builds
regionais em hd-jp-manual-build.log. Teste dentro do jogo depende do usuario;
nao foram enviados comandos ao jogo. Nenhum commit realizado.


Ajuste das ilustracoes (2026-10-18): retirada a sobreposicao de botoes e setas
no desenho central dos controles. Teste compara essa area pixel a pixel contra
o compositor original para PS e Xbox. Os baloes continuam em HD.
As bordas amarelas do teclado usam keys_for/PAD_KEYMAP do launcher, nao uma
lista fixa. Verificacao adicional muda square para F9+F10 e up para I: ambas
as teclas alternativas ficam marcadas, I entra, W sai e o conjunto inteiro e
comparado com o mapeamento atual. Seis PASS em hd-original-diagram-tests.log;
builds em hd-original-diagram-build.log. Nenhum commit realizado.


## Handoff para o Claudio — preparar o commit destas melhorias

Pedido do usuario em 2026-10-18: documentar o trabalho para o Claudio fazer o
commit depois. Este registro NAO executa nem cria commit. Nao houve envio de
mensagem para outro chat. O diretorio tem alteracoes de outras tarefas: nao
usar git add . nem incluir arquivos inteiros compartilhados sem revisar hunks.

### Estado final que deve entrar

1. Icones de teclado aprovados mantidos; sprites HD fieis de PS/Xbox e escala
   pela resolucao de renderizacao para os tres conjuntos (PS, Xbox, teclado).
   Reconhecimento da textura nativa, UVs e tamanho logico preservados. A imagem
   HD vai a um recurso GPU separado; nao e escrita no pequeno buffer do PS3.
2. Manual: correcoes de alinhamento nas paginas 00, 01 e 04 dos seis idiomas
   eng/fra/ger/ita/spa/jpn. Identificacao dos botoes na propria imagem de cada
   console; JP nao toma posicoes/simbolos da pagina Xbox como referencia PS.
   Fundo antigo limpo antes de sobrepor os novos botoes, sem duplicacao.
3. Somente manual japones: removido fallback que impedia a pagina de teclado.
   Layout proprio preserva o texto japones, com teclado central e teclas dos
   baloes derivadas do mapeamento do launcher. Funciona com o seletor existente,
   incluindo automatico; aviso de vibracao do rodape preservado e legivel.
4. Somente baloes japoneses: R1/L1, LT/RT e setas em desenho vetorial escalavel;
   face buttons em HD. Os outros idiomas conservam o desenho ja aprovado.
5. ULTIMO AJUSTE DO USUARIO: deixar as ILUSTRACOES dos controles PS/Xbox e o
   disco de movimento ORIGINAIS. Foi removida a funcao jp_manual_diagram e sua
   chamada, que sobrepunham face buttons e chevrons dentro desses desenhos.
   Nao reintroduzir essas sobreposicoes. Botao novo fica somente no balao.
6. Bordas amarelas do teclado NAO sao uma lista fixa: usam keys_for/PAD_KEYMAP
   e o conjunto HdKeyboard::lit. O watcher existente detecta mudanca do mapa,
   invalida a pagina e a recompõe; a revisao HD invalida a textura associada.
   Essa funcionalidade ja existia e foi confirmada, sem alterar os controles
   do jogo. Teclas alternativas tambem entram no destaque. Shift/Ctrl/Alt
   genericos iluminam suas respectivas teclas esquerda/direita no desenho.

### Arquivos/trechos para selecionar no commit

- projects/code_veronica/button_icons.cpp: compositor HD, carregamento da arte,
  registro das teclas, reconhecimento/limpeza das paginas, layout JP, botoes
  vetoriais dos baloes e integracao com o seletor existente. Revisar o diff:
  o arquivo tambem pode conter trabalho anterior da mesma sessao.
- projects/code_veronica/launcher_art/controller_buttons_hd.png: sprite sheet
  novo gerado/aprovado, oito face buttons PS/Xbox, fundo transparente. Nao e
  um dump de textura original do jogo.
- libs/video/rsx_tex_hook.h e .c: somente API e global g_rsx_tex_ui_hook.
- libs/video/rsx_d3d12_backend.c: SOMENTE hunks HD da interface: ui_res/ui_id/
  ui_scale em VPTexSlot; invalidacao da escala; chamada g_rsx_tex_ui_hook e
  upload/descarte do recurso na textura; preferencia por ui_res no binding.
  NAO levar os hunks de interpolacao/FPS, sombras, agua ou outros ajustes.
- projects/code_veronica/tests/test_hd_buttons.cpp: testes isolados do compositor.
- projects/code_veronica/CMakeLists.txt: somente alvo code_veronica_hd_button_tests
  e suas dependencias/opcoes. A glob de recursos ja incorpora o PNG novo.
- projects/code_veronica/build-release.cmd: somente bloco opcional
  CV_HD_BUTTON_TESTS_ONLY, incluindo a passagem pelos seis idiomas.
- projects/code_veronica/HD-BUTTONS.md: este registro e o handoff.

Dependencias da melhoria anterior de teclado, se ainda nao commitadas:
projects/code_veronica/cv_keycap_style.h, tests/preview_keyboard_icons.cpp e
hunks associados de launcher.cpp/CMakeLists.txt/build-release.cmd. Revisar
separadamente; nao incluir outras alteracoes do launcher por conveniencia.

Nao incluir por arrasto: rsx_d3d12_backend.h, main.cpp, cv_version.h,
executar-code-veronica-otimizado.cmd, tools/cv_native_menu_hooks.py, runtime SPU,
cheats, diagnosticos e arquivos novos nao relacionados. Nao incluir executaveis,
logs, arquivos do jogo, dumps, previews de manuais nem diretorios de build.

### Evidencias e reproducao

- Build mais recente: build-release/hd-original-diagram-build.log. EU, JP e US
  linkadas com sucesso pelo script oficial; executaveis atualizados.
- Testes mais recentes: build-release/hd-original-diagram-tests.log, seis PASS.
- Cobertura: prompts PS/Xbox/teclado, escalas ate 4x, alpha, cache/reuso,
  remapeamento, bytes nativos preservados, rejeicao de dados nao relacionados,
  opt-out CV_HD_BUTTONS=0, screen setting, mapa e paginas do manual.
- JP: destino SET_KB sem fallback PS, teclas de todas as acoes, limpeza confinada
  aos retangulos. Alterar square=120+121,up=73 marca F9/F10/I e remove W;
  compara o conjunto inteiro com o mapeamento atual e preserva imagens PS/Xbox.
- Ilustracoes: comparacao pixel a pixel da area central com o compositor sem
  as alteracoes de botoes dos baloes; PS e Xbox passam.
- Previews locais atuais: build-release/hd-button-check-jp/
  manual_page_0_set_0.png (PS), set_1.png (Xbox), set_2.png (teclado), com o
  mesmo prefixo manual_page_0_. Sao exportacoes do compositor, nao capturas de
  uma nova sessao de gameplay.
- Usuario confirmou gameplay e manual corrigido nas tres regioes antes deste
  ultimo ajuste. Confirmou visual bonito no JP; pediu retirar os botoes da
  ilustracao. Retirada e destaque por remapeamento testados offline; teste
  final dentro do jogo ainda depende dele.

Antes de qualquer build ou execucao, verificar CodeVeronicaPort*.exe e nunca
encerrar o jogo do usuario. Usar somente build-release.cmd, com CV_BUILD_JP=1
 e CV_BUILD_US=1. Para teste isolado definir tambem CV_HD_BUTTON_TESTS_ONLY=1;
para build dos jogos remover essa variavel e CV_KEYCAP_PREVIEW_ONLY. Nenhuma
entrada sintetica foi enviada ao jogo. Atalho de teste do usuario continua
executar-code-veronica-otimizado.cmd.

Sugestao de titulo para o commit apos revisar os hunks:
Improve resolution-aware input icons and Japanese keyboard manual
