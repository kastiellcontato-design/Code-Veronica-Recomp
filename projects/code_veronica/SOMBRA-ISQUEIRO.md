# Faixa fina na iluminacao do isqueiro (17/10/2026)

Pedido: investigar a faixa/sombra fina que aparece com o isqueiro ligado ao
andar. Preservar a agua corrigida, FXC/DXC e modo original de 30 FPS.

## Evidencia recebida

Atalho usado: executar-code-veronica-otimizado.cmd, FXC, 30 FPS normal.
Capturas F12 em rsxcap/frame_90 (frame_count=1100, 6775 ops), frame_91
(1152, 6023 ops), frame_92 (3157, 3636 ops), manifestos atualizados em
17/10 aproximadamente 13:16-13:18. Pastas reutilizadas: datas dos diretorios
nao indicam a idade da captura; confiar no manifesto e nas imagens listadas.
Imagens do usuario mostram uma faixa fina sobre as pedras e a borda da madeira
na area iluminada. Causa exata ainda NAO demonstrada.

A agua foi corrigida posteriormente com Claude. Ver AGUA-SALA-ALAGADA.md.
O codigo atual ja difere da primeira correcao de cubemap descrita nesse MD:
usa Texture2DArray e selecao de face manual (RSX_CUBE_SEAMLESS reverte),
alem de rsx_s2d para interpolar os valores 16-bit montados, e nao seus bytes.
Nao reintroduzir cegamente o antigo sampler ponto ou mudar todos os cubos.

## Limitacao localizada no diagnostico e corrigida

A captura gerava HLSL com mascara de cubos fixada em zero, mesmo quando o
pipeline real compilava com unidades cube (log [CUBE], mascara 0x6). Esse
HLSL bruto nao prova que a GPU estava usando Texture2D. O manifesto nao
registrava cube, address modes nem os metadados b1 da interpretacao Y16_X16.
O BMP de 24 bits tambem perde alpha: no atlas de sombra alpha e o byte alto
do primeiro momento; somente RGB nao permite reconstruir sua profundidade.

Mudanca SOMENTE de captura em libs/video/rsx_d3d12_backend.c:
- manifesto registra cube/mips/addr e texaddr efetivo de b1 por draw;
- HLSL bruto recebe mascara cube real; nome/chave incluem cube e exp32,
  evitando colisoes entre variantes do mesmo programa;
- atlas RGBA8 768x512 recebe arquivo .bmp.rgba8 junto ao BMP, em RGBA linear
  de bytes, linhas de cima para baixo, sem padding, 768*512*4 bytes. Inclui
  as etapas do atlas efetivamente fotografadas. Sem copia GPU adicional.
- fontes sao identificados como traducao bruta, sem os wrappers de amostragem
  inseridos posteriormente pelo backend. Nao confundir com fonte final.

Build oficial EU/JP/US concluido, exit 0; log
build-release/lighter-capture-build.log. git diff --check aprovado.
Nao foi alterada a imagem/calculo de sombra, nem o comportamento fora do F12.
Sem commit/push. Sem controle ou encerramento automatico da partida.

## Proximo passo

Usuario deve reabrir o MESMO atalho normal, reproduzir a faixa e pressionar
F12 novamente. Ler os novos arquivos e conferir a selecao de face e o atlas
16-bit antes de escolher uma correcao. Ainda nao afirmar defeito resolvido
ou uma causa confirmada. Nao retomar a interpolacao de 60 FPS nesta tarefa.

## Capturas novas e isolamento ao vivo (17/10, aproximadamente 13:28)

Recebidas quatro capturas novas: frame_90=1723/6023 ops,
frame_91=2128/3390, frame_92=2251/3390, frame_93=2289/5582.
O novo manifesto registra cubos corretamente (incluindo tabelas de 64x64
R16G16 e 1x1); HLSL bruto das variantes agora declara cubos. Sidecars RGBA8
existem e medem 1572864 bytes. A banda aparece em regiao de pedras iluminada.
As capturas 91-93 usam a mesma camera com pequenas variacoes de pose/luz.
Nao foi confirmada uma causa por esses dados. Nao alegar correcao visual.

