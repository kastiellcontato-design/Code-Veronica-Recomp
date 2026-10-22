# Sala alagada (rm_1130): desempenho corrigido, água ainda invisível

Nota de passagem (17/10/2026) para quem continuar, inclusive outra IA.
Projeto: recompilação estática de Resident Evil Code: Veronica X (PS3) para
Windows, em `D:\ps3recomp`, branch `code-veronica`. Backend de vídeo:
`libs/video/rsx_d3d12_backend.c` (RSX do PS3 -> D3D12).

## O que já foi feito (commit 271b9cf)

- Sintoma: na sala alagada `rm_1130` (área 1, sala 13), o jogo caía para
  1,6 FPS e a água não aparecia.
- Medição: amostragem das threads do processo. A thread do RSX passava quase
  todo o tempo em `rsx_fetch_vertex_planned` / `read_vp_vertex` /
  `upload_strip_vp_indexed`; as threads do jogo dormiam esperando.
  `[PERF]` mostrava `guest 99%`, `[PERF-FIFO] drain 99%` e `[PERF-VCB]` com
  ~10 milhões de vértices convertidos por quadro para ~28 mil desenhados.
- Causa: o RSX manda um triangle strip longo em vários pedaços de até 256
  índices dentro do mesmo BEGIN/END. A junção desses pedaços
  (`[STRIPJOIN]`, em `d3d12_draw_arrays_impl` e `d3d12_draw_indexed_impl`)
  descartava o que já tinha expandido e reexpandia o strip inteiro desde o
  primeiro índice a cada pedaço: custo quadrático.
- Correção: cada pedaço novo só acrescenta os seus triângulos.
  `upload_strip_vp_from(..., t_begin)` (não indexado) e
  `upload_strip_vp_indexed(..., cont)` (indexado, retoma o segmento aberto
  guardado em `s_carry_*` sem reler os índices). A reexpansão completa
  antiga ficou só como plano B, quando o estado não confere.
- Resultado no jogo (versão JP, testado pelo Kastiel): 29,9 FPS na sala;
  `[PERF-VCB]` caiu para ~140 mil vértices convertidos por quadro.

## O que falta: a água não renderiza

Continua invisível depois da correção. Não foi investigado ainda. É provável
que seja um problema separado do desempenho.

### Pistas

- Antes da correção, um `[BUDGET]` mostrou um quadro com
  `requested 46869 verts / 615 draws ... 179 draws truncated` (outros quadros
  com 0). "Truncated" conta `s_drop_draws` (buffer de vértices cheio em
  `upload_*_vp`). Ver se ainda acontece com a correção e se esses desenhos
  são a água.
- Os desenhos da sala são principalmente `prim=6` (TRIANGLE_STRIP), muitos e
  pequenos (3 a 10 vértices por entrada), mais strips indexados.
- Água em jogos de PS3 costuma usar: render-to-texture (reflexo/refração),
  blend aditivo/alfa, textura animada, ou formato de vértice/textura
  incomum. Cada hipótese tem um sinal diferente no log ou na captura.

### Como reproduzir

1. Usar `projects/code_veronica/executar-code-veronica-desempenho.cmd`
   (liga `PERF=1`, `PS3_TIMELINE=1`, `FRAME_BUDGET=1`; o log vai para
   `run-log-dxc-experimental.txt`).
2. O Kastiel tem um save do F7 nessa sala na versão JP (NPJB00135); na lista
   de saves aparece como "F7 RM1130". A janela F7 (opção no launcher, aba
   Jogo) também leva direto à sala: `rm_1130`, entrada 0.
3. F12 no jogo faz uma captura `rsxcap` de um quadro (pasta
   `projects/code_veronica/rsxcap`; ver `libs/video/rsx_commands.c`).
4. Referência: capturar o mesmo quadro no RPCS3 com RenderDoc e comparar
   passe a passe (qual passe desenha a água, com qual shader e estado).

### Perguntas a responder

- Quais desenhos são a água (programa de fragmento `shader_program`,
  programa de vértice, render target, estados de blend/profundidade)?
- Eles chegam ao backend? São cortados (`s_drop_draws`)? São pulados por
  shader não compilado (`[PERF-SHADER] draws left out`)?
- A água é desenhada numa textura intermediária (reflexo) que depois não é
  usada, ou é usada com o formato errado?

## Regras do projeto

- Compilar só com `projects/code_veronica/build-release.cmd`, pelo
  PowerShell: `cmd /c "D:\ps3recomp\projects\code_veronica\build-release.cmd"`,
  com `CV_BUILD_JP=1` e `CV_BUILD_US=1` para as três versões.
- Antes de rodar testes, ver se o jogo do usuário está aberto; fechar só
  processos que você mesmo abriu; nunca mandar teclas sintéticas.
- Comitar só depois que o usuário testar e disser "pode comitar".
- Nunca publicar arquivos do jogo, dumps, executáveis ou código levantado.


## Investigação e correção candidata (Codex, 17/10/2026)

- Captura F12 atual: `rsxcap/frame_90`, `frame_count=943`, 6250 ops.
  A água é o op4602, FP `0x02C56981` (código `AEBCCCCE`), VS 9,
  138612 vértices, blend ativado, depth LEQUAL, destino `0x01528000`.
  A GPU executa 220821 pixel shaders nesse draw: a água não está simplesmente
  ausente da lista de desenhos. O op4601 prepara a profundidade em
  `0x078A0000` (`R32_FLOAT`). O shader da água amostra t2 para reconstruir
  a profundidade e calcular espessura/transparência; na captura anterior
  t2 ainda apontava para a textura antiga de 1x1 (`0x00C60E80`, Y16_X16).
- Falha concreta no backend: `d3d12_bind_texture` só aceitava uma lista
  parcial de formatos e ignorava X32_FLOAT (`0x9C`). Assim, uma nova ligação
  desse tipo preservava silenciosamente a ligação anterior. O uploader e
  o decoder compartilhado já sabem trabalhar com R32_FLOAT.
- Alteração candidata: aceitar X32_FLOAT no bind e no fallback de upload,
  usando o SRV R32_FLOAT. RTT já usa o recurso GPU quando encontra o alvo.
  A captura F12 agora registra todas as 8 unidades de fragmento, não só 4.
- A otimização incremental dos strips do commit 271b9cf foi preservada.
- Build oficial terminou com sucesso para EU, JP e US. Sem commit/push.
- Pendente: confirmação visual do usuário e nova captura F12 para conferir
  t2 no draw da água. Não tratar apenas a compilação como validação visual.
- Evidência anterior preservada localmente em
  `C:/Users/gabri/Documents/Codex/2026-10-15/d-ps3-recomp-code-gpt-ps3recomp/water-before`.


### Resultado do primeiro teste

- Usuário testou e informou: a água ainda não aparece.
- F12 `frame_count=1116`, 3449 ops, confirmou X32_FLOAT corrigido: água
  op1801 (`AEBCCCCE`), t2 agora `0x078A0000`, formato `0xBC`, 1280x720;
  a GPU executa 222952 pixel shaders. Logs ~30 FPS fora da pausa do F12.
  Portanto manter a correção do bind, mas ela não resolve sozinha.
- Diagnóstico preparado: `executar-diagnostico-agua.cmd` ativa explicitamente
  `CV_WATER_PROBE=1`; após a limpeza FP_* do iniciador, ele seleciona
  `FP_SHOW_CODE=AEBCCCCE`. R=alpha, G=1 se RGB inválido (0.05 se válido),
  B=máxima componente RGB. Alpha do teste é 1 para tornar o resultado visível.
  F12 desse modo grava em `rsxcap-water-diag`, separado da captura normal.
  FP_SHOW foi corrigido para os exports buffered `_o` (antes só `_po.c0`).
  Os três executáveis compilaram. Logs confirmam `[FPSHOW] ... AEBCCCCE`.
- Pendente: analisar F12 do diagnóstico. Não declarar a água resolvida.


### Causa restante localizada: canais da profundidade (17/10)

- O verde na imagem FINAL do primeiro diagnóstico foi enganoso: o pós-processo
  amplifica o canal de teste. Na imagem ANTES do pós-processo,
  `rsxcap-water-diag/frame_90/step_op4499_01528000.bmp`, G=63 corresponde
  a sRGB(0.05), isto é, cor FINITA. A hipótese de NaN foi descartada.
- Os rastreadores de não finitos confirmaram zero operações inválidas; foram
  removidos do código. FP_SHOW com seleção por hash foi mantido para diagnóstico.
- `rsxcap-water-alpha/frame_90`, água op2227, snapshot `step_op2229_01528000.bmp`:
  R=0 (espessura positiva zero), G=255 (input.tc2.w=1, fator de vértice válido),
  B=13 (sRGB de ~0.004 para a profundidade amostrada, deveria estar perto de 1).
- Causa: `zconv_init` escrevia no alvo RGBA `{high,mid,low,0}` e a ligação
  aplicava `rsx_remap_to_d3d(...,0x85)`, que interpreta RGBA LÓGICO. Com o
  swizzle 0xAA93 da reconstrução de profundidade, isso virava `{0,high,mid}`.
  O shader desempacotava uma profundidade aproximadamente 256 vezes menor;
  a distância cena/água ficava negativa e alpha zerava.
- Correção: zconv agora escreve RGBA lógico `{mid,low,stencil=0,high}`.
  O mesmo 0xAA93 passa a apresentar `{high,mid,low}` para a conta original.
  Não é alpha forçado, nem troca do material da água.
- Verificação numérica independente do crossbar: 0x800000 retornava
  0.001953125, agora 0.5; 0xFEDCBA retornava 0.00388884544, agora
  0.99555552006. Casos de meia profundidade, 0xF00000 e 0xFFFFFF também batem.
- F12: R32_FLOAT agora é mostrado como cinza escalar, em vez de interpretar
  três bytes da representação IEEE como RGB (o expoente era perdido).
- Build oficial EU/JP/US concluído com sucesso. Usuário solicitado a abrir
  o atalho NORMAL `executar-code-veronica-desempenho.cmd` e fazer F12.
  Aguardar resultado visual antes de declarar resolvido e antes de commit.