Referencia RPCS3 agua_frame3669.rdc foi convertida SOMENTE para leitura no
workspace do Codex (lighter-reference.zip.xml/.zip). Nessa captura nao ha
as tabelas pequenas da sombra do isqueiro ativas, portanto ela nao confirma
qual filtro o isqueiro solicita. Tentativas de executar helper Python via
qrenderdoc nao produziram resultado; os processos criados especificamente
para esses helpers foram encerrados, sem tocar no jogo. Nenhum processo do
usuario foi encerrado. Capturas originais nao foram modificadas.

Preparado executar-teste-sombra-isqueiro.cmd (CV_LIGHTER_PROBE=1, FXC,
30 FPS, sem interpolacao), log run-log-sombra-isqueiro.txt. F10 alterna
NORMAL / SEM SOMBRA no titulo. F12 captura em ambos os modos. Consome
WM_SYSKEYDOWN/WM_SYSKEYUP do F10 no teste para nao ativar o menu do Windows;
autorepeat nao alterna mais de uma vez. Atalho normal preservado.

O teste injeta uma selecao por uniforme SOMENTE nos tres programas que
calculam visibilidade da luz: 59540949, 0C121A16, 4D26314D. Forca o export
primario a 1 em vez do termo de visibilidade/atenuacao desse prepass, sem
mexer em geometria, profundidade, shaders da agua ou memoria do jogo. Isso
pode clarear as superficies: e isolamento, NAO uma correcao nem comparacao
pixel-a-pixel com brilho constante. O shader 4FE0FA16 foi excluido depois
que a verificacao mostrou que e material final, nao o prepass da sombra.

Uniforme sentinela -1234 usa texaddr7.w somente com unidade 7 nao ligada,
RT 0x01C30000 e shader elegivel. O modo aparece no texaddr7 do manifesto.
Cache FXC separado em shader_cache/lighter-probe1; o teste nao grava seus
PSOs sobre o cache normal. Sem a chave, nao injeta expressoes nem reserva
teclas. Configuracao e local ao processo, sem INI.

Build oficial EU/JP/US concluido com exit 0. Os tres fontes de sombra da
captura, com o export condicional de diagnostico, compilaram com FXC SM5.
A compilacao do jogo nao testa o efeito visual do toggle. Log:
build-release/lighter-isolation-build.log. git diff --check aprovado.

PENDENTE: usuario abrir o atalho de teste, capturar F12 com a faixa visivel,
pressionar F10 uma vez e capturar F12 de novo no mesmo ponto. Comparar se a
faixa acompanha o termo de sombra ou continua no material/iluminacao final.
Nao retomar o experimento de FPS nesta tarefa. Sem commit/push.

## Falha no teste de F10, corrigida (17/10, aproximadamente 14:00)

Usuario relatou piscar depois de F10, com a listrinha acompanhando o piscar.
Log confirma UMA alternancia BYPASSED, nao autorepeat. Capturas:
frame_90=998/6772 ops (normal), frame_91=3470/5822 e frame_92=3506/5822
(depois do toggle). Esses dados NAO confirmam a causa do defeito original.

Encontrado erro no proprio diagnostico: vp_record_cb, no thread de gravacao,
consultava o hash por fp_snap_ptr(dr), que usa s_rb.fp_arena, isto e, a arena
do lote congelado do renderizador. O mesmo offset na arena anterior pode
nomear outro programa. A selecao podia aplicar o uniforme em alguns quadros
e omiti-lo em outros. Esse erro foi introduzido somente no teste de F10;
nao explica nem afeta o caminho normal, onde CV_LIGHTER_PROBE esta ausente.

Correcao: escrever a sentinela para todos os draws do prepass selecionado
com unidade 7 livre, sem consultar a arena de render na gravacao. A whitelist
no shader continua restringindo a leitura aos tres programas de sombra.
O manifesto agora inclui lighter_probe=NORMAL/BYPASSED explicitamente, mesmo
com unidade 7 nao ligada; antes omitia justamente o dado de diagnostico.