## Resolvido (17/10/2026, Claude): agua completa na rm_1130

Duas causas, as duas no backend D3D12:

1. **Recorte em z.** O RSX recorta em -w <= z <= w; o D3D recortava em z' >= 0
   e cortava a agua perto da camera. O VS gerado agora escreve
   `SV_ClipDistance0 = (z + w, w - z)` (rsx_vp_decompiler.c) e o
   `DepthClipEnable` do pipeline fica desligado (`RSX_ZCLIP=d3d` volta ao antigo).
2. **Nevoa com a profundidade velha.** O zconv (profundidade -> textura) rodava
   uma vez por quadro, antes da copia de profundidade que a agua usa. A agua grava
   profundidade depois, e a passada de nevoa (le a zeta 0x026E0000) recebia a
   conversao antiga, sem a agua: tratava a agua como fundo distante e a lavava com
   nevoa, mais forte perto da camera (a "faixa clara"). Agora `zconv_plan` agenda
   uma nova conversao sempre que a zeta muda entre duas leituras
   (`ZCONV_ONCE=1` volta ao antigo).

O que foi conferido e NAO era o problema: o cubemap de reflexo (bytes do jogo,
buffer enviado e ligacao como TextureCube batem com o RPCS3), o VS da agua
(saidas iguais as do RPCS3) e a profundidade gravada pela agua (z/w ~0.98).
O depurador de pixel do RenderDoc no RPCS3 mostra constantes do FP zeradas, mas
isso e artefato (le o buffer de constantes sem o deslocamento dinamico).

Ferramentas que ficaram: `TEX_DUMP=<off>` (bytes e buffer de envio de uma textura,
`[TEXBIND]` da ligacao), `FP_ZERO_K=<hash>:<i,...>`, `RSXCAP_STEP_VTX=<n>`
(F12 salva antes/depois do desenho com n vertices) e as sondas
`executar-sonda-agua-*.cmd` (CV_WATER_PROBE 2..8).

## Reflexo e isqueiro (17/10/2026, Claude)

3. **Cubemaps lidos como 2D.** O decompilador recebia a mascara de cubos como 0
   (`rsx_fp_decompile_alloc(..., 0u, ...)` em `fp_build_pso`) e o retoque de texto
   que deveria trocar `Texture2D` por `TextureCube` procurava declaracoes que o
   decompilador nao gera mais. Todo cubo do jogo era amostrado como 2D com uma
   view de cubo ligada: o reflexo da agua devolvia uma cor azulada constante (agua
   lisa e azul, sem o padrao esverdeado do PS3/RPCS3). Agora a mascara vai para o
   decompilador, que declara `TextureCube` e amostra com a direcao 3D
   (`RSX_CUBEDIR`, que `RSX_CUBE_FLIP=x|y|z` espelha para teste).
4. **Isqueiro com linhas pretas** depois disso: os cubos Y16_X16 de tabela da
   sombra precisam de leitura sem filtro; `dr_cube_mask` marca esses cubos
   (bits 16+u) e o shader usa o sampler ponto s(FP_TEX_UNITS+4)
   (`CUBE_LUT_LINEAR=1` volta ao linear).

Experimentos que ficaram atras de chave e NAO resolveram: `RSX_TEX_MIPS=1`
(sobe a cadeia de mipmaps; o normal map da agua tem 1 nivel so),
`TEX_NOGAMMA_FP=<hash>:<unidades>` (normal map sem sRGB ficou pior),
`FP_ZERO_K`. O modo wrap (1) de `RSX_ADDR` passou a devolver a coordenada sem
`frac()` (o sampler estatico ja repete). `RSX_DXC_DEBUG=1` embute debug no DXIL
para depurar no RenderDoc; `executar-renderdoc-agua.cmd` captura o nosso jogo.
O que mostrou o problema foi comparar no RenderDoc o tipo da textura declarada
no shader com o da view ligada (Texture2D x TextureCube).

## 22/10/2026 — recorte em z revisto (grade da primeira cutscene)

Na primeira cutscene, no close do homem depois de abrir a grade, aparecia uma faixa escura na frente
do rosto. O original não tem essa faixa. Era a grade aberta colada na câmera, entre metade da
distância do plano próximo e o próprio plano. O recorte -w <= z <= w do VS deixava essa geometria
passar.

Nos registradores do RSX de todas as capturas F12, o jogo usa um viewport de profundidade no estilo
D3D: escala z 1, deslocamento 0, CLIP_MIN 0 e CLIP_MAX 1. Então o RSX recorta 0 <= z' <= w depois do
viewport, que é o próprio recorte do D3D. `DepthClipEnable` voltou a ser ligado por padrão (log
`[ZCLIP]`), e o recorte do VS continua como redundância. `RSX_ZCLIP=vs` (ou `DEPTH_CLIP=0`) volta ao
recorte só do VS; atalho de comparação `executar-teste-recorte-profundidade-jp.cmd`.

Teste do usuário na JP: a faixa sumiu da cutscene, e a água da rm_1130 continua inteira. A faixa
clara da água vinha da névoa lendo a profundidade velha (item 2 acima), e essa correção continua.