A validacao visual do teste precisa ser repetida, sem mover a personagem:
mesmo executar-teste-sombra-isqueiro.cmd, F12 normal, F10 uma vez e F12.
O modo deve permanecer constante depois de alternar. Se a faixa acompanhar
a mudanca, investigar o prepass; se permanecer, investigar material/luz final.
Nao interpretar o piscar da versao anterior como prova de causa.

## Preservacao das fracoes na amostragem de sombra (17/10)

Usuario confirmou que a faixa desapareceu depois do F10 no teste corrigido.
Log confirma NORMAL -> BYPASSED uma vez, capturas frame_90=1200/6104 ops e
frame_91=1572/6922 ops. O manifesto do segundo quadro registra o uniforme
BYPASSED e o alvo 0x01C30000 fica azul no prepass, conforme o export forcado.
Importante: esse termo e oclusao/atenuacao; forca-lo a 1 pode suprimir a
contribuicao da luz. A chama continua, mas a iluminacao pode escurecer. Logo,
o resultado associa a faixa a esse caminho, sem provar sozinho a causa de
amostragem ou que a luz final ficou constante. O teste nao e a correcao.

Problema numerico encontrado independentemente: rsx_s2d decodificava os
quatro texels em pares UNORM16, interpolava em float, arredondava o resultado
para uint16 e reempacotava bytes. RSX_TEXOUT depois decodificava esses bytes
novamente. Isso perde a fracao interpolada e cria degraus em dados que o
shader usa para calcular a variancia/comparacao de sombra. Exemplo: centro
entre 255 e 256 deveria produzir 255.5 / 65535, mas era arredondado para
256 / 65535. Nao confundir com o decode de um texel armazenado: esse continua
arredondando bytes para recuperar exatamente seu valor inteiro.

Correcao aplicada ao caminho normal:
- libs/video/rsx_packed16_filter.h: HLSL compartilhado que faz quatro Loads,
  decodifica cada texel e interpola diretamente em float, sem quantizacao final;
- rsx_s2d retorna os canais logicos do par UNORM16; RSX_S2D_OUT evita o segundo
  decode apenas dessas chamadas. Texturas comuns recebem o MESMO gamma que
  antes, uma vez, agora dentro do wrapper. Cubos e SampleBias/SampleLevel
  conservam seus caminhos. Nao ha bias de profundidade nem sombras desativadas;
- RSX_S2D_OFF continua como diagnostico de retorno ao caminho sem wrapper.

Validacao: build-release.cmd com CV_BUILD_JP=1 CV_BUILD_US=1
CV_SHADOW_FILTER_TESTS=1 terminou exit 0. Teste opcional
code_veronica_shadow_filter_tests executa o MESMO HLSL de producao em compute
SM5 via D3D11 WARP, compara 130 canais contra referencia bilinear em double,
verifica carries, fracoes sub-LSB e limites da textura. PASS; ponto medio
0.00389868021. Teste usa recursos sinteticos, sem jogo, saves ou INI.
177 variantes reais das novas capturas (agua, sombras, materiais e posprocesso)
compilaram com FXC e o preambulo/wrappers novos. Verificador local fora do repo:
verify-shadow-shaders.py no workspace Codex. git diff --check aprovado.
Log oficial: build-release/shadow-fraction-build.log.

PENDENTE: usuario testar pelo executar-code-veronica-otimizado.cmd NORMAL,
com sombras ativadas, andar com o isqueiro e conferir a faixa. Essa perda de
precisao foi demonstrada, mas sua correcao ainda NAO foi validada como solucao
visual da faixa. Se persistir, continuar a investigar a leitura da sombra e
sua variancia; nao apresentar a remoção da luz do F10 como solucao. Agua e
30 FPS preservados no codigo; sem nova validacao visual de agua nesta etapa.
Sem commit/push e sem controle automatico da partida.

## Handoff solicitado pelo usuario: faixa persiste (17/10)

Usuario testou a correcao de preservacao das fracoes pelo atalho normal,
pressionou F12 algumas vezes e confirmou: NAO resolveu a listrinha.
A validacao visual dessa tentativa falhou, apesar dos testes numericos e de
compilacao aprovados. Nao apresentar essa mudanca como solucao do defeito.
Usuario decidiu continuar com Claude. Interromper aqui a investigacao.

Conclusao correta do teste de F10: a faixa desaparece quando os exports
primarios dos programas 59540949, 0C121A16 e 4D26314D sao forcados a 1
(no prepass 0x01C30000). Isso isola o caminho de oclusao/atenuacao da luz do
isqueiro como relevante ao defeito. NAO prova qual operacao esta errada,
nem distingue sozinho geracao do mapa, leitura/filtro, variancia, bias ou
calculo posterior da iluminacao. Nao chamar isso de simples 'desligar a
sombra mantendo a luz identica': 1 nesse buffer pode retirar a contribuicao
da luz, e as capturas mostram escurecimento. A chama permanece visivel.

O primeiro teste de F10 tinha leitura da arena errada e piscava; esse erro
foi corrigido antes do usuario confirmar que a faixa sumia no teste estavel.
Atalho diagnostico: executar-teste-sombra-isqueiro.cmd; normal:
executar-code-veronica-otimizado.cmd. Ultimos F12 relatados pelo usuario
estao em rsxcap/frame_9*; conferir datas dos manifestos e os arquivos que
eles referenciam (as pastas sao reutilizadas). Nao foram analisados nesta
etapa de handoff, pois o usuario pediu continuidade com outro agente.

Mudancas locais sem commit/push. Correcao numerica ainda presente, mas nao
resolveu o defeito visual; decidir sua manutencao com base no teste de
regressao, sem atribuir a ela uma correcao da faixa. Agua corrigida com Claude
preservada. Modo original de 30 FPS preservado. Experimento 60 FPS separado.

## Causa encontrada e corrigida com Claude (17/10, commit 3926720)

Pista do usuario: o risco so aparecia longe da parede; perto, nao. Na captura
F12 da parede do inicio, o risco aparece DENTRO da mascara de sombra do
isqueiro (alvo 0x01C30000), como uma linha reta na parede.

Causa: a tabela t1 do isqueiro (cubo Y16_X16 64x64, offset 0x06B60F80) foi
despejada com TEX_DUMP. Cada texel guarda i/63, de borda a borda (texel 0 = 0,
texel 63 = 1). Lida pelo centro do texel (padrao de textura), a resposta sai
com erro de (u-0.5)/63: zero no meio da face, ate ~2 texels do atlas na borda,
com sinal oposto na face vizinha. O salto na emenda entre faces era o risco.
Longe da parede cada texel do atlas cobre mais area, entao 2 texels viram
sombra (o shader nao tem bias e eleva o resultado a 4).

Correcao (libs/video/rsx_d3d12_backend.c):
- vp_record_cb marca rsx_texaddr[u].z bit 8 para cubos Y16_X16 quadrados de
  2..64 texels (as tabelas do isqueiro; agua e outros cubos nao entram);
- rsx_cube recebe a unidade e, com esse bit, mapeia a coordenada da face para
  a grade gravada: (uv*(N-1)+0.5)/N. A tabela devolve exatamente uv.
- RSX_CUBE_LUT_RAW=1 volta a leitura antiga (comparacao).

Validacao: usuario testou pelo atalho otimizado; o risco da parede sumiu.
Sobra um risco fininho, quase invisivel, perto da agua; usuario decidiu deixar
assim. Uma tolerancia (bias) no teste esconderia, mas o jogo original nao tem.

Commit 3926720 inclui tambem: cubos como Texture2DArray filtrados dentro da
face (RSX_CUBE_SEAMLESS=1 reverte), filtro 16-bit sem requantizar
(rsx_packed16_filter.h, RSX_S2D_OFF=1 reverte), captura F12 detalhada e o
diagnostico F10 (CV_LIGHTER_PROBE, desligado sem a variavel). Sem push.
Codigo de 60 FPS continua fora do commit.
